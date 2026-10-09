#pragma once
// Internal to sketchcad_core: the Ceres problem shared by the solver and
// the diagnostics. Not installed, not part of the public API.

#include <ceres/ceres.h>

#include <Eigen/Dense>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <vector>

#include "sketchcad/solver.h"

namespace sketchcad::detail {
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
  kPointOnLine,
  kPointOnCircle,
};

inline int residual_count(Shape shape) {
  return shape == Shape::kCoincident || shape == Shape::kFix ? 2 : 1;
}
inline bool angular(Shape shape) {
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
      case Shape::kPointOnLine: {
        // Slots: the point, then the line's endpoints.
        const T ux = x(2) - x(1), uy = y(2) - y(1);
        r[0] = (ux * (y(0) - y(1)) - uy * (x(0) - x(1))) / len(ux, uy);
        break;
      }
      case Shape::kPointOnCircle:
        // Slots: the point, then the centre; an arc counts as its circle.
        r[0] = len(x(0) - x(1), y(0) - y(1)) - rad(0);
        break;
    }
    return true;
  }
};

// Soft drag residual: weight * (value - target), per coordinate.
template <int N>
struct Anchor {
  std::array<double, N> target;
  double weight;
  template <typename T>
  bool operator()(const T* value, T* r) const {
    for (int i = 0; i < N; ++i) r[i] = T(weight) * (value[i] - T(target[i]));
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
    populate(problem);
    return minimize(problem, 200);
  }

  bool has_point(EntityId point) const { return points_.contains(point); }

  // Soft drag phase (U03): hard residuals weighted `hard`, `point` pulled
  // to `target` with weight 1 and every other variable held at its current
  // value with weight `stay`. The result still needs a hard projection.
  ceres::TerminationType run_drag(EntityId point, Position target,
                                  double hard, double stay,
                                  int max_iterations) {
    ceres::Problem problem;
    populate(problem, hard);
    for (auto& [id, p] : points_) {
      const bool dragged = id == point;
      problem.AddResidualBlock(
          new ceres::AutoDiffCostFunction<Anchor<2>, 2, 2>(new Anchor<2>{
              dragged ? std::array{target.x, target.y} : p,
              dragged ? 1.0 : stay}),
          nullptr, p.data());
    }
    for (auto& [id, r] : radii_)
      problem.AddResidualBlock(
          new ceres::AutoDiffCostFunction<Anchor<1>, 1, 1>(
              new Anchor<1>{{r}, stay}),
          nullptr, &r);
    return minimize(problem, max_iterations);
  }

 private:
  ceres::TerminationType minimize(ceres::Problem& problem,
                                  int max_iterations) {
    for (auto& [id, r] : radii_)
      if (problem.HasParameterBlock(&r))
        problem.SetParameterLowerBound(&r, 0, kMinRadius);
    ceres::Solver::Options options;
    options.linear_solver_type = ceres::DENSE_QR;
    options.max_num_iterations = max_iterations;
    options.function_tolerance = 1e-16;
    options.gradient_tolerance = 1e-16;
    options.parameter_tolerance = 1e-16;
    options.logging_type = ceres::SILENT;
    ceres::Solver::Summary summary;
    ceres::Solve(options, &problem, &summary);
    return summary.termination_type;
  }

 public:
  // Jacobian of all residuals at the current values: rows in constraint
  // order, columns over referenced points (x, y) then radii. `rows` receives
  // the constraint ID of each row.
  Eigen::MatrixXd jacobian(std::vector<EntityId>& rows) {
    ceres::Problem problem;
    ceres::Problem::EvaluateOptions options;
    options.residual_blocks = populate(problem);
    for (auto& [id, p] : points_) options.parameter_blocks.push_back(p.data());
    for (auto& [id, r] : radii_) options.parameter_blocks.push_back(&r);
    ceres::CRSMatrix crs;
    rows.clear();
    for (const Term& term : terms_)
      rows.insert(rows.end(), residual_count(term.cost.shape),
                  term.constraint);
    if (!problem.Evaluate(options, nullptr, nullptr, nullptr, &crs))
      return Eigen::MatrixXd::Constant(static_cast<int>(rows.size()), 1, NAN);
    Eigen::MatrixXd dense = Eigen::MatrixXd::Zero(crs.num_rows, crs.num_cols);
    for (int r = 0; r < crs.num_rows; ++r)
      for (int k = crs.rows[r]; k < crs.rows[r + 1]; ++k)
        dense(r, crs.cols[k]) = crs.values[k];
    return dense;
  }

  // Lines and curves whose length or radius the current values collapse,
  // although the input sketch did not.
  std::vector<EntityId> collapsed() const {
    std::vector<EntityId> ids;
    const auto now = [&](EntityId point) {
      auto it = points_.find(point);
      if (it != points_.end()) return Position{it->second[0], it->second[1]};
      return position(point);
    };
    for (const auto& [id, e] : sketch_.entities()) {
      if (const auto* l = std::get_if<SketchLine>(&e)) {
        const Position a = position(l->start), b = position(l->end);
        const Position c = now(l->start), d = now(l->end);
        if (std::hypot(b.x - a.x, b.y - a.y) > kLengthTolerance &&
            std::hypot(d.x - c.x, d.y - c.y) <= kLengthTolerance)
          ids.push_back(id);
      }
    }
    for (const auto& [id, r] : radii_) {
      const Entity e = *sketch_.entity(id);
      const double before = std::holds_alternative<SketchCircle>(e)
                                ? std::get<SketchCircle>(e).radius
                                : std::get<SketchArc>(e).radius;
      if (before > kLengthTolerance && r <= kLengthTolerance) ids.push_back(id);
    }
    return ids;
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
  // Adds one residual block per constraint, in `terms_` order.
  std::vector<ceres::ResidualBlockId> populate(ceres::Problem& problem,
                                               double weight = 1) {
    std::vector<ceres::ResidualBlockId> ids;
    for (Term& term : terms_) {
      auto cost = std::make_unique<ceres::DynamicAutoDiffCostFunction<Cost, 4>>(
          new Cost(term.cost));
      for (double* block : term.blocks)
        cost->AddParameterBlock(is_radius(block) ? 1 : 2);
      cost->SetNumResiduals(residual_count(term.cost.shape));
      ceres::LossFunction* loss =
          weight == 1 ? nullptr
                      : new ceres::ScaledLoss(nullptr, weight * weight,
                                              ceres::TAKE_OWNERSHIP);
      ids.push_back(problem.AddResidualBlock(cost.release(), loss,
                                             term.blocks));
    }
    return ids;
  }
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
      case ConstraintKind::kOnCurve:
        add_point(term, c.first);
        if (is_line(c.second)) {
          term.cost.shape = Shape::kPointOnLine;
          defined = add_line(term, c.second);
        } else {
          term.cost.shape = Shape::kPointOnCircle;
          add_curve(term, c.second);
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
}  // namespace sketchcad::detail
