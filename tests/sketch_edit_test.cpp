#include "sketchcad/sketch_edit.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "sketchcad/sketch_view.h"
#include "sketchcad/solver.h"
using namespace sketchcad;
using K = ConstraintKind;
namespace {
constexpr double kPi = std::numbers::pi;
constexpr double kNear = 1e-9;

Position at(const Sketch& s, EntityId point) {
  return std::get<SketchPoint>(*s.entity(point)).position;
}
EntityId line(Sketch& s, Position a, Position b) {
  return *s.create_line(*s.create_point(a), *s.create_point(b));
}
SketchLine line_of(const Sketch& s, EntityId id) {
  return std::get<SketchLine>(*s.entity(id));
}
void expect_at(const Sketch& s, EntityId point, Position p) {
  EXPECT_NEAR(at(s, point).x, p.x, kNear) << point;
  EXPECT_NEAR(at(s, point).y, p.y, kNear) << point;
}
std::vector<Constraint> of_kind(const Sketch& s, K kind) {
  std::vector<Constraint> out;
  for (const auto& [id, c] : s.constraints())
    if (c.kind == kind) out.push_back(c);
  return out;
}
// The curve of the kOnCurve constraints holding `point`, sorted.
std::vector<EntityId> curves_holding(const Sketch& s, EntityId point) {
  std::vector<EntityId> out;
  for (const Constraint& c : of_kind(s, K::kOnCurve))
    if (c.first == point) out.push_back(c.second);
  std::sort(out.begin(), out.end());
  return out;
}
std::size_t lines(const Sketch& s) {
  return std::count_if(s.entities().begin(), s.entities().end(), [](const auto& e) {
    return std::holds_alternative<SketchLine>(e.second);
  });
}
}  // namespace

TEST(Trim, MiddlePieceSplitsTheLineOntoBothCutters) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {30, 0});
  const SketchLine before = line_of(s, l);
  const EntityId v1 = line(s, {10, -5}, {10, 5});
  const EntityId v2 = line(s, {20, -5}, {20, 5});
  const EntityId length = *s.add_dimension(K::kLength, l, 0, 30);
  ASSERT_TRUE(trim(s, l, {15, 0.2}));
  EXPECT_EQ(lines(s), 4u);
  const SketchLine kept = line_of(s, l);
  EXPECT_EQ(kept.start, before.start);
  expect_at(s, kept.end, {10, 0});
  EXPECT_EQ(curves_holding(s, kept.end), std::vector<EntityId>{v1});
  EXPECT_NEAR(s.constraint(length)->value, 10, kNear);
  // The new piece runs from the second cut to the old end.
  EntityId piece = 0;
  for (const auto& [id, e] : s.entities())
    if (const auto* k = std::get_if<SketchLine>(&e); k && k->end == before.end)
      piece = id;
  ASSERT_NE(piece, 0u);
  expect_at(s, line_of(s, piece).start, {20, 0});
  EXPECT_EQ(curves_holding(s, line_of(s, piece).start), std::vector<EntityId>{v2});
  EXPECT_EQ(solve(s).status, SolveStatus::kSolved);
}

TEST(Trim, EndPieceMovesTheEndAndErasesTheUnusedPoint) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {30, 0});
  const SketchLine before = line_of(s, l);
  const EntityId v = line(s, {10, -5}, {10, 5});
  const EntityId corner = *s.create_line(before.end, *s.create_point({30, 10}));
  const EntityId fix = *s.add_constraint(K::kFix, before.start);
  ASSERT_TRUE(trim(s, l, {5, 0}));
  EXPECT_FALSE(s.entity(before.start)) << "unused old start is erased";
  EXPECT_FALSE(s.constraint(fix)) << "with its constraints";
  const SketchLine after = line_of(s, l);
  EXPECT_EQ(after.end, before.end);
  expect_at(s, after.start, {10, 0});
  EXPECT_EQ(curves_holding(s, after.start), std::vector<EntityId>{v});

  Sketch t;
  const EntityId m = line(t, {0, 0}, {30, 0});
  const SketchLine original = line_of(t, m);
  line(t, {10, -5}, {10, 5});
  const EntityId other = *t.create_line(original.end, *t.create_point({30, 10}));
  ASSERT_TRUE(trim(t, m, {25, 0}));
  expect_at(t, line_of(t, m).end, {10, 0});
  EXPECT_TRUE(t.entity(original.end)) << "corner shared with another line stays";
  EXPECT_EQ(line_of(t, other).start, original.end);
  (void)corner;
}

TEST(Trim, LineWithoutCrossingsIsDeleted) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {30, 0});
  const SketchLine before = line_of(s, l);
  ASSERT_TRUE(s.add_dimension(K::kLength, l, 0, 30));
  line(s, {0, 10}, {30, 10});  // Parallel, no crossing.
  ASSERT_TRUE(trim(s, l, {15, 0}));
  EXPECT_FALSE(s.entity(l));
  EXPECT_FALSE(s.entity(before.start));
  EXPECT_FALSE(s.entity(before.end));
  EXPECT_TRUE(s.constraints().empty());
}

TEST(Trim, CircleBecomesAnArcWithoutTheTappedPiece) {
  Sketch s;
  const EntityId centre = *s.create_point({0, 0});
  const EntityId c = *s.create_circle(centre, 10);
  const EntityId radius = *s.add_dimension(K::kRadius, c, 0, 10);
  line(s, {-20, 6}, {20, 6});  // Crosses at (8, 6) and (-8, 6).
  ASSERT_TRUE(trim(s, c, {0, 10}));
  const Entity trimmed = *s.entity(c);
  const auto* arc = std::get_if<SketchArc>(&trimmed);
  ASSERT_TRUE(arc) << "same ID, now an arc";
  EXPECT_EQ(arc->center, centre);
  EXPECT_NEAR(arc->radius, 10, kNear);
  const double a = std::atan2(6.0, 8.0), b = std::atan2(6.0, -8.0);
  EXPECT_NEAR(arc->start_angle, b, kNear);
  EXPECT_NEAR(arc->sweep_angle, 2 * kPi - (b - a), kNear);
  EXPECT_TRUE(s.constraint(radius));

  Sketch one;
  const EntityId c1 = *one.create_circle(*one.create_point({0, 0}), 10);
  line(one, {-20, 0}, {0, 0});  // Ends inside: one crossing.
  const Sketch unchanged = one;
  EXPECT_FALSE(trim(one, c1, {0, 10}));
  EXPECT_TRUE(one == unchanged);
}

TEST(Trim, ArcCrossedTwiceSplitsIntoEqualArcs) {
  Sketch s;
  const EntityId centre = *s.create_point({0, 0});
  const EntityId arc = *s.create_arc(centre, 10, 0, kPi);
  line(s, {5, -20}, {5, 20});
  line(s, {-5, -20}, {-5, 20});
  ASSERT_TRUE(trim(s, arc, {0, 10}));
  const SketchArc kept = std::get<SketchArc>(*s.entity(arc));
  EXPECT_NEAR(kept.start_angle, 0, kNear);
  EXPECT_NEAR(kept.sweep_angle, kPi / 3, kNear);
  EntityId piece = 0;
  for (const auto& [id, e] : s.entities())
    if (std::holds_alternative<SketchArc>(e) && id != arc) piece = id;
  ASSERT_NE(piece, 0u);
  const SketchArc second = std::get<SketchArc>(*s.entity(piece));
  EXPECT_EQ(second.center, centre);
  EXPECT_NEAR(second.start_angle, 2 * kPi / 3, kNear);
  EXPECT_NEAR(second.sweep_angle, kPi / 3, kNear);
  const auto equal = of_kind(s, K::kEqual);
  ASSERT_EQ(equal.size(), 1u);
  EXPECT_EQ(equal[0].first, arc);
  EXPECT_EQ(equal[0].second, piece);
  EXPECT_EQ(solve(s).status, SolveStatus::kSolved);
}

TEST(Extend, LineReachesTheNextBoundaryAndMovesOn) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 0});
  const EntityId end = line_of(s, l).end;
  const EntityId v = line(s, {20, -5}, {20, 5});
  const EntityId circle = *s.create_circle(*s.create_point({40, 0}), 5);
  const EntityId length = *s.add_dimension(K::kLength, l, 0, 10);
  ASSERT_TRUE(extend(s, l, {9, 0.3}));
  EXPECT_EQ(line_of(s, l).end, end);
  expect_at(s, end, {20, 0});
  EXPECT_EQ(curves_holding(s, end), std::vector<EntityId>{v});
  EXPECT_NEAR(s.constraint(length)->value, 20, kNear);
  EXPECT_EQ(solve(s).status, SolveStatus::kSolved);

  ASSERT_TRUE(extend(s, l, {19, 0}));
  expect_at(s, end, {35, 0});
  EXPECT_EQ(curves_holding(s, end), std::vector<EntityId>{circle});
  EXPECT_EQ(solve(s).status, SolveStatus::kSolved);
}

TEST(Extend, NothingAheadOrASharedEndIsRejected) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 0});
  line(s, {20, -5}, {20, 5});
  const Sketch before = s;
  EXPECT_FALSE(extend(s, l, {1, 0})) << "nothing beyond the start";
  EXPECT_TRUE(s == before);

  const EntityId up = *s.create_line(line_of(s, l).end, *s.create_point({10, 10}));
  const Sketch shared = s;
  EXPECT_FALSE(extend(s, l, {9, 0})) << "end shared with another line";
  EXPECT_TRUE(s == shared);
  (void)up;
}

TEST(Extend, ArcGrowsToALineAhead) {
  Sketch s;
  const EntityId arc = *s.create_arc(*s.create_point({0, 0}), 10, 0, kPi / 2);
  line(s, {-6, -20}, {-6, 20});  // Meets the circle at 126.87 deg ahead.
  ASSERT_TRUE(extend(s, arc, {0.5, 9.9}));
  const SketchArc after = std::get<SketchArc>(*s.entity(arc));
  EXPECT_NEAR(after.start_angle, 0, kNear);
  EXPECT_NEAR(after.sweep_angle, std::atan2(8.0, -6.0), kNear);
}

TEST(EditPreview, DescribesTheRemovedAndAddedPieces) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {30, 0});
  line(s, {10, -5}, {10, 5});
  line(s, {20, -5}, {20, 5});
  const auto removed = trim_preview(s, l, {15, 0});
  ASSERT_TRUE(removed);
  EXPECT_FALSE(removed->is_arc);
  EXPECT_NEAR(removed->a.x, 10, kNear);
  EXPECT_NEAR(removed->b.x, 20, kNear);

  Sketch t;
  const EntityId m = line(t, {0, 0}, {10, 0});
  line(t, {20, -5}, {20, 5});
  const auto added = extend_preview(t, m, {9, 0});
  ASSERT_TRUE(added);
  EXPECT_NEAR(added->a.x, 10, kNear);
  EXPECT_NEAR(added->b.x, 20, kNear);
  EXPECT_FALSE(extend_preview(t, m, {1, 0}));
}

TEST(PickCurve, IgnoresPoints) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {30, 0});
  EXPECT_EQ(pick(s, {0.1, 0}, 1), line_of(s, l).start);
  EXPECT_EQ(pick_curve(s, {0.1, 0}, 1), l);
  EXPECT_FALSE(pick_curve(s, {15, 5}, 1));
}
