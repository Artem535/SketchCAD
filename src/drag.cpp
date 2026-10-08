#include "sketchcad/drag.h"

namespace sketchcad {
SolveResult drag_point(Sketch&, EntityId, Position) {
  return {SolveStatus::kInvalidInput, {}};
}
}  // namespace sketchcad
