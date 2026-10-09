#include "sketchcad/sketch_tool.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <numbers>

#include "sketchcad/diagnostics.h"
#include "sketchcad/dimension_layout.h"
#include "sketchcad/solver.h"
using namespace sketchcad;
namespace {
constexpr double kPi = std::numbers::pi;
template <typename T>
std::size_t count(const Sketch& s) {
  std::size_t n = 0;
  for (const auto& [id, e] : s.entities()) n += std::holds_alternative<T>(e);
  return n;
}
template <typename T>
T only(const Sketch& s) {
  for (const auto& [id, e] : s.entities())
    if (std::holds_alternative<T>(e)) return std::get<T>(e);
  ADD_FAILURE() << "entity type not found";
  return {};
}
EntityId only_line_id(const Sketch& s) {
  for (const auto& [id, e] : s.entities())
    if (std::holds_alternative<SketchLine>(e)) return id;
  return 0;
}
Position point_at(const Sketch& s, EntityId id) {
  return std::get<SketchPoint>(s.entity(id).value()).position;
}
class Tools : public ::testing::Test {
 protected:
  // U01-U03 cases predate automatic dimensions (U08) and run without them.
  Tools() {
    session.set_snap({false, 1, 1});
    session.set_pick_tolerance(1);
    session.set_auto_dimensions(false);
  }
  void snapping() { session.set_snap({true, 1, 1}); }
  const Sketch& sketch() const { return doc.sketch(); }
  Document doc;
  ToolSession session{doc};
};
}  // namespace
TEST_F(Tools, LineIsOneCommandAndPreviewOnlyWhileDrawing) {
  session.set_tool(Tool::kLine);
  session.press({0, 0});
  session.hover({5, 5});
  EXPECT_TRUE(session.in_progress());
  EXPECT_TRUE(sketch().entities().empty());
  EXPECT_EQ(count<SketchLine>(session.preview()), 1u);
  session.press({10, 0});
  EXPECT_EQ(count<SketchPoint>(sketch()), 2u);
  EXPECT_EQ(count<SketchLine>(sketch()), 1u);
  EXPECT_EQ(doc.undo_label(), "Line");
  EXPECT_FALSE(session.in_progress());
  EXPECT_TRUE(session.preview().entities().empty());
  EXPECT_EQ(session.tool(), Tool::kLine);
}
TEST_F(Tools, LineReusesSnappedPointAsSharedVertex) {
  snapping();
  session.set_tool(Tool::kLine);
  session.press({0, 0});
  session.press({10, 0});
  const SketchLine first = only<SketchLine>(sketch());
  session.hover({10.3, 0.2});
  ASSERT_TRUE(session.last_snap());
  EXPECT_EQ(session.last_snap()->kind, SnapKind::kPoint);
  session.press({10.3, 0.2});
  session.press({10, 10});
  EXPECT_EQ(count<SketchPoint>(sketch()), 3u);
  EXPECT_EQ(count<SketchLine>(sketch()), 2u);
  for (const auto& [id, e] : sketch().entities()) {
    if (auto* l = std::get_if<SketchLine>(&e); l && l->id != first.id) {
      EXPECT_EQ(l->start, first.end);
    }
  }
}
TEST_F(Tools, ZeroLengthLineIsIgnored) {
  session.set_tool(Tool::kLine);
  session.press({0, 0});
  session.press({0, 0});
  EXPECT_TRUE(sketch().entities().empty());
  EXPECT_TRUE(session.in_progress());
}
TEST_F(Tools, OpenPolylineFinishesAsOneCommand) {
  session.set_tool(Tool::kPolyline);
  session.press({0, 0});
  session.press({10, 0});
  session.press({10, 10});
  EXPECT_TRUE(sketch().entities().empty());
  ASSERT_TRUE(session.finish());
  EXPECT_EQ(count<SketchPoint>(sketch()), 3u);
  EXPECT_EQ(count<SketchLine>(sketch()), 2u);
  ASSERT_TRUE(doc.undo());
  EXPECT_TRUE(sketch().entities().empty());
}
TEST_F(Tools, TappingLastVertexAgainFinishesPolyline) {
  session.set_tool(Tool::kPolyline);
  session.press({0, 0});
  session.press({10, 0});
  session.press({10, 0});
  EXPECT_FALSE(session.in_progress());
  EXPECT_EQ(count<SketchLine>(sketch()), 1u);
}
TEST_F(Tools, TappingFirstVertexClosesPolyline) {
  snapping();
  session.set_tool(Tool::kPolyline);
  session.press({0, 0});
  session.press({10, 0});
  session.press({10, 10});
  session.press({0.2, 0.1});
  EXPECT_FALSE(session.in_progress());
  EXPECT_EQ(count<SketchPoint>(sketch()), 3u);
  EXPECT_EQ(count<SketchLine>(sketch()), 3u);
}
TEST_F(Tools, PolylineNeedsTwoVerticesToFinish) {
  session.set_tool(Tool::kPolyline);
  EXPECT_FALSE(session.finish());
  session.press({0, 0});
  EXPECT_FALSE(session.finish());
  EXPECT_TRUE(session.in_progress());
}
TEST_F(Tools, RectangleFromTwoCornersIgnoresDegenerateInput) {
  session.set_tool(Tool::kRectangle);
  session.press({0, 0});
  session.press({20, 0});
  EXPECT_TRUE(sketch().entities().empty());
  session.press({20, 10});
  EXPECT_EQ(count<SketchPoint>(sketch()), 4u);
  EXPECT_EQ(count<SketchLine>(sketch()), 4u);
  EXPECT_EQ(doc.undo_label(), "Rectangle");
}
TEST_F(Tools, CircleFromCenterAndRim) {
  session.set_tool(Tool::kCircle);
  session.press({5, 5});
  session.press({5, 5});
  EXPECT_TRUE(sketch().entities().empty());
  session.press({8, 9});
  const SketchCircle c = only<SketchCircle>(sketch());
  EXPECT_DOUBLE_EQ(c.radius, 5);
  EXPECT_EQ(point_at(sketch(), c.center), (Position{5, 5}));
}
TEST_F(Tools, ArcRunsCounterClockwiseFromStartToEnd) {
  session.set_tool(Tool::kArc);
  session.press({0, 0});
  session.press({10, 0});
  session.press({0, 5});
  SketchArc a = only<SketchArc>(sketch());
  EXPECT_DOUBLE_EQ(a.radius, 10);
  EXPECT_NEAR(a.start_angle, 0, 1e-12);
  EXPECT_NEAR(a.sweep_angle, kPi / 2, 1e-12);
  ASSERT_TRUE(doc.undo());
  session.press({0, 0});
  session.press({0, 10});
  session.press({10, 0});
  a = only<SketchArc>(sketch());
  EXPECT_NEAR(a.start_angle, kPi / 2, 1e-12);
  EXPECT_NEAR(a.sweep_angle, 3 * kPi / 2, 1e-12);
}
TEST_F(Tools, CancelAndToolSwitchDiscardTheShape) {
  session.set_tool(Tool::kLine);
  session.press({0, 0});
  ASSERT_TRUE(session.cancel());
  EXPECT_FALSE(session.in_progress());
  EXPECT_TRUE(session.preview().entities().empty());
  EXPECT_FALSE(session.cancel());
  session.press({0, 0});
  session.set_tool(Tool::kCircle);
  EXPECT_FALSE(session.in_progress());
  EXPECT_TRUE(sketch().entities().empty());
  EXPECT_FALSE(doc.can_undo());
}
TEST_F(Tools, SelectDeleteRemovesCurveWithUnsharedPoints) {
  session.set_tool(Tool::kLine);
  session.press({0, 0});
  session.press({10, 0});
  const EntityId line = only<SketchLine>(sketch()).id;
  session.set_tool(Tool::kSelect);
  session.press({5, 0.2});
  session.release({5, 0.2});
  EXPECT_EQ(session.selection(), line);
  EXPECT_EQ(session.delete_selection(), DeleteResult::kDeleted);
  EXPECT_TRUE(sketch().entities().empty());
  EXPECT_FALSE(session.selection());
  ASSERT_TRUE(doc.undo());
  EXPECT_EQ(sketch().entities().size(), 3u);
  EXPECT_EQ(session.delete_selection(), DeleteResult::kNothingSelected);
}
TEST_F(Tools, DeleteKeepsSharedPointAndRejectsReferencedPoint) {
  snapping();
  session.set_tool(Tool::kLine);
  session.press({0, 0});
  session.press({10, 0});
  session.press({10, 0});
  session.press({10, 10});
  session.set_tool(Tool::kSelect);
  session.press({3, 0});
  session.release({3, 0});
  ASSERT_EQ(session.delete_selection(), DeleteResult::kDeleted);
  EXPECT_EQ(count<SketchPoint>(sketch()), 2u);
  EXPECT_EQ(count<SketchLine>(sketch()), 1u);
  session.press({10, 0});
  session.release({10, 0});
  ASSERT_TRUE(session.selection());
  const auto before = sketch().entities();
  EXPECT_EQ(session.delete_selection(), DeleteResult::kPointInUse);
  EXPECT_EQ(sketch().entities(), before);
}
TEST_F(Tools, PressOnEmptySpaceClearsSelection) {
  session.set_tool(Tool::kLine);
  session.press({0, 0});
  session.press({10, 0});
  session.set_tool(Tool::kSelect);
  session.press({5, 0});
  session.release({5, 0});
  ASSERT_TRUE(session.selection());
  session.press({50, 50});
  session.release({50, 50});
  EXPECT_FALSE(session.selection());
}
TEST_F(Tools, DraggingAPointIsOneUndoStep) {
  session.set_tool(Tool::kLine);
  session.press({0, 0});
  session.press({10, 0});
  const SketchLine line = only<SketchLine>(sketch());
  session.set_tool(Tool::kSelect);
  session.press({10, 0});
  session.drag({12, 1});
  session.drag({15, 2});
  session.release({15, 2});
  EXPECT_EQ(point_at(sketch(), line.end), (Position{15, 2}));
  EXPECT_EQ(only<SketchLine>(sketch()).end, line.end);
  EXPECT_EQ(doc.undo_label(), "Move point");
  ASSERT_TRUE(doc.undo());
  EXPECT_EQ(point_at(sketch(), line.end), (Position{10, 0}));
}
TEST_F(Tools, UndoWhileDrawingCancelsThenUndoes) {
  session.set_tool(Tool::kLine);
  session.press({0, 0});
  session.press({10, 0});
  session.press({20, 20});
  ASSERT_TRUE(session.undo());
  EXPECT_FALSE(session.in_progress());
  EXPECT_TRUE(sketch().entities().empty());
  ASSERT_TRUE(session.redo());
  EXPECT_EQ(count<SketchLine>(sketch()), 1u);
}
TEST_F(Tools, SnappingDisabledKeepsRawPositions) {
  session.set_tool(Tool::kLine);
  session.press({0.123, 0.456});
  session.press({9.87, 0.01});
  const SketchLine l = only<SketchLine>(sketch());
  EXPECT_EQ(point_at(sketch(), l.start), (Position{0.123, 0.456}));
  EXPECT_EQ(point_at(sketch(), l.end), (Position{9.87, 0.01}));
}
TEST_F(Tools, DeleteRemovesConstraintsOfErasedGeometryInOneStep) {
  snapping();  // The second line reuses the first line's end point.
  session.set_tool(Tool::kLine);
  session.press({0, 0});
  session.press({10, 0});
  session.set_tool(Tool::kLine);
  session.press({10, 0});
  session.press({10, 8});
  const SketchLine first = only<SketchLine>(sketch());
  EntityId second = 0, shared = 0;
  for (const auto& [id, e] : sketch().entities())
    if (const auto* l = std::get_if<SketchLine>(&e); l && id != first.id) {
      second = id;
      shared = l->start;
    }
  ASSERT_EQ(shared, first.end);
  ASSERT_TRUE(doc.execute("Constrain", [&](Sketch& s) {
    return s.add_constraint(ConstraintKind::kHorizontal, first.id) &&
           s.add_constraint(ConstraintKind::kFix, first.start) &&
           s.add_dimension(ConstraintKind::kLength, second, 0, 8) &&
           s.add_constraint(ConstraintKind::kFix, shared);
  }));
  const Sketch before = sketch();
  session.set_tool(Tool::kSelect);
  session.press({5, 0});
  session.release({5, 0});
  ASSERT_EQ(session.selection(), first.id);
  EXPECT_EQ(session.delete_selection(), DeleteResult::kDeleted);
  EXPECT_FALSE(sketch().entity(first.id));
  EXPECT_FALSE(sketch().entity(first.start));
  // The shared point stays with its own fix; the other line's length stays.
  EXPECT_TRUE(sketch().entity(shared));
  EXPECT_EQ(sketch().constraints().size(), 2u);
  for (const auto& [id, c] : sketch().constraints())
    EXPECT_NE(c.first, first.id);
  ASSERT_TRUE(session.undo());
  EXPECT_TRUE(sketch() == before);
}
namespace {
// Two horizontal lines 10 mm apart and a free point, drawn with snapping off.
struct ThreeThings {
  EntityId low, high, point;
};
ThreeThings three_things(Document& doc) {
  ThreeThings t{};
  doc.execute("Setup", [&](Sketch& s) {
    t.low = *s.create_line(*s.create_point({0, 0}), *s.create_point({10, 0}));
    t.high = *s.create_line(*s.create_point({0, 10}), *s.create_point({10, 10}));
    t.point = *s.create_point({20, 20});
    return true;
  });
  return t;
}
}  // namespace
TEST_F(Tools, SelectionAddsUpToTwoAndDropsTheOldest) {
  const ThreeThings t = three_things(doc);
  const auto tap = [&](Position p) {
    session.press(p);
    session.release(p);
  };
  tap({5, 0});
  EXPECT_EQ(session.selected(), (std::vector<EntityId>{t.low}));
  tap({5, 10});
  EXPECT_EQ(session.selected(), (std::vector<EntityId>{t.low, t.high}));
  EXPECT_EQ(session.selection(), t.high);
  tap({5, 0});  // Reselecting keeps both and makes it the most recent.
  EXPECT_EQ(session.selected(), (std::vector<EntityId>{t.high, t.low}));
  tap({20, 20});
  EXPECT_EQ(session.selected(), (std::vector<EntityId>{t.low, t.point}));
  EXPECT_EQ(session.selection(), t.point);
  tap({50, 50});
  EXPECT_TRUE(session.selected().empty());
  EXPECT_FALSE(session.selection());
}
TEST_F(Tools, DeletingTwoSelectedLinesIsOneCommand) {
  const ThreeThings t = three_things(doc);
  const Sketch before = sketch();
  for (Position p : {Position{5, 0}, Position{5, 10}}) {
    session.press(p);
    session.release(p);
  }
  EXPECT_EQ(session.delete_selection(), DeleteResult::kDeleted);
  EXPECT_FALSE(sketch().entity(t.low));
  EXPECT_FALSE(sketch().entity(t.high));
  EXPECT_TRUE(sketch().entity(t.point));
  EXPECT_TRUE(session.selected().empty());
  ASSERT_TRUE(session.undo());
  EXPECT_TRUE(sketch() == before);
}
TEST_F(Tools, DeleteFailsAsAWholeWhenOneMemberCannotBeErased) {
  const ThreeThings t = three_things(doc);
  const EntityId end = std::get<SketchLine>(*sketch().entity(t.low)).end;
  const Sketch before = sketch();
  for (Position p : {Position{20, 20}, Position{10, 0}}) {
    session.press(p);
    session.release(p);
  }
  ASSERT_EQ(session.selected(), (std::vector<EntityId>{t.point, end}));
  EXPECT_EQ(session.delete_selection(), DeleteResult::kPointInUse);
  EXPECT_TRUE(sketch() == before);
  EXPECT_EQ(session.selected(), (std::vector<EntityId>{t.point, end}));
}
TEST_F(Tools, ConstrainedDragMovesTheLineAsOneUndoStep) {
  doc.set_commit_step(solver_step());
  session.set_tool(Tool::kLine);
  session.press({0, 0});
  session.press({10, 0});
  const SketchLine line = only<SketchLine>(sketch());
  ASSERT_TRUE(doc.execute("Horizontal", [&](Sketch& s) {
    return s.add_constraint(ConstraintKind::kHorizontal, only_line_id(s))
        .has_value();
  }));
  session.set_tool(Tool::kSelect);
  session.press({0, 0});
  session.drag({0, 3});
  session.drag({0, 6});
  session.release({0, 6});
  EXPECT_NEAR(point_at(sketch(), line.start).y, 6, 1e-3);
  EXPECT_NEAR(point_at(sketch(), line.end).y, 6, 1e-3);
  EXPECT_NEAR(point_at(sketch(), line.end).x, 10, 1e-3);
  EXPECT_EQ(doc.undo_label(), "Move point");
  ASSERT_TRUE(doc.undo());
  EXPECT_EQ(point_at(sketch(), line.start), (Position{0, 0}));
  EXPECT_EQ(point_at(sketch(), line.end), (Position{10, 0}));
  EXPECT_EQ(doc.undo_label(), "Horizontal");
}
TEST_F(Tools, CancelledConstrainedDragRestoresTheStart) {
  doc.set_commit_step(solver_step());
  session.set_tool(Tool::kLine);
  session.press({0, 0});
  session.press({10, 0});
  const SketchLine line = only<SketchLine>(sketch());
  ASSERT_TRUE(doc.execute("Horizontal", [&](Sketch& s) {
    return s.add_constraint(ConstraintKind::kHorizontal, only_line_id(s))
        .has_value();
  }));
  const Sketch before = sketch();
  session.set_tool(Tool::kSelect);
  session.press({0, 0});
  session.drag({0, 6});
  ASSERT_NEAR(point_at(sketch(), line.end).y, 6, 1e-3);
  EXPECT_TRUE(session.cancel());
  session.release({0, 6});
  EXPECT_TRUE(sketch() == before);
  EXPECT_EQ(doc.undo_label(), "Horizontal");
}
TEST_F(Tools, RejectedDragStepKeepsTheLastValidGeometry) {
  doc.set_commit_step(solver_step());
  session.set_tool(Tool::kLine);
  session.press({0, 0});
  session.press({10, 0});
  const SketchLine line = only<SketchLine>(sketch());
  ASSERT_TRUE(doc.execute("Horizontal", [&](Sketch& s) {
    return s.add_constraint(ConstraintKind::kHorizontal, only_line_id(s))
        .has_value();
  }));
  session.set_tool(Tool::kSelect);
  session.press({0, 0});
  session.drag({2, 0});
  session.drag({10, 0});  // Would collapse the line.
  EXPECT_NEAR(point_at(sketch(), line.start).x, 2, 1e-3);
  session.drag({4, 0});
  session.release({4, 0});
  EXPECT_NEAR(point_at(sketch(), line.start).x, 4, 1e-3);
  EXPECT_NEAR(point_at(sketch(), line.end).x, 10, 1e-3);
  EXPECT_NEAR(point_at(sketch(), line.end).y, 0, 1e-3);
}

// U08: automatic dimensions and dynamic input (default-dimensions.adoc).
namespace {
class AutoDims : public Tools {
 protected:
  AutoDims() {
    session.set_auto_dimensions(true);
    doc.set_commit_step(solver_step());
  }
  std::vector<Constraint> of_kind(ConstraintKind kind) const {
    std::vector<Constraint> out;
    for (const auto& [id, c] : sketch().constraints())
      if (c.kind == kind) out.push_back(c);
    return out;
  }
};
constexpr double kNear = 1e-9;
}  // namespace

TEST_F(AutoDims, AutoDimensionsAreOnByDefault) {
  Document d;
  ToolSession fresh{d};
  EXPECT_TRUE(fresh.auto_dimensions());
}

TEST_F(AutoDims, LineGetsItsLengthInTheSameCommand) {
  session.set_tool(Tool::kLine);
  session.press({0, 0});
  session.press({30, 40});
  const auto lengths = of_kind(ConstraintKind::kLength);
  ASSERT_EQ(lengths.size(), 1u);
  EXPECT_NEAR(lengths[0].value, 50, kNear);
  EXPECT_EQ(sketch().constraints().size(), 1u);
  ASSERT_TRUE(doc.undo());
  EXPECT_TRUE(sketch().entities().empty());
  EXPECT_TRUE(sketch().constraints().empty());
}

TEST_F(AutoDims, OffCreatesNoConstraints) {
  session.set_auto_dimensions(false);
  session.set_tool(Tool::kLine);
  session.press({0, 0});
  session.press({30, 40});
  session.set_tool(Tool::kCircle);
  session.press({50, 50});
  session.press({55, 50});
  EXPECT_TRUE(sketch().constraints().empty());
}

TEST_F(AutoDims, RectangleGetsAxesWidthAndHeight) {
  session.set_tool(Tool::kRectangle);
  session.press({0, 0});
  session.press({40, 20});
  EXPECT_EQ(of_kind(ConstraintKind::kHorizontal).size(), 2u);
  EXPECT_EQ(of_kind(ConstraintKind::kVertical).size(), 2u);
  const auto lengths = of_kind(ConstraintKind::kLength);
  ASSERT_EQ(lengths.size(), 2u);
  std::vector<double> values{lengths[0].value, lengths[1].value};
  std::sort(values.begin(), values.end());
  EXPECT_NEAR(values[0], 20, kNear);
  EXPECT_NEAR(values[1], 40, kNear);
  const Diagnosis d = diagnose(sketch());
  ASSERT_TRUE(d.dof);
  EXPECT_EQ(*d.dof, 2);
  EXPECT_EQ(d.status, DiagnosisStatus::kConsistent);
}

TEST_F(AutoDims, CircleArcAndPolylineGetTheirDimensions) {
  session.set_tool(Tool::kCircle);
  session.press({0, 0});
  session.press({12, 0});
  session.set_tool(Tool::kArc);
  session.press({50, 0});
  session.press({60, 0});
  session.press({50, 10});
  const auto radii = of_kind(ConstraintKind::kRadius);
  ASSERT_EQ(radii.size(), 2u);
  EXPECT_NEAR(radii[0].value, 12, kNear);
  EXPECT_NEAR(radii[1].value, 10, kNear);
  session.set_tool(Tool::kPolyline);
  session.press({0, 50});
  session.press({10, 50});
  session.press({10, 60});
  ASSERT_TRUE(session.finish());
  EXPECT_EQ(of_kind(ConstraintKind::kLength).size(), 2u);
}

TEST_F(AutoDims, RedundantDimensionIsSkippedButTheShapeIsKept) {
  session.set_auto_dimensions(false);
  session.set_tool(Tool::kLine);
  session.press({0, 0});
  session.press({30, 0});
  const SketchLine first = only<SketchLine>(sketch());
  ASSERT_TRUE(doc.execute("Fix", [&](Sketch& s) {
    return s.add_constraint(ConstraintKind::kFix, first.start).has_value() &&
           s.add_constraint(ConstraintKind::kFix, first.end).has_value();
  }));
  session.set_auto_dimensions(true);
  snapping();
  session.press({0, 0});  // Snaps onto the fixed points.
  session.press({30, 0});
  EXPECT_EQ(count<SketchLine>(sketch()), 2u);
  EXPECT_TRUE(of_kind(ConstraintKind::kLength).empty());
  EXPECT_NE(diagnose(sketch()).status, DiagnosisStatus::kRedundant);
}

TEST_F(AutoDims, PreviewCarriesLiveDimensions) {
  session.set_tool(Tool::kLine);
  EXPECT_TRUE(session.preview().constraints().empty());
  session.press({0, 0});
  session.hover({6, 8});
  ASSERT_EQ(session.preview().constraints().size(), 1u);
  const Constraint c = session.preview().constraints().begin()->second;
  EXPECT_EQ(c.kind, ConstraintKind::kLength);
  EXPECT_NEAR(c.value, 10, kNear);
  session.set_tool(Tool::kRectangle);
  session.press({0, 0});
  session.hover({40, 20});
  EXPECT_EQ(session.preview().constraints().size(), 2u);
  session.cancel();
  EXPECT_TRUE(session.preview().constraints().empty());
}

TEST_F(AutoDims, LengthIsEnteredTowardsTheHover) {
  session.set_tool(Tool::kLine);
  EXPECT_EQ(session.input_field(), InputField::kNone);
  EXPECT_FALSE(session.enter_value(10));
  session.press({0, 0});
  session.hover({10, 10});
  EXPECT_EQ(session.input_field(), InputField::kLength);
  EXPECT_NEAR(session.input_value(), std::hypot(10, 10), kNear);
  for (double bad : {0.0, -1.0, double(NAN), double(INFINITY)}) EXPECT_FALSE(session.enter_value(bad));
  EXPECT_TRUE(session.in_progress());
  ASSERT_TRUE(session.enter_value(30));
  EXPECT_FALSE(session.in_progress());
  const SketchLine l = only<SketchLine>(sketch());
  const Position end = point_at(sketch(), l.end);
  EXPECT_NEAR(end.x, 30 / std::numbers::sqrt2, 1e-9);
  EXPECT_NEAR(end.y, 30 / std::numbers::sqrt2, 1e-9);
  ASSERT_EQ(of_kind(ConstraintKind::kLength).size(), 1u);
  EXPECT_NEAR(of_kind(ConstraintKind::kLength)[0].value, 30, 1e-9);
}

TEST_F(AutoDims, RectangleTakesWidthThenHeight) {
  session.set_tool(Tool::kRectangle);
  session.press({0, 0});
  session.hover({-10, 5});
  EXPECT_EQ(session.input_field(), InputField::kWidth);
  ASSERT_TRUE(session.enter_value(40));
  EXPECT_EQ(session.input_field(), InputField::kHeight);
  session.hover({-3, 30});
  EXPECT_NEAR(session.input_value(), 30, kNear);
  ASSERT_TRUE(session.enter_value(25));
  EXPECT_FALSE(session.in_progress());
  const auto box = bounds(sketch());
  ASSERT_TRUE(box);
  EXPECT_NEAR(box->min.x, -40, kNear);
  EXPECT_NEAR(box->max.x, 0, kNear);
  EXPECT_NEAR(box->min.y, 0, kNear);
  EXPECT_NEAR(box->max.y, 25, kNear);
}

TEST_F(AutoDims, RadiusIsEnteredForCirclesAndArcs) {
  session.set_tool(Tool::kCircle);
  session.press({0, 0});
  session.hover({3, 4});
  EXPECT_EQ(session.input_field(), InputField::kRadius);
  EXPECT_NEAR(session.input_value(), 5, kNear);
  ASSERT_TRUE(session.enter_value(12));
  EXPECT_DOUBLE_EQ(only<SketchCircle>(sketch()).radius, 12);
  session.set_tool(Tool::kArc);
  session.press({50, 0});
  session.hover({50, 3});
  EXPECT_EQ(session.input_field(), InputField::kRadius);
  ASSERT_TRUE(session.enter_value(10));
  EXPECT_TRUE(session.in_progress());
  EXPECT_EQ(session.input_field(), InputField::kNone);
  session.press({40, 0});
  const SketchArc a = only<SketchArc>(sketch());
  EXPECT_NEAR(a.radius, 10, kNear);
  EXPECT_NEAR(a.start_angle, kPi / 2, 1e-9);
}

// U04 (sketch-editing.adoc): snaps that create point-on-curve constraints
// and construction geometry.
namespace {
class Snaps : public AutoDims {
 protected:
  Snaps() {
    session.set_auto_dimensions(false);
    snapping();
  }
  EntityId add(const std::function<EntityId(Sketch&)>& make) {
    EntityId id = 0;
    EXPECT_TRUE(doc.execute("Setup", [&](Sketch& s) {
      id = make(s);
      return id != 0;
    }));
    return id;
  }
  EntityId add_line(Position a, Position b) {
    return add([&](Sketch& s) {
      return *s.create_line(*s.create_point(a), *s.create_point(b));
    });
  }
  EntityId only_circle_id() const {
    for (const auto& [i, e] : sketch().entities())
      if (std::holds_alternative<SketchCircle>(e)) return i;
    return 0;
  }
  EntityId newest_line() const {
    EntityId id = 0;
    for (const auto& [i, e] : sketch().entities())
      if (std::holds_alternative<SketchLine>(e)) id = i;
    return id;
  }
};
}  // namespace

TEST_F(Snaps, LineEndingOnACircleRimGetsOneOnCurve) {
  const EntityId circle = add([](Sketch& s) {
    return *s.create_circle(*s.create_point({0, 0}), 10.3);
  });
  session.set_tool(Tool::kLine);
  session.press({20, 20});
  session.press({10.5, 0.2});
  EXPECT_EQ(doc.undo_label(), "Line");
  const auto on = of_kind(ConstraintKind::kOnCurve);
  ASSERT_EQ(on.size(), 1u);
  EXPECT_EQ(on[0].second, circle);
  const SketchLine l = std::get<SketchLine>(*sketch().entity(newest_line()));
  EXPECT_EQ(on[0].first, l.end);
  const Position end = point_at(sketch(), l.end);
  EXPECT_NEAR(std::hypot(end.x, end.y), 10.3, kLengthTolerance);
  ASSERT_TRUE(doc.undo());
  EXPECT_TRUE(of_kind(ConstraintKind::kOnCurve).empty());
  EXPECT_EQ(count<SketchLine>(sketch()), 0u);
}

TEST_F(Snaps, LineEndingOnACrossingGetsTwoOnCurves) {
  const EntityId l1 = add_line({0, 0}, {10, 10});
  const EntityId l2 = add_line({0, 10}, {10, 0});
  session.set_tool(Tool::kLine);
  session.press({20, 0});
  session.press({5.3, 5.2});
  const auto on = of_kind(ConstraintKind::kOnCurve);
  ASSERT_EQ(on.size(), 2u);
  EXPECT_EQ(on[0].first, on[1].first);
  EXPECT_EQ((std::vector<EntityId>{on[0].second, on[1].second}),
            (std::vector<EntityId>{l1, l2}));
  const Position p = point_at(sketch(), on[0].first);
  EXPECT_NEAR(p.x, 5, kLengthTolerance);
  EXPECT_NEAR(p.y, 5, kLengthTolerance);
}

TEST_F(Snaps, PointSnapReusesThePointWithoutOnCurve) {
  const EntityId first = add_line({0, 0}, {10, 0});
  session.set_tool(Tool::kLine);
  session.press({10.2, 0.1});
  session.press({10, 8});
  EXPECT_TRUE(of_kind(ConstraintKind::kOnCurve).empty());
  const SketchLine a = std::get<SketchLine>(*sketch().entity(first));
  const SketchLine b = std::get<SketchLine>(*sketch().entity(newest_line()));
  EXPECT_EQ(b.start, a.end);
}

TEST_F(Snaps, CircleCentreOnALineGetsOnCurve) {
  const EntityId l = add_line({0, 0.3}, {20, 0.3});
  session.set_tool(Tool::kCircle);
  session.press({10.2, 0.1});
  session.press({10, 5});
  const auto on = of_kind(ConstraintKind::kOnCurve);
  ASSERT_EQ(on.size(), 1u);
  EXPECT_EQ(on[0].second, l);
  EXPECT_EQ(on[0].first, only<SketchCircle>(sketch()).center);
  EXPECT_NEAR(point_at(sketch(), on[0].first).y, 0.3, kLengthTolerance);
}

TEST_F(Snaps, LineEndingOnACurveGetsNoLengthAndStretchesWithIt) {
  session.set_auto_dimensions(true);
  add([](Sketch& s) {
    const EntityId c = *s.create_circle(*s.create_point({0, 0}), 10.3);
    return s.add_dimension(ConstraintKind::kRadius, c, 0, 10.3) ? c : 0;
  });
  session.set_tool(Tool::kLine);
  session.press({20, 20});
  session.press({10.5, 0.2});
  EXPECT_EQ(of_kind(ConstraintKind::kOnCurve).size(), 1u);
  EXPECT_TRUE(of_kind(ConstraintKind::kLength).empty());
  const SketchLine l = std::get<SketchLine>(*sketch().entity(newest_line()));

  // Moving the circle stretches the line; its far end stays.
  session.set_tool(Tool::kSelect);
  session.press({0, 0});
  // In pointer-sized steps, as a real drag arrives.
  for (int i = 1; i <= 20; ++i) session.drag({-0.25 * i, 0});
  session.release({-5, 0});
  EXPECT_EQ(doc.undo_label(), "Move point");
  const Position start = point_at(sketch(), l.start);
  EXPECT_NEAR(start.x, 20, 1e-3);
  EXPECT_NEAR(start.y, 20, 1e-3);
  const Position end = point_at(sketch(), l.end);
  const Position centre = point_at(sketch(), only<SketchCircle>(sketch()).center);
  EXPECT_NEAR(centre.x, -5, 1e-3);
  EXPECT_NEAR(std::hypot(end.x - centre.x, end.y - centre.y), 10.3, 1e-6);
}

TEST_F(Snaps, CircleCentreOnALineKeepsItsRadiusInOneUndoStep) {
  session.set_auto_dimensions(true);
  add_line({0, 0.3}, {20, 0.3});
  const auto revision = doc.revision();
  session.set_tool(Tool::kCircle);
  session.press({10.2, 0.1});
  session.press({10, 5});
  EXPECT_EQ(of_kind(ConstraintKind::kOnCurve).size(), 1u);
  EXPECT_EQ(of_kind(ConstraintKind::kRadius).size(), 1u);
  EXPECT_EQ(doc.revision(), revision + 1);
  ASSERT_TRUE(doc.undo());
  EXPECT_TRUE(sketch().constraints().empty());
}

TEST_F(Snaps, ConstructionToggleIsOneUndoableCommand) {
  const EntityId l = add_line({0, 0}, {10, 0});
  const EntityId circle = add([](Sketch& s) {
    return *s.create_circle(*s.create_point({30, 0}), 5);
  });
  const auto construction = [&](EntityId id) {
    return std::visit([](const auto& e) { return e.construction; },
                      *sketch().entity(id));
  };
  session.set_tool(Tool::kSelect);
  EXPECT_FALSE(session.toggle_construction()) << "nothing selected";
  session.press({5, 0});
  ASSERT_TRUE(session.toggle_construction());
  EXPECT_TRUE(construction(l));
  EXPECT_EQ(doc.undo_label(), "Construction");
  session.press({35, 0});
  // One of the two was off, so both become construction.
  ASSERT_TRUE(session.toggle_construction());
  EXPECT_TRUE(construction(l));
  EXPECT_TRUE(construction(circle));
  ASSERT_TRUE(session.toggle_construction());
  EXPECT_FALSE(construction(l));
  EXPECT_FALSE(construction(circle));
  ASSERT_TRUE(doc.undo());
  ASSERT_TRUE(doc.undo());
  EXPECT_TRUE(construction(l));
  EXPECT_FALSE(construction(circle));
}

TEST_F(Snaps, RectangleCornerOnALineGetsOnCurve) {
  session.set_auto_dimensions(true);
  const EntityId l = add_line({0, 0.3}, {20, 0.3});
  session.set_tool(Tool::kRectangle);
  session.press({5.2, 0.1});
  session.press({12, 8});
  const auto on = of_kind(ConstraintKind::kOnCurve);
  ASSERT_EQ(on.size(), 1u);
  EXPECT_EQ(on[0].second, l);
  const Position corner = point_at(sketch(), on[0].first);
  EXPECT_NEAR(corner.x, 5.2, kLengthTolerance);
  EXPECT_NEAR(corner.y, 0.3, kLengthTolerance);
  // Width and height would fight the curve; the sides stay axis-aligned.
  EXPECT_TRUE(of_kind(ConstraintKind::kLength).empty());
  EXPECT_EQ(of_kind(ConstraintKind::kHorizontal).size(), 2u);
  EXPECT_EQ(of_kind(ConstraintKind::kVertical).size(), 2u);
}

// U03b (constrained-drag.adoc#curves): dragging curves as a whole.
TEST_F(Snaps, DraggingACircleOutlineMovesItWithAttachedPoints) {
  const EntityId circle = add([](Sketch& s) {
    const EntityId c = *s.create_circle(*s.create_point({0, 0}), 10);
    return s.add_dimension(ConstraintKind::kRadius, c, 0, 10) ? c : 0;
  });
  session.set_tool(Tool::kLine);
  session.press({20, 20});
  session.press({0.2, 10.3});  // On the rim, at (0, 10).
  const SketchLine l = std::get<SketchLine>(*sketch().entity(newest_line()));
  const EntityId centre = only<SketchCircle>(sketch()).center;

  session.set_tool(Tool::kSelect);
  session.press({-10, 0.2});  // The outline, away from any point.
  EXPECT_EQ(session.selection(), circle);
  for (int i = 1; i <= 12; ++i) session.drag({-10 - 0.25 * i, 0.2});
  session.release({-13, 0.2});
  EXPECT_EQ(doc.undo_label(), "Move curve");
  EXPECT_EQ(session.selection(), circle);
  const Position c = point_at(sketch(), centre);
  EXPECT_NEAR(c.x, -3, 1e-3);
  EXPECT_NEAR(c.y, 0, 1e-3);
  EXPECT_NEAR(only<SketchCircle>(sketch()).radius, 10, 1e-9);
  const Position end = point_at(sketch(), l.end);
  EXPECT_NEAR(std::hypot(end.x - c.x, end.y - c.y), 10, 1e-6);
  EXPECT_NEAR(point_at(sketch(), l.start).x, 20, 1e-3);
  ASSERT_TRUE(doc.undo());
  EXPECT_EQ(point_at(sketch(), centre), (Position{0, 0}));
}

TEST_F(Snaps, DraggingALineTranslatesIt) {
  const EntityId l = add_line({0, 0}, {10, 0});
  const SketchLine ends = std::get<SketchLine>(*sketch().entity(l));
  session.set_tool(Tool::kSelect);
  session.press({5, 0});
  for (int i = 1; i <= 8; ++i) session.drag({5 + 0.25 * i, 0.5 * i});
  session.release({7, 4});
  EXPECT_EQ(doc.undo_label(), "Move curve");
  const Position a = point_at(sketch(), ends.start);
  const Position b = point_at(sketch(), ends.end);
  EXPECT_NEAR(a.x, 2, 1e-3);
  EXPECT_NEAR(a.y, 4, 1e-3);
  EXPECT_NEAR(b.x, 12, 1e-3);
  EXPECT_NEAR(b.y, 4, 1e-3);
}

TEST_F(Snaps, CurveDragCancelsAndATapAddsNoHistory) {
  const EntityId l = add_line({0, 0}, {10, 0});
  const SketchLine ends = std::get<SketchLine>(*sketch().entity(l));
  const auto revision = doc.revision();
  session.set_tool(Tool::kSelect);
  session.press({5, 0});
  session.release({5, 0});
  EXPECT_EQ(doc.revision(), revision);
  EXPECT_EQ(session.selection(), l);
  session.press({5, 0});
  session.drag({6, 2});
  EXPECT_TRUE(session.cancel());
  EXPECT_EQ(point_at(sketch(), ends.start), (Position{0, 0}));
  EXPECT_EQ(doc.revision(), revision);
}

// The user's scene: two outside lines and a chord ending on a circle with a
// radius dimension; dragging the outline moves the circle, the far ends stay
// and every end on the circle stays on it.
TEST_F(Snaps, UserSceneCircleWithLinesAndAChordIsDraggedByItsOutline) {
  session.set_auto_dimensions(true);
  session.set_tool(Tool::kCircle);
  session.press({0, 0});
  session.press({17, 0});
  const EntityId circle = only_circle_id();
  session.set_tool(Tool::kLine);
  session.press({-31, 18});
  session.press({-14.6, -9.7});  // Rim.
  session.press({-16, 24});
  session.press({9.9, 14.0});  // Rim, P.
  session.press({9.7, 13.8});  // P again: point snap.
  session.press({10.6, -13.8});  // Rim, Q.
  ASSERT_EQ(of_kind(ConstraintKind::kOnCurve).size(), 3u);
  ASSERT_TRUE(of_kind(ConstraintKind::kLength).empty());
  const EntityId centre = only<SketchCircle>(sketch()).center;

  session.set_tool(Tool::kSelect);
  session.press({0.2, -17.1});  // Bottom of the outline, away from points.
  ASSERT_EQ(session.selection(), circle);
  for (int i = 1; i <= 24; ++i) session.drag({0.2 + 0.25 * i, -17.1});
  session.release({6.2, -17.1});
  EXPECT_EQ(doc.undo_label(), "Move curve");
  const Position c = point_at(sketch(), centre);
  EXPECT_NEAR(c.x, 6, 1e-3);
  EXPECT_NEAR(c.y, 0, 1e-3);
  for (const Constraint& on : of_kind(ConstraintKind::kOnCurve)) {
    const Position p = point_at(sketch(), on.first);
    EXPECT_NEAR(std::hypot(p.x - c.x, p.y - c.y), 17, 1e-6) << on.first;
  }
  for (Position far : {Position{-31, 18}, Position{-16, 24}}) {
    bool found = false;
    for (const auto& [id, e] : sketch().entities())
      if (const auto* pt = std::get_if<SketchPoint>(&e))
        found |= std::hypot(pt->position.x - far.x, pt->position.y - far.y) < 1e-3;
    EXPECT_TRUE(found) << far.x << " " << far.y;
  }
}

// The user's second scene: a chord P-Q on a circle and a long line ending at
// Q. Points on the circle ride along instead of sliding around it.
namespace {
struct ChordScene {
  EntityId centre, p, q, far;
};
}  // namespace
TEST_F(Snaps, PointsOnADraggedCircleRideAlong) {
  for (const bool by_centre : {false, true}) {
    Document d;
    d.set_commit_step(solver_step());
    ToolSession t{d};
    t.set_snap({true, 1, 1});
    t.set_auto_dimensions(false);
    ChordScene scene{};
    const auto rim = [](double degrees) {
      const double a = degrees * kPi / 180;
      return Position{20.1 * std::cos(a), 20.1 * std::sin(a)};
    };
    ASSERT_TRUE(d.execute("Setup", [&](Sketch& s) {
      scene.centre = *s.create_point({0, 0});
      const EntityId c = *s.create_circle(scene.centre, 20.1);
      scene.p = *s.create_point(rim(210));
      scene.q = *s.create_point(rim(-50));
      scene.far = *s.create_point({-40, 40});
      return s.add_dimension(ConstraintKind::kRadius, c, 0, 20.1) &&
             s.create_line(scene.p, scene.q) &&
             s.create_line(scene.far, scene.q) &&
             s.add_constraint(ConstraintKind::kOnCurve, scene.p, c) &&
             s.add_constraint(ConstraintKind::kOnCurve, scene.q, c);
    }));
    const Position p0 = point_at(d.sketch(), scene.p);
    const Position q0 = point_at(d.sketch(), scene.q);
    t.set_tool(Tool::kSelect);
    const Position from = by_centre ? Position{0, 0} : Position{0, 20.1};
    t.press(from);
    for (int i = 1; i <= 20; ++i)
      t.drag({from.x - 0.4 * i, from.y + 0.25 * i});
    t.release({from.x - 8, from.y + 5});
    EXPECT_EQ(d.undo_label(), by_centre ? "Move point" : "Move curve");
    const Position c = point_at(d.sketch(), scene.centre);
    EXPECT_NEAR(c.x, -8, 1e-3) << by_centre;
    EXPECT_NEAR(c.y, 5, 1e-3) << by_centre;
    const Position p = point_at(d.sketch(), scene.p);
    const Position q = point_at(d.sketch(), scene.q);
    EXPECT_NEAR(p.x, p0.x - 8, 1e-3) << by_centre;
    EXPECT_NEAR(p.y, p0.y + 5, 1e-3) << by_centre;
    EXPECT_NEAR(q.x, q0.x - 8, 1e-3) << by_centre;
    EXPECT_NEAR(q.y, q0.y + 5, 1e-3) << by_centre;
    EXPECT_EQ(point_at(d.sketch(), scene.far), (Position{-40, 40}));
  }
}

// U10 (reference-dimensions.adoc): the dimension tool.
namespace {
std::vector<Constraint> references(const Sketch& s) {
  std::vector<Constraint> out;
  for (const auto& [id, c] : s.constraints())
    if (c.reference) out.push_back(c);
  return out;
}
}  // namespace

TEST_F(Snaps, DimensionToolPlacesAPointToPointReference) {
  const EntityId l = add_line({0, 0}, {10, 0});
  const SketchLine ends = std::get<SketchLine>(*sketch().entity(l));
  const EntityId far = add([](Sketch& s) { return *s.create_point({10, 20}); });
  const auto revision = doc.revision();
  session.set_tool(Tool::kDimension);
  session.press({0.2, 0.1});    // First point.
  session.press({30, 30});      // Empty: ignored.
  session.press({0.1, 0.2});    // The first point again: ignored.
  EXPECT_EQ(doc.revision(), revision);
  session.press({10.1, 19.8});  // Second point: complete.
  session.hover({-4, 12});
  EXPECT_EQ(references(session.preview()).size(), 1u);
  session.press({-4, 12});      // Place.
  EXPECT_EQ(doc.revision(), revision + 1);
  EXPECT_EQ(doc.undo_label(), "Reference dimension");
  const auto refs = references(sketch());
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_EQ(refs[0].kind, ConstraintKind::kDistance);
  EXPECT_EQ(refs[0].first, ends.start);
  EXPECT_EQ(refs[0].second, far);
  EXPECT_NEAR(refs[0].value, std::hypot(10, 20), 1e-9);
  ASSERT_TRUE(refs[0].placement);
  const auto expected =
      dimension_placement_at(sketch(), refs[0], {-4, 12}).value();
  EXPECT_NEAR(refs[0].placement->offset, expected.offset, 1e-9);
  EXPECT_NEAR(refs[0].placement->along, expected.along, 1e-9);
  EXPECT_TRUE(session.preview().constraints().empty());
  ASSERT_TRUE(doc.undo());
  EXPECT_TRUE(references(sketch()).empty());
}

TEST_F(Snaps, DimensionToolMeasuresALineAndACircle) {
  const EntityId l = add_line({0, 0}, {10, 0});
  const EntityId circle = add([](Sketch& s) {
    return *s.create_circle(*s.create_point({40, 0}), 5);
  });
  session.set_tool(Tool::kDimension);
  session.press({5, 0.1});  // Line, away from its points.
  session.press({5, -6});
  session.press({45.1, 0.2});  // Circle rim.
  session.press({48, 8});
  const auto refs = references(sketch());
  ASSERT_EQ(refs.size(), 2u);
  EXPECT_EQ(refs[0].kind, ConstraintKind::kLength);
  EXPECT_EQ(refs[0].first, l);
  EXPECT_NEAR(refs[0].placement->offset, -6, 1e-9);
  EXPECT_EQ(refs[1].kind, ConstraintKind::kRadius);
  EXPECT_EQ(refs[1].first, circle);
  EXPECT_NEAR(refs[1].value, 5, 1e-9);
}

TEST_F(Snaps, DimensionToolEscapeDropsThePicks) {
  add_line({0, 0}, {10, 0});
  session.set_tool(Tool::kDimension);
  session.press({0, 0});
  EXPECT_TRUE(session.in_progress());
  EXPECT_EQ(session.input_field(), InputField::kNone);
  EXPECT_TRUE(session.cancel());
  EXPECT_FALSE(session.in_progress());
  session.press({5, 0.1});
  session.set_tool(Tool::kSelect);
  EXPECT_FALSE(session.in_progress());
  EXPECT_TRUE(references(sketch()).empty());
}

// U04 part 2: trim and extend tools.
TEST_F(Snaps, TrimAndExtendTapsAreSingleUndoSteps) {
  const EntityId l = add_line({0, 0}, {30, 0});
  add_line({10, -5}, {10, 5});
  const Sketch before = sketch();
  const auto revision = doc.revision();
  session.set_tool(Tool::kTrim);
  session.press({50, 50});  // Nothing there.
  EXPECT_EQ(doc.revision(), revision);
  session.press({25, 0.2});
  EXPECT_EQ(doc.undo_label(), "Trim");
  EXPECT_EQ(doc.revision(), revision + 1);
  EXPECT_NEAR(point_at(sketch(), std::get<SketchLine>(*sketch().entity(l)).end).x,
              10, 1e-9);
  ASSERT_TRUE(doc.undo());
  EXPECT_TRUE(sketch() == before);

  session.set_tool(Tool::kExtend);
  session.press({1, 0});  // Start end: nothing ahead.
  EXPECT_EQ(doc.revision(), revision + 1);  // Undone trim keeps the counter.
  const auto m = add_line({0, 10}, {5, 10});
  add_line({20, 0}, {20, 20});
  const auto r = doc.revision();
  session.press({4.5, 10.1});
  EXPECT_EQ(doc.undo_label(), "Extend");
  EXPECT_EQ(doc.revision(), r + 1);
  EXPECT_NEAR(point_at(sketch(), std::get<SketchLine>(*sketch().entity(m)).end).x,
              20, 1e-9);
  EXPECT_EQ(session.tool(), Tool::kExtend) << "the tool stays for the next tap";
}
