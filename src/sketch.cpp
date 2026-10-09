#include "sketchcad/sketch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <type_traits>
#include <utility>

namespace sketchcad {
namespace {
bool valid_position(Position p) {
  return std::isfinite(p.x) && std::isfinite(p.y);
}
bool valid_radius(double radius) { return std::isfinite(radius) && radius > 0; }
bool valid_arc(double radius, double start, double sweep) {
  return valid_radius(radius) && std::isfinite(start) && std::isfinite(sweep) &&
         std::abs(sweep) > 0 && std::abs(sweep) < 2 * std::numbers::pi;
}
// Exhaustion (zero) is higher than any still usable watermark.
EntityId higher_watermark(EntityId a, EntityId b) {
  return a == 0 || b == 0 ? 0 : std::max(a, b);
}
// Center point and radius of a circle or arc.
std::pair<EntityId, double> curve_of(const Entity& entity) {
  if (const auto* c = std::get_if<SketchCircle>(&entity))
    return {c->center, c->radius};
  const auto& a = std::get<SketchArc>(entity);
  return {a.center, a.radius};
}
bool references_point(const Entity& entity, EntityId id) {
  return std::visit(
      [id](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, SketchLine>)
          return value.start == id || value.end == id;
        else if constexpr (std::is_same_v<T, SketchCircle> ||
                           std::is_same_v<T, SketchArc>)
          return value.center == id;
        else
          return false;
      },
      entity);
}
}  // namespace
Sketch& Sketch::operator=(const Sketch& other) {
  if (this == &other) return *this;
  auto candidate = other.entities_;
  auto constraints = other.constraints_;
  entities_.swap(candidate);
  constraints_.swap(constraints);
  next_id_ = higher_watermark(next_id_, other.next_id_);
  return *this;
}
Sketch& Sketch::operator=(Sketch&& other) {
  if (this == &other) return *this;
  entities_ = std::move(other.entities_);
  constraints_ = std::move(other.constraints_);
  next_id_ = higher_watermark(next_id_, other.next_id_);
  return *this;
}
bool Sketch::is_point(EntityId id) const {
  auto it = entities_.find(id);
  return it != entities_.end() &&
         std::holds_alternative<SketchPoint>(it->second);
}
std::optional<EntityId> Sketch::allocate() {
  if (next_id_ == 0) return std::nullopt;
  const EntityId id = next_id_;
  next_id_ = id == std::numeric_limits<EntityId>::max() ? 0 : id + 1;
  return id;
}
std::optional<EntityId> Sketch::insert(Entity entity) {
  const auto id = allocate();
  if (!id) return std::nullopt;
  std::visit([id](auto& value) { value.id = *id; }, entity);
  entities_.emplace(*id, std::move(entity));
  return id;
}
std::optional<EntityId> Sketch::create_point(Position p, bool construction) {
  if (!valid_position(p)) return std::nullopt;
  return insert(SketchPoint{0, p, construction});
}
std::optional<EntityId> Sketch::create_line(EntityId start, EntityId end,
                                            bool construction) {
  if (start == end || !is_point(start) || !is_point(end)) return std::nullopt;
  return insert(SketchLine{0, start, end, construction});
}
std::optional<EntityId> Sketch::create_circle(EntityId center, double radius,
                                              bool construction) {
  if (!is_point(center) || !valid_radius(radius)) return std::nullopt;
  return insert(SketchCircle{0, center, radius, construction});
}
std::optional<EntityId> Sketch::create_arc(EntityId center, double radius,
                                           double start, double sweep,
                                           bool construction) {
  if (!is_point(center) || !valid_arc(radius, start, sweep))
    return std::nullopt;
  return insert(SketchArc{0, center, radius, start, sweep, construction});
}
bool Sketch::update_point(EntityId id, Position p) {
  auto it = entities_.find(id);
  if (it == entities_.end() || !valid_position(p)) return false;
  auto* point = std::get_if<SketchPoint>(&it->second);
  if (!point) return false;
  point->position = p;
  return true;
}
bool Sketch::update_line(EntityId id, EntityId start, EntityId end) {
  auto it = entities_.find(id);
  if (it == entities_.end() || start == end || !is_point(start) ||
      !is_point(end))
    return false;
  auto* line = std::get_if<SketchLine>(&it->second);
  if (!line) return false;
  line->start = start;
  line->end = end;
  return true;
}
bool Sketch::update_circle(EntityId id, EntityId center, double radius) {
  auto it = entities_.find(id);
  if (it == entities_.end() || !is_point(center) || !valid_radius(radius))
    return false;
  auto* circle = std::get_if<SketchCircle>(&it->second);
  if (!circle) return false;
  circle->center = center;
  circle->radius = radius;
  return true;
}
bool Sketch::update_arc(EntityId id, EntityId center, double radius,
                        double start, double sweep) {
  auto it = entities_.find(id);
  if (it == entities_.end() || !is_point(center) ||
      !valid_arc(radius, start, sweep))
    return false;
  auto* arc = std::get_if<SketchArc>(&it->second);
  if (!arc) return false;
  arc->center = center;
  arc->radius = radius;
  arc->start_angle = start;
  arc->sweep_angle = sweep;
  return true;
}
bool Sketch::set_construction(EntityId id, bool construction) {
  auto it = entities_.find(id);
  if (it == entities_.end()) return false;
  std::visit([construction](auto& value) { value.construction = construction; },
             it->second);
  return true;
}
bool Sketch::erase(EntityId id) {
  if (constraints_.erase(id)) return true;
  auto it = entities_.find(id);
  if (it == entities_.end() || !constraints_of(id).empty()) return false;
  if (std::holds_alternative<SketchPoint>(it->second)) {
    for (const auto& [other_id, entity] : entities_)
      if (references_point(entity, id)) return false;
  }
  entities_.erase(it);
  return true;
}
std::optional<Entity> Sketch::entity(EntityId id) const {
  auto it = entities_.find(id);
  if (it == entities_.end()) return std::nullopt;
  return it->second;
}
std::optional<Polyline> Sketch::create_polyline(
    std::span<const Position> positions, bool closed, bool construction) {
  if (positions.size() < (closed ? 3u : 2u)) return std::nullopt;
  for (auto p : positions)
    if (!valid_position(p)) return std::nullopt;
  Sketch candidate = *this;
  Polyline result;
  result.points.reserve(positions.size());
  result.lines.reserve(positions.size() - (closed ? 0 : 1));
  for (auto p : positions) {
    auto id = candidate.create_point(p, construction);
    if (!id) return std::nullopt;
    result.points.push_back(*id);
  }
  const auto count = positions.size() - (closed ? 0 : 1);
  for (std::size_t i = 0; i < count; ++i) {
    auto id = candidate.create_line(result.points[i],
                                    result.points[(i + 1) % positions.size()],
                                    construction);
    if (!id) return std::nullopt;
    result.lines.push_back(*id);
  }
  *this = std::move(candidate);
  return result;
}
std::optional<Polyline> Sketch::create_rectangle(Position origin, double width,
                                                 double height,
                                                 bool construction) {
  if (!valid_position(origin) || !valid_radius(width) || !valid_radius(height))
    return std::nullopt;
  const double right = origin.x + width, top = origin.y + height;
  if (!std::isfinite(right) || !std::isfinite(top) || right == origin.x ||
      top == origin.y)
    return std::nullopt;
  const std::array<Position, 4> positions{
      {origin, {right, origin.y}, {right, top}, {origin.x, top}}};
  return create_polyline(positions, true, construction);
}

std::optional<EntityId> Sketch::add_constraint(ConstraintKind kind,
                                               EntityId first,
                                               EntityId second) {
  const auto get = [&](EntityId id) -> const Entity* {
    auto it = entities_.find(id);
    return it == entities_.end() ? nullptr : &it->second;
  };
  const auto point = [&](EntityId id) {
    const Entity* e = get(id);
    return e && std::holds_alternative<SketchPoint>(*e);
  };
  const auto line = [&](EntityId id) {
    const Entity* e = get(id);
    return e && std::holds_alternative<SketchLine>(*e);
  };
  const auto curve = [&](EntityId id) {
    const Entity* e = get(id);
    return e && (std::holds_alternative<SketchCircle>(*e) ||
                 std::holds_alternative<SketchArc>(*e));
  };
  const bool pair = second != 0 && first != second;
  Constraint c{};
  c.kind = kind;
  c.first = first;
  c.second = second;
  switch (kind) {
    case ConstraintKind::kCoincident:
      if (!pair || !point(first) || !point(second)) return std::nullopt;
      break;
    case ConstraintKind::kHorizontal:
    case ConstraintKind::kVertical:
      if (!(second == 0 && line(first)) &&
          !(pair && point(first) && point(second)))
        return std::nullopt;
      break;
    case ConstraintKind::kParallel:
    case ConstraintKind::kPerpendicular:
      if (!pair || !line(first) || !line(second)) return std::nullopt;
      break;
    case ConstraintKind::kTangent:
      if (!pair) return std::nullopt;
      if (curve(first) && line(second)) std::swap(c.first, c.second);
      if (line(c.first) && curve(c.second)) break;
      if (!curve(first) || !curve(second)) return std::nullopt;
      {
        // Internal or external contact, whichever is closer now.
        const auto [c1, r1] = curve_of(*get(first));
        const auto [c2, r2] = curve_of(*get(second));
        const Position a = std::get<SketchPoint>(*get(c1)).position;
        const Position b = std::get<SketchPoint>(*get(c2)).position;
        const double d = std::hypot(a.x - b.x, a.y - b.y);
        c.internal = std::abs(d - std::abs(r1 - r2)) < std::abs(d - (r1 + r2));
      }
      break;
    case ConstraintKind::kEqual:
      if (!pair || !((line(first) && line(second)) ||
                     (curve(first) && curve(second))))
        return std::nullopt;
      break;
    case ConstraintKind::kOnCurve:
      // A point onto a curve it does not define.
      if (!pair || !point(first) || !(line(second) || curve(second)) ||
          references_point(*get(second), first))
        return std::nullopt;
      break;
    case ConstraintKind::kFix:
      if (second != 0 || !point(first)) return std::nullopt;
      c.target = std::get<SketchPoint>(*get(first)).position;
      break;
    default:
      return std::nullopt;
  }
  const auto id = allocate();
  if (!id) return std::nullopt;
  c.id = *id;
  constraints_.emplace(*id, c);
  return id;
}
std::optional<EntityId> Sketch::add_dimension(ConstraintKind kind,
                                              EntityId first, EntityId second,
                                              double value) {
  const auto get = [&](EntityId id) -> const Entity* {
    auto it = entities_.find(id);
    return it == entities_.end() ? nullptr : &it->second;
  };
  const auto line = [&](EntityId id) {
    const Entity* e = get(id);
    return e && std::holds_alternative<SketchLine>(*e);
  };
  const auto curve = [&](EntityId id) {
    const Entity* e = get(id);
    return e && (std::holds_alternative<SketchCircle>(*e) ||
                 std::holds_alternative<SketchArc>(*e));
  };
  Constraint c{};
  c.kind = kind;
  c.first = first;
  c.second = second;
  c.value = value;
  switch (kind) {
    case ConstraintKind::kLength:
      if (second != 0 || !line(first)) return std::nullopt;
      break;
    case ConstraintKind::kDistance:
      if (second == 0 || first == second) return std::nullopt;
      if (line(first) && is_point(second)) std::swap(c.first, c.second);
      if (is_point(c.first) && line(c.second)) {
        // Remember the side of the line the point is on now.
        const auto& l = std::get<SketchLine>(*get(c.second));
        const Position a = std::get<SketchPoint>(*get(l.start)).position;
        const Position b = std::get<SketchPoint>(*get(l.end)).position;
        const Position p = std::get<SketchPoint>(*get(c.first)).position;
        const double cross =
            (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        c.side = cross < 0 ? -1 : 1;
      } else if (!is_point(first) || !is_point(second)) {
        return std::nullopt;
      }
      break;
    case ConstraintKind::kAngle:
      if (second == 0 || first == second || !line(first) || !line(second))
        return std::nullopt;
      break;
    case ConstraintKind::kRadius:
      if (second != 0 || !curve(first)) return std::nullopt;
      break;
    default:
      return std::nullopt;
  }
  if (!valid_dimension(c, value)) return std::nullopt;
  const auto id = allocate();
  if (!id) return std::nullopt;
  c.id = *id;
  constraints_.emplace(*id, c);
  return id;
}
bool Sketch::set_dimension(EntityId id, double value) {
  auto it = constraints_.find(id);
  if (it == constraints_.end() || !is_dimension(it->second.kind) ||
      !valid_dimension(it->second, value))
    return false;
  it->second.value = value;
  return true;
}
bool Sketch::set_dimension_placement(
    EntityId id, std::optional<DimensionPlacement> placement) {
  auto it = constraints_.find(id);
  if (it == constraints_.end() || !is_dimension(it->second.kind)) return false;
  Constraint& c = it->second;
  // Point-line distances stay automatic (dimension-placement.adoc).
  if (c.kind == ConstraintKind::kDistance && !is_point(c.second)) return false;
  if (placement) {
    const auto& [offset, along, angle] = *placement;
    if (!std::isfinite(offset) || !std::isfinite(along) ||
        !std::isfinite(angle) || along < 0 || along > 1)
      return false;
    if ((c.kind == ConstraintKind::kRadius ||
         c.kind == ConstraintKind::kAngle) &&
        offset <= 0)
      return false;
  }
  c.placement = placement;
  return true;
}
bool Sketch::valid_dimension(const Constraint& c, double value) const {
  if (!std::isfinite(value)) return false;
  switch (c.kind) {
    case ConstraintKind::kAngle:
      return value > 0 && value < std::numbers::pi;
    case ConstraintKind::kDistance:
      // Zero point-line distance puts the point on the line.
      return value > 0 || (value == 0 && !is_point(c.second));
    default:
      return value > 0;
  }
}
std::optional<Constraint> Sketch::constraint(EntityId id) const {
  auto it = constraints_.find(id);
  if (it == constraints_.end()) return std::nullopt;
  return it->second;
}
std::vector<EntityId> Sketch::constraints_of(EntityId entity) const {
  std::vector<EntityId> ids;
  for (const auto& [id, c] : constraints_)
    if (c.first == entity || c.second == entity) ids.push_back(id);
  return ids;
}
bool Sketch::operator==(const Sketch& other) const {
  return entities_ == other.entities_ && constraints_ == other.constraints_;
}
}  // namespace sketchcad
