#include "sketchcad/sketch_view.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sketchcad {
namespace {
constexpr double kTwoPi = 2 * std::numbers::pi;

double distance(Position a, Position b) {
  return std::hypot(a.x - b.x, a.y - b.y);
}

Position point_position(const Sketch& s, EntityId id) {
  return std::get<SketchPoint>(s.entity(id).value()).position;
}

double segment_distance(Position p, Position a, Position b) {
  const double dx = b.x - a.x, dy = b.y - a.y;
  const double length2 = dx * dx + dy * dy;
  if (length2 == 0) return distance(p, a);
  const double t =
      std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length2, 0.0, 1.0);
  return distance(p, {a.x + t * dx, a.y + t * dy});
}

// True when `angle` lies within the signed sweep that starts at `start`.
bool within_sweep(double angle, double start, double sweep) {
  double t = std::fmod(sweep > 0 ? angle - start : start - angle, kTwoPi);
  if (t < 0) t += kTwoPi;
  return t <= std::abs(sweep);
}

double arc_distance(Position p, const SketchArc& arc, Position c) {
  const double angle = std::atan2(p.y - c.y, p.x - c.x);
  if (within_sweep(angle, arc.start_angle, arc.sweep_angle))
    return std::abs(distance(p, c) - arc.radius);
  const double end = arc.start_angle + arc.sweep_angle;
  const Position a{c.x + arc.radius * std::cos(arc.start_angle),
                   c.y + arc.radius * std::sin(arc.start_angle)};
  const Position b{c.x + arc.radius * std::cos(end),
                   c.y + arc.radius * std::sin(end)};
  return std::min(distance(p, a), distance(p, b));
}

std::optional<double> curve_distance(const Sketch& s, const Entity& e,
                                     Position p) {
  if (const auto* l = std::get_if<SketchLine>(&e))
    return segment_distance(p, point_position(s, l->start),
                            point_position(s, l->end));
  if (const auto* c = std::get_if<SketchCircle>(&e))
    return std::abs(distance(p, point_position(s, c->center)) - c->radius);
  if (const auto* a = std::get_if<SketchArc>(&e))
    return arc_distance(p, *a, point_position(s, a->center));
  return std::nullopt;
}

std::optional<EntityId> nearest_point(const Sketch& s, Position p,
                                      double tolerance,
                                      std::optional<EntityId> exclude) {
  std::optional<EntityId> best;
  double best_distance = tolerance;
  for (const auto& [id, e] : s.entities()) {
    const auto* point = std::get_if<SketchPoint>(&e);
    if (!point || id == exclude) continue;
    const double d = distance(p, point->position);
    if (d < best_distance || (d == best_distance && !best)) {
      best = id;
      best_distance = d;
    }
  }
  return best;
}
}  // namespace

ScreenPoint ViewTransform::to_screen(Position p) const {
  return {p.x * scale_ + offset_x_, offset_y_ - p.y * scale_};
}

Position ViewTransform::to_world(ScreenPoint p) const {
  return {(p.x - offset_x_) / scale_, (offset_y_ - p.y) / scale_};
}

void ViewTransform::pan(double dx, double dy) {
  offset_x_ += dx;
  offset_y_ += dy;
}

void ViewTransform::zoom_at(ScreenPoint anchor, double factor) {
  if (!std::isfinite(factor) || factor <= 0) return;
  const Position world = to_world(anchor);
  scale_ = std::clamp(scale_ * factor, kMinScale, kMaxScale);
  offset_x_ = anchor.x - world.x * scale_;
  offset_y_ = anchor.y + world.y * scale_;
}

void ViewTransform::fit(const std::optional<Bounds>& b, double width,
                        double height, double margin_px) {
  const Bounds area = b.value_or(Bounds{{0, 0}, {100, 100}});
  double span_x = area.max.x - area.min.x, span_y = area.max.y - area.min.y;
  if (span_x <= 0 && span_y <= 0) span_x = span_y = 10;
  const double room_x = std::max(width - 2 * margin_px, 1.0);
  const double room_y = std::max(height - 2 * margin_px, 1.0);
  double s = kMaxScale;
  if (span_x > 0) s = std::min(s, room_x / span_x);
  if (span_y > 0) s = std::min(s, room_y / span_y);
  scale_ = std::clamp(s, kMinScale, kMaxScale);
  offset_x_ = width / 2 - (area.min.x + area.max.x) / 2 * scale_;
  offset_y_ = height / 2 + (area.min.y + area.max.y) / 2 * scale_;
}

std::optional<Bounds> bounds(const Sketch& s) {
  std::optional<Bounds> result;
  const auto extend = [&](Position lo, Position hi) {
    if (!result) {
      result = Bounds{lo, hi};
      return;
    }
    result->min = {std::min(result->min.x, lo.x),
                   std::min(result->min.y, lo.y)};
    result->max = {std::max(result->max.x, hi.x),
                   std::max(result->max.y, hi.y)};
  };
  for (const auto& [id, e] : s.entities()) {
    if (const auto* p = std::get_if<SketchPoint>(&e)) {
      extend(p->position, p->position);
      continue;
    }
    std::optional<EntityId> center;
    double r = 0;
    if (const auto* c = std::get_if<SketchCircle>(&e)) {
      center = c->center;
      r = c->radius;
    } else if (const auto* a = std::get_if<SketchArc>(&e)) {
      center = a->center;
      r = a->radius;
    }
    if (center) {
      const Position c = point_position(s, *center);
      extend({c.x - r, c.y - r}, {c.x + r, c.y + r});
    }
  }
  return result;
}

std::optional<EntityId> pick(const Sketch& s, Position p, double tolerance_mm) {
  if (auto point = nearest_point(s, p, tolerance_mm, std::nullopt))
    return point;
  std::optional<EntityId> best;
  double best_distance = tolerance_mm;
  for (const auto& [id, e] : s.entities()) {
    const auto d = curve_distance(s, e, p);
    if (d && (*d < best_distance || (*d == best_distance && !best))) {
      best = id;
      best_distance = *d;
    }
  }
  return best;
}

SnapResult snap(const Sketch& s, Position p, const SnapSettings& settings,
                std::optional<EntityId> exclude) {
  if (!settings.enabled) return {p, SnapKind::kNone, std::nullopt};
  if (auto point = nearest_point(s, p, settings.point_tolerance_mm, exclude))
    return {point_position(s, *point), SnapKind::kPoint, point};
  const double step = settings.grid_step_mm;
  if (!std::isfinite(step) || step <= 0)
    return {p, SnapKind::kNone, std::nullopt};
  return {{std::round(p.x / step) * step, std::round(p.y / step) * step},
          SnapKind::kGrid,
          std::nullopt};
}

double grid_step_for_scale(double scale_px_per_mm, double min_px) {
  const double target = min_px / scale_px_per_mm;
  if (!std::isfinite(target) || target <= 0) return 1;
  const double decade = std::pow(10.0, std::floor(std::log10(target)));
  for (double factor : {1.0, 2.0, 5.0, 10.0}) {
    const double step = factor * decade;
    if (step >= target * (1 - 1e-12)) return step;
  }
  return 10 * decade;
}
}  // namespace sketchcad
