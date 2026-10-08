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
