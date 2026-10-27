#include "sketchcad/sketch_edit.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "sketchcad/diagnostics.h"
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

namespace {
struct Corner {
  Sketch s;
  EntityId p = *s.create_point({0, 0});
  EntityId a = *s.create_point({10, 0});
  EntityId b = *s.create_point({0, 10});
  EntityId l1 = *s.create_line(p, a);
  EntityId l2 = *s.create_line(b, p);  // Exercise the reversed endpoint.
};
}  // namespace

TEST(Chamfer, KeepsVirtualSharpLineIdsAndDegreesOfFreedom) {
  Corner c;
  const auto dof = diagnose(c.s).dof;
  ASSERT_TRUE(chamfer(c.s, c.p, 2));
  EXPECT_EQ(lines(c.s), 3u);
  EXPECT_EQ(c.s.entities().size(), 8u);
  EXPECT_TRUE(std::get<SketchPoint>(*c.s.entity(c.p)).construction);
  expect_at(c.s, c.p, {0, 0});
  const auto l1 = line_of(c.s, c.l1), l2 = line_of(c.s, c.l2);
  EXPECT_EQ(l1.end, c.a);
  EXPECT_EQ(l2.start, c.b);
  expect_at(c.s, l1.start, {2, 0});
  expect_at(c.s, l2.end, {0, 2});
  EXPECT_EQ(curves_holding(c.s, c.p), (std::vector<EntityId>{c.l1, c.l2}));
  const auto legs = of_kind(c.s, K::kDistance);
  ASSERT_EQ(legs.size(), 2u);
  for (const auto& leg : legs) {
    EXPECT_EQ(leg.first, c.p);
    EXPECT_DOUBLE_EQ(leg.value, 2);
    EXPECT_FALSE(leg.reference);
  }
  EXPECT_EQ(solve(c.s).status, SolveStatus::kSolved);
  EXPECT_EQ(diagnose(c.s).dof, dof);
  EXPECT_FALSE(chamfer(c.s, c.p, 1));  // A virtual sharp is no longer a corner.
}

TEST(Chamfer, ConvertsLengthsWithoutLosingConstraintIdentityOrReferenceFlag) {
  Corner c;
  const auto length = *c.s.add_dimension(K::kLength, c.l1, 0, 10);
  const auto reference = *c.s.add_dimension(K::kLength, c.l2, 0, 10, true);
  ASSERT_TRUE(
      c.s.set_dimension_placement(length, DimensionPlacement{4, .3, 0}));
  const auto horizontal = *c.s.add_constraint(K::kHorizontal, c.l1);
  const auto fixed = *c.s.add_constraint(K::kFix, c.p);
  ASSERT_TRUE(chamfer(c.s, c.p, 2));
  for (auto [id, far] : {std::pair{length, c.a}, std::pair{reference, c.b}}) {
    const auto converted = c.s.constraint(id);
    ASSERT_TRUE(converted);
    EXPECT_EQ(converted->kind, K::kDistance);
    EXPECT_EQ(converted->first, far);
    EXPECT_EQ(converted->second, c.p);
    EXPECT_DOUBLE_EQ(converted->value, 10);
    EXPECT_FALSE(converted->placement);
  }
  EXPECT_FALSE(c.s.constraint(length)->reference);
  EXPECT_TRUE(c.s.constraint(reference)->reference);
  EXPECT_TRUE(c.s.constraint(horizontal));
  EXPECT_TRUE(c.s.constraint(fixed));
  EXPECT_EQ(solve(c.s).status, SolveStatus::kSolved);
  expect_at(c.s, c.p, {0, 0});
}

TEST(Chamfer,
     RectangleOverallDimensionsSurviveAndLegsCanBeEditedIndependently) {
  Sketch s;
  const auto rectangle = *s.create_rectangle({0, 0}, 20, 10);
  const auto p = rectangle.points;
  const auto l = rectangle.lines;
  ASSERT_TRUE(s.add_constraint(K::kFix, p[0]));
  for (std::size_t i = 0; i < l.size(); ++i)
    ASSERT_TRUE(
        s.add_constraint(i % 2 == 0 ? K::kHorizontal : K::kVertical, l[i]));
  const auto width = *s.add_dimension(K::kLength, l[0], 0, 20);
  const auto height = *s.add_dimension(K::kLength, l[3], 0, 10);
  const auto dof = diagnose(s).dof;
  const Sketch original = s;
  ASSERT_TRUE(chamfer(s, p[0], 2));
  ASSERT_EQ(solve(s).status, SolveStatus::kSolved);
  EXPECT_EQ(diagnose(s).dof, dof);
  EXPECT_NEAR(*measure(s, K::kDistance, p[1], p[0]), 20, kLengthTolerance);
  EXPECT_NEAR(*measure(s, K::kDistance, p[3], p[0]), 10, kLengthTolerance);
  EXPECT_EQ(s.constraint(width)->kind, K::kDistance);
  EXPECT_EQ(s.constraint(height)->kind, K::kDistance);
  const auto first = line_of(s, l[0]).start;
  const auto second = line_of(s, l[3]).end;
  EntityId leg = 0;
  for (const auto& [id, dim] : s.constraints())
    if (dim.kind == K::kDistance && dim.first == p[0] && dim.second == first)
      leg = id;
  ASSERT_NE(leg, 0u);
  ASSERT_TRUE(s.set_dimension(leg, 3));
  ASSERT_EQ(solve(s).status, SolveStatus::kSolved);
  EXPECT_NEAR(at(s, first).x, 3, kLengthTolerance);
  EXPECT_NEAR(at(s, second).y, 2, kLengthTolerance);
  for (auto id : p) {
    EXPECT_NEAR(at(s, id).x, at(original, id).x, kLengthTolerance);
    EXPECT_NEAR(at(s, id).y, at(original, id).y, kLengthTolerance);
  }
}

TEST(Chamfer, PreviewMatchesAnObliqueChamferAndDoesNotMutate) {
  Corner c;
  ASSERT_TRUE(c.s.update_point(c.b, {6, 8}));
  const Sketch before = c.s;
  const auto preview = chamfer_preview(c.s, c.p, 2);
  ASSERT_TRUE(preview);
  EXPECT_TRUE(c.s == before);
  EXPECT_FALSE(preview->is_arc);
  EXPECT_NEAR(preview->a.x, 2, kNear);
  EXPECT_NEAR(preview->b.x, 1.2, kNear);
  EXPECT_NEAR(preview->b.y, 1.6, kNear);
  ASSERT_TRUE(chamfer(c.s, c.p, 2));
  for (const auto& [id, e] : c.s.entities())
    if (const auto* l = std::get_if<SketchLine>(&e);
        l && id != c.l1 && id != c.l2) {
      EXPECT_EQ(at(c.s, l->start), preview->a);
      EXPECT_EQ(at(c.s, l->end), preview->b);
    }
}

TEST(Chamfer, RejectsInvalidOrTooLongDistancesWithoutConsumingIds) {
  for (double d :
       {0.0, -1.0, kLengthTolerance, 10.0, 10.0 - kLengthTolerance / 2,
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()}) {
    Corner c;
    const auto before = c.s.snapshot();
    EXPECT_FALSE(chamfer(c.s, c.p, d)) << d;
    EXPECT_FALSE(chamfer_preview(c.s, c.p, d));
    EXPECT_EQ(c.s.snapshot().entities, before.entities);
    EXPECT_EQ(c.s.snapshot().constraints, before.constraints);
    EXPECT_EQ(c.s.snapshot().next_id, before.next_id);
  }
}

TEST(Chamfer, RejectsCollinearAndEqualLengthCornersWithAReason) {
  Corner c;
  ASSERT_TRUE(c.s.update_point(c.b, {-10, 0}));
  EXPECT_EQ(chamfer_status(c.s, c.p, 2), ChamferStatus::kCollinear);
  EXPECT_FALSE(chamfer(c.s, c.p, 2));
  ASSERT_TRUE(c.s.update_point(c.b, {10, 0}));
  EXPECT_EQ(chamfer_status(c.s, c.p, 2), ChamferStatus::kCollinear);
  ASSERT_TRUE(c.s.update_point(c.b, {0, 10}));
  ASSERT_TRUE(c.s.add_constraint(K::kEqual, c.l1, c.l2));
  const Sketch before = c.s;
  EXPECT_EQ(chamfer_status(c.s, c.p, 2), ChamferStatus::kEqualLength);
  EXPECT_FALSE(chamfer(c.s, c.p, 2));
  EXPECT_TRUE(c.s == before);
  EXPECT_EQ(pick_corner(c.s, {0, 0}, 0), c.p);
}

TEST(Chamfer, RejectsNonCornersAndCircleOrArcCentres) {
  for (int use = 0; use < 3; ++use) {
    Corner c;
    if (use == 0) c.s.create_line(c.p, *c.s.create_point({-10, 0}));
    if (use == 1) c.s.create_circle(c.p, 5);
    if (use == 2) c.s.create_arc(c.p, 5, 0, kPi);
    const Sketch before = c.s;
    EXPECT_EQ(chamfer_status(c.s, c.p, 2), ChamferStatus::kNotCorner);
    EXPECT_FALSE(chamfer(c.s, c.p, 2));
    EXPECT_FALSE(pick_corner(c.s, {0, 0}, 1));
    EXPECT_TRUE(c.s == before);
  }
  Corner c;
  EXPECT_FALSE(chamfer(c.s, 9999, 2));
  EXPECT_FALSE(chamfer(c.s, c.l1, 2));
  EXPECT_FALSE(chamfer(c.s, c.a, 2));
}

TEST(Chamfer, ConstructionChamferRequiresBothConstructionLines) {
  for (bool both : {false, true}) {
    Corner c;
    c.s.set_construction(c.l1, true);
    c.s.set_construction(c.l2, both);
    ASSERT_TRUE(chamfer(c.s, c.p, 2));
    for (const auto& [id, e] : c.s.entities()) {
      if (const auto* l = std::get_if<SketchLine>(&e);
          l && id != c.l1 && id != c.l2) {
        EXPECT_EQ(l->construction, both);
      }
    }
  }
}

TEST(Chamfer, AllocationFailureRollsBackTheEntireEdit) {
  Corner c;
  auto snapshot = c.s.snapshot();
  snapshot.next_id = std::numeric_limits<EntityId>::max() - 1;
  auto exhausted = Sketch::from_snapshot(snapshot);
  ASSERT_TRUE(exhausted);
  EXPECT_FALSE(chamfer(*exhausted, c.p, 2));
  EXPECT_EQ(exhausted->snapshot().entities, snapshot.entities);
  EXPECT_EQ(exhausted->snapshot().constraints, snapshot.constraints);
  EXPECT_EQ(exhausted->snapshot().next_id, snapshot.next_id);
}

TEST(Chamfer, PicksOnlyCornersByDistanceThenIdAndRejectsInvalidCursor) {
  Corner c;
  const auto rectangle = *c.s.create_rectangle({-11, -10}, 10, 10);
  EXPECT_EQ(pick_corner(c.s, {-.5, 0}, 1), c.p);
  EXPECT_EQ(pick_corner(c.s, {-.9, 0}, 1), rectangle.points[2]);
  EXPECT_FALSE(pick_corner(c.s, {10, 0}, .1));
  EXPECT_FALSE(pick_corner(c.s, {0, 0}, -1));
  EXPECT_FALSE(pick_corner(c.s, {NAN, 0}, 1));
  EXPECT_FALSE(pick_corner(c.s, {0, 0}, INFINITY));
}

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

TEST(Extend, SlopingLineReachesBoundaryContinuation) {
  Sketch s;
  line(s, {0, 0}, {0, 28});
  const EntityId boundary = line(s, {12, 0}, {12, 28});
  const EntityId l = line(s, {0, 13}, {2, 10.3});
  const EntityId end = line_of(s, l).end;
  const auto length = *s.add_dimension(K::kLength, l, 0, std::hypot(2, 2.7));
  const auto preview = extend_preview(s, l, {2, 10.3});
  ASSERT_TRUE(preview);
  EXPECT_NEAR(preview->b.x, 12, kNear);
  EXPECT_NEAR(preview->b.y, -3.2, kNear);
  ASSERT_TRUE(extend(s, l, {2, 10.3}));
  EXPECT_EQ(line_of(s, l).end, end);
  expect_at(s, end, {12, -3.2});
  expect_at(s, line_of(s, boundary).start, {12, 0});
  expect_at(s, line_of(s, boundary).end, {12, 28});
  EXPECT_EQ(curves_holding(s, end), std::vector<EntityId>{boundary});
  EXPECT_NEAR(s.constraint(length)->value, std::hypot(12, 16.2), kNear);
  EXPECT_EQ(solve(s).status, SolveStatus::kSolved);
}

TEST(Extend, StartReachesBoundaryContinuationBackwards) {
  Sketch s;
  const auto l = line(s, {0, 0}, {5, 0});
  line(s, {-10, 2}, {-10, 8});
  ASSERT_TRUE(extend(s, l, {0, 0}));
  expect_at(s, line_of(s, l).start, {-10, 0});
}

TEST(Extend, ArcReachesLineContinuation) {
  Sketch s;
  const auto arc = *s.create_arc(*s.create_point({0, 0}), 10, 0, kPi / 2);
  line(s, {-6, -20}, {-6, -15});
  ASSERT_TRUE(extend(s, arc, {0, 10}));
  EXPECT_NEAR(std::get<SketchArc>(*s.entity(arc)).sweep_angle,
              std::atan2(8.0, -6.0), kNear);
}

TEST(Trim, IgnoresBoundaryContinuation) {
  Sketch s;
  const auto l = line(s, {0, 0}, {30, 0});
  line(s, {10, 5}, {10, 10});
  ASSERT_TRUE(trim(s, l, {20, 0}));
  EXPECT_FALSE(s.entity(l));  // No actual crossing: whole line is deleted.
}
