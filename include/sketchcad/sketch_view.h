#pragma once

#include <optional>
#include <vector>

#include "sketchcad/sketch.h"

namespace sketchcad {
struct ScreenPoint {
  double x;
  double y;
  bool operator==(const ScreenPoint&) const = default;
};

struct Bounds {
  Position min;
  Position max;
  bool operator==(const Bounds&) const = default;
};

// World millimetres (Y up) to screen pixels (Y down). Views never touch the
// document.
class ViewTransform {
 public:
  static constexpr double kMinScale = 0.05;
  static constexpr double kMaxScale = 200;

  double scale() const { return scale_; }
  ScreenPoint to_screen(Position) const;
  Position to_world(ScreenPoint) const;
  void pan(double dx, double dy);
  // Keeps the world point under `anchor` fixed; the scale is clamped.
  void zoom_at(ScreenPoint anchor, double factor);
  // Frames `bounds` (or a default 100x100 mm area) inside the viewport.
  void fit(const std::optional<Bounds>& bounds, double width, double height,
           double margin_px = 32);

 private:
  double scale_ = 4;
  double offset_x_ = 0;
  double offset_y_ = 0;
};

// Axis-aligned extents of all entities; circles and arcs use the full circle.
std::optional<Bounds> bounds(const Sketch&);

// Nearest entity within tolerance; points win over curves, ties by lower ID.
std::optional<EntityId> pick(const Sketch&, Position, double tolerance_mm);

enum class SnapKind { kNone, kGrid, kPoint, kIntersection, kOnCurve };

struct SnapSettings {
  bool enabled = true;
  double grid_step_mm = 1;
  double point_tolerance_mm = 1;
  // Curve snaps prefer where the curve crosses a grid line.
  bool grid_on_curves = false;
};

struct SnapResult {
  Position position;
  SnapKind kind;
  std::optional<EntityId> point;
  // kIntersection, kOnCurve: the one or two curves the position lies on.
  std::vector<EntityId> curves = {};
  bool operator==(const SnapResult&) const = default;
};

// Existing points first, then curve intersections, the nearest curve point
// and the grid (sketch-editing.adoc).
// `exclude` is skipped, e.g. the point being dragged.
SnapResult snap(const Sketch&, Position, const SnapSettings&,
                std::optional<EntityId> exclude = std::nullopt);

// Smallest step from the 1-2-5 series that is at least `min_px` on screen.
double grid_step_for_scale(double scale_px_per_mm, double min_px = 8);
}  // namespace sketchcad
