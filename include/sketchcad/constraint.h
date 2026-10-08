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
  bool internal = false;
  bool operator==(const Constraint&) const = default;
};
}  // namespace sketchcad
