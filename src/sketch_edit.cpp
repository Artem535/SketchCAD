#include "sketchcad/sketch_edit.h"

#include <algorithm>
#include <cmath>

#include "curve_geometry.h"

namespace sketchcad {
namespace {
using detail::crossings;
using detail::Curve;
using detail::curves;
using detail::kTwoPi;
using detail::on_extent;
using detail::point_position;

// Parameters closer than this are the same cut, or a curve's own end.
constexpr double kSame = 1e-9;

// A place where other curves cross the edited curve, by its parameter: t
// along a line, the angle travelled from an arc's start, or a circle's
// angle in [0, 2 pi).
struct Cut {
  double param;
  Position at;
  std::vector<EntityId> cutters;
};

std::optional<Curve> curve_of(const Sketch& s, EntityId id) {
  for (const Curve& k : curves(s, std::nullopt))
    if (k.id == id) return k;
  return std::nullopt;
}

double angle_of(const Curve& k, Position p) {
  return std::atan2(p.y - k.c.y, p.x - k.c.x);
}

// Angle travelled from `from` to `to` in the arc's direction, in [0, 2 pi).
double travelled(double from, double to, double sign) {
  double t = std::fmod((to - from) * sign, kTwoPi);
  if (t < 0) t += kTwoPi;
  return t;
}

double sign_of(const Curve& k) {
  return k.sweep && k.sweep->second < 0 ? -1.0 : 1.0;
}

double param_of(const Curve& k, Position p) {
  if (k.is_line) {
    const double dx = k.b.x - k.a.x, dy = k.b.y - k.a.y;
    return ((p.x - k.a.x) * dx + (p.y - k.a.y) * dy) / (dx * dx + dy * dy);
  }
  const double angle = angle_of(k, p);
  if (!k.sweep) {
    const double a = std::fmod(angle + kTwoPi, kTwoPi);
    return a;
  }
  return travelled(k.sweep->first, angle, sign_of(k));
}

Position at_param(const Curve& k, double t) {
  if (k.is_line) return {k.a.x + t * (k.b.x - k.a.x), k.a.y + t * (k.b.y - k.a.y)};
  const double angle = k.sweep ? k.sweep->first + sign_of(k) * t : t;
  return {k.c.x + k.r * std::cos(angle), k.c.y + k.r * std::sin(angle)};
}

// Crossings of the full line or circle of `k` with the extents of every
// other curve, grouped by parameter (unfiltered by `k`'s own extent).
std::vector<Cut> all_cuts(const Sketch& s, const Curve& k) {
  std::vector<Cut> cuts;
  for (const Curve& other : curves(s, std::nullopt)) {
    if (other.id == k.id) continue;
    for (Position q : crossings(k, other)) {
      if (!on_extent(other, q)) continue;
      const double t = param_of(k, q);
      auto same = std::find_if(cuts.begin(), cuts.end(), [&](const Cut& c) {
        return std::abs(c.param - t) <= kSame;
      });
      if (same == cuts.end())
        cuts.push_back({t, q, {other.id}});
      else if (std::find(same->cutters.begin(), same->cutters.end(),
                         other.id) == same->cutters.end())
        same->cutters.push_back(other.id);
    }
  }
  for (Cut& c : cuts) std::sort(c.cutters.begin(), c.cutters.end());
  std::sort(cuts.begin(), cuts.end(),
            [](const Cut& a, const Cut& b) { return a.param < b.param; });
  return cuts;
}

// Cuts strictly inside the curve's own extent.
std::vector<Cut> inner_cuts(const Sketch& s, const Curve& k) {
  const double end = k.is_line ? 1 : k.sweep ? std::abs(k.sweep->second) : kTwoPi;
  std::vector<Cut> cuts = all_cuts(s, k);
  // A circle has no ends; lines and arcs ignore cuts at their own ends.
  if (k.is_line || k.sweep)
    std::erase_if(cuts, [&](const Cut& c) {
      return c.param <= kSame || c.param >= end - kSame;
    });
  return cuts;
}

bool used_elsewhere(const Sketch& s, EntityId point, EntityId except) {
  for (const auto& [id, e] : s.entities()) {
    if (id == except) continue;
    if (const auto* l = std::get_if<SketchLine>(&e))
      if (l->start == point || l->end == point) return true;
    if (const auto* c = std::get_if<SketchCircle>(&e))
      if (c->center == point) return true;
    if (const auto* a = std::get_if<SketchArc>(&e))
      if (a->center == point) return true;
  }
  return false;
}

void unconstrain(Sketch& s, EntityId entity) {
  for (EntityId c : s.constraints_of(entity)) s.erase(c);
}

// Erases a point that no entity uses any more, with its constraints.
void drop_if_unused(Sketch& s, EntityId point) {
  if (used_elsewhere(s, point, 0)) return;
  unconstrain(s, point);
  s.erase(point);
}

// The whole curve, like the select-tool Delete.
bool erase_curve(Sketch& s, EntityId id) {
  std::vector<EntityId> defining;
  const Entity e = *s.entity(id);
  if (const auto* l = std::get_if<SketchLine>(&e)) defining = {l->start, l->end};
  if (const auto* c = std::get_if<SketchCircle>(&e)) defining = {c->center};
  if (const auto* a = std::get_if<SketchArc>(&e)) defining = {a->center};
  unconstrain(s, id);
  if (!s.erase(id)) return false;
  for (EntityId p : defining) drop_if_unused(s, p);
  return true;
}

// A new point at a cut, held on every cutter.
std::optional<EntityId> cut_point(Sketch& s, const Cut& cut) {
  const auto id = s.create_point(cut.at);
  if (!id) return std::nullopt;
  for (EntityId curve : cut.cutters)
    if (!s.add_constraint(ConstraintKind::kOnCurve, *id, curve)) return std::nullopt;
  return id;
}

// Driving lengths of `line` follow its new length.
bool update_lengths(Sketch& s, EntityId line) {
  const auto l = std::get<SketchLine>(*s.entity(line));
  const double length =
      detail::distance(point_position(s, l.start), point_position(s, l.end));
  for (EntityId c : s.constraints_of(line)) {
    const auto k = s.constraint(c);
    if (k->kind == ConstraintKind::kLength && !k->reference &&
        !s.set_dimension(c, length))
      return false;
  }
  return true;
}

// What a trim tap does: the removed parameter range [lo, hi] and the cuts
// bounding it (absent at a free end); `whole` when nothing cuts the curve.
struct TrimPlan {
  Curve curve;
  bool whole = false;
  std::optional<Cut> lo = std::nullopt, hi = std::nullopt;
};

std::optional<TrimPlan> plan_trim(const Sketch& s, EntityId id, Position at) {
  const auto k = curve_of(s, id);
  if (!k) return std::nullopt;
  TrimPlan plan{*k};
  const std::vector<Cut> cuts = inner_cuts(s, *k);
  if (cuts.empty()) {
    plan.whole = true;
    return plan;
  }
  double t = param_of(*k, at);
  if (k->sweep) {
    // Beyond the sweep: the nearer end.
    const double end = std::abs(k->sweep->second);
    if (t > end) t = t - end < kTwoPi - t ? end : 0;
  }
  if (!k->is_line && !k->sweep) {
    if (cuts.size() < 2) return std::nullopt;  // One cut cannot open a circle.
    // Cyclic neighbours of t.
    plan.lo = cuts.back();
    plan.hi = cuts.front();
    for (const Cut& c : cuts) {
      if (c.param < t) plan.lo = c;
      if (c.param > t) {
        plan.hi = c;
        break;
      }
    }
    return plan;
  }
  for (const Cut& c : cuts) {
    if (c.param < t) plan.lo = c;
    if (c.param > t && !plan.hi) plan.hi = c;
  }
  return plan;
}

// What an extend tap does: the moved end and the boundary it reaches.
struct ExtendPlan {
  Curve curve;
  bool at_end;  // The end (rather than the start) moves.
  Cut boundary;
};

std::optional<ExtendPlan> plan_extend(const Sketch& s, EntityId id, Position at) {
  const auto k = curve_of(s, id);
  if (!k || (!k->is_line && !k->sweep)) return std::nullopt;
  const Position start = at_param(*k, 0);
  const Position end = at_param(*k, k->is_line ? 1 : std::abs(k->sweep->second));
  const bool at_end = detail::distance(at, end) <= detail::distance(at, start);
  std::optional<Cut> best;
  double best_gap = INFINITY;
  for (const Cut& c : all_cuts(s, *k)) {
    double gap;
    if (k->is_line) {
      gap = at_end ? c.param - 1 : -c.param;
    } else {
      const double sweep = std::abs(k->sweep->second);
      // Ahead of the end in the arc's direction, or behind the start.
      gap = at_end ? c.param - sweep : kTwoPi - c.param;
      if (gap >= kTwoPi - sweep - kSame) continue;  // Would close the circle.
    }
    if (gap > kSame && gap < best_gap) {
      best_gap = gap;
      best = c;
    }
  }
  if (!best) return std::nullopt;
  return ExtendPlan{*k, at_end, *best};
}

CurvePiece segment(Position a, Position b) {
  CurvePiece p;
  p.a = a;
  p.b = b;
  return p;
}

CurvePiece arc_piece(const Curve& k, double from, double to) {
  // Parameters along the curve; for a circle `from` may exceed `to`.
  CurvePiece p;
  p.is_arc = true;
  p.center = k.c;
  p.radius = k.r;
  const double sign = sign_of(k);
  p.start = k.sweep ? k.sweep->first + sign * from : from;
  double span = to - from;
  if (span <= 0) span += kTwoPi;
  p.sweep = sign * span;
  return p;
}
}  // namespace

bool trim(Sketch& s, EntityId id, Position at) {
  const auto plan = plan_trim(s, id, at);
  if (!plan) return false;
  if (plan->whole) return erase_curve(s, id);
  const Curve& k = plan->curve;
  const Entity e = *s.entity(id);
  if (const auto* l = std::get_if<SketchLine>(&e)) {
    const EntityId old_start = l->start, old_end = l->end;
    if (plan->lo && plan->hi) {
      const auto a = cut_point(s, *plan->lo), b = cut_point(s, *plan->hi);
      if (!a || !b || !s.update_line(id, old_start, *a) ||
          !s.create_line(*b, old_end))
        return false;
    } else if (plan->hi) {
      const auto a = cut_point(s, *plan->hi);
      if (!a || !s.update_line(id, *a, old_end)) return false;
      drop_if_unused(s, old_start);
    } else {
      const auto b = cut_point(s, *plan->lo);
      if (!b || !s.update_line(id, old_start, *b)) return false;
      drop_if_unused(s, old_end);
    }
    return update_lengths(s, id);
  }
  if (const auto* a = std::get_if<SketchArc>(&e)) {
    const double sign = sign_of(k), sweep = std::abs(a->sweep_angle);
    if (plan->lo && plan->hi) {
      const auto piece = s.create_arc(
          a->center, a->radius, a->start_angle + sign * plan->hi->param,
          sign * (sweep - plan->hi->param), a->construction);
      return piece &&
             s.update_arc(id, a->center, a->radius, a->start_angle,
                          sign * plan->lo->param) &&
             s.add_constraint(ConstraintKind::kEqual, id, *piece);
    }
    if (plan->hi)
      return s.update_arc(id, a->center, a->radius,
                          a->start_angle + sign * plan->hi->param,
                          sign * (sweep - plan->hi->param));
    return s.update_arc(id, a->center, a->radius, a->start_angle,
                        sign * plan->lo->param);
  }
  // Circle: the rest runs from the cut after the piece round to the one
  // before it.
  return s.circle_to_arc(id, plan->hi->param,
                         travelled(plan->hi->param, plan->lo->param, 1));
}

std::optional<CurvePiece> trim_preview(const Sketch& s, EntityId id,
                                       Position at) {
  const auto plan = plan_trim(s, id, at);
  if (!plan) return std::nullopt;
  const Curve& k = plan->curve;
  const double end = k.is_line ? 1 : k.sweep ? std::abs(k.sweep->second) : kTwoPi;
  if (plan->whole) {
    if (k.is_line) return segment(k.a, k.b);
    return arc_piece(k, 0, end);
  }
  const double from = plan->lo ? plan->lo->param : 0;
  const double to = plan->hi ? plan->hi->param : end;
  if (k.is_line) return segment(at_param(k, from), at_param(k, to));
  return arc_piece(k, from, to);
}

bool extend(Sketch& s, EntityId id, Position at) {
  const auto plan = plan_extend(s, id, at);
  if (!plan) return false;
  const Entity e = *s.entity(id);
  if (const auto* l = std::get_if<SketchLine>(&e)) {
    const EntityId end = plan->at_end ? l->end : l->start;
    if (used_elsewhere(s, end, id)) return false;
    if (!s.update_point(end, plan->boundary.at)) return false;
    // Hold the end on the boundary, dropping holds on curves it leaves (an
    // earlier boundary).
    const auto& cutters = plan->boundary.cutters;
    std::vector<EntityId> held;
    for (EntityId c : s.constraints_of(end)) {
      const auto k = s.constraint(c);
      if (k->kind != ConstraintKind::kOnCurve || k->first != end) continue;
      if (std::find(cutters.begin(), cutters.end(), k->second) == cutters.end())
        s.erase(c);
      else
        held.push_back(k->second);
    }
    for (EntityId curve : cutters)
      if (std::find(held.begin(), held.end(), curve) == held.end() &&
          !s.add_constraint(ConstraintKind::kOnCurve, end, curve))
        return false;
    return update_lengths(s, id);
  }
  const auto& a = std::get<SketchArc>(e);
  const double sign = sign_of(plan->curve), sweep = std::abs(a.sweep_angle);
  if (plan->at_end)
    return s.update_arc(id, a.center, a.radius, a.start_angle,
                        sign * plan->boundary.param);
  const double back = kTwoPi - plan->boundary.param;
  return s.update_arc(id, a.center, a.radius, a.start_angle - sign * back,
                      sign * (sweep + back));
}

std::optional<CurvePiece> extend_preview(const Sketch& s, EntityId id,
                                         Position at) {
  const auto plan = plan_extend(s, id, at);
  if (!plan) return std::nullopt;
  const Curve& k = plan->curve;
  if (k.is_line) {
    if (plan->at_end) {
      if (used_elsewhere(s, std::get<SketchLine>(*s.entity(id)).end, id))
        return std::nullopt;
      return segment(k.b, plan->boundary.at);
    }
    if (used_elsewhere(s, std::get<SketchLine>(*s.entity(id)).start, id))
      return std::nullopt;
    return segment(k.a, plan->boundary.at);
  }
  const double sweep = std::abs(k.sweep->second);
  if (plan->at_end) return arc_piece(k, sweep, plan->boundary.param);
  return arc_piece(k, plan->boundary.param, kTwoPi);
}
}  // namespace sketchcad
