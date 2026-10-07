#include "sketchcad/rectangle.h"

#include <ceres/ceres.h>

#include <cmath>

namespace sketchcad {
namespace {
constexpr double kTolerance = 1e-7;
bool valid_dimension(double value) { return std::isfinite(value) && value > 0; }
struct RectangleResidual {
  double width, height, x, y;
  template <typename T>
  bool operator()(const T* p, T* r) const {
    r[0] = p[1] - p[3];
    r[1] = p[5] - p[7];
    r[2] = p[0] - p[6];
    r[3] = p[2] - p[4];
    r[4] = p[2] - p[0] - T(width);
    r[5] = p[5] - p[3] - T(height);
    r[6] = p[0] - T(x);
    r[7] = p[1] - T(y);
    return true;
  }
};
struct WidthResidual {
  double width;
  template <typename T>
  bool operator()(const T* p, T* r) const {
    r[0] = p[2] - p[0] - T(width);
    return true;
  }
};
}  // namespace
bool Rectangle::solve(double width, double height, bool anchor,
                      std::optional<double> additional_width) {
  if (!valid_dimension(width) || !valid_dimension(height) ||
      (additional_width && !valid_dimension(*additional_width)))
    return false;
  double parameters[8];
  for (int i = 0; i < 4; ++i) {
    parameters[2 * i] = points_[i].x;
    parameters[2 * i + 1] = points_[i].y;
  }
  // Preserve the free origin as a gauge for resize, not a persistent
  // constraint.
  RectangleResidual residual{width, height, anchor ? 0.0 : points_[0].x,
                             anchor ? 0.0 : points_[0].y};
  ceres::Problem problem;
  problem.AddResidualBlock(
      new ceres::AutoDiffCostFunction<RectangleResidual, 8, 8>(
          new RectangleResidual(residual)),
      nullptr, parameters);
  if (additional_width)
    problem.AddResidualBlock(
        new ceres::AutoDiffCostFunction<WidthResidual, 1, 8>(
            new WidthResidual{*additional_width}),
        nullptr, parameters);
  ceres::Solver::Options options;
  options.linear_solver_type = ceres::DENSE_QR;
  options.max_num_iterations = 50;
  options.function_tolerance = 1e-14;
  options.gradient_tolerance = 1e-14;
  options.parameter_tolerance = 1e-14;
  options.logging_type = ceres::SILENT;
  ceres::Solver::Summary summary;
  ceres::Solve(options, &problem, &summary);
  if (!summary.IsSolutionUsable()) return false;
  for (double value : parameters)
    if (!std::isfinite(value)) return false;
  double errors[8];
  residual(parameters, errors);
  for (double error : errors)
    if (!std::isfinite(error) || std::abs(error) > kTolerance) return false;
  if (additional_width &&
      std::abs(parameters[2] - parameters[0] - *additional_width) > kTolerance)
    return false;
  auto candidate = points_;
  for (int i = 0; i < 4; ++i) {
    candidate[i].x = parameters[2 * i];
    candidate[i].y = parameters[2 * i + 1];
  }
  points_ = candidate;
  anchored_ = anchor;
  return true;
}
bool Rectangle::translate(double dx, double dy) {
  if (anchored_ || !std::isfinite(dx) || !std::isfinite(dy)) return false;
  auto candidate = points_;
  for (auto& point : candidate) {
    point.x += dx;
    point.y += dy;
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) return false;
  }
  const double width = points_[1].x - points_[0].x;
  const double height = points_[2].y - points_[1].y;
  if (std::abs(candidate[1].x - candidate[0].x - width) > kTolerance ||
      std::abs(candidate[2].y - candidate[1].y - height) > kTolerance)
    return false;
  points_ = candidate;
  return true;
}
}  // namespace sketchcad
