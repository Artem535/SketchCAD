#pragma once

#include <cstdint>

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
  kEqual,
  kFix,
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

struct Constraint {
  EntityId id;
  ConstraintKind kind;
  EntityId first;
  // Zero for unary kinds.
  EntityId second = 0;
  // kFix: position captured at creation.
  Position target{0, 0};
  // Circle-circle kTangent: internal instead of external contact.
  bool internal = false;
  // Dimensions: millimetres, or radians for kAngle.
  double value = 0;
  // Point-line kDistance: side of the line at creation (+1 or -1).
  int side = 1;
  bool operator==(const Constraint&) const = default;
};
}  // namespace sketchcad
