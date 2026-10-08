#include <ceres/ceres.h>

#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <numbers>

#include "sketchcad/solver.h"

namespace sketchcad {
namespace {
// Radii stay strictly positive during the solve.
constexpr double kMinRadius = 1e-9;

// Residual formulas over point slots (x, y) and radius slots.
enum class Shape {
  kCoincident,
  kAxis,
  kParallel,
  kPerpendicular,
  kLineTangent,
  kCurveTangent,
  kEqualLength,
  kEqualRadius,
  kFix,
  kLength,
  kPointDistance,
  kPointLine,
  kAngle,
  kRadius,
};

int residual_count(Shape shape) {
  return shape == Shape::kCoincident || shape == Shape::kFix ? 2 : 1;
}
bool angular(Shape shape) {
  return shape == Shape::kParallel || shape == Shape::kPerpendicular ||
         shape == Shape::kAngle;
}

// One constraint as a cost over deduplicated parameter blocks: Ceres does
// not allow the same block twice in a residual, but lines may share points.
struct Cost {
  Shape shape = Shape::kFix;
  int axis = 0;  // kAxis: 0 compares x (vertical), 1 compares y.
  bool internal = false;
  Position target{0, 0};
  double value = 0;  // Dimensions.
  int side = 1;      // kPointLine.
  std::vector<int> point_slots;   // Indices into `blocks`.
  std::vector<int> radius_slots;  // Indices into `blocks`.

  template <typename T>
  bool operator()(T const* const* blocks, T* r) const {
    using std::abs;
    using std::atan2;
    using std::cos;
    using std::sin;
    using std::sqrt;
    const auto x = [&](int i) { return blocks[point_slots[i]][0]; };
    const auto y = [&](int i) { return blocks[point_slots[i]][1]; };
    const auto rad = [&](int i) { return blocks[radius_slots[i]][0]; };
    const auto len = [&](const T& dx, const T& dy) {
      return sqrt(dx * dx + dy * dy);
    };
    switch (shape) {
      case Shape::kCoincident:
        r[0] = x(1) - x(0);
        r[1] = y(1) - y(0);
        break;
      case Shape::kAxis:
        r[0] = axis == 0 ? x(1) - x(0) : y(1) - y(0);
        break;
      case Shape::kParallel:
      case Shape::kPerpendicular: {
        const T ux = x(1) - x(0), uy = y(1) - y(0);
        const T vx = x(3) - x(2), vy = y(3) - y(2);
        const T product = shape == Shape::kParallel ? ux * vy - uy * vx
                                                    : ux * vx + uy * vy;
        r[0] = product / (len(ux, uy) * len(vx, vy));
        break;
      }
      case Shape::kLineTangent: {
        const T ux = x(1) - x(0), uy = y(1) - y(0);
        const T cross = ux * (y(2) - y(0)) - uy * (x(2) - x(0));
        r[0] = abs(cross) / len(ux, uy) - rad(0);
        break;
      }
      case Shape::kCurveTangent: {
        const T d = len(x(1) - x(0), y(1) - y(0));
        r[0] = d - (internal ? abs(rad(0) - rad(1)) : rad(0) + rad(1));
        break;
      }
      case Shape::kEqualLength:
        r[0] = len(x(1) - x(0), y(1) - y(0)) - len(x(3) - x(2), y(3) - y(2));
        break;
      case Shape::kEqualRadius:
        r[0] = rad(0) - rad(1);
        break;
      case Shape::kFix:
        r[0] = x(0) - T(target.x);
        r[1] = y(0) - T(target.y);
        break;
      case Shape::kLength:
      case Shape::kPointDistance:
        r[0] = len(x(1) - x(0), y(1) - y(0)) - T(value);
        break;
      case Shape::kPointLine: {
        // Slots: the point, then the line's endpoints.
        const T ux = x(2) - x(1), uy = y(2) - y(1);
        const T cross = ux * (y(0) - y(1)) - uy * (x(0) - x(1));
        r[0] = T(side) * cross / len(ux, uy) - T(value);
        break;
      }
      case Shape::kAngle: {
        // Undirected: lines at theta and theta + pi are the same angle.
        const T ux = x(1) - x(0), uy = y(1) - y(0);
        const T vx = x(3) - x(2), vy = y(3) - y(2);
        const T phi = atan2(ux * vy - uy * vx, ux * vx + uy * vy) - T(value);
        r[0] = T(0.5) * atan2(sin(T(2) * phi), cos(T(2) * phi));
        break;
      }
      case Shape::kRadius:
        r[0] = rad(0) - T(value);
        break;
    }
    return true;
  }
};

struct Term {
  EntityId constraint;
  Cost cost;
  std::vector<double*> blocks;
};

class Problem {
 public:
  explicit Problem(const Sketch& sketch) : sketch_(sketch) {}

  // Translates every constraint; false if a residual is undefined for the
  // current geometry (degenerate line or concentric curves).
  bool build() {
    for (const auto& [id, c] : sketch_.constraints())
      if (!add(c)) return false;
    return true;
  }

  // Evaluates all residuals at the current values.
  SolveResult check() const {
    SolveResult result{SolveStatus::kSolved, {}};
    for (const Term& term : terms_) {
      std::array<double, 2> r{};
      term.cost(term.blocks.data(), r.data());
      double worst = 0;
      for (int i = 0; i < residual_count(term.cost.shape); ++i)
        worst = std::isfinite(r[i]) ? std::max(worst, std::abs(r[i]))
                                    : INFINITY;
      const bool is_angle = angular(term.cost.shape);
      double& max = is_angle ? result.max_angle_residual
                             : result.max_length_residual;
      max = std::max(max, worst);
      if (!(worst <= (is_angle ? kAngleTolerance : kLengthTolerance)))
        result.violated.push_back(term.constraint);
    }
    if (!result.violated.empty()) result.status = SolveStatus::kUnsatisfied;
    return result;
  }

  bool finite() const {
    for (const auto& [id, p] : points_)
      if (!std::isfinite(p[0]) || !std::isfinite(p[1])) return false;
    for (const auto& [id, r] : radii_)
      if (!std::isfinite(r) || r <= 0) return false;
    return true;
  }

  ceres::TerminationType run() {
    ceres::Problem problem;
    for (Term& term : terms_) {
      auto cost = std::make_unique<ceres::DynamicAutoDiffCostFunction<Cost, 4>>(
          new Cost(term.cost));
      for (double* block : term.blocks)
        cost->AddParameterBlock(is_radius(block) ? 1 : 2);
      cost->SetNumResiduals(residual_count(term.cost.shape));
      problem.AddResidualBlock(cost.release(), nullptr, term.blocks);
    }
    for (auto& [id, r] : radii_)
      if (problem.HasParameterBlock(&r))
        problem.SetParameterLowerBound(&r, 0, kMinRadius);
    ceres::Solver::Options options;
    options.linear_solver_type = ceres::DENSE_QR;
    options.max_num_iterations = 200;
    options.function_tolerance = 1e-16;
    options.gradient_tolerance = 1e-16;
    options.parameter_tolerance = 1e-16;
    options.logging_type = ceres::SILENT;
    ceres::Solver::Summary summary;
    ceres::Solve(options, &problem, &summary);
    return summary.termination_type;
  }

  // Applies solved values; only called when every residual is satisfied.
  Sketch apply() const {
    Sketch result = sketch_;
    for (const auto& [id, p] : points_) result.update_point(id, {p[0], p[1]});
    for (const auto& [id, r] : radii_) {
      const Entity e = *result.entity(id);
      if (const auto* c = std::get_if<SketchCircle>(&e))
        result.update_circle(id, c->center, r);
      else if (const auto* a = std::get_if<SketchArc>(&e))
        result.update_arc(id, a->center, r, a->start_angle, a->sweep_angle);
    }
    return result;
  }

 private:
  bool is_radius(const double* block) const {
    for (const auto& [id, r] : radii_)
      if (&r == block) return true;
    return false;
  }
  Position position(EntityId point) const {
    return std::get<SketchPoint>(*sketch_.entity(point)).position;
  }
  // Slot of a parameter block in `term`, adding it once.
  static int slot(Term& term, double* block) {
    for (std::size_t i = 0; i < term.blocks.size(); ++i)
      if (term.blocks[i] == block) return static_cast<int>(i);
    term.blocks.push_back(block);
    return static_cast<int>(term.blocks.size() - 1);
  }
  void add_point(Term& term, EntityId point) {
    auto [it, inserted] = points_.try_emplace(point);
    if (inserted) {
      const Position p = position(point);
      it->second = {p.x, p.y};
    }
    term.cost.point_slots.push_back(slot(term, it->second.data()));
  }
  // Adds a line's endpoints; false for a zero-length line.
  bool add_line(Term& term, EntityId line) {
    const auto l = std::get<SketchLine>(*sketch_.entity(line));
    const Position a = position(l.start), b = position(l.end);
    add_point(term, l.start);
    add_point(term, l.end);
    return std::hypot(b.x - a.x, b.y - a.y) > kLengthTolerance;
  }
  // Adds a curve's center (as a point slot) and radius.
  Position add_curve(Term& term, EntityId curve) {
    const Entity e = *sketch_.entity(curve);
    EntityId center;
    double radius;
    if (const auto* c = std::get_if<SketchCircle>(&e)) {
      center = c->center;
      radius = c->radius;
    } else {
      const auto& a = std::get<SketchArc>(e);
      center = a.center;
      radius = a.radius;
    }
    add_point(term, center);
    auto [it, inserted] = radii_.try_emplace(curve, radius);
    term.cost.radius_slots.push_back(slot(term, &it->second));
    return position(center);
  }
  bool is_line(EntityId id) const {
    return std::holds_alternative<SketchLine>(*sketch_.entity(id));
  }

  bool add(const Constraint& c) {
    Term term{c.id, {}, {}};
    bool defined = true;
    switch (c.kind) {
      case ConstraintKind::kCoincident:
        term.cost.shape = Shape::kCoincident;
        add_point(term, c.first);
        add_point(term, c.second);
        break;
      case ConstraintKind::kHorizontal:
      case ConstraintKind::kVertical:
        term.cost.shape = Shape::kAxis;
        term.cost.axis = c.kind == ConstraintKind::kHorizontal ? 1 : 0;
        if (c.second) {
          add_point(term, c.first);
          add_point(term, c.second);
        } else {
          add_line(term, c.first);
        }
        break;
      case ConstraintKind::kParallel:
      case ConstraintKind::kPerpendicular:
      case ConstraintKind::kEqual:
        if (c.kind == ConstraintKind::kEqual && !is_line(c.first)) {
          term.cost.shape = Shape::kEqualRadius;
          add_curve(term, c.first);
          add_curve(term, c.second);
          break;
        }
        term.cost.shape = c.kind == ConstraintKind::kParallel
                              ? Shape::kParallel
                          : c.kind == ConstraintKind::kPerpendicular
                              ? Shape::kPerpendicular
                              : Shape::kEqualLength;
        defined = add_line(term, c.first);
        defined = add_line(term, c.second) && defined;
        break;
      case ConstraintKind::kTangent:
        if (is_line(c.first)) {
          term.cost.shape = Shape::kLineTangent;
          defined = add_line(term, c.first);
          add_curve(term, c.second);
        } else {
          term.cost.shape = Shape::kCurveTangent;
          term.cost.internal = c.internal;
          const Position a = add_curve(term, c.first);
          const Position b = add_curve(term, c.second);
          defined = std::hypot(a.x - b.x, a.y - b.y) > kLengthTolerance;
        }
        break;
      case ConstraintKind::kFix:
        term.cost.shape = Shape::kFix;
        term.cost.target = c.target;
        add_point(term, c.first);
        break;
      case ConstraintKind::kLength:
        term.cost.shape = Shape::kLength;
        defined = add_line(term, c.first);
        break;
      case ConstraintKind::kDistance:
        add_point(term, c.first);
        if (is_line(c.second)) {
          term.cost.shape = Shape::kPointLine;
          term.cost.side = c.side;
          defined = add_line(term, c.second);
        } else {
          term.cost.shape = Shape::kPointDistance;
          add_point(term, c.second);
          const Position a = position(c.first), b = position(c.second);
          defined = std::hypot(a.x - b.x, a.y - b.y) > kLengthTolerance;
        }
        break;
      case ConstraintKind::kAngle:
        term.cost.shape = Shape::kAngle;
        defined = add_line(term, c.first);
        defined = add_line(term, c.second) && defined;
        break;
      case ConstraintKind::kRadius:
        term.cost.shape = Shape::kRadius;
        add_curve(term, c.first);
        break;
    }
    term.cost.value = c.value;
    terms_.push_back(std::move(term));
    return defined;
  }

  const Sketch& sketch_;
  // std::map keeps block addresses stable while terms are added.
  std::map<EntityId, std::array<double, 2>> points_;
  std::map<EntityId, double> radii_;
  std::vector<Term> terms_;
};
}  // namespace

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
