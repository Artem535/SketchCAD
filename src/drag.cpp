#include "sketchcad/drag.h"

#include "solver_problem.h"

namespace sketchcad {
namespace {
// constrained-drag.adoc: soft phase weights and iteration bound.
constexpr double kHardWeight = 10;
constexpr double kStayWeight = 1e-3;
constexpr int kSoftIterations = 50;

double size(const Sketch& sketch, const Entity& e) {
  const auto at = [&](EntityId p) {
    return std::get<SketchPoint>(*sketch.entity(p)).position;
  };
  if (const auto* l = std::get_if<SketchLine>(&e)) {
    const Position a = at(l->start), b = at(l->end);
    return std::hypot(b.x - a.x, b.y - a.y);
  }
  if (const auto* c = std::get_if<SketchCircle>(&e)) return c->radius;
  if (const auto* a = std::get_if<SketchArc>(&e)) return a->radius;
  return INFINITY;
}

// A line or radius longer than the tolerance in `before` collapsed.
bool collapsed(const Sketch& before, const Sketch& after) {
  for (const auto& [id, e] : before.entities())
    if (size(before, e) > kLengthTolerance &&
        size(after, *after.entity(id)) <= kLengthTolerance)
      return true;
  return false;
}
}  // namespace

SolveResult drag_points(Sketch& sketch,
                        const std::map<EntityId, Position>& targets) {
  if (targets.empty()) return {SolveStatus::kInvalidInput, {}};
  for (const auto& [id, target] : targets) {
    const auto e = sketch.entity(id);
    if (!e || !std::holds_alternative<SketchPoint>(*e) ||
        !std::isfinite(target.x) || !std::isfinite(target.y))
      return {SolveStatus::kInvalidInput, {}};
  }

  detail::Problem problem(sketch);
  if (!problem.build()) return {SolveStatus::kInvalidInput, {}};
  // Points no constraint reaches move freely, exactly to their targets.
  std::map<EntityId, Position> tied, free;
  for (const auto& [id, target] : targets)
    (problem.has_point(id) ? tied : free)[id] = target;
  const auto move_free = [&](Sketch& s) {
    for (const auto& [id, target] : free) s.update_point(id, target);
  };
  if (tied.empty()) {
    move_free(sketch);
    return {SolveStatus::kSolved, {}};
  }
  problem.run_drag(tied, kHardWeight, kStayWeight, kSoftIterations);
  if (!problem.finite()) return {SolveStatus::kNumericalFailure, {}};

  Sketch candidate = problem.apply();
  move_free(candidate);
  SolveResult result = solve(candidate);
  if (result.status != SolveStatus::kSolved) return result;
  if (collapsed(sketch, candidate)) return {SolveStatus::kDegenerate, {}};
  sketch = std::move(candidate);
  return result;
}

SolveResult drag_point(Sketch& sketch, EntityId point, Position target) {
  return drag_points(sketch, {{point, target}});
}
}  // namespace sketchcad
