#include <gtest/gtest.h>

#include "sketchcad/sketch.h"
using namespace sketchcad;
using K = ConstraintKind;
namespace {
// Fresh sketch with one of each primitive.
struct Fixture {
  Sketch s;
  EntityId p, q, r, line, other_line, circle, arc;
  Fixture() {
    p = *s.create_point({0, 0});
    q = *s.create_point({10, 1});
    r = *s.create_point({3, 7});
    line = *s.create_line(p, q);
    other_line = *s.create_line(q, r);
    circle = *s.create_circle(r, 4);
    arc = *s.create_arc(p, 2, 0, 1);
  }
};
// The next ID the allocator would issue, observed through a probe copy.
EntityId next_id(const Sketch& s) {
  Sketch probe = s;
  return *probe.create_point({0, 0});
}
}  // namespace

TEST(Constraint, AcceptsEverySupportedPair) {
  Fixture f;
  const struct {
    K kind;
    EntityId first, second;
  } cases[] = {
      {K::kCoincident, f.p, f.q},     {K::kHorizontal, f.line, 0},
      {K::kHorizontal, f.p, f.r},     {K::kVertical, f.line, 0},
      {K::kVertical, f.q, f.r},       {K::kParallel, f.line, f.other_line},
      {K::kPerpendicular, f.line, f.other_line},
      {K::kTangent, f.line, f.circle}, {K::kTangent, f.line, f.arc},
      {K::kTangent, f.circle, f.arc}, {K::kEqual, f.line, f.other_line},
      {K::kEqual, f.circle, f.arc},   {K::kFix, f.p, 0},
  };
  std::vector<EntityId> ids;
  for (const auto& c : cases) {
    const auto id = f.s.add_constraint(c.kind, c.first, c.second);
    ASSERT_TRUE(id) << static_cast<int>(c.kind);
    const Constraint stored = *f.s.constraint(*id);
    EXPECT_EQ(stored.id, *id);
    EXPECT_EQ(stored.kind, c.kind);
    EXPECT_EQ(stored.first, c.first);
    EXPECT_EQ(stored.second, c.second);
    EXPECT_FALSE(f.s.entity(*id)) << "constraint IDs are not entity IDs";
    for (EntityId previous : ids) EXPECT_NE(previous, *id);
    EXPECT_GT(*id, f.arc);
    ids.push_back(*id);
  }
  EXPECT_EQ(f.s.constraints().size(), std::size(cases));
}

TEST(Constraint, RejectsInvalidReferencesAtomically) {
  Fixture f;
  const Sketch before = f.s;
  const EntityId expected_next = next_id(f.s);
  const struct {
    K kind;
    EntityId first, second;
  } cases[] = {
      {K::kCoincident, f.p, f.line},       {K::kCoincident, f.p, f.p},
      {K::kCoincident, f.p, 0},            {K::kHorizontal, f.circle, 0},
      {K::kHorizontal, f.line, f.p},       {K::kVertical, f.p, 0},
      {K::kParallel, f.line, f.p},         {K::kParallel, f.line, f.line},
      {K::kPerpendicular, f.circle, f.line},
      {K::kTangent, f.line, f.other_line}, {K::kTangent, f.circle, f.circle},
      {K::kTangent, f.p, f.circle},        {K::kEqual, f.line, f.circle},
      {K::kEqual, f.arc, f.arc},           {K::kFix, f.line, 0},
      {K::kFix, f.p, f.q},                 {K::kFix, 0, 0},
      {K::kCoincident, f.p, 9999},         {K::kFix, 9999, 0},
  };
  for (const auto& c : cases) {
    EXPECT_FALSE(f.s.add_constraint(c.kind, c.first, c.second))
        << static_cast<int>(c.kind) << " " << c.first << " " << c.second;
  }
  EXPECT_TRUE(f.s == before);
  EXPECT_TRUE(f.s.constraints().empty());
  EXPECT_EQ(next_id(f.s), expected_next);
}

TEST(Constraint, ConstrainedEntitiesCannotBeErasedUntilConstraintIs) {
  Fixture f;
  const EntityId horizontal = *f.s.add_constraint(K::kHorizontal, f.line);
  const EntityId fix = *f.s.add_constraint(K::kFix, f.r);
  EXPECT_EQ(f.s.constraints_of(f.line), std::vector<EntityId>{horizontal});
  EXPECT_EQ(f.s.constraints_of(f.r), std::vector<EntityId>{fix});
  EXPECT_TRUE(f.s.constraints_of(f.q).empty());

  EXPECT_FALSE(f.s.erase(f.line));
  EXPECT_TRUE(f.s.entity(f.line));
  EXPECT_TRUE(f.s.erase(horizontal));
  EXPECT_FALSE(f.s.constraint(horizontal));
  EXPECT_FALSE(f.s.erase(horizontal));
  EXPECT_TRUE(f.s.erase(f.line));

  EXPECT_TRUE(f.s.erase(f.other_line));
  EXPECT_TRUE(f.s.erase(f.circle));
  EXPECT_FALSE(f.s.erase(f.r)) << "fixed point stays while fix exists";
  EXPECT_TRUE(f.s.erase(fix));
  EXPECT_TRUE(f.s.erase(f.r));
}

TEST(Constraint, TangentStoresLineFirst) {
  Fixture f;
  const EntityId id = *f.s.add_constraint(K::kTangent, f.circle, f.line);
  EXPECT_EQ(f.s.constraint(id)->first, f.line);
  EXPECT_EQ(f.s.constraint(id)->second, f.circle);
}

TEST(Constraint, CircleTangencyKindFollowsCurrentGeometry) {
  Sketch s;
  const EntityId big = *s.create_circle(*s.create_point({0, 0}), 10);
  const EntityId inside = *s.create_circle(*s.create_point({6, 0}), 3);
  const EntityId outside = *s.create_circle(*s.create_point({14, 0}), 3);
  // Distance 6: internal |10-3| = 7 is closer than external 13.
  EXPECT_TRUE(s.constraint(*s.add_constraint(K::kTangent, big, inside))
                  ->internal);
  // Distance 14: external 13 is closer than internal 7.
  EXPECT_FALSE(s.constraint(*s.add_constraint(K::kTangent, big, outside))
                   ->internal);
}

TEST(Constraint, FixCapturesCurrentPosition) {
  Fixture f;
  const EntityId id = *f.s.add_constraint(K::kFix, f.r);
  EXPECT_EQ(f.s.constraint(id)->target, (Position{3, 7}));
  ASSERT_TRUE(f.s.update_point(f.r, {5, 5}));
  EXPECT_EQ(f.s.constraint(id)->target, (Position{3, 7}));
}

TEST(Constraint, SnapshotsAndEqualityIncludeConstraints) {
  Fixture f;
  const Sketch without = f.s;
  const EntityId id = *f.s.add_constraint(K::kHorizontal, f.line);
  EXPECT_FALSE(f.s == without);
  const Sketch with = f.s;
  EXPECT_TRUE(with == f.s);

  f.s = without;
  EXPECT_TRUE(f.s.constraints().empty());
  EXPECT_TRUE(f.s == without);
  // The watermark survives the restore: the constraint ID is not reissued.
  EXPECT_GT(next_id(f.s), id);
  f.s = with;
  EXPECT_EQ(f.s.constraint(id)->kind, K::kHorizontal);
}

TEST(Constraint, IdsAreNotReusedAfterErase) {
  Fixture f;
  const EntityId id = *f.s.add_constraint(K::kFix, f.p);
  ASSERT_TRUE(f.s.erase(id));
  const EntityId again = *f.s.add_constraint(K::kFix, f.p);
  EXPECT_GT(again, id);
}

// U04 (sketch-editing.adoc): point on a line, circle or arc.
TEST(Constraint, OnCurveTakesAPointThenALineCircleOrArc) {
  Fixture f;
  const EntityId free = *f.s.create_point({5, 5});
  std::vector<EntityId> ids;
  for (EntityId curve : {f.line, f.circle, f.arc}) {
    const auto id = f.s.add_constraint(K::kOnCurve, free, curve);
    ASSERT_TRUE(id) << curve;
    EXPECT_EQ(f.s.constraint(*id)->first, free);
    EXPECT_EQ(f.s.constraint(*id)->second, curve);
    ids.push_back(*id);
  }
  const Sketch before = f.s;
  const EntityId expected_next = next_id(f.s);
  const std::pair<EntityId, EntityId> rejected[] = {
      {f.p, f.line},   {f.q, f.line},   {f.r, f.circle}, {f.p, f.arc},
      {f.line, free},  {f.circle, free}, {free, f.q},    {free, 0},
      {free, free},    {free, 9999},    {f.line, f.circle},
  };
  for (const auto& [first, second] : rejected)
    EXPECT_FALSE(f.s.add_constraint(K::kOnCurve, first, second))
        << first << " " << second;
  EXPECT_TRUE(f.s == before);
  EXPECT_EQ(next_id(f.s), expected_next);

  EXPECT_FALSE(f.s.erase(f.circle)) << "referenced by on-curve";
  ASSERT_TRUE(f.s.erase(ids[1]));
  EXPECT_TRUE(f.s.erase(f.circle));
}

TEST(ArcBindings, ValidationAndSnapshotMetadata) {
  using K = ConstraintKind;
  Sketch s;
  const auto p = *s.create_point({0, 0}), q = *s.create_point({4, 0});
  const auto center = *s.create_point({0, 2});
  const auto line = *s.create_line(p, q);
  const auto arc = *s.create_arc(center, 2, 0, 1);
  EXPECT_FALSE(s.add_arc_tangency(K::kTangent, line, arc, p));
  EXPECT_FALSE(s.add_arc_tangency(K::kTangentStart, line, arc, center));
  EXPECT_FALSE(s.add_arc_tangency(K::kTangentStart, arc, line, p));
  EXPECT_FALSE(s.add_constraint(K::kTangentStart, line, arc));
  const auto binding = s.add_arc_tangency(K::kTangentStart, line, arc, p);
  ASSERT_TRUE(binding);
  EXPECT_EQ(s.constraint(*binding)->side, 1);
  EXPECT_FALSE(s.add_arc_tangency(K::kTangentStart, line, arc, q));
  EXPECT_FALSE(s.add_arc_tangency(K::kTangentEnd, line, arc, p));
  auto snapshot = s.snapshot();
  ASSERT_TRUE(Sketch::from_snapshot(snapshot));
  for (int invalid = 0; invalid != 5; ++invalid) {
    auto bad = snapshot;
    auto& c = bad.constraints.back();
    if (invalid == 0) c.side = 0;
    if (invalid == 1) c.reference = true;
    if (invalid == 2) c.value = 2;
    if (invalid == 3) c.target.x = 1;
    if (invalid == 4)
      bad.constraints.push_back(c), bad.constraints.back().id = bad.next_id++;
    EXPECT_FALSE(Sketch::from_snapshot(bad)) << invalid;
  }
  const auto center_line = *s.create_line(center, q);
  EXPECT_FALSE(s.add_arc_tangency(K::kTangentEnd, center_line, arc, center));
  EXPECT_FALSE(s.add_arc_tangency(K::kTangentEnd, center_line, arc, q));
}

TEST(ArcBindings, CapturesNormalBranchAndPreservesItInUnsolvedSnapshot) {
  Sketch s;
  const auto p = *s.create_point({0, 0}), q = *s.create_point({4, 0});
  const auto center = *s.create_point({0, 2});
  const auto forward = *s.create_line(p, q), backward = *s.create_line(q, p);
  const auto arc = *s.create_arc(center, 2, 0, 1);
  const auto arc2 = *s.create_arc(center, 2, 0, 1);
  const auto a = s.add_arc_tangency(ConstraintKind::kTangentStart, forward, arc, p);
  const auto b = s.add_arc_tangency(ConstraintKind::kTangentEnd, backward, arc2, p);
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  EXPECT_FALSE(s.constraint(*a)->internal);
  EXPECT_TRUE(s.constraint(*b)->internal);
  ASSERT_TRUE(s.update_point(center, {0, -2}));
  const auto restored = Sketch::from_snapshot(s.snapshot());
  ASSERT_TRUE(restored);
  EXPECT_FALSE(restored->constraint(*a)->internal);
  EXPECT_TRUE(restored->constraint(*b)->internal);
  EXPECT_EQ(*restored, s);
}

TEST(ArcBindings, UpdateLineAndArcPreserveBindingInvariants) {
  Sketch s;
  const auto p1 = *s.create_point({2, 0}), q1 = *s.create_point({10, 0});
  const auto p2 = *s.create_point({0, 2}), q2 = *s.create_point({0, 10});
  const auto center = *s.create_point({2, 2});
  const auto alt = *s.create_point({3, 3});
  const auto on_pt = *s.create_point({5, 0});
  const auto l1 = *s.create_line(p1, q1);
  const auto l2 = *s.create_line(p2, q2);
  const auto arc = *s.create_arc(center, 2, -1.57, 1.57);
  ASSERT_TRUE(s.add_arc_tangency(K::kTangentStart, l1, arc, p1));
  ASSERT_TRUE(s.add_arc_tangency(K::kTangentEnd, l2, arc, p2));
  ASSERT_TRUE(s.add_constraint(K::kOnCurve, on_pt, l1));
  ASSERT_TRUE(s.add_constraint(K::kOnCurve, on_pt, arc));
  const Sketch before = s;
  EXPECT_FALSE(s.update_line(l1, center, q1));
  EXPECT_FALSE(s.update_line(l1, p1, center));
  EXPECT_FALSE(s.update_line(l1, p2, q1));
  EXPECT_FALSE(s.update_line(l1, on_pt, q1));
  EXPECT_FALSE(s.update_arc(arc, p1, 2, -1.57, 1.57));
  EXPECT_FALSE(s.update_arc(arc, q2, 2, -1.57, 1.57));
  EXPECT_FALSE(s.update_arc(arc, on_pt, 2, -1.57, 1.57));
  EXPECT_TRUE(s == before);
  EXPECT_TRUE(s.update_arc(arc, alt, 2, -1.57, 1.57));
  EXPECT_TRUE(Sketch::from_snapshot(s.snapshot()));
}
