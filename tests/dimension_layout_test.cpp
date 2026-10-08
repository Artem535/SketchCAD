#include "sketchcad/dimension_layout.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
using namespace sketchcad;
using K = ConstraintKind;
namespace {
constexpr double kPi = std::numbers::pi;
constexpr double kEps = 1e-6;

// Default view: 4 px/mm, world origin at screen origin, Y flipped.
bool same(ScreenPoint a, ScreenPoint b, double tol = kEps) {
  return std::abs(a.x - b.x) <= tol && std::abs(a.y - b.y) <= tol;
}
double distance(ScreenPoint a, ScreenPoint b) {
  return std::hypot(b.x - a.x, b.y - a.y);
}
bool has_segment(const DimensionGraphic& g, ScreenPoint a, ScreenPoint b) {
  for (const auto& [p, q] : g.segments)
    if ((same(p, a) && same(q, b)) || (same(p, b) && same(q, a))) return true;
  return false;
}
// Arrow with its tip at `tip`; `outward` is the direction the tip points.
bool has_arrow(const DimensionGraphic& g, ScreenPoint tip, ScreenPoint outward) {
  for (const auto& a : g.arrows) {
    if (!same(a[0], tip)) continue;
    const ScreenPoint base{(a[1].x + a[2].x) / 2, (a[1].y + a[2].y) / 2};
    const double dx = tip.x - base.x, dy = tip.y - base.y;
    const double len = std::hypot(dx, dy);
    if (std::abs(len - 12) > 1e-6) continue;
    if (std::abs(dx / len - outward.x) < 1e-6 &&
        std::abs(dy / len - outward.y) < 1e-6)
      return true;
  }
  return false;
}
EntityId line(Sketch& s, Position a, Position b) {
  const EntityId p = *s.create_point(a);
  const EntityId q = *s.create_point(b);
  return *s.create_line(p, q);
}
// The single graphic, or an empty one (failing the test) when missing.
DimensionGraphic only(const std::vector<DimensionGraphic>& v) {
  EXPECT_EQ(v.size(), 1u);
  return v.empty() ? DimensionGraphic{} : v.front();
}
}  // namespace

TEST(DimensionLayout, LengthOfALoneHorizontalLineGoesUp) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {50, 0});
  const EntityId id = *s.add_dimension(K::kLength, l, 0, 50);
  const DimensionGraphic g = only(layout_dimensions(s, ViewTransform{}));
  EXPECT_EQ(g.id, id);
  EXPECT_EQ(g.kind, K::kLength);
  EXPECT_DOUBLE_EQ(g.value, 50);
  EXPECT_TRUE(has_segment(g, {0, -32}, {200, -32}));
  EXPECT_TRUE(has_segment(g, {0, 0}, {0, -38}));
  EXPECT_TRUE(has_segment(g, {200, 0}, {200, -38}));
  EXPECT_EQ(g.segments.size(), 3u);
  EXPECT_EQ(g.arrows.size(), 2u);
  EXPECT_TRUE(has_arrow(g, {0, -32}, {-1, 0}));
  EXPECT_TRUE(has_arrow(g, {200, -32}, {1, 0}));
  EXPECT_TRUE(same(g.text_position, {100, -36}));
  EXPECT_NEAR(g.text_angle, 0, kEps);
}

TEST(DimensionLayout, RectangleDimensionsGoOutsideAndVerticalTextReadsUp) {
  Sketch s;
  const Polyline r = *s.create_rectangle({0, 0}, 40, 20);
  ASSERT_TRUE(s.add_dimension(K::kLength, r.lines[0], 0, 40));  // Bottom.
  ASSERT_TRUE(s.add_dimension(K::kLength, r.lines[1], 0, 20));  // Right.
  const auto all = layout_dimensions(s, ViewTransform{});
  ASSERT_EQ(all.size(), 2u);
  const DimensionGraphic& bottom = all[0];
  const DimensionGraphic& right = all[1];
  // Screen: the rectangle spans x 0..160 and y -80..0.
  EXPECT_TRUE(has_segment(bottom, {0, 32}, {160, 32}));
  EXPECT_NEAR(bottom.text_angle, 0, kEps);
  // Text sits above the dimension line in its own frame (ESKD).
  EXPECT_TRUE(same(bottom.text_position, {80, 28}));
  EXPECT_TRUE(has_segment(right, {192, 0}, {192, -80}));
  EXPECT_NEAR(right.text_angle, -kPi / 2, kEps);
  EXPECT_TRUE(same(right.text_position, {188, -40}));
}

TEST(DimensionLayout, ShortDimensionPutsArrowsOutside) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {2, 0});  // 8 px.
  ASSERT_TRUE(s.add_dimension(K::kLength, l, 0, 2));
  const DimensionGraphic g = only(layout_dimensions(s, ViewTransform{}));
  EXPECT_TRUE(has_arrow(g, {0, -32}, {1, 0}));
  EXPECT_TRUE(has_arrow(g, {8, -32}, {-1, 0}));
  EXPECT_TRUE(has_segment(g, {-22, -32}, {30, -32}));
}

TEST(DimensionLayout, RadiusLeaderFromTheCentre) {
  Sketch s;
  const EntityId c = *s.create_circle(*s.create_point({0, 0}), 10);  // 40 px.
  ASSERT_TRUE(s.add_dimension(K::kRadius, c, 0, 10));
  const DimensionGraphic g = only(layout_dimensions(s, ViewTransform{}));
  const double h = std::numbers::sqrt2 / 2;
  const ScreenPoint on_circle{40 * h, -40 * h};
  EXPECT_TRUE(has_segment(g, {0, 0}, on_circle));
  ASSERT_EQ(g.arrows.size(), 1u);
  EXPECT_TRUE(has_arrow(g, on_circle, {h, -h}));
  EXPECT_GE(g.text_angle, -kPi / 2);
  EXPECT_LT(g.text_angle, kPi / 2);
}

TEST(DimensionLayout, SmallRadiusLeaderGoesOutside) {
  Sketch s;
  const EntityId c = *s.create_circle(*s.create_point({0, 0}), 4);  // 16 px.
  ASSERT_TRUE(s.add_dimension(K::kRadius, c, 0, 4));
  const DimensionGraphic g = only(layout_dimensions(s, ViewTransform{}));
  const double h = std::numbers::sqrt2 / 2;
  const ScreenPoint on_circle{16 * h, -16 * h};
  EXPECT_TRUE(has_segment(g, on_circle, {52 * h, -52 * h}));
  EXPECT_TRUE(has_arrow(g, on_circle, {-h, h}));
}

TEST(DimensionLayout, RightAngleArcAtTheCorner) {
  Sketch s;
  const EntityId corner = *s.create_point({0, 0});
  const EntityId a = *s.create_line(corner, *s.create_point({30, 0}));
  const EntityId b = *s.create_line(corner, *s.create_point({0, 20}));
  ASSERT_TRUE(s.add_dimension(K::kAngle, a, b, kPi / 2));
  const DimensionGraphic g = only(layout_dimensions(s, ViewTransform{}));
  ASSERT_EQ(g.arcs.size(), 1u);
  EXPECT_TRUE(same(g.arcs[0].center, {0, 0}));
  EXPECT_NEAR(std::abs(g.arcs[0].sweep), kPi / 2, kEps);
  EXPECT_GE(g.arcs[0].radius, 32);
  EXPECT_LE(g.arcs[0].radius, 120);
  ASSERT_EQ(g.arrows.size(), 2u);
  for (const auto& arrow : g.arrows)
    EXPECT_NEAR(distance(arrow[0], {0, 0}), g.arcs[0].radius, kEps);
}

TEST(DimensionLayout, PointLineDistanceIsPerpendicularWithExtension) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 0});
  const EntityId p = *s.create_point({20, 5});
  ASSERT_TRUE(s.add_dimension(K::kDistance, p, l, 5));
  const DimensionGraphic g = only(layout_dimensions(s, ViewTransform{}));
  EXPECT_TRUE(has_segment(g, {80, -20}, {80, 0}) ||
              [&] {  // Short: the dimension line runs past both ends.
                for (const auto& [a, b] : g.segments)
                  if (std::abs(a.x - 80) < kEps && std::abs(b.x - 80) < kEps)
                    return true;
                return false;
              }());
  EXPECT_TRUE(has_segment(g, {40, 0}, {86, 0}));
  EXPECT_EQ(g.arrows.size(), 2u);
}

TEST(DimensionLayout, OnlyDimensionsAndStableAngles) {
  Sketch s;
  const Polyline r = *s.create_rectangle({0, 0}, 40, 20);
  ASSERT_TRUE(s.add_constraint(K::kHorizontal, r.lines[0]));
  ASSERT_TRUE(s.add_dimension(K::kLength, r.lines[0], 0, 40));
  ASSERT_TRUE(s.add_dimension(K::kLength, r.lines[1], 0, 20));
  ASSERT_TRUE(s.add_dimension(K::kLength, r.lines[2], 0, 40));
  ASSERT_TRUE(s.add_dimension(K::kLength, r.lines[3], 0, 20));
  ASSERT_TRUE(s.add_dimension(K::kAngle, r.lines[0], r.lines[1], kPi / 2));
  const auto first = layout_dimensions(s, ViewTransform{});
  ASSERT_EQ(first.size(), 5u);
  for (const auto& g : first) {
    EXPECT_GE(g.text_angle, -kPi / 2) << g.id;
    EXPECT_LT(g.text_angle, kPi / 2) << g.id;
  }
  const auto second = layout_dimensions(s, ViewTransform{});
  ASSERT_EQ(second.size(), first.size());
  for (std::size_t i = 0; i < first.size(); ++i) {
    EXPECT_EQ(first[i].id, second[i].id);
    EXPECT_TRUE(same(first[i].text_position, second[i].text_position, 0));
    EXPECT_EQ(first[i].segments.size(), second[i].segments.size());
  }
}
