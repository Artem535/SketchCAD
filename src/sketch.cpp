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
  entities_.swap(candidate);
  next_id_ = higher_watermark(next_id_, other.next_id_);
  return *this;
}
Sketch& Sketch::operator=(Sketch&& other) {
  if (this == &other) return *this;
  entities_ = std::move(other.entities_);
  next_id_ = higher_watermark(next_id_, other.next_id_);
  return *this;
}
bool Sketch::is_point(EntityId id) const {
  auto it = entities_.find(id);
  return it != entities_.end() &&
         std::holds_alternative<SketchPoint>(it->second);
}
std::optional<EntityId> Sketch::insert(Entity entity) {
  if (next_id_ == 0) return std::nullopt;
  const EntityId id = next_id_;
  std::visit([id](auto& value) { value.id = id; }, entity);
  entities_.emplace(id, std::move(entity));
  next_id_ = id == std::numeric_limits<EntityId>::max() ? 0 : id + 1;
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
  auto it = entities_.find(id);
  if (it == entities_.end()) return false;
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
}  // namespace sketchcad
