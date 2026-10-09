#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>

#include "sketchcad/diagnostics.h"
#include "sketchcad/solver.h"
using namespace sketchcad;
using K = ConstraintKind;
namespace {
constexpr double kPi = std::numbers::pi;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

// Independent measurement oracle (atan2 angles, projection distances).
Position at(const Sketch& s, EntityId point) {
  return std::get<SketchPoint>(*s.entity(point)).position;
}
std::pair<Position, Position> ends(const Sketch& s, EntityId line) {
  const auto l = std::get<SketchLine>(*s.entity(line));
  return {at(s, l.start), at(s, l.end)};
}
double length(Position a, Position b) { return std::hypot(b.x - a.x, b.y - a.y); }
double line_length(const Sketch& s, EntityId line) {
  const auto [a, b] = ends(s, line);
  return length(a, b);
}
double point_line(const Sketch& s, EntityId point, EntityId line) {
  const auto [a, b] = ends(s, line);
  const Position p = at(s, point);
  const double t = ((p.x - a.x) * (b.x - a.x) + (p.y - a.y) * (b.y - a.y)) /
                   std::pow(length(a, b), 2);
  return length(p, {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)});
}
// Undirected angle from line a to line b in [0, pi).
double angle(const Sketch& s, EntityId a, EntityId b) {
  const auto [p, q] = ends(s, a);
  const auto [u, v] = ends(s, b);
  double t = std::atan2(v.y - u.y, v.x - u.x) - std::atan2(q.y - p.y, q.x - p.x);
  t = std::fmod(t, kPi);
  return t < 0 ? t + kPi : t;
}
double radius(const Sketch& s, EntityId curve) {
  const Entity e = *s.entity(curve);
  if (const auto* c = std::get_if<SketchCircle>(&e)) return c->radius;
  return std::get<SketchArc>(e).radius;
}
EntityId line(Sketch& s, Position a, Position b) {
  return *s.create_line(*s.create_point(a), *s.create_point(b));
}
void pin(Sketch& s, EntityId id) {
  const auto l = std::get<SketchLine>(*s.entity(id));
  ASSERT_TRUE(s.add_constraint(K::kFix, l.start));
  ASSERT_TRUE(s.add_constraint(K::kFix, l.end));
}
EntityId next_id(const Sketch& s) {
  Sketch probe = s;
  return *probe.create_point({0, 0});
}

// A document running the solver on every command.
class Dimensions : public ::testing::Test {
 protected:
  void open(const Sketch& s) {
    doc = Document(s);
    doc.set_commit_step(solver_step());
  }
  std::optional<EntityId> add(K kind, EntityId a, EntityId b, double value) {
    std::optional<EntityId> id;
    const bool ok = doc.execute("Dimension", [&](Sketch& s) {
      id = s.add_dimension(kind, a, b, value);
      return id.has_value();
    });
    return ok ? id : std::nullopt;
  }
  bool change(EntityId id, double value) {
    return doc.execute("Dimension",
                       [&](Sketch& s) { return s.set_dimension(id, value); });
  }
  double value(EntityId id) const { return doc.sketch().constraint(id)->value; }
  // Changes, checks the oracle, then undoes back to the original value.
  template <typename Oracle>
  void drives(EntityId id, double target, double tolerance, Oracle oracle) {
    const Sketch before = doc.sketch();
    const double original = value(id);
    ASSERT_TRUE(change(id, target));
    EXPECT_EQ(value(id), target);
    EXPECT_NEAR(oracle(doc.sketch()), target, tolerance);
    ASSERT_TRUE(doc.undo());
    EXPECT_TRUE(doc.sketch() == before);
    EXPECT_EQ(value(id), original);
  }
  // An impossible value fails and keeps geometry, value and history.
  void rejects(EntityId id, double target) {
    const Sketch before = doc.sketch();
    const auto label = doc.undo_label();
    EXPECT_FALSE(change(id, target));
    EXPECT_TRUE(doc.sketch() == before);
    EXPECT_EQ(doc.undo_label(), label);
    EXPECT_FALSE(doc.can_redo());
  }
  Document doc;
};
}  // namespace

TEST(Dimension, ValidatesReferencesAndValuesAtomically) {
  Sketch s;
  const EntityId p = *s.create_point({0, 0}), q = *s.create_point({3, 4});
  const EntityId l = line(s, {0, 5}, {10, 5});
  const EntityId m = line(s, {0, 9}, {10, 12});
  const EntityId c = *s.create_circle(p, 2);
  const EntityId arc = *s.create_arc(q, 3, 0, 1);
  const Sketch before = s;
  const EntityId expected_next = next_id(s);
  const struct {
    K kind;
    EntityId a, b;
    double v;
  } invalid[] = {
      {K::kLength, l, 0, 0},       {K::kLength, l, 0, -1},
      {K::kLength, l, 0, kNaN},    {K::kLength, l, 0, kInf},
      {K::kLength, c, 0, 5},       {K::kLength, l, m, 5},
      {K::kDistance, p, q, 0},     {K::kDistance, p, q, -2},
      {K::kDistance, p, p, 2},     {K::kDistance, p, c, 2},
      {K::kDistance, p, l, -1},    {K::kDistance, l, m, 2},
      {K::kDistance, p, 0, 2},     {K::kAngle, l, m, 0},
      {K::kAngle, l, m, kPi},      {K::kAngle, l, m, -0.5},
      {K::kAngle, l, l, 1},        {K::kAngle, l, c, 1},
      {K::kRadius, c, 0, 0},       {K::kRadius, arc, 0, kNaN},
      {K::kRadius, l, 0, 2},       {K::kRadius, c, arc, 2},
      {K::kHorizontal, l, 0, 1},   {K::kLength, 9999, 0, 1},
  };
  for (const auto& d : invalid)
    EXPECT_FALSE(s.add_dimension(d.kind, d.a, d.b, d.v))
        << static_cast<int>(d.kind) << " " << d.a << " " << d.b << " " << d.v;
  EXPECT_FALSE(s.add_constraint(K::kLength, l)) << "dimensions need a value";
  EXPECT_TRUE(s == before);
  EXPECT_EQ(next_id(s), expected_next);

  const struct {
    K kind;
    EntityId a, b;
    double v;
  } valid[] = {
      {K::kLength, l, 0, 12},      {K::kDistance, p, q, 4},
      {K::kDistance, p, l, 0},     {K::kDistance, l, q, 2},
      {K::kAngle, l, m, 1},        {K::kRadius, c, 0, 1},
      {K::kRadius, arc, 0, 5},
  };
  for (const auto& d : valid) {
    const auto id = s.add_dimension(d.kind, d.a, d.b, d.v);
    ASSERT_TRUE(id) << static_cast<int>(d.kind);
    EXPECT_EQ(s.constraint(*id)->value, d.v);
  }
  const EntityId length = *s.add_dimension(K::kLength, l, 0, 12);
  const Sketch with = s;
  EXPECT_FALSE(s.set_dimension(length, 0));
  EXPECT_FALSE(s.set_dimension(length, kNaN));
  EXPECT_FALSE(s.set_dimension(9999, 3));
  const EntityId fix = *s.add_constraint(K::kFix, p);
  EXPECT_FALSE(s.set_dimension(fix, 3)) << "not a dimension";
  ASSERT_TRUE(s.erase(fix));
  EXPECT_TRUE(s == with);
  EXPECT_TRUE(s.set_dimension(length, 20));
  EXPECT_EQ(s.constraint(length)->value, 20);
}

TEST(Dimension, PointLineDistanceStoresPointFirstAndSide) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 0});
  const EntityId above = *s.create_point({5, 3});
  const EntityId below = *s.create_point({5, -3});
  const EntityId a = *s.add_dimension(K::kDistance, l, above, 3);
  const EntityId b = *s.add_dimension(K::kDistance, below, l, 3);
  EXPECT_EQ(s.constraint(a)->first, above);
  EXPECT_EQ(s.constraint(a)->second, l);
  EXPECT_EQ(s.constraint(a)->side, 1);
  EXPECT_EQ(s.constraint(b)->side, -1);
}

TEST(Dimension, MeasureReturnsCurrentValues) {
  Sketch s;
  const EntityId p = *s.create_point({0, 0}), q = *s.create_point({3, 4});
  const EntityId l = line(s, {0, 0}, {10, 0});
  const EntityId m = line(s, {0, 0}, {-5, 5});
  const EntityId c = *s.create_circle(p, 2.5);
  EXPECT_NEAR(*measure(s, K::kLength, l), 10, 1e-12);
  EXPECT_NEAR(*measure(s, K::kDistance, p, q), 5, 1e-12);
  EXPECT_NEAR(*measure(s, K::kDistance, q, l), 4, 1e-12);
  EXPECT_NEAR(*measure(s, K::kAngle, l, m), 3 * kPi / 4, 1e-12);
  EXPECT_NEAR(*measure(s, K::kAngle, m, l), kPi / 4, 1e-12);
  EXPECT_NEAR(*measure(s, K::kRadius, c), 2.5, 1e-12);
  EXPECT_FALSE(measure(s, K::kLength, c));
  EXPECT_FALSE(measure(s, K::kRadius, l));
  EXPECT_FALSE(measure(s, K::kFix, p));
  EXPECT_FALSE(measure(s, K::kDistance, p, 9999));
}

TEST_F(Dimensions, LengthDrivesGeometry) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 0});
  open(s);
  const EntityId id = *add(K::kLength, l, 0, *measure(doc.sketch(), K::kLength, l));
  drives(id, 25, kLengthTolerance,
         [&](const Sketch& sk) { return line_length(sk, l); });
  Sketch fixed = doc.sketch();
  pin(fixed, l);
  open(fixed);
  rejects(id, 25);
}

TEST_F(Dimensions, PointDistanceDrivesGeometry) {
  Sketch s;
  const EntityId p = *s.create_point({0, 0}), q = *s.create_point({3, 4});
  open(s);
  const EntityId id = *add(K::kDistance, p, q, 5);
  drives(id, 12, kLengthTolerance,
         [&](const Sketch& sk) { return length(at(sk, p), at(sk, q)); });
  Sketch fixed = doc.sketch();
  ASSERT_TRUE(fixed.add_constraint(K::kFix, p));
  ASSERT_TRUE(fixed.add_constraint(K::kFix, q));
  open(fixed);
  rejects(id, 12);
}

TEST_F(Dimensions, PointLineDistanceDrivesGeometryIncludingZero) {
  Sketch s;
  const EntityId l = line(s, {0, 0}, {10, 0});
  const EntityId p = *s.create_point({5, 3});
  open(s);
  const EntityId id = *add(K::kDistance, p, l, 3);
  const auto oracle = [&](const Sketch& sk) { return point_line(sk, p, l); };
  drives(id, 7, kLengthTolerance, oracle);
  drives(id, 0, kLengthTolerance, oracle);
  // The point stays on its side of the line.
  ASSERT_TRUE(change(id, 9));
  EXPECT_GT(at(doc.sketch(), p).y, 0);
  Sketch fixed = doc.sketch();
  pin(fixed, l);
  ASSERT_TRUE(fixed.add_constraint(K::kFix, p));
  open(fixed);
  rejects(id, 2);
}

TEST_F(Dimensions, AngleDrivesGeometryFromAnyStart) {
  Sketch s;
  const EntityId a = line(s, {0, 0}, {10, 0});
  const EntityId b = line(s, {0, 5}, {10, 5});  // Parallel start.
  const EntityId r = line(s, {20, 0}, {10, -6});  // Reversed direction.
  open(s);
  const auto between = [&](EntityId x, EntityId y) {
    return [=](const Sketch& sk) { return angle(sk, x, y); };
  };
  // 90 degrees from parallel starts exactly on the residual's branch cut.
  const EntityId right = *add(K::kAngle, a, b, kPi / 2);
  EXPECT_NEAR(angle(doc.sketch(), a, b), kPi / 2, kAngleTolerance);
  drives(right, kPi / 6, kAngleTolerance, between(a, b));
  const EntityId oblique = *add(K::kAngle, a, r, 2 * kPi / 3);
  drives(oblique, kPi / 4, kAngleTolerance, between(a, r));

  Sketch fixed = doc.sketch();
  pin(fixed, a);
  pin(fixed, b);
  open(fixed);
  rejects(right, kPi / 3);
}

TEST_F(Dimensions, RadiusDrivesCirclesAndArcs) {
  Sketch s;
  const EntityId c = *s.create_circle(*s.create_point({0, 0}), 5);
  const EntityId arc = *s.create_arc(*s.create_point({20, 0}), 3, 0, 2);
  open(s);
  const EntityId on_circle = *add(K::kRadius, c, 0, 5);
  const EntityId on_arc = *add(K::kRadius, arc, 0, 3);
  drives(on_circle, 12, kLengthTolerance,
         [&](const Sketch& sk) { return radius(sk, c); });
  drives(on_arc, 0.5, kLengthTolerance,
         [&](const Sketch& sk) { return radius(sk, arc); });
  // Equal radii make the two radius dimensions contradict.
  Sketch tied = doc.sketch();
  ASSERT_TRUE(tied.add_constraint(K::kEqual, c, arc));
  tied.set_dimension(on_arc, 5);
  ASSERT_EQ(solve(tied).status, SolveStatus::kSolved);
  open(tied);
  rejects(on_circle, 7);
}

TEST_F(Dimensions, UndefinedGeometryIsInvalidInput) {
  Sketch s;
  const EntityId collapsed = line(s, {5, 5}, {5, 5});
  ASSERT_TRUE(s.add_dimension(K::kLength, collapsed, 0, 3));
  EXPECT_EQ(solve(s).status, SolveStatus::kInvalidInput);
  Sketch t;
  const EntityId p = *t.create_point({1, 1}), q = *t.create_point({1, 1});
  ASSERT_TRUE(t.add_dimension(K::kDistance, p, q, 3));
  EXPECT_EQ(solve(t).status, SolveStatus::kInvalidInput);
}

TEST_F(Dimensions, PrototypeRectangleScenario) {
  // Issue #1 acceptance, now on the general solver.
  Sketch s;
  const Polyline rect = *s.create_rectangle({3, 2}, 10, 10);
  ASSERT_TRUE(s.add_constraint(K::kHorizontal, rect.lines[0]));
  ASSERT_TRUE(s.add_constraint(K::kVertical, rect.lines[1]));
  ASSERT_TRUE(s.add_constraint(K::kHorizontal, rect.lines[2]));
  ASSERT_TRUE(s.add_constraint(K::kVertical, rect.lines[3]));
  open(s);
  const EntityId width = *add(K::kLength, rect.lines[0], 0, 50);
  const EntityId height = *add(K::kLength, rect.lines[1], 0, 30);
  ASSERT_TRUE(doc.execute("Anchor", [&](Sketch& sk) {
    return sk.update_point(rect.points[0], {0, 0}) &&
           sk.add_constraint(K::kFix, rect.points[0]).has_value();
  }));
  const auto corner = [&](int i) { return at(doc.sketch(), rect.points[i]); };
  EXPECT_NEAR(corner(0).x, 0, kLengthTolerance);
  EXPECT_NEAR(corner(0).y, 0, kLengthTolerance);
  EXPECT_NEAR(std::abs(corner(2).x), 50, kLengthTolerance);
  EXPECT_NEAR(std::abs(corner(2).y), 30, kLengthTolerance);
  EXPECT_NEAR(line_length(doc.sketch(), rect.lines[1]), 30, kLengthTolerance);

  ASSERT_TRUE(change(width, 70));
  EXPECT_NEAR(line_length(doc.sketch(), rect.lines[0]), 70, kLengthTolerance);
  EXPECT_NEAR(line_length(doc.sketch(), rect.lines[2]), 70, kLengthTolerance);

  const Sketch before = doc.sketch();
  EXPECT_FALSE(add(K::kLength, rect.lines[0], 0, 60)) << "contradictory width";
  EXPECT_TRUE(doc.sketch() == before);
  EXPECT_FALSE(change(height, std::numeric_limits<double>::quiet_NaN()));
  EXPECT_TRUE(doc.sketch() == before);
}

// U09: manual placement (dimension-placement.adoc).
TEST(Dimension, PlacementIsValidatedAtomicallyAndPartOfEquality) {
  Sketch s;
  const EntityId p = *s.create_point({0, 0}), q = *s.create_point({10, 0});
  const EntityId l = *s.create_line(p, q);
  const EntityId other = *s.create_point({5, 5});
  const EntityId c = *s.create_circle(*s.create_point({50, 0}), 5);
  const EntityId l2 = *s.create_line(*s.create_point({0, 20}), *s.create_point({5, 30}));
  const EntityId length = *s.add_dimension(K::kLength, l, 0, 10);
  const EntityId pp = *s.add_dimension(K::kDistance, p, other, std::hypot(5, 5));
  const EntityId pl = *s.add_dimension(K::kDistance, other, l, 5);
  const EntityId radius = *s.add_dimension(K::kRadius, c, 0, 5);
  const EntityId angle = *s.add_dimension(K::kAngle, l, l2, *measure(s, K::kAngle, l, l2));
  const EntityId axis = *s.add_constraint(K::kHorizontal, l);

  const Sketch before = s;
  for (const auto& [id, placement] :
       std::vector<std::pair<EntityId, DimensionPlacement>>{
           {pl, {3, 0.5, 0}},         // Point-line distances stay automatic.
           {axis, {3, 0.5, 0}},       // Not a dimension.
           {999, {3, 0.5, 0}},        // Unknown.
           {length, {kNaN, 0.5, 0}},
           {length, {3, 1.5, 0}},
           {length, {3, -0.1, 0}},
           {radius, {0, 0.5, 0}},
           {radius, {3, 0.5, kInf}},
           {angle, {-1, 0.5, 0}}}) {
    EXPECT_FALSE(s.set_dimension_placement(id, placement)) << id;
    EXPECT_TRUE(s == before) << id;
  }
  ASSERT_TRUE(s.set_dimension_placement(length, DimensionPlacement{-6, 0.25, 0}));
  EXPECT_FALSE(s == before);
  EXPECT_EQ(s.constraint(length)->placement, (DimensionPlacement{-6, 0.25, 0}));
  EXPECT_TRUE(s.set_dimension_placement(pp, DimensionPlacement{4, 0, 0}));
  EXPECT_TRUE(s.set_dimension_placement(radius, DimensionPlacement{9, 0.5, 1}));
  EXPECT_TRUE(s.set_dimension_placement(angle, DimensionPlacement{12, 0.5, 0}));
  // Placement never changes the solve.
  Sketch solved = s;
  EXPECT_EQ(solve(solved).status, SolveStatus::kSolved);
  EXPECT_EQ(solved.constraint(length)->placement, s.constraint(length)->placement);
  EXPECT_TRUE(s.set_dimension_placement(length, std::nullopt));
  EXPECT_FALSE(s.constraint(length)->placement);
}

// U10 (reference-dimensions.adoc).
TEST(ReferenceDimension, KindsAreValidatedAndValueIsNotAnInput) {
  Sketch s;
  const EntityId a = *s.create_point({0, 0}), b = *s.create_point({30, 40});
  const EntityId l = *s.create_line(a, b);
  const EntityId other = *s.create_line(*s.create_point({0, 10}),
                                        *s.create_point({10, 20}));
  const EntityId c = *s.create_circle(a, 5);
  const EntityId length = *s.add_dimension(K::kLength, l, 0, 50, true);
  EXPECT_TRUE(s.constraint(length)->reference);
  EXPECT_TRUE(s.add_dimension(K::kDistance, a, b, 50, true));
  EXPECT_TRUE(s.add_dimension(K::kRadius, c, 0, 5, true));
  const Sketch before = s;
  EXPECT_FALSE(s.add_dimension(K::kDistance, a, other, 5, true));
  EXPECT_FALSE(s.add_dimension(K::kAngle, l, other, 0.3, true));
  EXPECT_FALSE(s.set_dimension(length, 60));
  EXPECT_TRUE(s == before);
  EXPECT_TRUE(s.set_dimension_placement(length, DimensionPlacement{5, 0.5, 0}));
  // Driving dimensions are unchanged.
  EXPECT_FALSE(s.constraint(*s.add_dimension(K::kLength, other, 0, 14))->reference);
}

TEST(ReferenceDimension, IsNotSolverInput) {
  Sketch s;
  const EntityId l = *s.create_line(*s.create_point({0, 0}),
                                    *s.create_point({50, 0}));
  ASSERT_TRUE(s.add_dimension(K::kLength, l, 0, 50));
  const int dof = *diagnose(s).dof;
  // A reference contradicting the driving length changes nothing.
  ASSERT_TRUE(s.add_dimension(K::kLength, l, 0, 70, true));
  const Diagnosis d = diagnose(s);
  EXPECT_EQ(d.status, DiagnosisStatus::kConsistent);
  EXPECT_EQ(*d.dof, dof);
  EXPECT_TRUE(d.dependent.empty());
  const Sketch before = s;
  EXPECT_EQ(solve(s).status, SolveStatus::kSolved);
  EXPECT_TRUE(s == before);
}

TEST(ReferenceDimension, OnlyReferencesLeaveEveryFreedom) {
  Sketch s;
  const EntityId l = *s.create_line(*s.create_point({0, 0}),
                                    *s.create_point({50, 0}));
  ASSERT_TRUE(s.add_dimension(K::kLength, l, 0, 50, true));
  const Diagnosis d = diagnose(s);
  EXPECT_EQ(d.status, DiagnosisStatus::kConsistent);
  EXPECT_EQ(d.dof, 4);
  EXPECT_EQ(solve(s).status, SolveStatus::kSolved);
}
