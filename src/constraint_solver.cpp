#include "sketchcad/solver.h"

#include "solver_problem.h"

namespace sketchcad {
using detail::Problem;

std::optional<double> measure(const Sketch& sketch, ConstraintKind kind,
                              EntityId first, EntityId second) {
  const auto get = [&](EntityId id) { return sketch.entity(id); };
  const auto point = [&](EntityId id) -> std::optional<Position> {
    const auto e = get(id);
    if (!e || !std::holds_alternative<SketchPoint>(*e)) return std::nullopt;
    return std::get<SketchPoint>(*e).position;
  };
  // Line direction start->end and its start, if `id` is a non-degenerate line.
  const auto line = [&](EntityId id)
      -> std::optional<std::pair<Position, Position>> {
    const auto e = get(id);
    if (!e || !std::holds_alternative<SketchLine>(*e)) return std::nullopt;
    const auto& l = std::get<SketchLine>(*e);
    const Position a = *point(l.start), b = *point(l.end);
    const Position d{b.x - a.x, b.y - a.y};
    if (std::hypot(d.x, d.y) <= kLengthTolerance) return std::nullopt;
    return std::pair{a, d};
  };
  switch (kind) {
    case ConstraintKind::kLength:
      if (const auto l = line(first); l && second == 0)
        return std::hypot(l->second.x, l->second.y);
      return std::nullopt;
    case ConstraintKind::kDistance: {
      if (line(first) && point(second)) std::swap(first, second);
      const auto p = point(first);
      if (!p || first == second) return std::nullopt;
      if (const auto q = point(second)) return std::hypot(q->x - p->x, q->y - p->y);
      const auto l = line(second);
      if (!l) return std::nullopt;
      const auto [a, d] = *l;
      return std::abs(d.x * (p->y - a.y) - d.y * (p->x - a.x)) /
             std::hypot(d.x, d.y);
    }
    case ConstraintKind::kAngle: {
      const auto a = line(first), b = line(second);
      if (!a || !b || first == second) return std::nullopt;
      const Position u = a->second, v = b->second;
      const double angle = std::fmod(
          std::atan2(u.x * v.y - u.y * v.x, u.x * v.x + u.y * v.y) +
              2 * std::numbers::pi,
          std::numbers::pi);
      return angle;
    }
    case ConstraintKind::kRadius: {
      const auto e = get(first);
      if (!e || second != 0) return std::nullopt;
      if (const auto* c = std::get_if<SketchCircle>(&*e)) return c->radius;
      if (const auto* a = std::get_if<SketchArc>(&*e)) return a->radius;
      return std::nullopt;
    }
    default:
      return std::nullopt;
  }
}

SolveResult solve(Sketch& sketch) {
  if (sketch.constraints().empty()) return {SolveStatus::kSolved, {}};
  Problem problem(sketch);
  if (!problem.build()) return {SolveStatus::kInvalidInput, {}};
  const SolveResult initial = problem.check();
  if (initial.status == SolveStatus::kSolved) return initial;
  if (!std::isfinite(initial.max_length_residual) ||
      !std::isfinite(initial.max_angle_residual))
    return {SolveStatus::kInvalidInput, {}};

  const ceres::TerminationType termination = problem.run();
  if (!problem.finite()) return {SolveStatus::kNumericalFailure, {}};
  SolveResult result = problem.check();
  if (result.status == SolveStatus::kSolved) {
    sketch = problem.apply();
  } else if (termination != ceres::CONVERGENCE) {
    result.status = SolveStatus::kNumericalFailure;
  }
  return result;
}

Edit solver_step() {
  return [](Sketch& sketch) {
    return solve(sketch).status == SolveStatus::kSolved;
  };
}
}  // namespace sketchcad
