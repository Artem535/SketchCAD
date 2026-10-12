#pragma once

#include <optional>

#include "sketchcad/sketch.h"

namespace sketchcad {
// A piece of a curve for display: a segment a-b, or an arc (sketch-editing.adoc,
// part 2).
struct CurvePiece {
  bool is_arc = false;
  Position a{0, 0}, b{0, 0};
  Position center{0, 0};
  double radius = 0;
  double start = 0;
  double sweep = 0;
};

// nanoCAD-style trim and extend; every other curve is a cutting edge or a
// boundary. False and unchanged when nothing applies.
bool trim(Sketch&, EntityId curve, Position at);
bool extend(Sketch&, EntityId curve, Position at);

// The piece that trim would remove, or extend would add.
std::optional<CurvePiece> trim_preview(const Sketch&, EntityId curve,
                                       Position at);
std::optional<CurvePiece> extend_preview(const Sketch&, EntityId curve,
                                         Position at);
}  // namespace sketchcad
