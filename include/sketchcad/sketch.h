#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "sketchcad/constraint.h"

namespace sketchcad {
struct SketchPoint {
  EntityId id;
  Position position;
  bool construction = false;
  bool operator==(const SketchPoint&) const = default;
};
struct SketchLine {
  EntityId id;
  EntityId start;
  EntityId end;
  bool construction = false;
  bool operator==(const SketchLine&) const = default;
};
struct SketchCircle {
  EntityId id;
  EntityId center;
  double radius;
  bool construction = false;
  bool operator==(const SketchCircle&) const = default;
};
struct SketchArc {
  EntityId id;
  EntityId center;
  double radius;
  double start_angle;
  double sweep_angle;
  bool construction = false;
  bool operator==(const SketchArc&) const = default;
};
using Entity = std::variant<SketchPoint, SketchLine, SketchCircle, SketchArc>;
struct Polyline {
  std::vector<EntityId> points;
  std::vector<EntityId> lines;
  bool operator==(const Polyline&) const = default;
};
class Sketch {
 public:
  Sketch() = default;
  Sketch(const Sketch&) = default;
  Sketch(Sketch&&) = default;
  Sketch& operator=(const Sketch&);
  Sketch& operator=(Sketch&&);
  std::optional<EntityId> create_point(Position, bool construction = false);
  std::optional<EntityId> create_line(EntityId start, EntityId end,
                                      bool construction = false);
  std::optional<EntityId> create_circle(EntityId center, double radius,
                                        bool construction = false);
  std::optional<EntityId> create_arc(EntityId center, double radius,
                                     double start_angle, double sweep_angle,
                                     bool construction = false);
  bool update_point(EntityId, Position);
  bool update_line(EntityId, EntityId start, EntityId end);
  bool update_circle(EntityId, EntityId center, double radius);
  bool update_arc(EntityId, EntityId center, double radius, double start_angle,
                  double sweep_angle);
  bool set_construction(EntityId, bool);
  // Erases an entity or a constraint. Entities referenced by another
  // entity or by a constraint are kept.
  bool erase(EntityId);
  std::optional<Entity> entity(EntityId) const;
  const std::map<EntityId, Entity>& entities() const { return entities_; }
  std::optional<Polyline> create_polyline(std::span<const Position>,
                                          bool closed = false,
                                          bool construction = false);
  std::optional<Polyline> create_rectangle(Position origin, double width,
                                           double height,
                                           bool construction = false);

  std::optional<EntityId> add_constraint(ConstraintKind kind, EntityId first,
                                         EntityId second = 0);
  std::optional<Constraint> constraint(EntityId) const;
  const std::map<EntityId, Constraint>& constraints() const {
    return constraints_;
  }
  std::vector<EntityId> constraints_of(EntityId entity) const;

  // Content equality: entities and constraints, not the allocator.
  bool operator==(const Sketch& other) const;

 private:
  bool is_point(EntityId) const;
  std::optional<EntityId> allocate();
  std::optional<EntityId> insert(Entity);
  std::map<EntityId, Entity> entities_;
  std::map<EntityId, Constraint> constraints_;
  // Zero denotes exhaustion, not the next usable ID.
  EntityId next_id_ = 1;
};
}  // namespace sketchcad
