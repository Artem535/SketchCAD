#pragma once

#include <array>
#include <numbers>
#include <utility>
#include <vector>

#include "sketchcad/sketch.h"
#include "sketchcad/sketch_view.h"

namespace sketchcad {
// Screen-constant ESKD style of ADR-0004, in pixels.
struct DimensionStyle {
  double offset = 32;
  double overshoot = 6;
  double arrow_length = 12;
  double arrow_half_angle = 10 * std::numbers::pi / 180;
  double text_gap = 4;
  double angle_radius_min = 32;
  double angle_radius_max = 120;
};

struct ScreenArc {
  ScreenPoint center;
  double radius;
  double start;  // Screen radians (Y down).
  double sweep;
};

// Drawing of one driving dimension in screen pixels (dimension-style.adoc).
struct DimensionGraphic {
  EntityId id = 0;
  ConstraintKind kind = ConstraintKind::kLength;
  double value = 0;
  std::vector<std::pair<ScreenPoint, ScreenPoint>> segments;
  std::vector<ScreenArc> arcs;
  std::vector<std::array<ScreenPoint, 3>> arrows;  // Tip first.
  ScreenPoint text_position{0, 0};  // Bottom centre of the text.
  double text_angle = 0;            // In [-pi/2, pi/2).
};

std::vector<DimensionGraphic> layout_dimensions(const Sketch&,
                                                const ViewTransform&,
                                                const DimensionStyle& = {});
}  // namespace sketchcad
