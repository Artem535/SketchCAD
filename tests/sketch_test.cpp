#include "sketchcad/sketch.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>
using namespace sketchcad;
namespace {
template <typename T>
T read(const Sketch& s, EntityId id) {
  return std::get<T>(s.entity(id).value());
}
constexpr double kPi = std::numbers::pi;
const std::array<double, 3> kNonFinite{
    std::numeric_limits<double>::infinity(),
    -std::numeric_limits<double>::infinity(),
    std::numeric_limits<double>::quiet_NaN()};
}  // namespace
TEST(Sketch, PointCrudAndConstructionPreserveIdentity) {
  Sketch s;
  auto p = s.create_point({1, 2}, true);
  ASSERT_TRUE(p);
  EXPECT_NE(*p, 0);
  EXPECT_EQ(read<SketchPoint>(s, *p).position, (Position{1, 2}));
  EXPECT_TRUE(read<SketchPoint>(s, *p).construction);
  ASSERT_TRUE(s.update_point(*p, {3, 4}));
  EXPECT_EQ(read<SketchPoint>(s, *p).id, *p);
  EXPECT_EQ(read<SketchPoint>(s, *p).position, (Position{3, 4}));
  EXPECT_TRUE(read<SketchPoint>(s, *p).construction);
  ASSERT_TRUE(s.set_construction(*p, false));
  EXPECT_FALSE(read<SketchPoint>(s, *p).construction);
  EXPECT_FALSE(s.entity(0));
  EXPECT_FALSE(s.entity(999));
  ASSERT_TRUE(s.erase(*p));
  EXPECT_FALSE(s.entity(*p));
  EXPECT_FALSE(s.erase(*p));
}
TEST(Sketch, SharedEndpointsAndLineCrud) {
  Sketch s;
  auto a = s.create_point({0, 0}), b = s.create_point({1, 0}),
       c = s.create_point({1, 1});
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  ASSERT_TRUE(c);
  auto ab = s.create_line(*a, *b, true), bc = s.create_line(*b, *c);
  ASSERT_TRUE(ab);
  ASSERT_TRUE(bc);
  ASSERT_TRUE(s.update_point(*b, {5, 6}));
  EXPECT_EQ(read<SketchLine>(s, *ab).end, *b);
  EXPECT_EQ(read<SketchLine>(s, *bc).start, *b);
  EXPECT_EQ(read<SketchPoint>(s, *b).position, (Position{5, 6}));
  ASSERT_TRUE(s.update_line(*ab, *a, *c));
  EXPECT_EQ(read<SketchLine>(s, *ab).end, *c);
  EXPECT_TRUE(read<SketchLine>(s, *ab).construction);
  ASSERT_TRUE(s.set_construction(*ab, false));
  EXPECT_FALSE(read<SketchLine>(s, *ab).construction);
  EXPECT_FALSE(s.erase(*c));
  ASSERT_TRUE(s.erase(*ab));
  ASSERT_TRUE(s.erase(*bc));
  EXPECT_TRUE(s.erase(*c));
}
TEST(Sketch, CircleAndArcCrudShareCenter) {
  Sketch s;
  auto center = s.create_point({0, 0}), other = s.create_point({2, 3});
  ASSERT_TRUE(center);
  ASSERT_TRUE(other);
  auto circle = s.create_circle(*center, 5, true);
  auto arc = s.create_arc(*center, 3, 0, kPi / 2, true);
  ASSERT_TRUE(circle);
  ASSERT_TRUE(arc);
  EXPECT_FALSE(s.erase(*center));
  ASSERT_TRUE(s.update_point(*center, {4, 5}));
  EXPECT_EQ(read<SketchCircle>(s, *circle).center, *center);
  EXPECT_EQ(read<SketchArc>(s, *arc).center, *center);
  ASSERT_TRUE(s.update_circle(*circle, *other, 7));
  ASSERT_TRUE(s.update_arc(*arc, *other, 8, 1, -kPi));
  EXPECT_EQ(read<SketchCircle>(s, *circle).radius, 7);
  EXPECT_EQ(read<SketchArc>(s, *arc).sweep_angle, -kPi);
  EXPECT_TRUE(read<SketchCircle>(s, *circle).construction);
  EXPECT_TRUE(read<SketchArc>(s, *arc).construction);
  ASSERT_TRUE(s.set_construction(*circle, false));
  ASSERT_TRUE(s.set_construction(*arc, false));
  EXPECT_TRUE(s.erase(*center));
  EXPECT_FALSE(s.erase(*other));
  ASSERT_TRUE(s.erase(*circle));
  EXPECT_FALSE(s.erase(*other));
  ASSERT_TRUE(s.erase(*arc));
  EXPECT_TRUE(s.erase(*other));
}
TEST(Sketch, NonFinitePositionsAreRejectedWithoutChangingStateOrAllocator) {
  Sketch s;
  auto p = s.create_point({0, 0});
  ASSERT_TRUE(p);
  auto before = s.entities();
  for (double bad : kNonFinite) {
    EXPECT_FALSE(s.create_point({bad, 0}));
    EXPECT_FALSE(s.create_point({0, bad}));
    EXPECT_FALSE(s.update_point(*p, {bad, 0}));
    EXPECT_FALSE(s.update_point(*p, {0, bad}));
    EXPECT_EQ(s.entities(), before);
  }
  auto next = s.create_point({1, 1});
  ASSERT_TRUE(next);
  EXPECT_EQ(*next, *p + 1);
}
TEST(Sketch, InvalidReferencesAndWrongTypesAreAtomic) {
  Sketch s;
  auto p = s.create_point({0, 0}), q = s.create_point({1, 1});
  ASSERT_TRUE(p);
  ASSERT_TRUE(q);
  auto line = s.create_line(*p, *q);
  auto circle = s.create_circle(*p, 1);
  auto arc = s.create_arc(*p, 1, 0, 1);
  ASSERT_TRUE(line);
  ASSERT_TRUE(circle);
  ASSERT_TRUE(arc);
  auto before = s.entities();
  EXPECT_FALSE(s.create_line(*p, *p));
  EXPECT_FALSE(s.create_line(*p, 999));
  EXPECT_FALSE(s.create_line(*line, *q));
  EXPECT_FALSE(s.create_circle(*line, 2));
  EXPECT_FALSE(s.create_arc(0, 1, 0, 1));
  EXPECT_FALSE(s.update_line(*line, *p, *circle));
  EXPECT_FALSE(s.update_line(*line, *p, *p));
  EXPECT_FALSE(s.update_circle(*circle, *line, 2));
  EXPECT_FALSE(s.update_arc(*arc, 999, 2, 0, 1));
  EXPECT_FALSE(s.update_point(*line, {1, 2}));
  EXPECT_FALSE(s.update_line(*p, *p, *q));
  EXPECT_FALSE(s.update_circle(*arc, *p, 1));
  EXPECT_FALSE(s.update_arc(*circle, *p, 1, 0, 1));
  EXPECT_FALSE(s.update_point(999, {1, 2}));
  EXPECT_FALSE(s.update_line(999, *p, *q));
  EXPECT_FALSE(s.update_circle(999, *p, 1));
  EXPECT_FALSE(s.update_arc(999, *p, 1, 0, 1));
  EXPECT_FALSE(s.set_construction(999, true));
  EXPECT_FALSE(s.erase(999));
  EXPECT_EQ(s.entities(), before);
  auto next = s.create_point({2, 2});
  ASSERT_TRUE(next);
  EXPECT_EQ(*next, *arc + 1);
}
TEST(Sketch, InvalidRadiusAndSweepRejectCreateAndUpdate) {
  Sketch s;
  auto p = s.create_point({0, 0});
  ASSERT_TRUE(p);
  auto c = s.create_circle(*p, 1);
  auto a = s.create_arc(*p, 1, 0, 1);
  ASSERT_TRUE(c);
  ASSERT_TRUE(a);
  auto before = s.entities();
  const std::array<double, 5> bad_radius{0, -1, kNonFinite[0], kNonFinite[1],
                                         kNonFinite[2]};
  for (double bad : bad_radius) {
    EXPECT_FALSE(s.create_circle(*p, bad));
    EXPECT_FALSE(s.create_arc(*p, bad, 0, 1));
    EXPECT_FALSE(s.update_circle(*c, *p, bad));
    EXPECT_FALSE(s.update_arc(*a, *p, bad, 0, 1));
  }
  const std::array<double, 7> bad_sweep{0,
                                        2 * kPi,
                                        -2 * kPi,
                                        3 * kPi,
                                        kNonFinite[0],
                                        kNonFinite[1],
                                        kNonFinite[2]};
  for (double bad : bad_sweep) {
    EXPECT_FALSE(s.create_arc(*p, 1, 0, bad));
    EXPECT_FALSE(s.update_arc(*a, *p, 1, 0, bad));
  }
  for (double bad : kNonFinite) {
    EXPECT_FALSE(s.create_arc(*p, 1, bad, 1));
    EXPECT_FALSE(s.update_arc(*a, *p, 1, bad, 1));
  }
  EXPECT_EQ(s.entities(), before);
}
TEST(Sketch, ErasedIdsAndSnapshotAssignmentAreNotReused) {
  Sketch s;
  auto p = s.create_point({0, 0});
  ASSERT_TRUE(p);
  Sketch snapshot = s;
  auto q = s.create_point({1, 1});
  ASSERT_TRUE(q);
  ASSERT_TRUE(s.erase(*q));
  auto r = s.create_point({2, 2});
  ASSERT_TRUE(r);
  EXPECT_GT(*r, *q);
  s = snapshot;
  EXPECT_FALSE(s.entity(*r));
  auto next = s.create_point({3, 3});
  ASSERT_TRUE(next);
  EXPECT_GT(*next, *r);
  Sketch old = snapshot;
  s = std::move(old);
  auto after_move = s.create_point({4, 4});
  ASSERT_TRUE(after_move);
  EXPECT_GT(*after_move, *next);
  s = s;
  auto after_self = s.create_point({5, 5});
  ASSERT_TRUE(after_self);
  EXPECT_GT(*after_self, *after_move);
  s = std::move(s);
  EXPECT_TRUE(s.entity(*after_self));
}
TEST(Sketch, ValueCopiesAndQueryValuesAreIndependent) {
  Sketch s;
  auto p = s.create_point({0, 0});
  ASSERT_TRUE(p);
  Sketch copy = s;
  ASSERT_TRUE(copy.update_point(*p, {7, 8}));
  EXPECT_EQ(read<SketchPoint>(s, *p).position, (Position{0, 0}));
  auto entity = s.entity(*p);
  ASSERT_TRUE(entity);
  std::get<SketchPoint>(*entity).position = {9, 9};
  EXPECT_EQ(read<SketchPoint>(s, *p).position, (Position{0, 0}));
  Sketch assigned;
  assigned = s;
  ASSERT_TRUE(assigned.erase(*p));
  EXPECT_TRUE(s.entity(*p));
  Sketch moved = std::move(copy);
  EXPECT_EQ(read<SketchPoint>(moved, *p).position, (Position{7, 8}));
}
TEST(Sketch, OpenAndClosedPolylinesUseSharedPoints) {
  Sketch s;
  const std::array<Position, 3> positions{{{0, 0}, {5, 0}, {5, 4}}};
  auto open = s.create_polyline(positions);
  ASSERT_TRUE(open);
  EXPECT_EQ(open->points.size(), 3);
  EXPECT_EQ(open->lines.size(), 2);
  for (int i = 0; i < 2; ++i) {
    auto line = read<SketchLine>(s, open->lines[i]);
    EXPECT_EQ(line.start, open->points[i]);
    EXPECT_EQ(line.end, open->points[i + 1]);
  }
  auto closed = s.create_polyline(positions, true, true);
  ASSERT_TRUE(closed);
  ASSERT_EQ(closed->lines.size(), 3);
  EXPECT_EQ(read<SketchLine>(s, closed->lines.back()).end,
            closed->points.front());
  for (auto id : closed->points)
    EXPECT_TRUE(read<SketchPoint>(s, id).construction);
  for (auto id : closed->lines)
    EXPECT_TRUE(read<SketchLine>(s, id).construction);
}
TEST(Sketch, RectangleCreatesSharedCornersWithRequestedDimensions) {
  Sketch s;
  auto rect = s.create_rectangle({7, -3}, 50, 30, true);
  ASSERT_TRUE(rect);
  ASSERT_EQ(rect->points.size(), 4);
  ASSERT_EQ(rect->lines.size(), 4);
  const std::array<Position, 4> expected{
      {{7, -3}, {57, -3}, {57, 27}, {7, 27}}};
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(read<SketchPoint>(s, rect->points[i]).position, expected[i]);
    auto line = read<SketchLine>(s, rect->lines[i]);
    EXPECT_EQ(line.start, rect->points[i]);
    EXPECT_EQ(line.end, rect->points[(i + 1) % 4]);
    EXPECT_TRUE(line.construction);
  }
  EXPECT_EQ(s.entities().size(), 8);
}
TEST(Sketch, CompoundFailureLeavesGeometryAndAllocationUnchanged) {
  Sketch s;
  auto p = s.create_point({0, 0});
  ASSERT_TRUE(p);
  auto before = s.entities();
  const std::array<Position, 1> short_input{{{1, 2}}};
  const std::array<Position, 2> pair{{{0, 0}, {1, 1}}};
  EXPECT_FALSE(s.create_polyline({}));
  EXPECT_FALSE(s.create_polyline(short_input));
  EXPECT_FALSE(s.create_polyline(pair, true));
  const std::array<Position, 3> bad{{{0, 0}, {1, 1}, {kNonFinite[0], 2}}};
  EXPECT_FALSE(s.create_polyline(bad));
  for (double value : {0.0, -1.0, kNonFinite[0], kNonFinite[2]}) {
    EXPECT_FALSE(s.create_rectangle({0, 0}, value, 30));
    EXPECT_FALSE(s.create_rectangle({0, 0}, 50, value));
  }
  EXPECT_FALSE(s.create_rectangle({kNonFinite[0], 0}, 50, 30));
  EXPECT_FALSE(s.create_rectangle({1e308, 0}, 1e308, 30));
  EXPECT_FALSE(s.create_rectangle({1e308, 0}, 1, 30));
  EXPECT_EQ(s.entities(), before);
  auto next = s.create_point({1, 1});
  ASSERT_TRUE(next);
  EXPECT_EQ(*next, *p + 1);
}
