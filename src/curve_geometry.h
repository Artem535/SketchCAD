#pragma once
// Internal to sketchcad_core: curves as plain geometry, shared by the snaps
// (sketch_view.cpp) and trim/extend (sketch_edit.cpp). Not installed.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

#include "sketchcad/sketch.h"

namespace sketchcad::detail {
inline constexpr double kTwoPi = 2 * std::numbers::pi;

inline double distance(Position a, Position b) {
  return std::hypot(a.x - b.x, a.y - b.y);
}

inline Position point_position(const Sketch& s, EntityId id) {
  return std::get<SketchPoint>(s.entity(id).value()).position;
}

// True when `angle` lies within the signed sweep that starts at `start`.
inline bool within_sweep(double angle, double start, double sweep) {
  double t = std::fmod(sweep > 0 ? angle - start : start - angle, kTwoPi);
  if (t < 0) t += kTwoPi;
  return t <= std::abs(sweep);
}

// A line segment or a circle/arc as plain geometry.
struct Curve {
  EntityId id;
  bool is_line;
  Position a, b;  // Line ends.
  Position c;     // Centre.
  double r = 0;
  std::optional<std::pair<double, double>> sweep;  // Arc start, sweep.
};

// Every curve of the sketch, skipping those defined by `exclude`.
inline std::vector<Curve> curves(const Sketch& s,
                                 std::optional<EntityId> exclude) {
  std::vector<Curve> out;
  for (const auto& [id, e] : s.entities()) {
    if (const auto* l = std::get_if<SketchLine>(&e)) {
      if (l->start == exclude || l->end == exclude) continue;
      out.push_back({id, true, point_position(s, l->start),
                     point_position(s, l->end), {}, 0, std::nullopt});
    } else if (const auto* c = std::get_if<SketchCircle>(&e)) {
      if (c->center == exclude) continue;
      out.push_back(
          {id, false, {}, {}, point_position(s, c->center), c->radius, {}});
    } else if (const auto* a = std::get_if<SketchArc>(&e)) {
      if (a->center == exclude) continue;
      out.push_back({id, false, {}, {}, point_position(s, a->center),
                     a->radius, std::pair{a->start_angle, a->sweep_angle}});
    }
  }
  return out;
}

// True when `p`, known to lie on the curve's line or circle, is on the
// segment or within the arc's sweep.
inline bool on_extent(const Curve& k, Position p) {
  constexpr double kSlack = 1e-9;
  if (k.is_line) {
    const double dx = k.b.x - k.a.x, dy = k.b.y - k.a.y;
    const double t = ((p.x - k.a.x) * dx + (p.y - k.a.y) * dy) /
                     (dx * dx + dy * dy);
    return t >= -kSlack && t <= 1 + kSlack;
  }
  return !k.sweep || within_sweep(std::atan2(p.y - k.c.y, p.x - k.c.x),
                                  k.sweep->first, k.sweep->second);
}

// Intersections of the full lines or circles; extents are checked later.
inline std::vector<Position> crossings(const Curve& u, const Curve& v) {
  if (!u.is_line && v.is_line) return crossings(v, u);
  std::vector<Position> out;
  if (u.is_line && v.is_line) {
    const double d1x = u.b.x - u.a.x, d1y = u.b.y - u.a.y;
    const double d2x = v.b.x - v.a.x, d2y = v.b.y - v.a.y;
    const double den = d1x * d2y - d1y * d2x;
    if (std::abs(den) <= 1e-12 * std::hypot(d1x, d1y) * std::hypot(d2x, d2y))
      return out;  // Parallel or coincident: no unique point.
    const double t =
        ((v.a.x - u.a.x) * d2y - (v.a.y - u.a.y) * d2x) / den;
    out.push_back({u.a.x + t * d1x, u.a.y + t * d1y});
  } else if (u.is_line) {
    const double dx = u.b.x - u.a.x, dy = u.b.y - u.a.y;
    const double fx = u.a.x - v.c.x, fy = u.a.y - v.c.y;
    const double a = dx * dx + dy * dy, b = 2 * (fx * dx + fy * dy);
    const double disc = b * b - 4 * a * (fx * fx + fy * fy - v.r * v.r);
    if (a == 0 || disc < 0) return out;
    for (double sign : {-1.0, 1.0}) {
      const double t = (-b + sign * std::sqrt(disc)) / (2 * a);
      out.push_back({u.a.x + t * dx, u.a.y + t * dy});
    }
  } else {
    const double dx = v.c.x - u.c.x, dy = v.c.y - u.c.y;
    const double d = std::hypot(dx, dy);
    if (d < 1e-12 || d > u.r + v.r || d < std::abs(u.r - v.r)) return out;
    const double along = (u.r * u.r - v.r * v.r + d * d) / (2 * d);
    const double h = std::sqrt(std::max(u.r * u.r - along * along, 0.0));
    const Position base{u.c.x + along * dx / d, u.c.y + along * dy / d};
    for (double sign : {-1.0, 1.0})
      out.push_back({base.x - sign * h * dy / d, base.y + sign * h * dx / d});
  }
  return out;
}

// Nearest point of the curve's extent to `p`, if any.
inline std::optional<Position> nearest_on(const Curve& k, Position p) {
  if (k.is_line) {
    const double dx = k.b.x - k.a.x, dy = k.b.y - k.a.y;
    const double length2 = dx * dx + dy * dy;
    if (length2 == 0) return k.a;
    const double t = std::clamp(
        ((p.x - k.a.x) * dx + (p.y - k.a.y) * dy) / length2, 0.0, 1.0);
    return Position{k.a.x + t * dx, k.a.y + t * dy};
  }
  const double d = distance(p, k.c);
  if (d == 0) return std::nullopt;
  const Position q{k.c.x + k.r * (p.x - k.c.x) / d,
                   k.c.y + k.r * (p.y - k.c.y) / d};
  if (!on_extent(k, q)) return std::nullopt;
  return q;
}

}  // namespace sketchcad::detail
