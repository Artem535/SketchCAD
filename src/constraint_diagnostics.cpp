#include "sketchcad/diagnostics.h"

#include <Eigen/SVD>
#include <algorithm>

#include "solver_problem.h"

namespace sketchcad {
namespace {
// ADR-0003 thresholds, relative to the largest singular value.
constexpr double kRankThreshold = 1e-6;
constexpr double kGrayZoneFloor = 1e-12;
constexpr double kDependencyThreshold = 1e-6;
constexpr Eigen::Index kMaxSize = 2000;

int parameter_count(const Sketch& sketch) {
  int count = 0;
  for (const auto& [id, e] : sketch.entities()) {
    if (std::holds_alternative<SketchPoint>(e)) count += 2;
    if (std::holds_alternative<SketchCircle>(e)) count += 1;
    if (std::holds_alternative<SketchArc>(e)) count += 3;
  }
  return count;
}

Diagnosis unknown(Diagnosis d, UnknownReason reason) {
  d.status = DiagnosisStatus::kUnknown;
  d.reason = reason;
  d.dof.reset();
  d.rank.reset();
  d.dependent.clear();
  return d;
}

std::vector<EntityId> sorted_unique(std::vector<EntityId> ids) {
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  return ids;
}
}  // namespace

Diagnosis diagnose(const Sketch& sketch) {
  Diagnosis d;
  d.parameters = parameter_count(sketch);
  if (sketch.constraints().empty()) {
    d.status = DiagnosisStatus::kConsistent;
    d.dof = d.parameters;
    d.rank = 0;
    return d;
  }

  // Solved geometry when possible, the input geometry for a conflict.
  Sketch solved = sketch;
  const SolveResult result = solve(solved);
  if (result.status == SolveStatus::kInvalidInput ||
      result.status == SolveStatus::kDegenerate)
    return unknown(d, UnknownReason::kInvalidGeometry);
  if (result.status == SolveStatus::kNumericalFailure)
    return unknown(d, UnknownReason::kNumericalFailure);
  d.violated = sorted_unique(result.violated);
  const Sketch& at =
      result.status == SolveStatus::kSolved ? solved : sketch;

  detail::Problem problem(at);
  if (!problem.build()) return unknown(d, UnknownReason::kInvalidGeometry);
  std::vector<EntityId> rows;
  Eigen::MatrixXd j = problem.jacobian(rows);
  if (j.rows() > kMaxSize || j.cols() > kMaxSize)
    return unknown(d, UnknownReason::kTooLarge);

  // Unit rows: residual units and scale do not affect the rank.
  for (Eigen::Index i = 0; i < j.rows(); ++i) {
    const double norm = j.row(i).norm();
    if (!std::isfinite(norm) || norm == 0)
      return unknown(d, UnknownReason::kInvalidGeometry);
    j.row(i) /= norm;
  }

  const Eigen::JacobiSVD<Eigen::MatrixXd, Eigen::ComputeFullU> svd(j);
  const Eigen::VectorXd& sigma = svd.singularValues();
  const double largest = sigma.size() > 0 ? sigma(0) : 0;
  int rank = 0;
  for (Eigen::Index i = 0; i < sigma.size(); ++i) {
    const double ratio = sigma(i) / largest;
    if (ratio >= kRankThreshold)
      ++rank;
    else if (ratio > kGrayZoneFloor)
      return unknown(d, UnknownReason::kIllConditioned);
  }

  // Rows with a component in the left null space take part in a linear
  // dependency (ADR-0003): all participants, not a minimal set.
  const Eigen::MatrixXd& u = svd.matrixU();
  const Eigen::Index null_dims = j.rows() - rank;
  std::vector<EntityId> dependent;
  if (null_dims > 0)
    for (Eigen::Index i = 0; i < j.rows(); ++i)
      if (u.row(i).tail(null_dims).norm() >= kDependencyThreshold)
        dependent.push_back(rows[i]);
  d.dependent = sorted_unique(std::move(dependent));

  d.rank = rank;
  d.dof = d.parameters - rank;
  d.status = !d.violated.empty()     ? DiagnosisStatus::kConflicting
             : !d.dependent.empty() ? DiagnosisStatus::kRedundant
                                    : DiagnosisStatus::kConsistent;
  return d;
}
}  // namespace sketchcad
