#pragma once

#include <map>

#include "sketchcad/solver.h"

namespace sketchcad {
// Moves `point` as close to `target` as the constraints allow and the rest
// of the geometry as little as practical; every hard constraint holds in
// the result. Writes back only on kSolved (U03, constrained-drag.adoc).
SolveResult drag_point(Sketch& sketch, EntityId point, Position target);
// Several dragged points at once, e.g. both ends of a dragged line (U03b).
SolveResult drag_points(Sketch& sketch,
                        const std::map<EntityId, Position>& targets);
}  // namespace sketchcad
