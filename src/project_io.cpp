#include "sketchcad/project_io.h"

#include <rfl.hpp>
#include <rfl/json.hpp>
#include <yyjson.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_set>

#include "sketchcad/solver.h"

namespace sketchcad {
namespace {
struct WirePoint {
  using Tag = rfl::Literal<"point">;
  std::string id;
  bool construction;
  double x;
  double y;
};
struct WireLine {
  using Tag = rfl::Literal<"line">;
  std::string id;
  bool construction;
  std::string start;
  std::string end;
};
struct WireCircle {
  using Tag = rfl::Literal<"circle">;
  std::string id;
  bool construction;
  std::string center;
  double radius;
};
struct WireArc {
  using Tag = rfl::Literal<"arc">;
  std::string id;
  bool construction;
  std::string center;
  double radius;
  double start_angle;
  double sweep_angle;
};
using WireEntity = rfl::TaggedUnion<"kind", WirePoint, WireLine, WireCircle, WireArc>;
struct WirePosition { double x; double y; };
struct WirePlacement { double offset; double along; double angle; };
struct WireConstraint {
  std::string id;
  std::string kind;
  std::string first;
  std::string second;
  WirePosition target;
  bool internal;
  double value;
  int side;
  std::optional<WirePlacement> placement;
  bool reference;
};
struct WireProject {
  std::string format;
  unsigned version;
  std::string units;
  std::string revision;
  std::string next_id;
  std::vector<WireEntity> entities;
  std::vector<WireConstraint> constraints;
};
constexpr std::array<std::string_view, 13> kKinds = {
    "coincident", "horizontal", "vertical", "parallel", "perpendicular",
    "tangent", "equal", "fix", "on_curve", "length", "distance", "angle", "radius"};
std::optional<std::uint64_t> decimal(std::string_view text) {
  if (text.empty() || (text.size() > 1 && text[0] == '0') || text[0] < '0' || text[0] > '9')
    return std::nullopt;
  std::uint64_t number = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
  if (error != std::errc() || end != text.data() + text.size()) return std::nullopt;
  return number;
}
std::string_view text(yyjson_val* v) {
  return yyjson_is_str(v) ? std::string_view(yyjson_get_str(v), yyjson_get_len(v)) : std::string_view();
}
bool bounded_tree(yyjson_val* v, unsigned depth = 1) {
  if (depth > 16) return false;
  std::size_t i, count;
  yyjson_val* key;
  yyjson_val* value;
  if (yyjson_is_obj(v)) {
    std::unordered_set<std::string_view> keys;
    yyjson_obj_foreach(v, i, count, key, value) {
      if (!keys.insert(text(key)).second || !bounded_tree(value, depth + 1)) return false;
    }
  } else if (yyjson_is_arr(v)) {
    yyjson_arr_foreach(v, i, count, value) {
      if (!bounded_tree(value, depth + 1)) return false;
    }
  }
  return true;
}
enum class Type { kString, kNumber, kBoolean, kInteger, kObject, kArray, kNullableObject };
using Field = std::pair<std::string_view, Type>;
bool shape(yyjson_val* v, std::initializer_list<Field> fields) {
  if (!yyjson_is_obj(v) || yyjson_obj_size(v) != fields.size()) return false;
  for (const auto& [name, type] : fields) {
    auto* value = yyjson_obj_getn(v, name.data(), name.size());
    if (!value) return false;
    switch (type) {
      case Type::kString: if (!yyjson_is_str(value)) return false; break;
      case Type::kNumber:
        if (!yyjson_is_num(value) || !std::isfinite(yyjson_get_num(value))) return false;
        break;
      case Type::kBoolean: if (!yyjson_is_bool(value)) return false; break;
      case Type::kInteger: if (!yyjson_is_int(value)) return false; break;
      case Type::kObject: if (!yyjson_is_obj(value)) return false; break;
      case Type::kArray: if (!yyjson_is_arr(value)) return false; break;
      case Type::kNullableObject: if (!yyjson_is_null(value) && !yyjson_is_obj(value)) return false; break;
    }
  }
  return true;
}
bool entity_shape(yyjson_val* e) {
  using T = Type;
  const auto kind = text(yyjson_obj_get(e, "kind"));
  if (kind == "point")
    return shape(e, {{"kind",T::kString},{"id",T::kString},{"construction",T::kBoolean},
                     {"x",T::kNumber},{"y",T::kNumber}});
  if (kind == "line")
    return shape(e, {{"kind",T::kString},{"id",T::kString},{"construction",T::kBoolean},
                     {"start",T::kString},{"end",T::kString}});
  if (kind == "circle")
    return shape(e, {{"kind",T::kString},{"id",T::kString},{"construction",T::kBoolean},
                     {"center",T::kString},{"radius",T::kNumber}});
  if (kind == "arc")
    return shape(e, {{"kind",T::kString},{"id",T::kString},{"construction",T::kBoolean},
                     {"center",T::kString},{"radius",T::kNumber},{"start_angle",T::kNumber},
                     {"sweep_angle",T::kNumber}});
  return false;
}
bool constraint_shape(yyjson_val* c) {
  using T = Type;
  if (!shape(c, {{"id",T::kString},{"kind",T::kString},{"first",T::kString},{"second",T::kString},
                 {"target",T::kObject},{"internal",T::kBoolean},{"value",T::kNumber},
                 {"side",T::kInteger},{"placement",T::kNullableObject},{"reference",T::kBoolean}}))
    return false;
  if (!shape(yyjson_obj_get(c,"target"), {{"x",T::kNumber},{"y",T::kNumber}})) return false;
  auto* placement = yyjson_obj_get(c,"placement");
  return yyjson_is_null(placement) || shape(placement, {{"offset",T::kNumber},{"along",T::kNumber},{"angle",T::kNumber}});
}
}  // namespace

ProjectDecodeResult decode_project(std::string_view bytes) {
  if (bytes.size() > kProjectMaxBytes) return ProjectError::kTooLarge;
  const auto invalid = ProjectError::kInvalidProject;
  try {
    std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(
        yyjson_read(bytes.data(), bytes.size(), 0), yyjson_doc_free);
    if (!doc) return invalid;
    auto* root = yyjson_doc_get_root(doc.get());
    using T = Type;
    if (!bounded_tree(root) ||
        !shape(root, {{"format",T::kString},{"version",T::kInteger},{"units",T::kString},
                      {"revision",T::kString},{"next_id",T::kString},{"entities",T::kArray},
                      {"constraints",T::kArray}}) || text(yyjson_obj_get(root,"format")) != "SketchCAD") return invalid;
    auto* version = yyjson_obj_get(root,"version");
    if (!yyjson_is_uint(version)) return invalid;
    if (yyjson_get_uint(version) != 1) return ProjectError::kUnsupportedVersion;
    if (text(yyjson_obj_get(root,"units")) != "mm") return invalid;
    auto* entities = yyjson_obj_get(root,"entities");
    auto* constraints = yyjson_obj_get(root,"constraints");
    if (yyjson_arr_size(entities) + yyjson_arr_size(constraints) > kProjectMaxRecords) return ProjectError::kTooLarge;
    std::size_t i, count;
    yyjson_val* value;
    yyjson_arr_foreach(entities, i, count, value) { if (!entity_shape(value)) return invalid; }
    yyjson_arr_foreach(constraints, i, count, value) { if (!constraint_shape(value)) return invalid; }
    auto parsed = rfl::json::read<WireProject, rfl::NoExtraFields>(rfl::json::InputVarType(root));
    if (!parsed) return invalid;
    const auto& wire = parsed.value();
    const auto revision = decimal(wire.revision), next = decimal(wire.next_id);
    if (!revision || *revision == UINT64_MAX || !next) return invalid;
    SketchSnapshot snapshot;
    snapshot.next_id = *next;
    bool valid_ids = true;
    const auto id = [&](const std::string& s) {
      const auto result = decimal(s);
      if (!result) valid_ids = false;
      return result.value_or(0);
    };
    for (const auto& entity : wire.entities) {
      snapshot.entities.push_back(entity.visit([&](const auto& v) -> Entity {
        using W = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<W,WirePoint>) return SketchPoint{id(v.id),{v.x,v.y},v.construction};
        else if constexpr (std::is_same_v<W,WireLine>) return SketchLine{id(v.id),id(v.start),id(v.end),v.construction};
        else if constexpr (std::is_same_v<W,WireCircle>) return SketchCircle{id(v.id),id(v.center),v.radius,v.construction};
        else return SketchArc{id(v.id),id(v.center),v.radius,v.start_angle,v.sweep_angle,v.construction};
      }));
    }
    for (const auto& v : wire.constraints) {
      const auto kind = std::find(kKinds.begin(), kKinds.end(), v.kind);
      if (kind == kKinds.end()) return invalid;
      Constraint c{};
      c.id=id(v.id); c.kind=static_cast<ConstraintKind>(kind-kKinds.begin());
      c.first=id(v.first); c.second=id(v.second); c.target={v.target.x,v.target.y};
      c.internal=v.internal; c.value=v.value; c.side=v.side; c.reference=v.reference;
      if(v.placement) c.placement=DimensionPlacement{v.placement->offset,v.placement->along,v.placement->angle};
      snapshot.constraints.push_back(c);
    }
    if (!valid_ids) return invalid;
    auto sketch = Sketch::from_snapshot(snapshot);
    if (!sketch || check_constraints(*sketch).status != SolveStatus::kSolved) return invalid;
    return ProjectData{std::move(*sketch),*revision};
  } catch (const std::exception&) { return invalid; }
}

std::optional<std::string> encode_project(const ProjectData& project) {
  try {
    const auto snapshot = project.sketch.snapshot();
    if (snapshot.entities.size()+snapshot.constraints.size() > kProjectMaxRecords ||
        project.revision == UINT64_MAX || !Sketch::from_snapshot(snapshot) ||
        check_constraints(project.sketch).status != SolveStatus::kSolved) return std::nullopt;
    WireProject wire{"SketchCAD",1,"mm",std::to_string(project.revision),std::to_string(snapshot.next_id),{}, {}};
    for (const auto& entity : snapshot.entities) {
      wire.entities.push_back(std::visit([](const auto& v) -> WireEntity {
        using E = std::decay_t<decltype(v)>;
        const auto id = std::to_string(v.id);
        if constexpr(std::is_same_v<E,SketchPoint>) return WirePoint{id,v.construction,v.position.x,v.position.y};
        else if constexpr(std::is_same_v<E,SketchLine>) return WireLine{id,v.construction,std::to_string(v.start),std::to_string(v.end)};
        else if constexpr(std::is_same_v<E,SketchCircle>) return WireCircle{id,v.construction,std::to_string(v.center),v.radius};
        else return WireArc{id,v.construction,std::to_string(v.center),v.radius,v.start_angle,v.sweep_angle};
      },entity));
    }
    for (const auto& c : snapshot.constraints) {
      const auto index=static_cast<std::size_t>(c.kind);
      if(index>=kKinds.size()) return std::nullopt;
      WireConstraint v{std::to_string(c.id),std::string(kKinds[index]),std::to_string(c.first),
                       std::to_string(c.second),{c.target.x,c.target.y},c.internal,c.value,c.side,std::nullopt,c.reference};
      if(c.placement) v.placement=WirePlacement{c.placement->offset,c.placement->along,c.placement->angle};
      wire.constraints.push_back(v);
    }
    auto bytes = rfl::json::write<rfl::NoOptionals>(wire);
    if(bytes.size()>kProjectMaxBytes) return std::nullopt;
    return bytes;
  } catch(const std::exception&) { return std::nullopt; }
}
}  // namespace sketchcad
