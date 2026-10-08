#include "sketchcad/sketch_tool.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <numbers>
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
Position point_at(const Sketch& s, EntityId id) {
  return std::get<SketchPoint>(s.entity(id).value()).position;
}
class Tools : public ::testing::Test {
 protected:
  Tools() {
    session.set_snap({false, 1, 1});
    session.set_pick_tolerance(1);
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
