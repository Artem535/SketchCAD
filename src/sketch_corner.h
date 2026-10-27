#pragma once

#include <array>
#include <optional>

#include "sketchcad/sketch.h"

namespace sketchcad::detail {
// Topology only: two incident lines, no other geometry using the point.
inline std::optional<std::array<SketchLine, 2>> corner_lines(const Sketch& s,
                                                             EntityId point) {
  const auto entity = s.entity(point);
  if (!entity || !std::holds_alternative<SketchPoint>(*entity))
    return std::nullopt;
  std::array<SketchLine, 2> result{};
  std::size_t count = 0;
  for (const auto& [id, e] : s.entities()) {
    if (const auto* line = std::get_if<SketchLine>(&e)) {
      if (line->start != point && line->end != point) continue;
      if (count == result.size()) return std::nullopt;
      result[count++] = *line;
    } else if (const auto* circle = std::get_if<SketchCircle>(&e)) {
      if (circle->center == point) return std::nullopt;
    } else if (const auto* arc = std::get_if<SketchArc>(&e)) {
      if (arc->center == point) return std::nullopt;
    }
  }
  if (count != result.size()) return std::nullopt;
  return result;
}
}  // namespace sketchcad::detail
