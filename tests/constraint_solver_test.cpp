#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <tuple>

#include "sketchcad/solver.h"
using namespace sketchcad;
using K = ConstraintKind;
namespace {
// Independent residual oracle: plain geometry, deliberately not sharing
// formulas with the solver (angles via atan2, distances via projection).
Position at(const Sketch& s, EntityId point) {
  return std::get<SketchPoint>(*s.entity(point)).position;
}
std::pair<Position, Position> ends(const Sketch& s, EntityId line) {
  const auto l = std::get<SketchLine>(*s.entity(line));
  return {at(s, l.start), at(s, l.end)};
}
std::pair<Position, double> curve(const Sketch& s, EntityId id) {
  const Entity e = *s.entity(id);
  if (const auto* c = std::get_if<SketchCircle>(&e))
    return {at(s, c->center), c->radius};
  const auto a = std::get<SketchArc>(e);
  return {at(s, a.center), a.radius};
}
double length(Position a, Position b) { return std::hypot(b.x - a.x, b.y - a.y); }
double direction(const Sketch& s, EntityId line) {
  const auto [a, b] = ends(s, line);
  return std::atan2(b.y - a.y, b.x - a.x);
}
bool is_line(const Sketch& s, EntityId id) {
  return std::holds_alternative<SketchLine>(*s.entity(id));
}
// Residual and whether it is angular.
std::pair<double, bool> oracle(const Sketch& s, const Constraint& c) {
  switch (c.kind) {
    case K::kCoincident:
      return {length(at(s, c.first), at(s, c.second)), false};
    case K::kHorizontal:
    case K::kVertical: {
      const auto [a, b] = c.second ? std::pair{at(s, c.first), at(s, c.second)}
                                   : ends(s, c.first);
      return {c.kind == K::kHorizontal ? b.y - a.y : b.x - a.x, false};
    }
    case K::kParallel:
      return {std::sin(direction(s, c.first) - direction(s, c.second)), true};
    case K::kPerpendicular:
      return {std::cos(direction(s, c.first) - direction(s, c.second)), true};
    case K::kTangentStart:
    case K::kTangentEnd: {
      const auto l = std::get<SketchLine>(*s.entity(c.first));
      const Position p = at(s, c.side == 1 ? l.start : l.end);
      const auto [center, radius] = curve(s, c.second);
      const auto [a, b] = ends(s, c.first);
      const double dot =
          ((p.x - center.x) * (b.x - a.x) + (p.y - center.y) * (b.y - a.y)) /
          length(a, b);
      const double normal = ((b.x - a.x) * (center.y - p.y) -
                             (b.y - a.y) * (center.x - p.x)) / length(a, b);
      return {std::max(std::abs((c.internal ? -1 : 1) * normal - radius),
                       std::abs(dot)), false};
    }
    case K::kTangent: {
      const auto [c2, r2] = curve(s, c.second);
      if (is_line(s, c.first)) {
        const auto [a, b] = ends(s, c.first);
        const double t = ((c2.x - a.x) * (b.x - a.x) + (c2.y - a.y) * (b.y - a.y)) /
                         std::pow(length(a, b), 2);
        const Position foot{a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)};
        return {length(foot, c2) - r2, false};
      }
      const auto [c1, r1] = curve(s, c.first);
      const double contact = c.internal ? std::abs(r1 - r2) : r1 + r2;
      return {length(c1, c2) - contact, false};
    }
    case K::kEqual:
      if (is_line(s, c.first)) {
        const auto [a, b] = ends(s, c.first);
        const auto [d, e] = ends(s, c.second);
        return {length(a, b) - length(d, e), false};
      }
      return {curve(s, c.first).second - curve(s, c.second).second, false};
    case K::kFix:
      return {length(at(s, c.first), c.target), false};
    case K::kOnCurve: {
      const Position p = at(s, c.first);
      if (is_line(s, c.second)) {
        const auto [a, b] = ends(s, c.second);
        const double t = ((p.x - a.x) * (b.x - a.x) + (p.y - a.y) * (b.y - a.y)) /
                         std::pow(length(a, b), 2);
        return {length(p, {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)}), false};
      }
      const auto [centre, r] = curve(s, c.second);
      return {length(p, centre) - r, false};
    }
    case K::kLength:
    case K::kDistance:
    case K::kAngle:
    case K::kRadius:
      break;  // Dimensions have their own oracle in dimension_test.
  }
  return {INFINITY, false};
}
void expect_satisfied(const Sketch& s) {
  ASSERT_FALSE(s.constraints().empty());
  for (const auto& [id, c] : s.constraints()) {
    const auto [r, angular] = oracle(s, c);
    EXPECT_LE(std::abs(r), angular ? kAngleTolerance : kLengthTolerance)
        << "constraint " << id << " kind " << static_cast<int>(c.kind);
  }
}
void expect_solved(Sketch& s) {
  const SolveResult result = solve(s);
  EXPECT_EQ(result.status, SolveStatus::kSolved);
  EXPECT_TRUE(result.violated.empty());
  expect_satisfied(s);
}
void expect_conflict(Sketch& s) {
  const Sketch before = s;
  const SolveResult result = solve(s);
  EXPECT_EQ(result.status, SolveStatus::kUnsatisfied);
  EXPECT_FALSE(result.violated.empty());
  EXPECT_TRUE(s == before) << "a failed solve must not change the sketch";
}
EntityId line(Sketch& s, Position a, Position b) {
  return *s.create_line(*s.create_point(a), *s.create_point(b));
}
std::pair<EntityId, EntityId> points_of(const Sketch& s, EntityId id) {
  const auto l = std::get<SketchLine>(*s.entity(id));
  return {l.start, l.end};
}
// Fixes both endpoints of a line.
void pin(Sketch& s, EntityId id) {
  const auto [a, b] = points_of(s, id);
  ASSERT_TRUE(s.add_constraint(K::kFix, a));
  ASSERT_TRUE(s.add_constraint(K::kFix, b));
}
}  // namespace

TEST(Solver, EmptyConstraintSetIsSolvedWithoutChange) {
  Sketch s;
  line(s, {0, 0}, {3, 4});
  const Sketch before = s;
  EXPECT_EQ(solve(s).status, SolveStatus::kSolved);
  EXPECT_TRUE(s == before);
}

TEST(Solver, Coincident) {
  Sketch s;
  const EntityId p = *s.create_point({0, 0}), q = *s.create_point({3, 4});
  ASSERT_TRUE(s.add_constraint(K::kCoincident, p, q));
  expect_solved(s);

  Sketch conflict;
  const EntityId a = *conflict.create_point({0, 0});
  const EntityId b = *conflict.create_point({3, 4});
  ASSERT_TRUE(conflict.add_constraint(K::kFix, a));
  ASSERT_TRUE(conflict.add_constraint(K::kFix, b));
  ASSERT_TRUE(conflict.add_constraint(K::kCoincident, a, b));
  expect_conflict(conflict);
}

TEST(Solver, HorizontalAndVertical) {
  for (K kind : {K::kHorizontal, K::kVertical}) {
    Sketch s;
    ASSERT_TRUE(s.add_constraint(kind, line(s, {0, 0}, {10, 3})));
    const EntityId p = *s.create_point({20, 0}), q = *s.create_point({26, 5});
    ASSERT_TRUE(s.add_constraint(kind, p, q));
    expect_solved(s);

    Sketch conflict;
    const EntityId l = line(conflict, {0, 0}, {10, 3});
    pin(conflict, l);
    ASSERT_TRUE(conflict.add_constraint(kind, l));
    expect_conflict(conflict);
  }
}

TEST(Solver, ParallelAndPerpendicular) {
  for (K kind : {K::kParallel, K::kPerpendicular}) {
    Sketch s;
    const EntityId a = line(s, {0, 0}, {10, 0});
    const EntityId b = line(s, {2, 5}, {9, 9});
    ASSERT_TRUE(s.add_constraint(kind, a, b));
    expect_solved(s);

    Sketch conflict;
    const EntityId c = line(conflict, {0, 0}, {10, 0});
    const EntityId d = line(conflict, {2, 5}, {9, 9});
    pin(conflict, c);
    pin(conflict, d);
    ASSERT_TRUE(conflict.add_constraint(kind, c, d));
    expect_conflict(conflict);
  }
}

TEST(Solver, TangentLineToCircleAndArc) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 0});
  const EntityId circle = *s.create_circle(*s.create_point({5, 7}), 3);
  const EntityId arc = *s.create_arc(*s.create_point({-6, -4}), 2, 0, 2);
  ASSERT_TRUE(s.add_constraint(K::kTangent, l, circle));
  ASSERT_TRUE(s.add_constraint(K::kTangent, arc, l));
  expect_solved(s);
}

TEST(Solver, TangentCircles) {
  Sketch s;
  const EntityId a = *s.create_circle(*s.create_point({0, 0}), 5);
  const EntityId b = *s.create_circle(*s.create_point({12, 0}), 4);
  const EntityId inner = *s.create_arc(*s.create_point({1, 1}), 2, 0, 3);
  ASSERT_TRUE(s.add_constraint(K::kTangent, a, b));
  const EntityId internal = *s.add_constraint(K::kTangent, a, inner);
  ASSERT_TRUE(s.constraint(internal)->internal);
  expect_solved(s);
}

TEST(Solver, TangentConflictWithEqualRadii) {
  // Fixed centres 10 mm apart touching externally need r_a + r_b = 10;
  // the fixed line 3 mm from A forces r_a = 3, contradicting r_a = r_b.
  Sketch s;
  const EntityId ca = *s.create_point({0, 20}), cb = *s.create_point({10, 20});
  const EntityId a = *s.create_circle(ca, 4), b = *s.create_circle(cb, 4);
  const EntityId l = line(s, {-10, 17}, {10, 17});
  ASSERT_TRUE(s.add_constraint(K::kFix, ca));
  ASSERT_TRUE(s.add_constraint(K::kFix, cb));
  pin(s, l);
  const EntityId touch = *s.add_constraint(K::kTangent, a, b);
  ASSERT_FALSE(s.constraint(touch)->internal);
  ASSERT_TRUE(s.add_constraint(K::kEqual, a, b));
  ASSERT_TRUE(s.add_constraint(K::kTangent, l, a));
  expect_conflict(s);
}

TEST(Solver, EqualLengthAndRadius) {
  Sketch s;
  const EntityId a = line(s, {0, 0}, {10, 0});
  const EntityId b = line(s, {0, 5}, {6, 5});
  const EntityId circle = *s.create_circle(*s.create_point({30, 0}), 5);
  const EntityId arc = *s.create_arc(*s.create_point({40, 0}), 3, 0, 1);
  ASSERT_TRUE(s.add_constraint(K::kEqual, a, b));
  ASSERT_TRUE(s.add_constraint(K::kEqual, circle, arc));
  expect_solved(s);

  Sketch conflict;
  const EntityId c = line(conflict, {0, 0}, {10, 0});
  const EntityId d = line(conflict, {0, 5}, {6, 5});
  pin(conflict, c);
  pin(conflict, d);
  ASSERT_TRUE(conflict.add_constraint(K::kEqual, c, d));
  expect_conflict(conflict);
}

TEST(Solver, FixRestoresTargetAndConflictsWithSecondFix) {
  Sketch s;
  const EntityId p = *s.create_point({3, 7});
  ASSERT_TRUE(s.add_constraint(K::kFix, p));
  ASSERT_TRUE(s.update_point(p, {9, -2}));
  expect_solved(s);
  EXPECT_NEAR(at(s, p).x, 3, kLengthTolerance);

  ASSERT_TRUE(s.update_point(p, {9, -2}));
  ASSERT_TRUE(s.add_constraint(K::kFix, p));
  expect_conflict(s);
}

TEST(Solver, MixedGeometrySolvesTogether) {
  Sketch s;
  const Polyline rect = *s.create_rectangle({0, 0}, 40, 20);
  // Lines run bottom, right, top, left from the origin corner.
  ASSERT_TRUE(s.add_constraint(K::kHorizontal, rect.lines[0]));
  ASSERT_TRUE(s.add_constraint(K::kVertical, rect.lines[1]));
  ASSERT_TRUE(s.add_constraint(K::kHorizontal, rect.lines[2]));
  ASSERT_TRUE(s.add_constraint(K::kVertical, rect.lines[3]));
  ASSERT_TRUE(s.add_constraint(K::kEqual, rect.lines[0], rect.lines[2]));
  ASSERT_TRUE(s.add_constraint(K::kFix, rect.points[0]));
  const EntityId circle = *s.create_circle(*s.create_point({20, 30}), 5);
  const EntityId arc = *s.create_arc(*s.create_point({60, 10}), 2, 0, 2);
  ASSERT_TRUE(s.add_constraint(K::kTangent, rect.lines[2], circle));
  ASSERT_TRUE(s.add_constraint(K::kEqual, circle, arc));
  ASSERT_TRUE(s.update_point(rect.points[2], {43, 24}));
  const EntityId loose = *s.create_circle(*s.create_point({-50, -50}), 7);
  const Entity loose_before = *s.entity(loose);

  expect_solved(s);
  EXPECT_NEAR(at(s, rect.points[0]).x, 0, kLengthTolerance);
  EXPECT_NEAR(at(s, rect.points[0]).y, 0, kLengthTolerance);
  EXPECT_EQ(*s.entity(loose), loose_before) << "unconstrained geometry moved";
}

TEST(Solver, AlreadySatisfiedConstraintsLeaveValuesUnchanged) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 0});
  ASSERT_TRUE(s.add_constraint(K::kHorizontal, l));
  const Sketch before = s;
  EXPECT_EQ(solve(s).status, SolveStatus::kSolved);
  EXPECT_TRUE(s == before);
}

TEST(Solver, ZeroLengthLineIsInvalidInput) {
  for (K kind : {K::kParallel, K::kPerpendicular, K::kEqual}) {
    Sketch s;
    const EntityId collapsed = line(s, {5, 5}, {5, 5});
    const EntityId other = line(s, {0, 0}, {10, 3});
    ASSERT_TRUE(s.add_constraint(kind, collapsed, other));
    const Sketch before = s;
    EXPECT_EQ(solve(s).status, SolveStatus::kInvalidInput)
        << static_cast<int>(kind);
    EXPECT_TRUE(s == before);
  }
}

TEST(Solver, ConcentricInternalTangencyIsInvalidInput) {
  Sketch s;
  const EntityId a = *s.create_circle(*s.create_point({0, 0}), 5);
  const EntityId b = *s.create_circle(*s.create_point({0, 0}), 4);
  const EntityId id = *s.add_constraint(K::kTangent, a, b);
  ASSERT_TRUE(s.constraint(id)->internal);
  EXPECT_EQ(solve(s).status, SolveStatus::kInvalidInput);
}

TEST(SolverDocument, ConflictingConstraintRollsBackWithConstraintSet) {
  Sketch initial;
  const EntityId p = *initial.create_point({0, 0});
  const EntityId q = *initial.create_point({3, 4});
  ASSERT_TRUE(initial.add_constraint(K::kFix, p));
  ASSERT_TRUE(initial.add_constraint(K::kFix, q));
  Document doc(initial);
  doc.set_commit_step(solver_step());
  const Sketch before = doc.sketch();

  EXPECT_FALSE(doc.execute("Coincident", [&](Sketch& s) {
    return s.add_constraint(K::kCoincident, p, q).has_value();
  }));
  EXPECT_TRUE(doc.sketch() == before);
  EXPECT_FALSE(doc.can_undo());
}

TEST(SolverDocument, SolvedConstraintIsOneUndoStep) {
  Sketch initial;
  const EntityId l = line(initial, {0, 0}, {10, 3});
  Document doc(initial);
  doc.set_commit_step(solver_step());
  const Sketch before = doc.sketch();

  ASSERT_TRUE(doc.execute("Horizontal", [&](Sketch& s) {
    return s.add_constraint(K::kHorizontal, l).has_value();
  }));
  expect_satisfied(doc.sketch());
  ASSERT_TRUE(doc.undo());
  EXPECT_TRUE(doc.sketch() == before);
  EXPECT_TRUE(doc.sketch().constraints().empty());
  ASSERT_TRUE(doc.redo());
  EXPECT_EQ(doc.sketch().constraints().size(), 1u);
}

TEST(SolverDocument, ConstraintOnSatisfiedGeometryIsStillAChange) {
  Sketch initial;
  const EntityId l = line(initial, {0, 0}, {10, 0});
  Document doc(initial);
  doc.set_commit_step(solver_step());
  ASSERT_TRUE(doc.execute("Horizontal", [&](Sketch& s) {
    return s.add_constraint(K::kHorizontal, l).has_value();
  }));
  EXPECT_TRUE(doc.can_undo());
  EXPECT_TRUE(doc.dirty());
}

TEST(SolverDegenerate, HorizontalAndVerticalOnOneLine) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 3});
  ASSERT_TRUE(s.add_constraint(K::kHorizontal, l));
  ASSERT_EQ(solve(s).status, SolveStatus::kSolved);
  ASSERT_TRUE(s.add_constraint(K::kVertical, l));
  const Sketch before = s;
  EXPECT_EQ(solve(s).status, SolveStatus::kDegenerate);
  EXPECT_TRUE(s == before);
}

TEST(SolverDegenerate, CoincidentEndpointsOfOneLine) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 3});
  const auto [a, b] = points_of(s, l);
  ASSERT_TRUE(s.add_constraint(K::kCoincident, a, b));
  const Sketch before = s;
  EXPECT_EQ(solve(s).status, SolveStatus::kDegenerate);
  EXPECT_TRUE(s == before);
}

TEST(SolverDegenerate, RadiusCollapse) {
  Sketch s;
  const EntityId center = *s.create_point({5, 0});
  const EntityId c = *s.create_circle(center, 3);
  const EntityId l = line(s, {0, 0}, {10, 0});
  ASSERT_TRUE(s.add_constraint(K::kFix, center));
  pin(s, l);
  ASSERT_TRUE(s.add_constraint(K::kTangent, l, c));
  const Sketch before = s;
  EXPECT_EQ(solve(s).status, SolveStatus::kDegenerate);
  EXPECT_TRUE(s == before);
}

TEST(SolverDegenerate, DocumentRollsBackAndFreePointsStillMeet) {
  Sketch initial;
  const EntityId l = line(initial, {0, 0}, {10, 3});
  const EntityId p = *initial.create_point({20, 0});
  const EntityId q = *initial.create_point({25, 4});
  ASSERT_TRUE(initial.add_constraint(K::kHorizontal, l));
  Document doc(initial);
  doc.set_commit_step(solver_step());
  const Sketch before = doc.sketch();
  EXPECT_FALSE(doc.execute("Vertical", [&](Sketch& s) {
    return s.add_constraint(K::kVertical, l).has_value();
  }));
  EXPECT_TRUE(doc.sketch() == before);
  EXPECT_TRUE(doc.execute("Coincident", [&](Sketch& s) {
    return s.add_constraint(K::kCoincident, p, q).has_value();
  }));
}

TEST(SolverDegenerate, VerticalOnHorizontalLineRotatesKeepingLength) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 0});
  ASSERT_TRUE(s.add_constraint(K::kVertical, l));
  const std::size_t constraints = s.constraints().size();
  expect_solved(s);
  const auto [a, b] = points_of(s, l);
  EXPECT_NEAR(length(at(s, a), at(s, b)), 10, kLengthTolerance);
  EXPECT_EQ(s.constraints().size(), constraints) << "no constraint is kept";
}

// U04 (sketch-editing.adoc).
TEST(Solver, OnCurveMovesAPointOntoALineACircleAndAnArc) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 0});
  pin(s, l);
  const EntityId p = *s.create_point({4, 3});
  ASSERT_TRUE(s.add_constraint(K::kOnCurve, p, l));
  expect_solved(s);
  EXPECT_NEAR(at(s, p).y, 0, kLengthTolerance);

  for (const bool arc : {false, true}) {
    Sketch t;
    const EntityId centre = *t.create_point({0, 0});
    ASSERT_TRUE(t.add_constraint(K::kFix, centre));
    const EntityId curve =
        arc ? *t.create_arc(centre, 5, 0, std::acos(-1.0) / 2)
            : *t.create_circle(centre, 5);
    ASSERT_TRUE(t.add_dimension(K::kRadius, curve, 0, 5));
    // Outside the arc's sweep: the arc counts as its full circle.
    const EntityId q = *t.create_point({-1, -2});
    ASSERT_TRUE(t.add_constraint(K::kOnCurve, q, curve));
    // The radius dimension is outside this oracle; check the point directly.
    EXPECT_EQ(solve(t).status, SolveStatus::kSolved) << arc;
    EXPECT_NEAR(length(at(t, q), {0, 0}), 5, kLengthTolerance) << arc;
  }
}

TEST(Solver, OnCurveConflictsWithAFixedPointOffTheLine) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 0});
  pin(s, l);
  const EntityId p = *s.create_point({4, 3});
  ASSERT_TRUE(s.add_constraint(K::kFix, p));
  ASSERT_TRUE(s.add_constraint(K::kOnCurve, p, l));
  expect_conflict(s);
}

TEST(ProjectValidation, ChecksStoredPositionsWithoutSolving) {
  Sketch s;
  EXPECT_EQ(check_constraints(s).status, SolveStatus::kSolved);
  const auto a = *s.create_point({0, 0});
  const auto b = *s.create_point({10, 1});
  const auto l = *s.create_line(a, b);
  s.add_constraint(ConstraintKind::kHorizontal, l);
  const Sketch before = s;
  EXPECT_EQ(check_constraints(s).status, SolveStatus::kUnsatisfied);
  EXPECT_TRUE(s == before);
}

// Issue #59: endpoint bindings must constrain contact and derive arc angles.
TEST(ArcBindings, InitialSolvedSynchronizesAnglesAndRadiusEdit) {
  Sketch s;
  const auto far_x = *s.create_point({10, 0});
  const auto contact_x = *s.create_point({2, 0});
  const auto contact_y = *s.create_point({0, 2});
  const auto far_y = *s.create_point({0, 10});
  const auto center = *s.create_point({2, 2});
  const auto lx = *s.create_line(far_x, contact_x);
  const auto ly = *s.create_line(contact_y, far_y);
  const auto arc = *s.create_arc(center, 2, 0.1, 0.2);
  ASSERT_TRUE(s.add_arc_tangency(K::kTangentStart, ly, arc, contact_y));
  ASSERT_TRUE(s.add_arc_tangency(K::kTangentEnd, lx, arc, contact_x));
  s.add_constraint(K::kFix, far_x);
  s.add_constraint(K::kFix, far_y);
  s.add_constraint(K::kHorizontal, lx);
  s.add_constraint(K::kVertical, ly);
  const auto radius = *s.add_dimension(K::kRadius, arc, 0, 2);
  ASSERT_EQ(solve(s).status, SolveStatus::kSolved);
  auto a = std::get<SketchArc>(*s.entity(arc));
  EXPECT_NEAR(std::cos(a.start_angle), -1, 1e-7);
  EXPECT_NEAR(a.sweep_angle, std::acos(-1.0) / 2, 1e-7);
  ASSERT_TRUE(s.set_dimension(radius, 3));
  ASSERT_EQ(solve(s).status, SolveStatus::kSolved);
  a = std::get<SketchArc>(*s.entity(arc));
  const auto c = at(s, center);
  for (const auto& [p, angle] :
       {std::pair{contact_y, a.start_angle},
        std::pair{contact_x, a.start_angle + a.sweep_angle}}) {
    EXPECT_NEAR(at(s, p).x, c.x + a.radius * std::cos(angle), 1e-6);
    EXPECT_NEAR(at(s, p).y, c.y + a.radius * std::sin(angle), 1e-6);
  }
  EXPECT_NEAR(at(s, contact_x).x, 3, 1e-6);
  EXPECT_NEAR(at(s, contact_y).y, 3, 1e-6);
}

TEST(ArcBindings, SingleBindingPreservesOtherEndAndOrdinaryArcs) {
  Sketch s;
  const auto c = *s.create_point({0, 2});
  const auto p = *s.create_point({0, 0}), q = *s.create_point({5, 0});
  const auto l = *s.create_line(p, q);
  const auto arc = *s.create_arc(c, 2, 0, 1);
  const auto ordinary = *s.create_arc(c, 4, 0.3, -1.1);
  const auto before = s.entity(ordinary);
  ASSERT_TRUE(s.add_arc_tangency(K::kTangentStart, l, arc, p));
  ASSERT_EQ(solve(s).status, SolveStatus::kSolved);
  const auto a = std::get<SketchArc>(*s.entity(arc));
  EXPECT_NEAR(
      std::remainder(a.start_angle + a.sweep_angle - 1, 2 * std::acos(-1.0)), 0,
      1e-7);
  EXPECT_NEAR(std::sin(a.start_angle), -1, 1e-7);
  EXPECT_EQ(s.entity(ordinary), before);
}

TEST(ArcBindings, CoincidentContactsRejectedTransactionally) {
  Sketch s;
  const auto c = *s.create_point({0, 2});
  const auto p = *s.create_point({0, 0}), q = *s.create_point({5, 0});
  const auto p2 = *s.create_point({0, 0}), q2 = *s.create_point({-5, 0});
  const auto l = *s.create_line(p, q), l2 = *s.create_line(q2, p2);
  const auto arc = *s.create_arc(c, 2, 0, 1);
  ASSERT_TRUE(s.add_arc_tangency(K::kTangentStart, l, arc, p));
  ASSERT_TRUE(s.add_arc_tangency(K::kTangentEnd, l2, arc, p2));
  const auto before = s.snapshot();
  EXPECT_NE(check_constraints(s).status, SolveStatus::kSolved);
  EXPECT_NE(solve(s).status, SolveStatus::kSolved);
  EXPECT_EQ(s.snapshot().entities, before.entities);
  EXPECT_EQ(s.snapshot().constraints, before.constraints);
}

TEST(ArcBindings, ObliqueContactsAndRotation) {
  const double pi = std::acos(-1.0);
  for (const double theta : {pi / 3, pi / 2, 2 * pi / 3}) {
    for (const double rotation : {0.37, 2.6, -2.1}) {
      Sketch s;
      const auto transform = [&](double x, double y) {
        return Position{7 + x * std::cos(rotation) - y * std::sin(rotation),
                        -4 + x * std::sin(rotation) + y * std::cos(rotation)};
      };
      const double t = 2 / std::tan(theta / 2);
      const auto p = *s.create_point(transform(t, 0));
      const auto q = *s.create_point(transform(12, 0));
      const auto p2 =
          *s.create_point(transform(t * std::cos(theta), t * std::sin(theta)));
      const auto q2 = *s.create_point(
          transform(12 * std::cos(theta), 12 * std::sin(theta)));
      const auto c = *s.create_point(transform(t, 2));
      const auto l = *s.create_line(q, p), l2 = *s.create_line(p2, q2);
      const auto arc = *s.create_arc(c, 2, 0.2, 0.4);
      ASSERT_TRUE(s.add_arc_tangency(K::kTangentStart, l2, arc, p2));
      ASSERT_TRUE(s.add_arc_tangency(K::kTangentEnd, l, arc, p));
      s.add_constraint(K::kFix, q);
      s.add_constraint(K::kFix, q2);
      const auto origin = *s.create_point(transform(0, 0));
      s.add_constraint(K::kFix, origin);
      s.add_constraint(K::kOnCurve, origin, l);
      s.add_constraint(K::kOnCurve, origin, l2);
      const auto radius = *s.add_dimension(K::kRadius, arc, 0, 2);
      ASSERT_EQ(solve(s).status, SolveStatus::kSolved);
      ASSERT_TRUE(s.set_dimension(radius, 3));
      ASSERT_EQ(solve(s).status, SolveStatus::kSolved);
      const auto a = std::get<SketchArc>(*s.entity(arc));
      EXPECT_NEAR(a.sweep_angle, pi - theta, 1e-6);
      for (const auto& [line, point, angle] :
           {std::tuple{l2, p2, a.start_angle},
            std::tuple{l, p, a.start_angle + a.sweep_angle}}) {
        const auto center = at(s, c), contact = at(s, point);
        EXPECT_NEAR(contact.x, center.x + 3 * std::cos(angle), 1e-6);
        EXPECT_NEAR(contact.y, center.y + 3 * std::sin(angle), 1e-6);
        const auto [u, v] = ends(s, line);
        EXPECT_NEAR((contact.x - center.x) * (v.x - u.x) +
                        (contact.y - center.y) * (v.y - u.y),
                    0, 1e-6);
      }
    }
  }
}

TEST(ArcBindings, CoordinateEditRestoresCapturedNormalBranch) {
  Sketch s;
  const auto p = *s.create_point({0, 0}), q = *s.create_point({4, 0});
  const auto center = *s.create_point({0, 2});
  const auto line = *s.create_line(q, p);
  const auto arc = *s.create_arc(center, 2, -std::acos(-1.0) / 2, 1);
  const auto binding = s.add_arc_tangency(K::kTangentStart, line, arc, p);
  ASSERT_TRUE(binding);
  ASSERT_TRUE(s.constraint(*binding)->internal);
  s.add_constraint(K::kFix, p);
  s.add_constraint(K::kFix, q);
  s.add_dimension(K::kRadius, arc, 0, 2);
  ASSERT_TRUE(s.update_point(center, {0, -2}));
  ASSERT_NE(check_constraints(s).status, SolveStatus::kSolved);
  ASSERT_EQ(solve(s).status, SolveStatus::kSolved);
  EXPECT_NEAR(at(s, center).y, 2, 1e-6);
  EXPECT_TRUE(s.constraint(*binding)->internal);
  EXPECT_LE(oracle(s, *s.constraint(*binding)).first, kLengthTolerance);
  EXPECT_EQ(check_constraints(s).status, SolveStatus::kSolved);
}

TEST(ArcBindings, InitiallySatisfiedContactsCannotSwitchMajorToMinorBranch) {
  Sketch s;
  const auto center = *s.create_point({2, 2});
  const auto px = *s.create_point({2, 0}), py = *s.create_point({0, 2});
  const auto lx = *s.create_line(*s.create_point({10, 0}), px);
  const auto ly = *s.create_line(py, *s.create_point({0, 10}));
  const auto arc = *s.create_arc(center, 2, std::acos(-1.0),
                                  3 * std::acos(-1.0) / 2);
  ASSERT_TRUE(s.add_arc_tangency(K::kTangentStart, ly, arc, py));
  ASSERT_TRUE(s.add_arc_tangency(K::kTangentEnd, lx, arc, px));
  const auto before = s;
  EXPECT_NE(check_constraints(s).status, SolveStatus::kSolved);
  EXPECT_NE(solve(s).status, SolveStatus::kSolved);
  EXPECT_EQ(s, before);
}
