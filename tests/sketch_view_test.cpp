#include "sketchcad/sketch_view.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>
using namespace sketchcad;
namespace {
void expect_near(Position a, Position b) {
  EXPECT_NEAR(a.x, b.x, 1e-9);
  EXPECT_NEAR(a.y, b.y, 1e-9);
}
EntityId point(Sketch& s, Position p) { return s.create_point(p).value(); }
}  // namespace
TEST(ViewTransform, MapsMillimetresToPixelsWithYUp) {
  ViewTransform v;
  EXPECT_EQ(v.to_screen({0, 0}), (ScreenPoint{0, 0}));
  EXPECT_EQ(v.to_screen({1, 1}), (ScreenPoint{4, -4}));
  v.pan(10, 20);
  v.zoom_at({50, 50}, 2.5);
  expect_near(v.to_world(v.to_screen({3, -7})), {3, -7});
}
TEST(ViewTransform, ZoomKeepsAnchorAndClampsScale) {
  ViewTransform v;
  v.pan(300, 200);
  const Position anchored = v.to_world({100, 80});
  v.zoom_at({100, 80}, 3);
  expect_near(v.to_world({100, 80}), anchored);
  EXPECT_DOUBLE_EQ(v.scale(), 12);
  v.zoom_at({0, 0}, 1e12);
  EXPECT_DOUBLE_EQ(v.scale(), ViewTransform::kMaxScale);
  v.zoom_at({0, 0}, 1e-12);
  EXPECT_DOUBLE_EQ(v.scale(), ViewTransform::kMinScale);
  for (double bad : {0.0, -2.0, std::numeric_limits<double>::quiet_NaN()}) {
    v.zoom_at({0, 0}, bad);
    EXPECT_DOUBLE_EQ(v.scale(), ViewTransform::kMinScale);
  }
}
TEST(ViewTransform, FitFramesBoundsOrDefaultArea) {
  ViewTransform v;
  v.fit(Bounds{{0, 0}, {80, 50}}, 800, 600);
  const ScreenPoint lo = v.to_screen({0, 0}), hi = v.to_screen({80, 50});
  EXPECT_NEAR(lo.x, 32, 1e-9);
  EXPECT_NEAR(hi.x, 768, 1e-9);
  EXPECT_NEAR((lo.y + hi.y) / 2, 300, 1e-9);
  EXPECT_GE(hi.y, 32);
  v.fit(std::nullopt, 800, 600);
  const ScreenPoint a = v.to_screen({0, 0}), b = v.to_screen({100, 100});
  EXPECT_NEAR((a.x + b.x) / 2, 400, 1e-9);
  EXPECT_NEAR(a.y - b.y, 536, 1e-9);
}
TEST(SketchBounds, CoverPointsAndFullCircles) {
  Sketch s;
  EXPECT_FALSE(bounds(s));
  ASSERT_TRUE(s.create_rectangle({0, 0}, 50, 30));
  ASSERT_TRUE(s.create_circle(point(s, {100, 0}), 10));
  EXPECT_EQ(bounds(s), (Bounds{{0, -10}, {110, 30}}));
}
TEST(Pick, PointWinsOverLineAndNearestCurveWins) {
  Sketch s;
  const EntityId a = point(s, {0, 0}), b = point(s, {10, 0});
  const EntityId low = s.create_line(a, b).value();
  const EntityId c = point(s, {0, 2}), d = point(s, {10, 2});
  const EntityId high = s.create_line(c, d).value();
  EXPECT_EQ(pick(s, {0.5, 0}, 1), a);
  EXPECT_EQ(pick(s, {5, 0.6}, 1), low);
  EXPECT_EQ(pick(s, {5, 1.5}, 1), high);
  EXPECT_FALSE(pick(s, {5, 5}, 1));
}
TEST(Pick, CirclesUseCircumferenceAndArcsTheirSpan) {
  Sketch s;
  const EntityId center = point(s, {0, 0});
  const EntityId circle = s.create_circle(center, 10).value();
  EXPECT_EQ(pick(s, {10.5, 0}, 1), circle);
  EXPECT_EQ(pick(s, {0.4, 0}, 1), center);
  EXPECT_FALSE(pick(s, {5, 0}, 1));
  Sketch t;
  const EntityId c2 = point(t, {0, 0});
  const EntityId arc =
      t.create_arc(c2, 10, 0, std::numbers::pi / 2).value();
  const double r = 10 / std::sqrt(2.0);
  EXPECT_EQ(pick(t, {r, r}, 1), arc);
  EXPECT_FALSE(pick(t, {0, -10}, 1));
  EXPECT_EQ(pick(t, {10.5, 0.3}, 1), arc);
}
TEST(Snap, DisabledGridAndPointSnapping) {
  Sketch s;
  const EntityId p = point(s, {3.1, 4.2});
  SnapSettings off{false, 0.5, 0.5};
  EXPECT_EQ(snap(s, {1.26, -2.74}, off),
            (SnapResult{{1.26, -2.74}, SnapKind::kNone, std::nullopt}));
  SnapSettings on{true, 0.5, 0.5};
  EXPECT_EQ(snap(s, {1.26, -2.74}, on),
            (SnapResult{{1.5, -2.5}, SnapKind::kGrid, std::nullopt}));
  EXPECT_EQ(snap(s, {3.4, 4.0}, on),
            (SnapResult{{3.1, 4.2}, SnapKind::kPoint, p}));
  EXPECT_EQ(snap(s, {3.4, 4.0}, on, p).kind, SnapKind::kGrid);
}
TEST(Snap, GridStepFollowsOneTwoFiveSeries) {
  EXPECT_DOUBLE_EQ(grid_step_for_scale(4), 2);
  EXPECT_DOUBLE_EQ(grid_step_for_scale(1), 10);
  EXPECT_NEAR(grid_step_for_scale(100), 0.1, 1e-12);
  EXPECT_DOUBLE_EQ(grid_step_for_scale(0.05), 200);
}

// U04 (sketch-editing.adoc): intersection and curve snaps.
namespace {
EntityId segment(Sketch& s, Position a, Position b) {
  return s.create_line(point(s, a), point(s, b)).value();
}
const SnapSettings kOn{true, 1, 0.5};
}  // namespace
TEST(Snap, PointBeatsIntersectionBeatsCurveBeatsGrid) {
  Sketch s;
  const EntityId l1 = segment(s, {0, 0}, {10, 10});
  const EntityId l2 = segment(s, {0, 10}, {10, 0});
  SnapResult r = snap(s, {5.2, 4.9}, kOn);
  EXPECT_EQ(r.kind, SnapKind::kIntersection);
  expect_near(r.position, {5, 5});
  EXPECT_EQ(r.curves, (std::vector<EntityId>{l1, l2}));
  EXPECT_FALSE(r.point);

  r = snap(s, {2.5, 2.2}, kOn);
  EXPECT_EQ(r.kind, SnapKind::kOnCurve);
  expect_near(r.position, {2.35, 2.35});
  EXPECT_EQ(r.curves, (std::vector<EntityId>{l1}));

  r = snap(s, {8.4, 6.6}, kOn);
  EXPECT_EQ(r.kind, SnapKind::kGrid);
  EXPECT_TRUE(r.curves.empty());

  const EntityId near_crossing = point(s, {5.3, 5});
  r = snap(s, {5.2, 4.9}, kOn);
  EXPECT_EQ(r.kind, SnapKind::kPoint);
  EXPECT_EQ(r.point, near_crossing);
  EXPECT_TRUE(r.curves.empty());
}
TEST(Snap, LineCircleAndCircleCircleIntersections) {
  Sketch s;
  const EntityId circle = s.create_circle(point(s, {0, 0}), 5).value();
  const EntityId l = segment(s, {-10, 3}, {10, 3});
  SnapResult r = snap(s, {4.2, 3.1}, kOn);
  EXPECT_EQ(r.kind, SnapKind::kIntersection);
  expect_near(r.position, {4, 3});
  EXPECT_EQ(r.curves, (std::vector<EntityId>{circle, l}));

  Sketch t;
  const EntityId c1 = t.create_circle(point(t, {0, 0}), 5).value();
  const EntityId c2 = t.create_circle(point(t, {8, 0}), 5).value();
  r = snap(t, {4.2, -2.9}, kOn);
  EXPECT_EQ(r.kind, SnapKind::kIntersection);
  expect_near(r.position, {4, -3});
  EXPECT_EQ(r.curves, (std::vector<EntityId>{c1, c2}));
}
TEST(Snap, IntersectionsStayWithinArcSweepsAndSegments) {
  Sketch s;
  s.create_arc(point(s, {0, 0}), 5, 0, std::numbers::pi / 2).value();
  const EntityId l = segment(s, {-10, -3}, {10, -3});
  // (4, -3) is on the arc's full circle but outside its sweep.
  SnapResult r = snap(s, {4.1, -3.1}, kOn);
  EXPECT_EQ(r.kind, SnapKind::kOnCurve);
  EXPECT_EQ(r.curves, (std::vector<EntityId>{l}));
  expect_near(r.position, {4.1, -3});

  Sketch t;
  segment(t, {0, 0}, {10, 0});
  const EntityId vertical = segment(t, {12, -5}, {12, 5});
  // The infinite lines cross at (12, 0), beyond the first segment.
  r = snap(t, {12.1, 0.2}, kOn);
  EXPECT_EQ(r.kind, SnapKind::kOnCurve);
  EXPECT_EQ(r.curves, (std::vector<EntityId>{vertical}));
  expect_near(r.position, {12, 0.2});
}
TEST(Snap, NearestPointOnACircleAndOnAnArc) {
  Sketch s;
  const EntityId circle = s.create_circle(point(s, {0, 0}), 10).value();
  SnapResult r = snap(s, {0.2, 10.3}, kOn);
  EXPECT_EQ(r.kind, SnapKind::kOnCurve);
  EXPECT_EQ(r.curves, (std::vector<EntityId>{circle}));
  EXPECT_NEAR(std::hypot(r.position.x, r.position.y), 10, 1e-9);

  Sketch t;
  const EntityId arc =
      t.create_arc(point(t, {0, 0}), 10, 0, std::numbers::pi / 2).value();
  r = snap(t, {7.2, 7.0}, kOn);
  EXPECT_EQ(r.kind, SnapKind::kOnCurve);
  EXPECT_EQ(r.curves, (std::vector<EntityId>{arc}));
  // Outside the sweep the arc does not attract.
  EXPECT_EQ(snap(t, {-7.2, -7.0}, kOn).kind, SnapKind::kGrid);
}
