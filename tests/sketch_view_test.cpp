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
