#pragma once

#include <optional>
#include <vector>

#include "sketchcad/sketch.h"

namespace sketchcad {
// Local DOF and dependency analysis (diagnostics.adoc, ADR-0003).
enum class DiagnosisStatus { kConsistent, kRedundant, kConflicting, kUnknown };
enum class UnknownReason {
  kNone,
  kInvalidGeometry,
  kNumericalFailure,
  kIllConditioned,
  kTooLarge,
};

struct Diagnosis {
  DiagnosisStatus status = DiagnosisStatus::kUnknown;
  UnknownReason reason = UnknownReason::kNone;
  std::optional<int> dof;
  // Sketch parameters counted for DOF: 2 per point, 1 per circle, 3 per arc.
  int parameters = 0;
  std::optional<int> rank;
  // Constraints in a linear dependency; not a minimal set. Ascending IDs.
  std::vector<EntityId> dependent;
  // Constraints out of tolerance after the solve attempt. Ascending IDs.
  std::vector<EntityId> violated;
  bool operator==(const Diagnosis&) const = default;
};

// Analyses persistent constraints only; never changes `sketch`.
Diagnosis diagnose(const Sketch& sketch);
}  // namespace sketchcad
