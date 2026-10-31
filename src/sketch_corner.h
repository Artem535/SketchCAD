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
  // Arc contacts reference a line endpoint through their binding metadata.
  // Such a point already joins an arc as well as its incident lines.
  for (const auto& [id, c] : s.constraints()) {
    if (c.kind != ConstraintKind::kTangentStart &&
        c.kind != ConstraintKind::kTangentEnd)
      continue;
    const auto line_entity = s.entity(c.first);
    if (!line_entity) continue;
    if (const auto* line = std::get_if<SketchLine>(&*line_entity))
      if ((c.side == 1 ? line->start : line->end) == point) return std::nullopt;
  }
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
