#pragma once

#include <optional>
#include <vector>

#include "sketchcad/document.h"
#include "sketchcad/sketch.h"

namespace sketchcad {
enum class SolveStatus {
  kSolved,
  kInvalidInput,
  kUnsatisfied,
  kNumericalFailure,
  // Satisfied only by collapsing a line or a radius.
  kDegenerate,
};

struct SolveResult {
  SolveStatus status;
  // Constraints whose hard residual is out of tolerance after solving.
  std::vector<EntityId> violated;
  double max_length_residual = 0;
  double max_angle_residual = 0;
};

inline constexpr double kLengthTolerance = 1e-7;
inline constexpr double kAngleTolerance = 1e-9;

// Current value of a dimension kind for the given references, nullopt if
// they are invalid or the value is undefined.
std::optional<double> measure(const Sketch& sketch, ConstraintKind kind,
                              EntityId first, EntityId second = 0);

// Solves all constraints of `sketch`; writes back only on kSolved.
SolveResult solve(Sketch& sketch);

// Document commit step that rejects any change the solver cannot satisfy.
Edit solver_step();
}  // namespace sketchcad
