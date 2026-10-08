#pragma once

#include "sketchcad/solver.h"

namespace sketchcad {
// Moves `point` as close to `target` as the constraints allow and the rest
// of the geometry as little as practical; every hard constraint holds in
// the result. Writes back only on kSolved (U03, constrained-drag.adoc).
SolveResult drag_point(Sketch& sketch, EntityId point, Position target);
}  // namespace sketchcad
