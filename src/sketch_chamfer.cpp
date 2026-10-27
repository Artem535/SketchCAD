#include <array>
#include <cmath>

#include "sketch_corner.h"
#include "sketchcad/sketch_edit.h"
#include "sketchcad/solver.h"

namespace sketchcad {
namespace {
struct ChamferPlan {
  ChamferStatus status = ChamferStatus::kNotCorner;
  std::array<SketchLine, 2> lines{};
  std::array<EntityId, 2> far{};
  std::array<Position, 2> ends{};
};

ChamferPlan plan_chamfer(const Sketch& s, EntityId corner, double distance) {
  ChamferPlan plan;
  const auto lines = detail::corner_lines(s, corner);
  if (!lines) return plan;
  plan.lines = *lines;
  if (!std::isfinite(distance) || distance <= kLengthTolerance) {
    plan.status = ChamferStatus::kInvalidDistance;
    return plan;
  }
  const auto p = std::get<SketchPoint>(*s.entity(corner)).position;
  std::array<Position, 2> directions{};
  for (std::size_t i = 0; i < 2; ++i) {
    const auto& line = plan.lines[i];
    plan.far[i] = line.start == corner ? line.end : line.start;
    const auto far = std::get<SketchPoint>(*s.entity(plan.far[i])).position;
    const double dx = far.x - p.x, dy = far.y - p.y;
    const double length = std::hypot(dx, dy);
    if (!std::isfinite(length) || distance >= length - kLengthTolerance) {
      plan.status = ChamferStatus::kTooLong;
      return plan;
    }
    directions[i] = {dx / length, dy / length};
    plan.ends[i] = {p.x + distance * directions[i].x,
                    p.y + distance * directions[i].y};
  }
  // |sin(angle)| rejects both parallel and antiparallel directions.
  const auto a = directions[0], b = directions[1];
  if (std::abs(a.x * b.y - a.y * b.x) <= std::sin(kAngleTolerance)) {
    plan.status = ChamferStatus::kCollinear;
    return plan;
  }
  for (const auto& [id, c] : s.constraints())
    if (c.kind == ConstraintKind::kEqual &&
        (c.first == plan.lines[0].id || c.second == plan.lines[0].id ||
         c.first == plan.lines[1].id || c.second == plan.lines[1].id)) {
      plan.status = ChamferStatus::kEqualLength;
      return plan;
    }
  plan.status = ChamferStatus::kReady;
  return plan;
}
}  // namespace

ChamferStatus chamfer_status(const Sketch& s, EntityId corner,
                             double distance) {
  return plan_chamfer(s, corner, distance).status;
}

bool chamfer(Sketch& s, EntityId corner, double distance) {
  const auto plan = plan_chamfer(s, corner, distance);
  if (plan.status != ChamferStatus::kReady) return false;
  // Rewrite the old lengths through the validated snapshot boundary to retain
  // their IDs. All subsequent edits use the candidate, including allocations.
  auto snapshot = s.snapshot();
  for (auto& c : snapshot.constraints)
    if (c.kind == ConstraintKind::kLength)
      for (std::size_t i = 0; i < 2; ++i)
        if (c.first == plan.lines[i].id) {
          c.kind = ConstraintKind::kDistance;
          c.first = plan.far[i];
          c.second = corner;
          c.placement.reset();
          break;
        }
  auto candidate = Sketch::from_snapshot(snapshot);
  if (!candidate || !candidate->set_construction(corner, true)) return false;
  std::array<EntityId, 2> ends{};
  for (std::size_t i = 0; i < 2; ++i) {
    const auto point = candidate->create_point(plan.ends[i]);
    if (!point) return false;
    ends[i] = *point;
    const auto& l = plan.lines[i];
    if (!candidate->update_line(l.id, l.start == corner ? *point : l.start,
                                l.end == corner ? *point : l.end) ||
        !candidate->add_constraint(ConstraintKind::kOnCurve, corner, l.id) ||
        !candidate->add_dimension(ConstraintKind::kDistance, corner, *point,
                                  distance))
      return false;
  }
  if (!candidate->create_line(
          ends[0], ends[1],
          plan.lines[0].construction && plan.lines[1].construction))
    return false;
  s.swap(*candidate);
  return true;
}

std::optional<CurvePiece> chamfer_preview(const Sketch& s, EntityId corner,
                                          double distance) {
  const auto plan = plan_chamfer(s, corner, distance);
  if (plan.status != ChamferStatus::kReady) return std::nullopt;
  CurvePiece piece;
  piece.a = plan.ends[0];
  piece.b = plan.ends[1];
  return piece;
}
}  // namespace sketchcad
