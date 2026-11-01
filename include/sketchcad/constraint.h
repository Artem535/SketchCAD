#pragma once

#include <cstdint>
#include <optional>

namespace sketchcad {
using EntityId = std::uint64_t;
struct Position {
  double x;
  double y;
  bool operator==(const Position&) const = default;
};

// Geometric relations solved as hard residuals (constraint-solver.adoc).
enum class ConstraintKind {
  kCoincident,
  kHorizontal,
  kVertical,
  kParallel,
  kPerpendicular,
  kTangent,
  kTangentStart,
  kTangentEnd,
  kEqual,
  kFix,
  // A point on a line (infinite), circle or arc (its full circle).
  kOnCurve,
  // Driving dimensions (dimensions.adoc): constraints with a value.
  kLength,
  kDistance,
  kAngle,
  kRadius,
};

constexpr bool is_dimension(ConstraintKind kind) {
  return kind == ConstraintKind::kLength || kind == ConstraintKind::kDistance ||
         kind == ConstraintKind::kAngle || kind == ConstraintKind::kRadius;
}

// Manual position of a dimension (dimension-placement.adoc), world units.
// Linear: signed offset along the left normal of first -> second and the
// text position `along` in [0, 1]. Radius: leader `angle` and text distance
// `offset` from the centre. Angle: arc radius `offset`.
struct DimensionPlacement {
  double offset = 0;
  double along = 0.5;
  double angle = 0;
  bool operator==(const DimensionPlacement&) const = default;
};

struct Constraint {
  EntityId id;
  ConstraintKind kind;
  EntityId first;
  // Zero for unary kinds.
  EntityId second = 0;
  // kFix: position captured at creation.
  Position target{0, 0};
  // Circle-circle kTangent: internal instead of external contact.
  // Arc endpoint tangency: captured negative normal branch, i.e.
  // cross(line.end-line.start, center-contact) < 0 at creation.
  bool internal = false;
  // Dimensions: millimetres, or radians for kAngle.
  double value = 0;
  // Point-line kDistance: side at creation (+1 or -1).
  // kTangentStart/kTangentEnd: +1 binds line.start, -1 binds line.end.
  int side = 1;
  // Dimensions: set when the user moved the dimension; automatic otherwise.
  std::optional<DimensionPlacement> placement;
  // Reference dimension (U10): shown, never solved.
  bool reference = false;
  bool operator==(const Constraint&) const = default;
};
}  // namespace sketchcad
