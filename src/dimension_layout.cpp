#include "sketchcad/dimension_layout.h"

#include <algorithm>
#include <cmath>

namespace sketchcad {
namespace {
constexpr double kPi = std::numbers::pi;
constexpr double kEps = 1e-9;
// Free space next to a dimension line for its arrows and text, px.
constexpr double kRoom = 8;
constexpr double kTail = 10;     // Dimension line past outside arrows.
constexpr double kLeader = 24;   // Outside radius leader past the arrow.
constexpr double kRadiusShare = 0.6;

using P = ScreenPoint;
P operator+(P a, P b) { return {a.x + b.x, a.y + b.y}; }
P operator-(P a, P b) { return {a.x - b.x, a.y - b.y}; }
P operator*(P a, double k) { return {a.x * k, a.y * k}; }
double dot(P a, P b) { return a.x * b.x + a.y * b.y; }
double cross(P a, P b) { return a.x * b.y - a.y * b.x; }
double norm(P a) { return std::hypot(a.x, a.y); }
P unit(P a) { return a * (1 / norm(a)); }
P mid(P a, P b) { return (a + b) * 0.5; }
P polar(double angle) { return {std::cos(angle), std::sin(angle)}; }

// ESKD: text along the dimension line, never upside down; vertical reads
// bottom to top.
double readable(double angle) {
  while (angle >= kPi / 2) angle -= kPi;
  while (angle < -kPi / 2) angle += kPi;
  return angle;
}
// The text's own up direction on screen.
P up(double text_angle) { return {std::sin(text_angle), -std::cos(text_angle)}; }

class Builder {
 public:
  Builder(const DimensionStyle& style, DimensionGraphic& g)
      : style_(style), g_(g) {}

  void segment(P a, P b) { g_.segments.push_back({a, b}); }

  void arrow(P tip, P direction) {
    const P base = tip - direction * style_.arrow_length;
    const P side = P{-direction.y, direction.x} *
                   (style_.arrow_length * std::tan(style_.arrow_half_angle));
    g_.arrows.push_back({tip, base + side, base - side});
  }

  void text(P at, double direction) {
    g_.text_angle = readable(direction);
    g_.text_position = at + up(g_.text_angle) * style_.text_gap;
  }

  // Dimension line between a and b with arrows at both ends; outside
  // arrows pointing inward when there is no room between them.
  void dimension_line(P a, P b) {
    const double length = norm(b - a);
    if (length < kEps) {
      text(a, 0);
      return;
    }
    const P u = unit(b - a);
    if (length >= 2 * style_.arrow_length + kRoom) {
      segment(a, b);
      arrow(a, u * -1);
      arrow(b, u);
    } else {
      const double tail = style_.arrow_length + kTail;
      segment(a - u * tail, b + u * tail);
      arrow(a, u);
      arrow(b, u * -1);
    }
    text(mid(a, b), std::atan2(u.y, u.x));
  }

  // Aligned linear dimension of a-b offset along the unit normal n.
  void linear(P a, P b, P n) {
    const double reach = style_.offset + style_.overshoot;
    segment(a, a + n * reach);
    segment(b, b + n * reach);
    dimension_line(a + n * style_.offset, b + n * style_.offset);
  }

  // Extension along `ray` from the segment's farthest point on it to just
  // past `radius`, when the segment does not reach the arc.
  void ray_extension(P vertex, P ray, P e1, P e2, double radius) {
    const double reach = std::max({dot(e1 - vertex, ray), dot(e2 - vertex, ray), 0.0});
    if (reach < radius)
      segment(vertex + ray * reach, vertex + ray * (radius + style_.overshoot));
  }

 private:
  const DimensionStyle& style_;
  DimensionGraphic& g_;
};

// Normal of a-b facing away from `centre`; up, or left for vertical, on a
// tie.
P outward(P a, P b, P centre) {
  const P u = unit(b - a);
  P n{-u.y, u.x};
  const double side = dot(n, mid(a, b) - centre);
  if (std::abs(side) > 1e-6) return side > 0 ? n : n * -1;
  if (std::abs(n.y) > 1e-9) return n.y < 0 ? n : n * -1;
  return n.x < 0 ? n : n * -1;
}
}  // namespace

std::vector<DimensionGraphic> layout_dimensions(const Sketch& sketch,
                                                const ViewTransform& view,
                                                const DimensionStyle& style) {
  std::vector<DimensionGraphic> result;
  const auto box = bounds(sketch);
  const P centre = box ? view.to_screen({(box->min.x + box->max.x) / 2,
                                         (box->min.y + box->max.y) / 2})
                       : P{0, 0};
  const auto point = [&](EntityId id) {
    return view.to_screen(std::get<SketchPoint>(*sketch.entity(id)).position);
  };
  const auto ends = [&](EntityId line) {
    const auto l = std::get<SketchLine>(*sketch.entity(line));
    return std::pair{point(l.start), point(l.end)};
  };
  const auto is_line = [&](EntityId id) {
    return std::holds_alternative<SketchLine>(*sketch.entity(id));
  };

  for (const auto& [id, c] : sketch.constraints()) {
    if (!is_dimension(c.kind)) continue;
    DimensionGraphic g;
    g.id = id;
    g.kind = c.kind;
    g.value = c.value;
    Builder b(style, g);
    switch (c.kind) {
      case ConstraintKind::kLength: {
        const auto [s, e] = ends(c.first);
        if (norm(e - s) < kEps) b.text(s, 0);
        else b.linear(s, e, outward(s, e, centre));
        break;
      }
      case ConstraintKind::kDistance: {
        const P p = point(c.first);
        if (!is_line(c.second)) {
          const P q = point(c.second);
          if (norm(q - p) < kEps) b.text(p, 0);
          else b.linear(p, q, outward(p, q, centre));
          break;
        }
        const auto [s, e] = ends(c.second);
        const P d = e - s;
        const double t = norm(d) < kEps ? 0 : dot(p - s, d) / dot(d, d);
        const P foot = s + d * t;
        if (t < 0 || t > 1) {
          const P from = t < 0 ? s : e;
          b.segment(from, foot + unit(foot - from) * style.overshoot);
        }
        b.dimension_line(p, foot);
        break;
      }
      case ConstraintKind::kRadius: {
        const Entity e = *sketch.entity(c.first);
        const auto* circle = std::get_if<SketchCircle>(&e);
        const auto* arc = std::get_if<SketchArc>(&e);
        const P centre_px = point(circle ? circle->center : arc->center);
        const double r = (circle ? circle->radius : arc->radius) * view.scale();
        // World angles are counter-clockwise; screen Y is flipped.
        const double world = circle ? kPi / 4
                                    : arc->start_angle + arc->sweep_angle / 2;
        const P u{std::cos(world), -std::sin(world)};
        const P tip = centre_px + u * r;
        const double direction = std::atan2(u.y, u.x);
        if (r >= 2 * style.arrow_length) {
          b.segment(centre_px, tip);
          b.arrow(tip, u);
          b.text(mid(centre_px, tip), direction);
        } else {
          const P end = centre_px + u * (r + style.arrow_length + kLeader);
          b.segment(tip, end);
          b.arrow(tip, u * -1);
          b.text(mid(tip + u * style.arrow_length, end), direction);
        }
        break;
      }
      case ConstraintKind::kAngle: {
        const auto [a1, b1] = ends(c.first);
        const auto [a2, b2] = ends(c.second);
        const P d1 = b1 - a1, d2 = b2 - a2;
        const double det = cross(d1, d2);
        if (std::abs(det) <= 1e-9 * norm(d1) * norm(d2)) {
          b.text(mid(mid(a1, b1), mid(a2, b2)), 0);
          break;
        }
        const P vertex = a1 + d1 * (cross(a2 - a1, d2) / det);
        const auto ray = [&](P s, P e) {
          return unit(norm(s - vertex) > norm(e - vertex) ? s - vertex
                                                          : e - vertex);
        };
        const P r1 = ray(a1, b1);
        P r2 = ray(a2, b2);
        const double between = std::acos(std::clamp(dot(r1, r2), -1.0, 1.0));
        if (std::abs(between - c.value) > 1e-6) r2 = r2 * -1;
        const double reach1 = std::max(dot(a1 - vertex, r1), dot(b1 - vertex, r1));
        const double reach2 = std::max(dot(a2 - vertex, r2), dot(b2 - vertex, r2));
        const double shorter = std::min(std::max(reach1, 0.0), std::max(reach2, 0.0));
        const double radius = std::clamp(kRadiusShare * shorter,
                                         style.angle_radius_min,
                                         style.angle_radius_max);
        const double start = std::atan2(r1.y, r1.x);
        double sweep = std::atan2(r2.y, r2.x) - start;
        while (sweep > kPi) sweep -= 2 * kPi;
        while (sweep <= -kPi) sweep += 2 * kPi;
        g.arcs.push_back({vertex, radius, start, sweep});
        b.ray_extension(vertex, r1, a1, b1, radius);
        b.ray_extension(vertex, r2, a2, b2, radius);
        // Tangents at the ends, pointing away from the arc's middle; inward
        // from outside when the arc is too short.
        const double sign = sweep >= 0 ? 1 : -1;
        const bool room =
            radius * std::abs(sweep) >= 2 * style.arrow_length + kRoom;
        const auto tangent = [](double angle) {
          return P{-std::sin(angle), std::cos(angle)};
        };
        const double end = start + sweep;
        b.arrow(vertex + polar(start) * radius,
                tangent(start) * (room ? -sign : sign));
        b.arrow(vertex + polar(end) * radius,
                tangent(end) * (room ? sign : -sign));
        const double middle = start + sweep / 2;
        b.text(vertex + polar(middle) * radius, middle + kPi / 2);
        break;
      }
      default:
        break;
    }
    result.push_back(std::move(g));
  }
  return result;
}
}  // namespace sketchcad
