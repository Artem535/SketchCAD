#include <ceres/ceres.h>

#include <array>
#include <cmath>
#include <map>
#include <memory>

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
};

int residual_count(Shape shape) {
  return shape == Shape::kCoincident || shape == Shape::kFix ? 2 : 1;
}
bool angular(Shape shape) {
  return shape == Shape::kParallel || shape == Shape::kPerpendicular;
}

// One constraint as a cost over deduplicated parameter blocks: Ceres does
// not allow the same block twice in a residual, but lines may share points.
struct Cost {
  Shape shape = Shape::kFix;
  int axis = 0;  // kAxis: 0 compares x (vertical), 1 compares y.
  bool internal = false;
  Position target{0, 0};
  std::vector<int> point_slots;   // Indices into `blocks`.
  std::vector<int> radius_slots;  // Indices into `blocks`.

  template <typename T>
  bool operator()(T const* const* blocks, T* r) const {
    using std::abs;
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
    }
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
