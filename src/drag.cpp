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

SolveResult drag_point(Sketch& sketch, EntityId point, Position target) {
  const auto e = sketch.entity(point);
  if (!e || !std::holds_alternative<SketchPoint>(*e) ||
      !std::isfinite(target.x) || !std::isfinite(target.y))
    return {SolveStatus::kInvalidInput, {}};

  detail::Problem problem(sketch);
  if (!problem.build()) return {SolveStatus::kInvalidInput, {}};
  if (!problem.has_point(point)) {
    // No constraint reaches the point: it moves freely.
    sketch.update_point(point, target);
    return {SolveStatus::kSolved, {}};
  }
  problem.run_drag(point, target, kHardWeight, kStayWeight, kSoftIterations);
  if (!problem.finite()) return {SolveStatus::kNumericalFailure, {}};

  Sketch candidate = problem.apply();
  SolveResult result = solve(candidate);
  if (result.status != SolveStatus::kSolved) return result;
  if (collapsed(sketch, candidate)) return {SolveStatus::kDegenerate, {}};
  sketch = std::move(candidate);
  return result;
}
}  // namespace sketchcad
