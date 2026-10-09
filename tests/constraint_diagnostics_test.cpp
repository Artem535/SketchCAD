#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>

#include "sketchcad/diagnostics.h"
using namespace sketchcad;
using K = ConstraintKind;
using S = DiagnosisStatus;
namespace {
EntityId line(Sketch& s, Position a, Position b) {
  return *s.create_line(*s.create_point(a), *s.create_point(b));
}
EntityId start_of(const Sketch& s, EntityId id) {
  return std::get<SketchLine>(*s.entity(id)).start;
}
bool contains(const std::vector<EntityId>& ids, EntityId id) {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}
// Rectangle with horizontal/vertical sides; lines run bottom, right, top,
// left from `origin`.
Polyline boxed(Sketch& s, Position origin, double w, double h) {
  const Polyline r = *s.create_rectangle(origin, w, h);
  EXPECT_TRUE(s.add_constraint(K::kHorizontal, r.lines[0]));
  EXPECT_TRUE(s.add_constraint(K::kVertical, r.lines[1]));
  EXPECT_TRUE(s.add_constraint(K::kHorizontal, r.lines[2]));
  EXPECT_TRUE(s.add_constraint(K::kVertical, r.lines[3]));
  return r;
}
void expect_dof(const Sketch& s, int dof, S status = S::kConsistent) {
  const Diagnosis d = diagnose(s);
  EXPECT_EQ(d.status, status);
  EXPECT_EQ(d.reason, UnknownReason::kNone);
  ASSERT_TRUE(d.dof);
  EXPECT_EQ(*d.dof, dof);
  if (status == S::kConsistent) {
    EXPECT_TRUE(d.dependent.empty());
  }
  EXPECT_TRUE(d.violated.empty());
}
}  // namespace

TEST(Diagnostics, FreeGeometryCountsAllParameters) {
  Sketch s;
  expect_dof(s, 0);
  s.create_point({1, 2});
  expect_dof(s, 2);
  line(s, {0, 0}, {5, 0});
  expect_dof(s, 6);
  s.create_circle(*s.create_point({9, 9}), 3);
  expect_dof(s, 9);
  s.create_arc(*s.create_point({-9, 9}), 2, 0, 1);
  expect_dof(s, 14);
  EXPECT_EQ(diagnose(s).parameters, 14);
}

TEST(Diagnostics, ConstrainingALineRemovesItsDof) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 3});
  expect_dof(s, 4);
  ASSERT_TRUE(s.add_constraint(K::kHorizontal, l));
  expect_dof(s, 3);
  ASSERT_TRUE(s.add_constraint(K::kFix, start_of(s, l)));
  expect_dof(s, 1);
  ASSERT_TRUE(s.add_dimension(K::kLength, l, 0, 12));
  expect_dof(s, 0);
}

TEST(Diagnostics, RectangleReachesFullyConstrained) {
  Sketch s;
  const Polyline r = boxed(s, {3, 2}, 40, 20);
  expect_dof(s, 4);
  ASSERT_TRUE(s.add_constraint(K::kFix, r.points[0]));
  expect_dof(s, 2);
  ASSERT_TRUE(s.add_dimension(K::kLength, r.lines[0], 0, 40));
  ASSERT_TRUE(s.add_dimension(K::kLength, r.lines[1], 0, 20));
  expect_dof(s, 0);
}

TEST(Diagnostics, CircleWithFixedCenterAndRadius) {
  Sketch s;
  const EntityId center = *s.create_point({4, 4});
  const EntityId c = *s.create_circle(center, 6);
  ASSERT_TRUE(s.add_constraint(K::kFix, center));
  expect_dof(s, 1);
  ASSERT_TRUE(s.add_dimension(K::kRadius, c, 0, 6));
  expect_dof(s, 0);
}

TEST(Diagnostics, DuplicateConstraintIsRedundant) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 0});
  const EntityId other = line(s, {0, 5}, {10, 7});
  const EntityId a = *s.add_constraint(K::kHorizontal, l);
  const EntityId b = *s.add_constraint(K::kHorizontal, l);
  const EntityId lone = *s.add_constraint(K::kVertical, other);
  const Diagnosis d = diagnose(s);
  EXPECT_EQ(d.status, S::kRedundant);
  ASSERT_TRUE(d.dof);
  EXPECT_EQ(*d.dof, 6) << "a redundant constraint removes no DOF";
  EXPECT_TRUE(contains(d.dependent, a));
  EXPECT_TRUE(contains(d.dependent, b));
  EXPECT_FALSE(contains(d.dependent, lone));
  EXPECT_TRUE(std::is_sorted(d.dependent.begin(), d.dependent.end()));
}

TEST(Diagnostics, ParallelOnHorizontalSidesIsRedundant) {
  Sketch s;
  const Polyline r = boxed(s, {0, 0}, 30, 10);
  const EntityId parallel =
      *s.add_constraint(K::kParallel, r.lines[0], r.lines[2]);
  const Diagnosis d = diagnose(s);
  EXPECT_EQ(d.status, S::kRedundant);
  ASSERT_TRUE(d.dof);
  EXPECT_EQ(*d.dof, 4);
  EXPECT_TRUE(contains(d.dependent, parallel));
  EXPECT_TRUE(d.violated.empty());
}

TEST(Diagnostics, ContradictoryDimensionsConflict) {
  Sketch s;
  const Polyline r = boxed(s, {0, 0}, 50, 30);
  ASSERT_TRUE(s.add_constraint(K::kFix, r.points[0]));
  const EntityId width = *s.add_dimension(K::kLength, r.lines[0], 0, 50);
  const EntityId other = *s.add_dimension(K::kLength, r.lines[2], 0, 60);
  const Sketch before = s;
  const Diagnosis d = diagnose(s);
  EXPECT_EQ(d.status, S::kConflicting);
  EXPECT_FALSE(d.violated.empty());
  EXPECT_TRUE(contains(d.dependent, width));
  EXPECT_TRUE(contains(d.dependent, other));
  EXPECT_TRUE(s == before) << "diagnose must not change the sketch";
}

TEST(Diagnostics, UndefinedGeometryIsUnknown) {
  Sketch s;
  const EntityId collapsed = line(s, {5, 5}, {5, 5});
  const EntityId other = line(s, {0, 0}, {10, 3});
  ASSERT_TRUE(s.add_constraint(K::kParallel, collapsed, other));
  const Diagnosis d = diagnose(s);
  EXPECT_EQ(d.status, S::kUnknown);
  EXPECT_EQ(d.reason, UnknownReason::kInvalidGeometry);
  EXPECT_FALSE(d.dof);
  EXPECT_FALSE(d.rank);
  EXPECT_TRUE(d.dependent.empty());
}

TEST(Diagnostics, UnsolvedButSatisfiableSketchIsDiagnosedAfterSolving) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 4});
  ASSERT_TRUE(s.add_constraint(K::kHorizontal, l));
  const Sketch before = s;
  expect_dof(s, 3);
  EXPECT_TRUE(s == before);
}

TEST(Diagnostics, ResultsAreReproducibleAndCostIsRecorded) {
  Sketch s;
  for (int i = 0; i < 20; ++i) {
    const Polyline r = boxed(s, {i * 50.0, 0}, 40, 20 + i);
    ASSERT_TRUE(s.add_constraint(K::kFix, r.points[0]));
    ASSERT_TRUE(s.add_dimension(K::kLength, r.lines[0], 0, 40));
    ASSERT_TRUE(s.add_dimension(K::kLength, r.lines[1], 0, 20 + i));
  }
  const auto start = std::chrono::steady_clock::now();
  const Diagnosis first = diagnose(s);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  ::testing::Test::RecordProperty(
      "diagnose_20_rectangles_us",
      static_cast<int>(
          std::chrono::duration_cast<std::chrono::microseconds>(elapsed)
              .count()));
  EXPECT_EQ(first.status, S::kConsistent);
  ASSERT_TRUE(first.dof);
  EXPECT_EQ(*first.dof, 0);
  EXPECT_EQ(first.parameters, 160);
  EXPECT_EQ(diagnose(s), first);
}

// U04: a point on a fixed line can still slide along it.
TEST(Diagnostics, PointOnAFixedLineKeepsOneFreedom) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 0});
  const auto ends = std::get<SketchLine>(*s.entity(l));
  ASSERT_TRUE(s.add_constraint(K::kFix, ends.start));
  ASSERT_TRUE(s.add_constraint(K::kFix, ends.end));
  const EntityId p = *s.create_point({4, 0});
  ASSERT_TRUE(s.add_constraint(K::kOnCurve, p, l));
  expect_dof(s, 1);
}
