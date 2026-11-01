#include "sketchcad/sketch_profile.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <set>
#include <tuple>
#include <utility>

#include "curve_geometry.h"
#include "sketchcad/solver.h"

namespace sketchcad {
namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kTwoPi = 2.0 * std::numbers::pi;

struct DisjointSet {
  explicit DisjointSet(std::size_t n) : parent(n) {
    std::iota(parent.begin(), parent.end(), 0);
  }
  std::size_t find(std::size_t x) {
    while (parent[x] != x) {
      parent[x] = parent[parent[x]];
      x = parent[x];
    }
    return x;
  }
  void unite(std::size_t a, std::size_t b) {
    a = find(a);
    b = find(b);
    if (a != b) parent[b] = a;
  }
  std::vector<std::size_t> parent;
};

void append_unique_position(std::vector<Position>& list, Position p,
                            double tol = 1e-4) {
  for (const Position& q : list) {
    if (detail::distance(p, q) <= tol) return;
  }
  list.push_back(p);
}

double compute_loop_signed_area(const std::vector<ProfileEdge>& edges) {
  if (edges.size() == 1 && edges[0].kind == ProfileEdgeKind::kCircle) {
    const double sign = edges[0].sweep_angle >= 0.0 ? 1.0 : -1.0;
    return sign * kPi * edges[0].radius * edges[0].radius;
  }
  const Position origin = edges.front().start;
  double area = 0.0;
  for (const ProfileEdge& e : edges) {
    area += 0.5 * ((e.start.x - origin.x) * (e.end.y - origin.y) -
                   (e.start.y - origin.y) * (e.end.x - origin.x));
    if (e.kind == ProfileEdgeKind::kArc) {
      area +=
          0.5 * e.radius * e.radius * (e.sweep_angle - std::sin(e.sweep_angle));
    }
  }
  return area;
}

void reverse_loop(ProfileLoop& loop) {
  std::reverse(loop.edges.begin(), loop.edges.end());
  for (ProfileEdge& e : loop.edges) {
    std::swap(e.start, e.end);
    if (e.kind == ProfileEdgeKind::kArc) {
      e.start_angle = e.start_angle + e.sweep_angle;
      e.sweep_angle = -e.sweep_angle;
    } else if (e.kind == ProfileEdgeKind::kCircle) {
      e.sweep_angle = -e.sweep_angle;
    }
  }
  loop.signed_area = -loop.signed_area;
}

Position sample_point_on_loop(const ProfileLoop& loop) {
  const ProfileEdge& e = loop.edges.front();
  if (e.kind == ProfileEdgeKind::kLine) {
    return {0.5 * (e.start.x + e.end.x), 0.5 * (e.start.y + e.end.y)};
  }
  const double mid = e.start_angle + 0.5 * e.sweep_angle;
  return {e.center.x + e.radius * std::cos(mid),
          e.center.y + e.radius * std::sin(mid)};
}

bool ray_y_hits_critical(const ProfileLoop& loop, double yr) {
  constexpr double kCritEps = 1e-7;
  for (const ProfileEdge& e : loop.edges) {
    if (e.kind == ProfileEdgeKind::kLine) {
      if (std::abs(e.start.y - yr) <= kCritEps ||
          std::abs(e.end.y - yr) <= kCritEps) {
        return true;
      }
    } else {
      if (std::abs(e.start.y - yr) <= kCritEps ||
          std::abs(e.end.y - yr) <= kCritEps ||
          std::abs((e.center.y + e.radius) - yr) <= kCritEps ||
          std::abs((e.center.y - e.radius) - yr) <= kCritEps) {
        return true;
      }
    }
  }
  return false;
}

bool loop_contains_point(const ProfileLoop& loop, Position p) {
  if (loop.edges.empty()) return false;
  if (loop.edges.size() == 1 &&
      loop.edges[0].kind == ProfileEdgeKind::kCircle) {
    return detail::distance(p, loop.edges[0].center) < loop.edges[0].radius;
  }

  double yr = p.y;
  if (ray_y_hits_critical(loop, yr)) {
    for (int step = 1; step <= 64; ++step) {
      const double candidate_pos = p.y + step * 1.37e-6;
      if (!ray_y_hits_critical(loop, candidate_pos)) {
        yr = candidate_pos;
        break;
      }
      const double candidate_neg = p.y - step * 1.37e-6;
      if (!ray_y_hits_critical(loop, candidate_neg)) {
        yr = candidate_neg;
        break;
      }
    }
  }

  int crossings = 0;
  for (const ProfileEdge& e : loop.edges) {
    if (e.kind == ProfileEdgeKind::kLine) {
      const bool above0 = e.start.y > yr;
      const bool above1 = e.end.y > yr;
      if (above0 != above1) {
        const double xhit = e.start.x + (yr - e.start.y) *
                                            (e.end.x - e.start.x) /
                                            (e.end.y - e.start.y);
        if (xhit > p.x) ++crossings;
      }
    } else if (e.kind == ProfileEdgeKind::kArc) {
      const double dy = yr - e.center.y;
      if (std::abs(dy) >= e.radius) continue;
      const double dx = std::sqrt(std::max(0.0, e.radius * e.radius - dy * dy));
      for (double sign : {-1.0, 1.0}) {
        const double xhit = e.center.x + sign * dx;
        if (xhit <= p.x) continue;
        const double angle = std::atan2(dy, xhit - e.center.x);
        if (detail::within_sweep(angle, e.start_angle, e.sweep_angle)) {
          ++crossings;
        }
      }
    }
  }
  return (crossings % 2) == 1;
}

bool on_curve_with_tol(const detail::Curve& c, Position p, double tol) {
  if (c.is_line) {
    const double dx = c.b.x - c.a.x;
    const double dy = c.b.y - c.a.y;
    const double len2 = dx * dx + dy * dy;
    if (len2 <= 0.0) return detail::distance(p, c.a) <= tol;
    const double t =
        std::clamp(((p.x - c.a.x) * dx + (p.y - c.a.y) * dy) / len2, 0.0, 1.0);
    const Position proj{c.a.x + t * dx, c.a.y + t * dy};
    return detail::distance(p, proj) <= tol;
  }
  if (std::abs(detail::distance(p, c.c) - c.r) > tol) return false;
  if (!c.sweep) return true;
  const double a0 = c.sweep->first;
  const double sw = c.sweep->second;
  const Position p0{c.c.x + c.r * std::cos(a0), c.c.y + c.r * std::sin(a0)};
  const Position p1{c.c.x + c.r * std::cos(a0 + sw),
                    c.c.y + c.r * std::sin(a0 + sw)};
  if (detail::distance(p, p0) <= tol || detail::distance(p, p1) <= tol) {
    return true;
  }
  return detail::within_sweep(std::atan2(p.y - c.c.y, p.x - c.c.x), a0, sw);
}

std::optional<Position> overlap_midpoint(const detail::Curve& u,
                                         const detail::Curve& v, double tol) {
  if (u.is_line != v.is_line) return std::nullopt;
  if (u.is_line) {
    const double dx = u.b.x - u.a.x;
    const double dy = u.b.y - u.a.y;
    const double len = std::hypot(dx, dy);
    if (len <= tol) return std::nullopt;
    const double ux = dx / len;
    const double uy = dy / len;
    const double dist_va =
        std::abs((v.a.x - u.a.x) * uy - (v.a.y - u.a.y) * ux);
    const double dist_vb =
        std::abs((v.b.x - u.a.x) * uy - (v.b.y - u.a.y) * ux);
    if (dist_va > tol || dist_vb > tol) return std::nullopt;
    double t0 = (v.a.x - u.a.x) * ux + (v.a.y - u.a.y) * uy;
    double t1 = (v.b.x - u.a.x) * ux + (v.b.y - u.a.y) * uy;
    if (t0 > t1) std::swap(t0, t1);
    const double lo = std::max(0.0, t0);
    const double hi = std::min(len, t1);
    if (hi - lo > tol) {
      const double mid = 0.5 * (lo + hi);
      return Position{u.a.x + mid * ux, u.a.y + mid * uy};
    }
    return std::nullopt;
  }

  // Concentric equal-radius circles/arcs.
  if (detail::distance(u.c, v.c) > tol || std::abs(u.r - v.r) > tol) {
    return std::nullopt;
  }
  if (!u.sweep && !v.sweep) {
    return Position{u.c.x + u.r, u.c.y};
  }
  if (!u.sweep) {
    const double mid = v.sweep->first + 0.5 * v.sweep->second;
    return Position{v.c.x + v.r * std::cos(mid), v.c.y + v.r * std::sin(mid)};
  }
  if (!v.sweep) {
    const double mid = u.sweep->first + 0.5 * u.sweep->second;
    return Position{u.c.x + u.r * std::cos(mid), u.c.y + u.r * std::sin(mid)};
  }
  // Compare unwrapped CCW angular intervals, including across the 2pi seam.
  auto interval = [](const detail::Curve& curve) {
    double start = curve.sweep->first;
    if (curve.sweep->second < 0.0) start += curve.sweep->second;
    start = std::fmod(start, kTwoPi);
    if (start < 0.0) start += kTwoPi;
    return std::pair{start, start + std::abs(curve.sweep->second)};
  };
  const auto [u_lo, u_hi] = interval(u);
  const auto [v_lo, v_hi] = interval(v);
  for (double shift : {-kTwoPi, 0.0, kTwoPi}) {
    const double lo = std::max(u_lo, v_lo + shift);
    const double hi = std::min(u_hi, v_hi + shift);
    // Normalization and shifted endpoint sums can differ by a few ulps at
    // a shared seam. This is angular roundoff, not a geometric length cutoff.
    const double roundoff =
        4.0 * std::numeric_limits<double>::epsilon() *
        std::max({kTwoPi, std::abs(u_lo), std::abs(u_hi),
                  std::abs(v_lo + shift), std::abs(v_hi + shift)});
    if (hi - lo > roundoff) {
      const double mid = lo + 0.5 * (hi - lo);
      return Position{u.c.x + u.r * std::cos(mid), u.c.y + u.r * std::sin(mid)};
    }
  }
  return std::nullopt;
}

std::vector<Position> curve_intersections_with_tol(const detail::Curve& u,
                                                   const detail::Curve& v,
                                                   double tol) {
  std::vector<Position> hits = detail::crossings(u, v);
  // Catch near-tangent circle/circle and line/circle contacts within tol.
  if (!u.is_line && !v.is_line) {
    const double dx = v.c.x - u.c.x;
    const double dy = v.c.y - u.c.y;
    const double d = std::hypot(dx, dy);
    if (d > tol) {
      if (std::abs(d - (u.r + v.r)) <= tol ||
          std::abs(d - std::abs(u.r - v.r)) <= tol) {
        const double sign =
            (std::abs(d - (u.r + v.r)) <= tol || u.r >= v.r) ? 1.0 : -1.0;
        hits.push_back(
            {u.c.x + sign * u.r * dx / d, u.c.y + sign * u.r * dy / d});
      }
    }
  } else if (u.is_line != v.is_line) {
    const detail::Curve& ln = u.is_line ? u : v;
    const detail::Curve& cr = u.is_line ? v : u;
    if (const auto foot = detail::nearest_on(ln, cr.c)) {
      const double dist = detail::distance(*foot, cr.c);
      if (std::abs(dist - cr.r) <= tol) hits.push_back(*foot);
    }
  }

  std::vector<Position> valid;
  for (const Position& p : hits) {
    if (on_curve_with_tol(u, p, tol) && on_curve_with_tol(v, p, tol)) {
      append_unique_position(valid, p, tol * 2.0);
    }
  }
  return valid;
}

}  // namespace

MergeResult merge_nearby_vertices(Sketch& sketch, double tolerance,
                                 bool collapse_short_edges) {
  if (!std::isfinite(tolerance) || tolerance <= 0.0 || tolerance > 1e6) {
    return {MergeStatus::kInvalidTolerance, 0};
  }

  std::set<EntityId> center_points;
  std::set<EntityId> line_endpoints;
  for (const auto& [id, e] : sketch.entities()) {
    if (const auto* c = std::get_if<SketchCircle>(&e)) {
      center_points.insert(c->center);
    } else if (const auto* a = std::get_if<SketchArc>(&e)) {
      center_points.insert(a->center);
    } else if (const auto* l = std::get_if<SketchLine>(&e)) {
      if (!l->construction) {
        line_endpoints.insert(l->start);
        line_endpoints.insert(l->end);
      }
    }
  }

  std::set<EntityId> fixed_points;
  std::set<EntityId> bound_arc_points;
  for (const auto& [cid, c] : sketch.constraints()) {
    if (c.kind == ConstraintKind::kFix) {
      fixed_points.insert(c.first);
    } else if (c.kind == ConstraintKind::kTangentStart ||
               c.kind == ConstraintKind::kTangentEnd) {
      if (const auto ent = sketch.entity(c.first)) {
        if (const auto* l = std::get_if<SketchLine>(&*ent)) {
          bound_arc_points.insert(c.side == 1 ? l->start : l->end);
        }
      }
    }
  }

  std::vector<SketchPoint> eligible;
  for (EntityId pid : line_endpoints) {
    if (center_points.contains(pid)) continue;
    const auto ent = sketch.entity(pid);
    if (!ent) continue;
    const auto* pt = std::get_if<SketchPoint>(&*ent);
    if (!pt || pt->construction) continue;
    eligible.push_back(*pt);
  }
  if (eligible.size() < 2) {
    return {MergeStatus::kNothingToMerge, 0};
  }

  DisjointSet dsu(eligible.size());
  bool any_pair = false;
  for (std::size_t i = 0; i < eligible.size(); ++i) {
    for (std::size_t j = i + 1; j < eligible.size(); ++j) {
      if (detail::distance(eligible[i].position, eligible[j].position) <=
          tolerance) {
        dsu.unite(i, j);
        any_pair = true;
      }
    }
  }
  if (!any_pair) {
    return {MergeStatus::kNothingToMerge, 0};
  }

  std::map<std::size_t, std::vector<std::size_t>> groups;
  for (std::size_t i = 0; i < eligible.size(); ++i) {
    groups[dsu.find(i)].push_back(i);
  }

  std::map<EntityId, EntityId> remap;
  std::size_t merged_points = 0;
  auto priority_of = [&](EntityId id) {
    if (fixed_points.contains(id)) return 2;
    if (bound_arc_points.contains(id)) return 1;
    return 0;
  };

  for (const auto& [root, members] : groups) {
    if (members.size() < 2) continue;
    std::size_t best = members[0];
    for (std::size_t idx : members) {
      const int p_cand = priority_of(eligible[idx].id);
      const int p_best = priority_of(eligible[best].id);
      if (p_cand > p_best ||
          (p_cand == p_best && eligible[idx].id < eligible[best].id)) {
        best = idx;
      }
    }
    const EntityId survivor = eligible[best].id;
    for (std::size_t idx : members) {
      if (eligible[idx].id != survivor) {
        remap[eligible[idx].id] = survivor;
        ++merged_points;
      }
    }
  }
  if (merged_points == 0) {
    return {MergeStatus::kNothingToMerge, 0};
  }

  auto map_id = [&](EntityId id) {
    const auto it = remap.find(id);
    return it == remap.end() ? id : it->second;
  };
  auto was_point = [&](EntityId id) {
    const auto ent = sketch.entity(id);
    return ent && std::holds_alternative<SketchPoint>(*ent);
  };

  SketchSnapshot snap = sketch.snapshot();
  std::vector<Entity> new_entities;
  new_entities.reserve(snap.entities.size());
  std::map<EntityId, SketchLine> updated_lines;
  std::map<EntityId, SketchLine> removed_lines;
  std::set<EntityId> collapsed_points;
  for (Entity& e : snap.entities) {
    if (const auto* pt = std::get_if<SketchPoint>(&e)) {
      if (remap.contains(pt->id)) continue;
      new_entities.push_back(e);
    } else if (auto* ln = std::get_if<SketchLine>(&e)) {
      ln->start = map_id(ln->start);
      ln->end = map_id(ln->end);
      if (ln->start == ln->end) {
        if (!collapse_short_edges || ln->construction)
          return {MergeStatus::kCollapsedEntity, 0};
        const auto original = std::get<SketchLine>(*sketch.entity(ln->id));
        for (EntityId endpoint : {original.start, original.end}) {
          int degree = 0;
          for (const auto& [id, entity] : sketch.entities()) {
            if (const auto* line = std::get_if<SketchLine>(&entity);
                line && (line->start == endpoint || line->end == endpoint)) {
              if (line->construction) return {MergeStatus::kConflict, 0};
              ++degree;
            }
          }
          if (degree != 2 || bound_arc_points.contains(endpoint))
            return {MergeStatus::kConflict, 0};
          collapsed_points.insert(endpoint);
        }
        removed_lines.emplace(original.id, original);
        continue;
      }
      updated_lines[ln->id] = *ln;
      new_entities.push_back(e);
    } else {
      new_entities.push_back(e);
    }
  }
  snap.entities = std::move(new_entities);

  std::vector<Constraint> new_constraints;
  new_constraints.reserve(snap.constraints.size());
  std::set<std::pair<EntityId, EntityId>> seen_coincident;
  std::set<std::pair<EntityId, EntityId>> seen_on_curve;
  std::map<EntityId, Position> seen_fix;

  for (Constraint c : snap.constraints) {
    const bool affected_distance =
        c.kind == ConstraintKind::kDistance &&
        (collapsed_points.contains(c.first) || collapsed_points.contains(c.second));
    if (removed_lines.contains(c.first) || removed_lines.contains(c.second)) {
      if ((c.kind == ConstraintKind::kHorizontal ||
           c.kind == ConstraintKind::kVertical) && c.second == 0)
        continue;  // An axis relation on the removed edge is a tautology.
      if (c.kind != ConstraintKind::kOnCurve ||
          !removed_lines.contains(c.second))
        return {MergeStatus::kConflict, 0};
      const auto& line = removed_lines.at(c.second);
      const auto a = std::get<SketchPoint>(*sketch.entity(line.start)).position;
      const auto b = std::get<SketchPoint>(*sketch.entity(line.end)).position;
      if (std::abs(a.x - b.x) <= kLengthTolerance)
        c.kind = ConstraintKind::kVertical;
      else if (std::abs(a.y - b.y) <= kLengthTolerance)
        c.kind = ConstraintKind::kHorizontal;
      else
        return {MergeStatus::kConflict, 0};
      c.second = map_id(line.start);
    }
    if (affected_distance) c.reference = true;
    if (c.kind == ConstraintKind::kTangentStart ||
        c.kind == ConstraintKind::kTangentEnd) {
      new_constraints.push_back(c);
      continue;
    }

    if (was_point(c.first)) c.first = map_id(c.first);
    if (c.second != 0 && was_point(c.second)) c.second = map_id(c.second);

    if (c.kind == ConstraintKind::kCoincident) {
      if (c.first == c.second) continue;
      const auto key = std::minmax(c.first, c.second);
      if (!seen_coincident.insert(key).second) continue;
    } else if ((c.kind == ConstraintKind::kHorizontal ||
                c.kind == ConstraintKind::kVertical) &&
               c.second != 0) {
      if (c.first == c.second) continue;
    } else if (c.kind == ConstraintKind::kOnCurve) {
      const auto ln_it = updated_lines.find(c.second);
      if (ln_it != updated_lines.end() &&
          (c.first == ln_it->second.start || c.first == ln_it->second.end)) {
        continue;
      }
      if (!seen_on_curve.insert({c.first, c.second}).second) continue;
    } else if (c.kind == ConstraintKind::kFix) {
      const auto fx_it = seen_fix.find(c.first);
      if (fx_it != seen_fix.end()) {
        if (detail::distance(fx_it->second, c.target) > kLengthTolerance) {
          return {MergeStatus::kConflict, 0};
        }
        continue;
      }
      seen_fix.emplace(c.first, c.target);
    } else if (c.kind == ConstraintKind::kDistance) {
      if (c.first == c.second) {
        return {MergeStatus::kConflict, 0};
      }
    }

    if (c.kind == ConstraintKind::kHorizontal ||
        c.kind == ConstraintKind::kVertical ||
        c.kind == ConstraintKind::kDistance) {
      // Compare the entire payload: reference/value/placement are significant.
      const bool duplicate = std::any_of(
          new_constraints.begin(), new_constraints.end(), [&](Constraint prev) {
            prev.id = c.id;
            return prev == c;
          });
      if (duplicate) continue;
    }
    new_constraints.push_back(c);
  }
  snap.constraints = std::move(new_constraints);

  auto candidate = Sketch::from_snapshot(snap);
  if (!candidate) {
    return {MergeStatus::kConflict, 0};
  }
  const SolveResult solved = solve(*candidate);
  if (solved.status != SolveStatus::kSolved) {
    return {MergeStatus::kConflict, 0};
  }

  if (!removed_lines.empty()) {
    const auto before = analyze_profiles(sketch);
    const auto after = analyze_profiles(*candidate);
    if (after.faces.empty() || !after.invalid_intersections.empty() ||
        after.faces.size() < before.faces.size() ||
        after.open_endpoints.size() > before.open_endpoints.size())
      return {MergeStatus::kConflict, 0};
  }

  sketch.swap(*candidate);
  return {MergeStatus::kMerged, merged_points};
}

ProfileAnalysis analyze_profiles(const Sketch& sketch) {
  ProfileAnalysis result;

  struct OpenSeg {
    EntityId id = 0;
    bool is_arc = false;
    Position a{0, 0};
    Position b{0, 0};
    std::optional<EntityId> pt_a;
    std::optional<EntityId> pt_b;
    Position center{0, 0};
    double radius = 0.0;
    double start_angle = 0.0;
    double sweep_angle = 0.0;
    std::size_t node_a = 0;
    std::size_t node_b = 0;
  };

  struct CircleSeg {
    EntityId id = 0;
    Position center{0, 0};
    double radius = 0.0;
  };

  // Map (arc_id, is_end) -> bound line endpoint EntityId.
  std::map<std::pair<EntityId, bool>, EntityId> arc_bound_pt;
  std::vector<std::pair<EntityId, EntityId>> coincident_pairs;
  for (const auto& [cid, c] : sketch.constraints()) {
    if (c.kind == ConstraintKind::kCoincident) {
      coincident_pairs.emplace_back(c.first, c.second);
    } else if (c.kind == ConstraintKind::kTangentStart ||
               c.kind == ConstraintKind::kTangentEnd) {
      const auto ent = sketch.entity(c.first);
      if (!ent) continue;
      if (const auto* ln = std::get_if<SketchLine>(&*ent)) {
        const EntityId ep = c.side == 1 ? ln->start : ln->end;
        arc_bound_pt[{c.second, c.kind == ConstraintKind::kTangentEnd}] = ep;
      }
    }
  }

  std::vector<OpenSeg> open_segs;
  std::vector<CircleSeg> circle_segs;

  for (const auto& [id, e] : sketch.entities()) {
    if (const auto* ln = std::get_if<SketchLine>(&e)) {
      if (ln->construction) continue;
      const Position pa = detail::point_position(sketch, ln->start);
      const Position pb = detail::point_position(sketch, ln->end);
      if (detail::distance(pa, pb) <= kLengthTolerance) {
        append_unique_position(result.invalid_intersections, pa);
        continue;
      }
      open_segs.push_back(
          OpenSeg{id, false, pa, pb, ln->start, ln->end, {}, 0.0, 0.0, 0.0});
    } else if (const auto* arc = std::get_if<SketchArc>(&e)) {
      if (arc->construction) continue;
      if (arc->radius <= kLengthTolerance) {
        append_unique_position(result.invalid_intersections,
                               detail::point_position(sketch, arc->center));
        continue;
      }
      const Position center = detail::point_position(sketch, arc->center);
      const double a0 = arc->start_angle;
      const double a1 = arc->start_angle + arc->sweep_angle;
      const Position pa{center.x + arc->radius * std::cos(a0),
                        center.y + arc->radius * std::sin(a0)};
      const Position pb{center.x + arc->radius * std::cos(a1),
                        center.y + arc->radius * std::sin(a1)};
      std::optional<EntityId> bound_a;
      std::optional<EntityId> bound_b;
      if (auto it = arc_bound_pt.find({id, false}); it != arc_bound_pt.end()) {
        bound_a = it->second;
      }
      if (auto it = arc_bound_pt.find({id, true}); it != arc_bound_pt.end()) {
        bound_b = it->second;
      }
      open_segs.push_back(OpenSeg{id, true, pa, pb, bound_a, bound_b, center,
                                  arc->radius, arc->start_angle,
                                  arc->sweep_angle});
    } else if (const auto* cir = std::get_if<SketchCircle>(&e)) {
      if (cir->construction) continue;
      if (cir->radius <= kLengthTolerance) {
        append_unique_position(result.invalid_intersections,
                               detail::point_position(sketch, cir->center));
        continue;
      }
      circle_segs.push_back(CircleSeg{
          id, detail::point_position(sketch, cir->center), cir->radius});
    }
  }

  if (open_segs.empty() && circle_segs.empty()) {
    return result;
  }

  // Cluster endpoints of open_segs into contour nodes.
  struct Slot {
    Position pos;
    std::optional<EntityId> pt_id;
    std::size_t seg_index;
    bool is_b;
  };
  std::vector<Slot> slots;
  slots.reserve(open_segs.size() * 2);
  for (std::size_t i = 0; i < open_segs.size(); ++i) {
    slots.push_back({open_segs[i].a, open_segs[i].pt_a, i, false});
    slots.push_back({open_segs[i].b, open_segs[i].pt_b, i, true});
  }

  std::set<std::pair<EntityId, EntityId>> coinc_lookup;
  for (const auto& [u, v] : coincident_pairs) {
    coinc_lookup.insert(std::minmax(u, v));
  }

  DisjointSet slot_dsu(slots.size());
  for (std::size_t i = 0; i < slots.size(); ++i) {
    for (std::size_t j = i + 1; j < slots.size(); ++j) {
      bool same = false;
      if (slots[i].pt_id && slots[j].pt_id) {
        if (*slots[i].pt_id == *slots[j].pt_id ||
            coinc_lookup.contains(
                std::minmax(*slots[i].pt_id, *slots[j].pt_id))) {
          same = true;
        }
      }
      if (!same && detail::distance(slots[i].pos, slots[j].pos) <=
                       kProfileEndpointTolerance) {
        same = true;
      }
      if (same) slot_dsu.unite(i, j);
    }
  }

  struct Node {
    Position pos{0, 0};
    std::optional<EntityId> point_id;
    std::vector<std::size_t> incident_segs;
  };
  std::map<std::size_t, std::size_t> root_to_node;
  std::vector<Node> nodes;
  std::vector<std::pair<double, double>> node_sum;
  std::vector<std::size_t> node_count;

  for (std::size_t s = 0; s < slots.size(); ++s) {
    const std::size_t r = slot_dsu.find(s);
    auto [it, inserted] = root_to_node.emplace(r, nodes.size());
    const std::size_t nid = it->second;
    if (inserted) {
      nodes.push_back(Node{slots[s].pos, slots[s].pt_id, {}});
      node_sum.emplace_back(0.0, 0.0);
      node_count.push_back(0);
    }
    if (!nodes[nid].point_id && slots[s].pt_id) {
      nodes[nid].point_id = slots[s].pt_id;
      nodes[nid].pos = slots[s].pos;
    } else if (nodes[nid].point_id && slots[s].pt_id &&
               *slots[s].pt_id < *nodes[nid].point_id) {
      nodes[nid].point_id = slots[s].pt_id;
      nodes[nid].pos = slots[s].pos;
    }
    node_sum[nid].first += slots[s].pos.x;
    node_sum[nid].second += slots[s].pos.y;
    ++node_count[nid];
    if (slots[s].is_b) {
      open_segs[slots[s].seg_index].node_b = nid;
    } else {
      open_segs[slots[s].seg_index].node_a = nid;
    }
  }

  for (std::size_t nid = 0; nid < nodes.size(); ++nid) {
    if (!nodes[nid].point_id && node_count[nid] > 0) {
      nodes[nid].pos = {
          node_sum[nid].first / static_cast<double>(node_count[nid]),
          node_sum[nid].second / static_cast<double>(node_count[nid])};
    }
  }

  // Build unified curve list: [0 .. open_segs.size()-1] open, then circles.
  const std::size_t total_curves = open_segs.size() + circle_segs.size();
  std::vector<detail::Curve> all_curves;
  all_curves.reserve(total_curves);
  for (const OpenSeg& seg : open_segs) {
    if (!seg.is_arc) {
      all_curves.push_back({seg.id, true, seg.a, seg.b, {}, 0.0, std::nullopt});
    } else {
      all_curves.push_back({seg.id,
                            false,
                            {},
                            {},
                            seg.center,
                            seg.radius,
                            std::pair{seg.start_angle, seg.sweep_angle}});
    }
  }
  for (const CircleSeg& cir : circle_segs) {
    all_curves.push_back(
        {cir.id, false, {}, {}, cir.center, cir.radius, std::nullopt});
  }

  DisjointSet comp_dsu(total_curves);
  std::vector<bool> curve_bad(total_curves, false);

  for (std::size_t i = 0; i < open_segs.size(); ++i) {
    if (open_segs[i].node_a == open_segs[i].node_b) {
      append_unique_position(result.invalid_intersections,
                             nodes[open_segs[i].node_a].pos);
      curve_bad[i] = true;
    }
    nodes[open_segs[i].node_a].incident_segs.push_back(i);
    nodes[open_segs[i].node_b].incident_segs.push_back(i);
  }

  for (std::size_t nid = 0; nid < nodes.size(); ++nid) {
    const auto& inc = nodes[nid].incident_segs;
    for (std::size_t k = 1; k < inc.size(); ++k) {
      comp_dsu.unite(inc[0], inc[k]);
    }
    if (inc.size() == 1) {
      result.open_endpoints.push_back(OpenEndpoint{
          nodes[nid].pos, nodes[nid].point_id, open_segs[inc[0]].id});
      curve_bad[inc[0]] = true;
    } else if (inc.size() >= 3) {
      append_unique_position(result.invalid_intersections, nodes[nid].pos);
      for (std::size_t seg_idx : inc) curve_bad[seg_idx] = true;
    }
  }

  // Pairwise curve intersection and overlap checks.
  for (std::size_t u = 0; u < total_curves; ++u) {
    for (std::size_t v = u + 1; v < total_curves; ++v) {
      if (const auto ov = overlap_midpoint(all_curves[u], all_curves[v],
                                           kProfileEndpointTolerance)) {
        append_unique_position(result.invalid_intersections, *ov);
        curve_bad[u] = true;
        curve_bad[v] = true;
        comp_dsu.unite(u, v);
        continue;
      }

      const auto hits = curve_intersections_with_tol(
          all_curves[u], all_curves[v], kProfileEndpointTolerance);
      for (const Position& p : hits) {
        bool shared_endpoint = false;
        if (u < open_segs.size() && v < open_segs.size()) {
          const OpenSeg& su = open_segs[u];
          const OpenSeg& sv = open_segs[v];
          const bool on_u_end =
              (su.node_a == sv.node_a || su.node_a == sv.node_b) &&
              detail::distance(p, nodes[su.node_a].pos) <=
                  2.0 * kProfileEndpointTolerance;
          const bool on_u_end_b =
              (su.node_b == sv.node_a || su.node_b == sv.node_b) &&
              detail::distance(p, nodes[su.node_b].pos) <=
                  2.0 * kProfileEndpointTolerance;
          shared_endpoint = on_u_end || on_u_end_b;
        }
        if (!shared_endpoint) {
          append_unique_position(result.invalid_intersections, p);
          curve_bad[u] = true;
          curve_bad[v] = true;
          comp_dsu.unite(u, v);
        }
      }
    }
  }

  std::map<std::size_t, bool> comp_invalid;
  std::map<std::size_t, std::vector<std::size_t>> comp_members;
  for (std::size_t i = 0; i < total_curves; ++i) {
    const std::size_t root = comp_dsu.find(i);
    comp_members[root].push_back(i);
    if (curve_bad[i]) comp_invalid[root] = true;
  }

  std::vector<ProfileLoop> loops;
  for (const auto& [root, members] : comp_members) {
    if (comp_invalid[root] || members.empty()) continue;

    if (members.size() == 1 && members[0] >= open_segs.size()) {
      const CircleSeg& cir = circle_segs[members[0] - open_segs.size()];
      ProfileEdge edge;
      edge.entity_id = cir.id;
      edge.kind = ProfileEdgeKind::kCircle;
      edge.start = {cir.center.x + cir.radius, cir.center.y};
      edge.end = edge.start;
      edge.center = cir.center;
      edge.radius = cir.radius;
      edge.start_angle = 0.0;
      edge.sweep_angle = kTwoPi;
      ProfileLoop loop;
      loop.edges.push_back(edge);
      loop.signed_area = compute_loop_signed_area(loop.edges);
      if (!std::isfinite(loop.signed_area) || loop.signed_area == 0.0) {
        append_unique_position(result.invalid_intersections, edge.start);
        continue;
      }
      loops.push_back(std::move(loop));
      continue;
    }

    // 2-regular cycle of open_segs.
    std::set<std::size_t> visited_segs;
    const std::size_t first_seg = members[0];
    std::size_t curr_seg = first_seg;
    std::size_t curr_node = open_segs[first_seg].node_a;
    const std::size_t start_node = curr_node;

    ProfileLoop loop;
    bool valid_cycle = true;
    while (visited_segs.size() < members.size()) {
      visited_segs.insert(curr_seg);
      const OpenSeg& seg = open_segs[curr_seg];
      const bool forward = (seg.node_a == curr_node);
      if (!forward && seg.node_b != curr_node) {
        valid_cycle = false;
        break;
      }
      const std::size_t next_node = forward ? seg.node_b : seg.node_a;

      ProfileEdge edge;
      edge.entity_id = seg.id;
      edge.start = nodes[curr_node].pos;
      edge.end = nodes[next_node].pos;
      if (!seg.is_arc) {
        edge.kind = ProfileEdgeKind::kLine;
      } else {
        edge.kind = ProfileEdgeKind::kArc;
        edge.center = seg.center;
        edge.radius = seg.radius;
        if (forward) {
          edge.start_angle = seg.start_angle;
          edge.sweep_angle = seg.sweep_angle;
        } else {
          edge.start_angle = seg.start_angle + seg.sweep_angle;
          edge.sweep_angle = -seg.sweep_angle;
        }
      }
      loop.edges.push_back(edge);
      curr_node = next_node;

      if (visited_segs.size() == members.size()) break;
      const auto& inc = nodes[curr_node].incident_segs;
      if (inc.size() != 2) {
        valid_cycle = false;
        break;
      }
      curr_seg = (inc[0] == curr_seg) ? inc[1] : inc[0];
      if (visited_segs.contains(curr_seg)) {
        valid_cycle = false;
        break;
      }
    }

    if (!valid_cycle || curr_node != start_node ||
        visited_segs.size() != members.size()) {
      continue;
    }
    loop.signed_area = compute_loop_signed_area(loop.edges);
    if (!std::isfinite(loop.signed_area) || loop.signed_area == 0.0) {
      append_unique_position(result.invalid_intersections,
                             loop.edges.front().start);
      continue;
    }
    loops.push_back(std::move(loop));
  }

  if (loops.empty()) {
    return result;
  }

  // Containment matrix and depth parity classification.
  const std::size_t nl = loops.size();
  std::vector<Position> samples(nl);
  for (std::size_t i = 0; i < nl; ++i) {
    samples[i] = sample_point_on_loop(loops[i]);
  }

  std::vector<std::vector<bool>> contains(nl, std::vector<bool>(nl, false));
  std::vector<std::size_t> depth(nl, 0);
  for (std::size_t i = 0; i < nl; ++i) {
    for (std::size_t j = 0; j < nl; ++j) {
      if (i == j) continue;
      if (loop_contains_point(loops[j], samples[i])) {
        contains[j][i] = true;
        ++depth[i];
      }
    }
  }

  std::map<std::size_t, std::size_t> loop_to_face;
  for (std::size_t i = 0; i < nl; ++i) {
    if (depth[i] % 2 == 0) {
      if (loops[i].signed_area < 0.0) reverse_loop(loops[i]);
      ProfileFace face;
      face.area = loops[i].signed_area;
      face.outer = loops[i];
      loop_to_face[i] = result.faces.size();
      result.faces.push_back(std::move(face));
    }
  }

  for (std::size_t i = 0; i < nl; ++i) {
    if (depth[i] % 2 == 1) {
      if (loops[i].signed_area > 0.0) reverse_loop(loops[i]);
      for (std::size_t p = 0; p < nl; ++p) {
        if (contains[p][i] && depth[p] + 1 == depth[i]) {
          const std::size_t fidx = loop_to_face.at(p);
          result.faces[fidx].area += loops[i].signed_area;
          result.faces[fidx].holes.push_back(loops[i]);
          break;
        }
      }
    }
  }

  return result;
}

}  // namespace sketchcad
