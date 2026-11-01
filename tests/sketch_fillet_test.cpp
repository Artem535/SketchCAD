#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>

#include "sketchcad/diagnostics.h"
#include "sketchcad/project_io.h"
#include "sketchcad/sketch_edit.h"
#include "sketchcad/solver.h"

using namespace sketchcad;
namespace {
Position point(const Sketch& s, EntityId id) {
  return std::get<SketchPoint>(*s.entity(id)).position;
}
SketchArc arc(const Sketch& s) {
  for (const auto& [id, e] : s.entities())
    if (const auto* a = std::get_if<SketchArc>(&e)) return *a;
  ADD_FAILURE() << "No fillet arc";
  return {};
}
void contacts_are_connected(const Sketch& s, int expected = 2) {
  int contacts = 0;
  for (const auto& [id, k] : s.constraints()) {
    if (k.kind != ConstraintKind::kTangentStart &&
        k.kind != ConstraintKind::kTangentEnd)
      continue;
    const auto a = std::get<SketchArc>(*s.entity(k.second));
    const auto c = point(s, a.center);
    const auto l = std::get<SketchLine>(*s.entity(k.first));
    const auto p = point(s, k.side == 1 ? l.start : l.end);
    const auto x = point(s, l.start), y = point(s, l.end);
    const double angle =
        a.start_angle +
        (k.kind == ConstraintKind::kTangentEnd ? a.sweep_angle : 0);
    EXPECT_NEAR(p.x, c.x + a.radius * std::cos(angle), 1e-6);
    EXPECT_NEAR(p.y, c.y + a.radius * std::sin(angle), 1e-6);
    EXPECT_NEAR(((p.x - c.x) * (y.x - x.x) + (p.y - c.y) * (y.y - x.y)) /
                    std::hypot(y.x - x.x, y.y - x.y),
                0, 1e-6);
    ++contacts;
  }
  EXPECT_EQ(contacts, expected);
}
}  // namespace

TEST(Fillet, PreviewAndGeometryAtAcuteRightAndObtuseCorners) {
  for (const double degrees : {60., 90., 120.}) {
    Sketch s;
    const double theta = degrees * std::numbers::pi / 180;
    const auto p = *s.create_point({0, 0});
    const auto a = *s.create_point({20, 0});
    const auto b =
        *s.create_point({20 * std::cos(theta), 20 * std::sin(theta)});
    const auto l1 = *s.create_line(p, a), l2 = *s.create_line(p, b);
    const auto dof = diagnose(s).dof;
    const auto before = s;
    ASSERT_EQ(fillet_status(s, p, 2), FilletStatus::kReady);
    const auto preview = fillet_preview(s, p, 2);
    ASSERT_TRUE(preview);
    EXPECT_TRUE(preview->is_arc);
    EXPECT_TRUE(s == before);
    EXPECT_NEAR(preview->sweep, std::numbers::pi - theta, 1e-10);
    ASSERT_TRUE(fillet(s, p, 2));
    ASSERT_EQ(solve(s).status, SolveStatus::kSolved);
    EXPECT_EQ(diagnose(s).dof, dof);
    const auto actual = arc(s);
    EXPECT_NEAR(actual.radius, 2, 1e-10);
    EXPECT_NEAR(actual.sweep_angle, preview->sweep, 1e-8);
    EXPECT_TRUE(std::get<SketchPoint>(*s.entity(p)).construction);
    EXPECT_TRUE(s.entity(l1));
    EXPECT_TRUE(s.entity(l2));
    contacts_are_connected(s);
  }
}

TEST(Fillet, OverallSizesAndRadiusRemainEditableWithoutOpeningTheContour) {
  Sketch s;
  const auto rect = *s.create_rectangle({0, 0}, 20, 10);
  s.add_constraint(ConstraintKind::kFix, rect.points[0]);
  for (int i = 0; i < 4; ++i)
    s.add_constraint(
        i % 2 ? ConstraintKind::kVertical : ConstraintKind::kHorizontal,
        rect.lines[i]);
  const auto width =
      *s.add_dimension(ConstraintKind::kLength, rect.lines[0], 0, 20);
  const auto height =
      *s.add_dimension(ConstraintKind::kLength, rect.lines[1], 0, 10);
  ASSERT_EQ(solve(s).status, SolveStatus::kSolved);
  const auto dof = diagnose(s).dof;
  ASSERT_TRUE(fillet(s, rect.points[1], 2));
  ASSERT_EQ(solve(s).status, SolveStatus::kSolved);
  EXPECT_EQ(diagnose(s).dof, dof);
  EXPECT_EQ(s.constraint(width)->kind, ConstraintKind::kDistance);
  EXPECT_DOUBLE_EQ(s.constraint(width)->value, 20);
  EXPECT_EQ(s.constraint(height)->kind, ConstraintKind::kDistance);
  EntityId radius = 0;
  for (const auto& [id, c] : s.constraints())
    if (c.kind == ConstraintKind::kRadius) radius = id;
  ASSERT_NE(radius, 0u);
  ASSERT_TRUE(s.set_dimension(radius, 3));
  ASSERT_EQ(solve(s).status, SolveStatus::kSolved);
  contacts_are_connected(s);
  EXPECT_NEAR(arc(s).radius, 3, 1e-6);
  ASSERT_TRUE(s.set_dimension(width, 24));
  ASSERT_EQ(solve(s).status, SolveStatus::kSolved);
  contacts_are_connected(s);
  EXPECT_NEAR(*measure(s, s.constraint(width)->kind, s.constraint(width)->first,
                       s.constraint(width)->second),
              24, 1e-6);
  ASSERT_TRUE(s.set_dimension(radius, 100));
  const auto oversized_input = s;
  EXPECT_NE(solve(s).status, SolveStatus::kSolved);
  EXPECT_TRUE(s == oversized_input);
}

TEST(Fillet, InvalidInputsAndExhaustedAllocatorAreAtomic) {
  Sketch s;
  const auto rect = *s.create_rectangle({0, 0}, 20, 10);
  const auto before = s;
  for (const double radius :
       {0., -1., 10., 50., std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity()}) {
    EXPECT_FALSE(fillet(s, rect.points[0], radius));
    EXPECT_FALSE(fillet_preview(s, rect.points[0], radius));
    EXPECT_TRUE(s == before);
  }
  auto snapshot = s.snapshot();
  snapshot.next_id = std::numeric_limits<EntityId>::max();
  auto exhausted = Sketch::from_snapshot(snapshot);
  ASSERT_TRUE(exhausted);
  const auto exhausted_before = *exhausted;
  EXPECT_FALSE(fillet(*exhausted, rect.points[0], 2));
  EXPECT_TRUE(*exhausted == exhausted_before);
  EXPECT_EQ(exhausted->snapshot().next_id, snapshot.next_id);
  s.add_constraint(ConstraintKind::kEqual, rect.lines[0], rect.lines[2]);
  const auto equal_before = s;
  EXPECT_EQ(fillet_status(s, rect.points[0], 2), FilletStatus::kEqualLength);
  EXPECT_FALSE(fillet(s, rect.points[0], 2));
  EXPECT_TRUE(s == equal_before);
}

TEST(Fillet, ConstructionAndUnsupportedTopology) {
  Sketch s;
  const auto rect = *s.create_rectangle({0, 0}, 20, 10, true);
  ASSERT_TRUE(fillet(s, rect.points[0], 2));
  EXPECT_TRUE(arc(s).construction);
  Sketch t;
  const auto p = *t.create_point({0, 0});
  t.create_line(p, *t.create_point({10, 0}));
  t.create_line(p, *t.create_point({0, 10}));
  t.create_line(p, *t.create_point({-10, 0}));
  const auto before = t;
  EXPECT_EQ(fillet_status(t, p, 2), FilletStatus::kNotCorner);
  EXPECT_FALSE(fillet(t, p, 2));
  EXPECT_TRUE(t == before);
}

TEST(Fillet, RejectsCornerAlreadyUsedAsAnArcContact) {
  Sketch s;
  const auto rect = *s.create_rectangle({0, 0}, 20, 10);
  ASSERT_TRUE(fillet(s, rect.points[0], 2));
  EntityId contact = 0;
  for (const auto& [id, c] : s.constraints()) {
    if (c.kind == ConstraintKind::kTangentStart) {
      const auto l = std::get<SketchLine>(*s.entity(c.first));
      contact = c.side == 1 ? l.start : l.end;
    }
  }
  ASSERT_NE(contact, 0u);
  const auto p = point(s, contact);
  s.create_line(contact, *s.create_point({p.x + 5, p.y + 5}));
  const auto before = s;
  EXPECT_EQ(fillet_status(s, contact, 0.5), FilletStatus::kNotCorner);
  EXPECT_FALSE(fillet(s, contact, 0.5));
  EXPECT_TRUE(s == before);
}

TEST(Fillet, SequentialRectangleCornersKeepAllContactsConnected) {
  Sketch s;
  const auto rect = *s.create_rectangle({0, 0}, 20, 10);
  for (const auto corner : rect.points) {
    ASSERT_TRUE(fillet(s, corner, 2));
    ASSERT_EQ(solve(s).status, SolveStatus::kSolved);
  }
  contacts_are_connected(s, 8);
}

TEST(Fillet, OrientedTangencyCannotEscapeThroughAMajorArcBranch) {
  Sketch s;
  const auto rect = *s.create_rectangle({0, 0}, 20, 10);
  ASSERT_TRUE(fillet(s, rect.points[1], 2));
  const auto a = arc(s);
  for (const auto& [id, c] : s.constraints()) {
    if (c.kind == ConstraintKind::kRadius) s.set_dimension(id, 12);
    if (c.kind != ConstraintKind::kTangentStart &&
        c.kind != ConstraintKind::kTangentEnd) continue;
    const auto l = std::get<SketchLine>(*s.entity(c.first));
    s.update_point(c.side == 1 ? l.start : l.end,
                   c.first == rect.lines[0] ? Position{32, 0} : Position{20, 12});
  }
  s.update_point(a.center, {32, 12});
  s.update_arc(a.id, a.center, 12, a.start_angle, a.sweep_angle);
  const auto before = s;
  EXPECT_NE(check_constraints(s).status, SolveStatus::kSolved);
  EXPECT_NE(solve(s).status, SolveStatus::kSolved);
  EXPECT_TRUE(s == before);
}

TEST(Fillet, ExtendAndTrimRespectBoundArcContacts) {
  Sketch s;
  const auto rect = *s.create_rectangle({0, 0}, 20, 10);
  ASSERT_TRUE(fillet(s, rect.points[1], 2));
  const auto a = arc(s);
  // Add a line crossing the fillet circle outside the minor arc so extend on
  // the arc has a candidate boundary ahead of both ends.
  Sketch with_boundary = s;
  with_boundary.create_line(*with_boundary.create_point({15, 2.5}),
                            *with_boundary.create_point({21, 2.5}));
  const auto before = with_boundary;
  // Bound line ends and bound arc ends are not free ends for extend.
  EXPECT_FALSE(extend_preview(with_boundary, rect.lines[0], {17.5, 0}));
  EXPECT_FALSE(extend(with_boundary, rect.lines[0], {17.5, 0}));
  EXPECT_FALSE(extend_preview(with_boundary, rect.lines[1], {20, 2.5}));
  EXPECT_FALSE(extend(with_boundary, rect.lines[1], {20, 2.5}));
  EXPECT_FALSE(extend_preview(with_boundary, a.id, {18.2, 0.1}));
  EXPECT_FALSE(extend(with_boundary, a.id, {18.2, 0.1}));
  EXPECT_FALSE(extend_preview(with_boundary, a.id, {19.9, 1.8}));
  EXPECT_FALSE(extend(with_boundary, a.id, {19.9, 1.8}));
  EXPECT_TRUE(with_boundary == before);

  // Splitting a filleted side in the middle keeps the contact connected on the
  // segment that retains the filleted endpoint.
  Sketch split_mid = s;
  const auto c1_lo = *split_mid.create_point({6, -5});
  const auto c1_hi = *split_mid.create_point({6, 5});
  split_mid.add_constraint(ConstraintKind::kFix, c1_lo);
  split_mid.add_constraint(ConstraintKind::kFix, c1_hi);
  split_mid.create_line(c1_lo, c1_hi);
  split_mid.create_line(*split_mid.create_point({12, -5}),
                        *split_mid.create_point({12, 5}));
  ASSERT_TRUE(trim(split_mid, rect.lines[0], {9, 0}));
  ASSERT_EQ(solve(split_mid).status, SolveStatus::kSolved);
  EXPECT_NEAR(
      point(split_mid, std::get<SketchLine>(*split_mid.entity(rect.lines[0])).end).x,
      6, 1e-6);
  contacts_are_connected(split_mid, 2);
  EXPECT_TRUE(encode_project({split_mid, 1}));

  // Trimming away the filleted end of a side detaches that tangency cleanly.
  Sketch trim_line_end = s;
  trim_line_end.create_line(*trim_line_end.create_point({12, -5}),
                            *trim_line_end.create_point({12, 5}));
  ASSERT_TRUE(trim(trim_line_end, rect.lines[0], {16, 0}));
  ASSERT_EQ(solve(trim_line_end).status, SolveStatus::kSolved);
  contacts_are_connected(trim_line_end, 1);
  EXPECT_TRUE(encode_project({trim_line_end, 1}));

  // Trimming a bound end of the fillet arc detaches that end's tangency so
  // solving and saving preserve the trimmed arc.
  Sketch trim_arc_end = s;
  trim_arc_end.create_line(*trim_arc_end.create_point({18, 2}),
                           *trim_arc_end.create_point({22, -2}));
  ASSERT_TRUE(trim(trim_arc_end, a.id, {18.2, 0.05}));
  ASSERT_EQ(solve(trim_arc_end).status, SolveStatus::kSolved);
  EXPECT_NEAR(arc(trim_arc_end).start_angle, -std::numbers::pi / 4, 1e-6);
  contacts_are_connected(trim_arc_end, 1);
  EXPECT_TRUE(encode_project({trim_arc_end, 1}));

  // Splitting the fillet arc in the middle keeps both tangencies attached to
  // their respective outer arc pieces.
  Sketch split_arc_mid = s;
  split_arc_mid.create_line(*split_arc_mid.create_point({17.5, 2.5}),
                            *split_arc_mid.create_point({19.5, -1.5}));
  split_arc_mid.create_line(*split_arc_mid.create_point({17.5, 2.5}),
                            *split_arc_mid.create_point({21.5, 0.5}));
  ASSERT_TRUE(trim(split_arc_mid, a.id, {19.41, 0.59}));
  ASSERT_EQ(solve(split_arc_mid).status, SolveStatus::kSolved);
  contacts_are_connected(split_arc_mid, 2);
  EXPECT_TRUE(encode_project({split_arc_mid, 1}));
}
