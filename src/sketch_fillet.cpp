#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "sketch_corner.h"
#include "sketchcad/sketch_edit.h"
#include "sketchcad/solver.h"

namespace sketchcad {
namespace {
struct FilletPlan {
  FilletStatus status = FilletStatus::kNotCorner;
  std::array<SketchLine, 2> lines{};
  std::array<EntityId, 2> far{};
  std::array<Position, 2> contacts{};
  std::size_t start_index = 0;
  CurvePiece piece;
};

FilletPlan plan_fillet(const Sketch& s, EntityId corner, double radius) {
  FilletPlan plan;
  const auto lines = detail::corner_lines(s, corner);
  if (!lines) return plan;
  plan.lines = *lines;
  if (!std::isfinite(radius) || radius <= kLengthTolerance) {
    plan.status = FilletStatus::kInvalidRadius;
    return plan;
  }
  const auto p = std::get<SketchPoint>(*s.entity(corner)).position;
  std::array<Position, 2> u;
  std::array<double, 2> lengths;
  for (std::size_t i = 0; i < 2; ++i) {
    const auto line = plan.lines[i];
    plan.far[i] = line.start == corner ? line.end : line.start;
    const auto q = std::get<SketchPoint>(*s.entity(plan.far[i])).position;
    lengths[i] = std::hypot(q.x - p.x, q.y - p.y);
    if (!std::isfinite(lengths[i]) || lengths[i] <= kLengthTolerance)
      return plan;
    u[i] = {(q.x - p.x) / lengths[i], (q.y - p.y) / lengths[i]};
  }
  const double cross = u[0].x * u[1].y - u[0].y * u[1].x;
  const double dot = std::clamp(u[0].x * u[1].x + u[0].y * u[1].y, -1., 1.);
  if (std::abs(cross) <= std::sin(kAngleTolerance)) {
    plan.status = FilletStatus::kCollinear;
    return plan;
  }
  const double setback = radius * (1 + dot) / std::abs(cross);
  if (!std::isfinite(setback) || setback <= kLengthTolerance ||
      setback >= std::min(lengths[0], lengths[1]) -
                     detail::kCornerLengthMargin) {
    plan.status = FilletStatus::kTooLong;
    return plan;
  }
  for (const auto& [id, c] : s.constraints()) {
    if (c.kind == ConstraintKind::kEqual &&
        (c.first == plan.lines[0].id || c.second == plan.lines[0].id ||
         c.first == plan.lines[1].id || c.second == plan.lines[1].id)) {
      plan.status = FilletStatus::kEqualLength;
      return plan;
    }
  }
  const double offset = radius / std::abs(cross);
  const Position center{p.x + (u[0].x + u[1].x) * offset,
                        p.y + (u[0].y + u[1].y) * offset};
  for (std::size_t i = 0; i < 2; ++i)
    plan.contacts[i] = {p.x + u[i].x * setback, p.y + u[i].y * setback};
  plan.start_index = cross > 0 ? 1 : 0;
  plan.piece.is_arc = true;
  plan.piece.a = plan.contacts[plan.start_index];
  plan.piece.b = plan.contacts[1 - plan.start_index];
  plan.piece.center = center;
  plan.piece.radius = radius;
  plan.piece.start =
      std::atan2(plan.piece.a.y - center.y, plan.piece.a.x - center.x);
  plan.piece.sweep = std::numbers::pi - std::atan2(std::abs(cross), dot);
  if (!std::isfinite(center.x) || !std::isfinite(center.y) ||
      !std::isfinite(plan.piece.start) || plan.piece.sweep <= kAngleTolerance)
    return plan;
  plan.status = FilletStatus::kReady;
  return plan;
}
}  // namespace

FilletStatus fillet_status(const Sketch& s, EntityId corner, double radius) {
  return plan_fillet(s, corner, radius).status;
}

std::optional<CurvePiece> fillet_preview(const Sketch& s, EntityId corner,
                                         double radius) {
  const auto plan = plan_fillet(s, corner, radius);
  if (plan.status != FilletStatus::kReady) return std::nullopt;
  return plan.piece;
}

bool fillet(Sketch& s, EntityId corner, double radius) {
  const auto plan = plan_fillet(s, corner, radius);
  if (plan.status != FilletStatus::kReady) return false;
  auto snapshot = s.snapshot();
  for (auto& c : snapshot.constraints) {
    if (c.kind != ConstraintKind::kLength) continue;
    for (std::size_t i = 0; i < 2; ++i) {
      if (c.first != plan.lines[i].id) continue;
      c.kind = ConstraintKind::kDistance;
      c.first = plan.far[i];
      c.second = corner;
      c.placement.reset();
      break;
    }
  }
  auto candidate = Sketch::from_snapshot(snapshot);
  if (!candidate || !candidate->set_construction(corner, true)) return false;
  std::array<EntityId, 2> contacts;
  for (std::size_t i = 0; i < 2; ++i) {
    const auto point = candidate->create_point(plan.contacts[i]);
    if (!point) return false;
    contacts[i] = *point;
    const auto l = plan.lines[i];
    if (!candidate->update_line(l.id, l.start == corner ? *point : l.start,
                                l.end == corner ? *point : l.end) ||
        !candidate->add_constraint(ConstraintKind::kOnCurve, corner, l.id))
      return false;
  }
  const auto center = candidate->create_point(plan.piece.center, true);
  if (!center) return false;
  const auto arc = candidate->create_arc(
      *center, radius, plan.piece.start, plan.piece.sweep,
      plan.lines[0].construction && plan.lines[1].construction);
  if (!arc ||
      !candidate->add_dimension(ConstraintKind::kRadius, *arc, 0, radius))
    return false;
  for (std::size_t i = 0; i < 2; ++i) {
    const auto kind = i == plan.start_index ? ConstraintKind::kTangentStart
                                            : ConstraintKind::kTangentEnd;
    if (!candidate->add_arc_tangency(kind, plan.lines[i].id, *arc, contacts[i]))
      return false;
  }
  s.swap(*candidate);
  return true;
}
}  // namespace sketchcad
