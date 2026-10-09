#include <gtest/gtest.h>

#include <chrono>
#include <cmath>

#include "sketchcad/drag.h"
using namespace sketchcad;
using K = ConstraintKind;
namespace {
constexpr double kNear = 1e-3;  // mm, for the soft (intent) expectations.

// Independent oracle for the kinds used here; plain geometry.
Position at(const Sketch& s, EntityId point) {
  return std::get<SketchPoint>(*s.entity(point)).position;
}
std::pair<EntityId, EntityId> ends(const Sketch& s, EntityId line) {
  const auto l = std::get<SketchLine>(*s.entity(line));
  return {l.start, l.end};
}
double distance(Position a, Position b) { return std::hypot(b.x - a.x, b.y - a.y); }
double residual(const Sketch& s, const Constraint& c) {
  switch (c.kind) {
    case K::kCoincident:
      return distance(at(s, c.first), at(s, c.second));
    case K::kHorizontal:
    case K::kVertical: {
      const auto [p, q] = c.second ? std::pair{c.first, c.second} : ends(s, c.first);
      const Position a = at(s, p), b = at(s, q);
      return c.kind == K::kHorizontal ? b.y - a.y : b.x - a.x;
    }
    case K::kFix:
      return distance(at(s, c.first), c.target);
    case K::kLength: {
      const auto [p, q] = ends(s, c.first);
      return distance(at(s, p), at(s, q)) - c.value;
    }
    default:
      return INFINITY;  // Not used by these fixtures.
  }
}
void expect_satisfied(const Sketch& s) {
  for (const auto& [id, c] : s.constraints())
    EXPECT_LE(std::abs(residual(s, c)), kLengthTolerance) << "constraint " << id;
}
void expect_at(const Sketch& s, EntityId point, Position p, double tol = kNear) {
  const Position q = at(s, point);
  EXPECT_NEAR(q.x, p.x, tol) << "point " << point;
  EXPECT_NEAR(q.y, p.y, tol) << "point " << point;
}
EntityId line(Sketch& s, Position a, Position b) {
  return *s.create_line(*s.create_point(a), *s.create_point(b));
}
Polyline boxed(Sketch& s, Position origin, double w, double h) {
  const Polyline r = *s.create_rectangle(origin, w, h);
  EXPECT_TRUE(s.add_constraint(K::kHorizontal, r.lines[0]));
  EXPECT_TRUE(s.add_constraint(K::kVertical, r.lines[1]));
  EXPECT_TRUE(s.add_constraint(K::kHorizontal, r.lines[2]));
  EXPECT_TRUE(s.add_constraint(K::kVertical, r.lines[3]));
  return r;
}
}  // namespace

TEST(Drag, FreePointMovesExactlyToTarget) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 0});
  const EntityId other = line(s, {50, 50}, {60, 50});
  const auto [a, b] = ends(s, l);
  const Sketch before = s;
  EXPECT_EQ(drag_point(s, a, {3, 4}).status, SolveStatus::kSolved);
  EXPECT_EQ(at(s, a).x, 3);
  EXPECT_EQ(at(s, a).y, 4);
  expect_at(s, b, {10, 0}, 0);
  EXPECT_EQ(s.entity(other), before.entity(other));
}

TEST(Drag, HorizontalLineFollowsVerticallyOnly) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 0});
  ASSERT_TRUE(s.add_constraint(K::kHorizontal, l));
  const auto [a, b] = ends(s, l);
  ASSERT_EQ(drag_point(s, a, {0, 6}).status, SolveStatus::kSolved);
  expect_satisfied(s);
  expect_at(s, a, {0, 6});
  expect_at(s, b, {10, 6});

  ASSERT_EQ(drag_point(s, a, {4, 6}).status, SolveStatus::kSolved);
  expect_at(s, a, {4, 6});
  expect_at(s, b, {10, 6});
}

TEST(Drag, LengthDimensionIsKept) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {40, 0});
  ASSERT_TRUE(s.add_dimension(K::kLength, l, 0, 40));
  const auto [a, b] = ends(s, l);
  ASSERT_EQ(drag_point(s, b, {30, 30}).status, SolveStatus::kSolved);
  expect_satisfied(s);
  EXPECT_NEAR(distance(at(s, a), at(s, b)), 40, kLengthTolerance);
  EXPECT_GT(at(s, b).y, 1);  // Moved towards the target, not stuck.
}

TEST(Drag, FixedPointDoesNotMove) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 0});
  const auto [a, b] = ends(s, l);
  ASSERT_TRUE(s.add_constraint(K::kFix, a));
  ASSERT_TRUE(s.add_constraint(K::kHorizontal, l));
  EXPECT_EQ(drag_point(s, a, {5, 5}).status, SolveStatus::kSolved);
  expect_at(s, a, {0, 0}, kLengthTolerance);
  expect_at(s, b, {10, 0}, kNear);
  expect_satisfied(s);
}

TEST(Drag, PivotKeepsFreeEndOnCircleTowardsTarget) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {30, 0});
  const auto [a, b] = ends(s, l);
  ASSERT_TRUE(s.add_constraint(K::kFix, a));
  ASSERT_TRUE(s.add_dimension(K::kLength, l, 0, 30));
  ASSERT_EQ(drag_point(s, b, {0, 100}).status, SolveStatus::kSolved);
  expect_satisfied(s);
  expect_at(s, b, {0, 30});
}

TEST(Drag, RectangleResizesAroundFixedCorner) {
  Sketch s;
  const Polyline r = boxed(s, {0, 0}, 40, 20);
  ASSERT_TRUE(s.add_constraint(K::kFix, r.points[0]));
  ASSERT_EQ(drag_point(s, r.points[2], {50, 30}).status, SolveStatus::kSolved);
  expect_satisfied(s);
  expect_at(s, r.points[0], {0, 0}, kLengthTolerance);
  expect_at(s, r.points[2], {50, 30});
  expect_at(s, r.points[1], {50, 0});
  expect_at(s, r.points[3], {0, 30});
}

TEST(Drag, InvalidInputLeavesSketchUnchanged) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 0});
  ASSERT_TRUE(s.add_constraint(K::kHorizontal, l));
  const auto [a, b] = ends(s, l);
  const Sketch before = s;
  EXPECT_EQ(drag_point(s, 999, {1, 1}).status, SolveStatus::kInvalidInput);
  EXPECT_EQ(drag_point(s, l, {1, 1}).status, SolveStatus::kInvalidInput);
  EXPECT_EQ(drag_point(s, a, {NAN, 1}).status, SolveStatus::kInvalidInput);
  EXPECT_EQ(drag_point(s, a, {INFINITY, 1}).status, SolveStatus::kInvalidInput);
  EXPECT_TRUE(s == before);
}

TEST(Drag, CollapseIsRejected) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 0});
  ASSERT_TRUE(s.add_constraint(K::kHorizontal, l));
  const auto [a, b] = ends(s, l);
  const Sketch before = s;
  const SolveStatus status = drag_point(s, a, {10, 0}).status;
  EXPECT_TRUE(status == SolveStatus::kInvalidInput ||
              status == SolveStatus::kDegenerate)
      << static_cast<int>(status);
  EXPECT_TRUE(s == before);
}

TEST(Drag, UnconstrainedGeometryIsUntouched) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 0});
  ASSERT_TRUE(s.add_constraint(K::kHorizontal, l));
  const EntityId free = line(s, {0, 20}, {10, 25});
  const EntityId circle = *s.create_circle(*s.create_point({30, 30}), 5);
  const Sketch before = s;
  ASSERT_EQ(drag_point(s, ends(s, l).first, {2, 3}).status, SolveStatus::kSolved);
  EXPECT_EQ(s.entity(free), before.entity(free));
  EXPECT_EQ(at(s, ends(s, free).first), at(before, ends(before, free).first));
  EXPECT_EQ(at(s, ends(s, free).second), at(before, ends(before, free).second));
  EXPECT_EQ(s.entity(circle), before.entity(circle));
}

TEST(Drag, CostOfTwentyRectanglesIsRecorded) {
  Sketch s;
  std::vector<Polyline> boxes;
  for (int i = 0; i < 20; ++i) {
    boxes.push_back(boxed(s, {i * 50.0, 0}, 40, 20));
    ASSERT_TRUE(s.add_constraint(K::kFix, boxes.back().points[0]));
  }
  const EntityId corner = boxes[10].points[2];
  const auto start = std::chrono::steady_clock::now();
  for (int step = 1; step <= 20; ++step)
    ASSERT_EQ(drag_point(s, corner, {540 + step * 0.5, 20 + step * 0.5}).status,
              SolveStatus::kSolved);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  ::testing::Test::RecordProperty(
      "drag_step_20_rectangles_us",
      static_cast<int>(
          std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() /
          20));
  expect_satisfied(s);
  expect_at(s, corner, {550, 30});
}

// U03b (constrained-drag.adoc#curves).
TEST(DragPoints, BothEndsOfALineTranslateKeepingItsLength) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 0});
  ASSERT_TRUE(s.add_dimension(K::kLength, l, 0, 10));
  const auto [a, b] = ends(s, l);
  const SolveResult r = drag_points(s, {{a, {5, 3}}, {b, {15, 3}}});
  ASSERT_EQ(r.status, SolveStatus::kSolved);
  expect_at(s, a, {5, 3});
  expect_at(s, b, {15, 3});
  expect_satisfied(s);
}

TEST(DragPoints, InvalidInputLeavesSketchUnchanged) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 0});
  ASSERT_TRUE(s.add_constraint(K::kHorizontal, l));
  const auto [a, b] = ends(s, l);
  const Sketch before = s;
  EXPECT_EQ(drag_points(s, {}).status, SolveStatus::kInvalidInput);
  EXPECT_EQ(drag_points(s, {{l, {1, 1}}}).status, SolveStatus::kInvalidInput);
  EXPECT_EQ(drag_points(s, {{a, {1, 1}}, {b, {NAN, 0}}}).status,
            SolveStatus::kInvalidInput);
  EXPECT_TRUE(s == before);
}

TEST(DragPoints, UnconstrainedPointsMoveExactly) {
  Sketch s;
  const EntityId a = *s.create_point({0, 0}), b = *s.create_point({4, 4});
  ASSERT_EQ(drag_points(s, {{a, {1, 2}}, {b, {5, 6}}}).status,
            SolveStatus::kSolved);
  EXPECT_EQ(at(s, a), (Position{1, 2}));
  EXPECT_EQ(at(s, b), (Position{5, 6}));
}
