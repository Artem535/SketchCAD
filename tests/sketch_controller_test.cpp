#include "sketch_controller.h"

#include <QSignalSpy>
#include <QtTest>
#include <cmath>
#include <numbers>

#include "sketchcad/solver.h"

using sketchcad::SketchLine;
using sketchcad::SketchPoint;
namespace {
bool near(double a, double b) { return std::abs(a - b) < 1e-7; }
sketchcad::Position point_of(const sketchcad::Sketch& s,
                             sketchcad::EntityId id) {
  return std::get<SketchPoint>(s.entity(id).value()).position;
}
}  // namespace
class SketchControllerTest : public QObject {
  Q_OBJECT
  SketchController* make() {
    auto* c = new SketchController(this);
    c->set_viewport_size(800, 600);
    c->set_snap_enabled(false);
    return c;
  }
  void tap(SketchController* c, double x_mm, double y_mm) {
    const QPointF p = c->screen_of(x_mm, y_mm);
    c->press(p.x(), p.y());
    c->release(p.x(), p.y());
  }
  void line(SketchController* c) {
    c->set_tool("line");
    tap(c, 10, 10);
    tap(c, 60, 10);
  }
  // IDs of lines in creation order.
  std::vector<sketchcad::EntityId> lines(SketchController* c) {
    std::vector<sketchcad::EntityId> ids;
    for (const auto& [id, e] : c->document().sketch().entities())
      if (std::holds_alternative<SketchLine>(e)) ids.push_back(id);
    return ids;
  }
  double length(SketchController* c, sketchcad::EntityId id) {
    const auto& s = c->document().sketch();
    const auto l = std::get<SketchLine>(*s.entity(id));
    const auto a = point_of(s, l.start), b = point_of(s, l.end);
    return std::hypot(b.x - a.x, b.y - a.y);
  }
  QVariantMap dimension(SketchController* c, int index = 0) {
    return c->dimensions().value(index).toMap();
  }
 private slots:
  void viewport_frames_default_area_and_draws_grid() {
    auto* c = make();
    QVERIFY(c->scale() > 0);
    const QPointF origin = c->screen_of(0, 0);
    const QPointF corner = c->screen_of(100, 100);
    QVERIFY(origin.x() > 0 && corner.x() < 800);
    QVERIFY(corner.y() > 0 && origin.y() < 600);
    QVERIFY(!c->grid_minor_path().isEmpty());
    QVERIFY(!c->axes_path().isEmpty());
    QCOMPARE(c->tool(), QString("select"));
  }
  void pixel_input_creates_line_at_millimetres() {
    auto* c = make();
    line(c);
    QCOMPARE(c->entity_count(), 3);
    const auto& s = c->document().sketch();
    for (const auto& [id, e] : s.entities()) {
      if (const auto* l = std::get_if<SketchLine>(&e)) {
        QVERIFY(near(point_of(s, l->start).x, 10));
        QVERIFY(near(point_of(s, l->start).y, 10));
        QVERIFY(near(point_of(s, l->end).x, 60));
        QVERIFY(near(point_of(s, l->end).y, 10));
      }
    }
    QVERIFY(c->geometry_path().contains('L'));
    QVERIFY(!c->points_path().isEmpty());
  }
  void zoom_and_pan_leave_document_unchanged() {
    auto* c = make();
    line(c);
    const auto before = c->document().sketch().entities();
    const double scale = c->scale();
    const auto revision = c->document().revision();
    c->zoom_at(400, 300, 2);
    c->pan(30, -20);
    QVERIFY(near(c->scale(), 2 * scale));
    QCOMPARE(c->document().sketch().entities(), before);
    QCOMPARE(c->document().revision(), revision);
    const QPointF moved = c->screen_of(0, 0);
    c->fit();
    QVERIFY(c->screen_of(0, 0) != moved);
  }
  void selection_works_at_any_zoom() {
    for (double factor : {0.25, 8.0}) {
      auto* c = make();
      line(c);
      c->zoom_at(400, 300, factor);
      c->set_tool("select");
      const QPointF on_line = c->screen_of(35, 10);
      c->press(on_line.x(), on_line.y() + 6);
      c->release(on_line.x(), on_line.y() + 6);
      QVERIFY(c->has_selection());
      QVERIFY(!c->selected_path().isEmpty());
      QVERIFY(c->delete_selection());
      QCOMPARE(c->entity_count(), 0);
      QVERIFY(c->selected_path().isEmpty());
    }
  }
  void preview_follows_hover_and_cancel_clears_it() {
    auto* c = make();
    c->set_tool("line");
    tap(c, 0, 0);
    QVERIFY(c->in_progress());
    const QPointF p = c->screen_of(20, 20);
    c->hover(p.x(), p.y());
    QVERIFY(c->preview_path().contains('L'));
    QVERIFY(c->cancel());
    QVERIFY(c->preview_path().isEmpty());
    QVERIFY(!c->in_progress());
    QCOMPARE(c->entity_count(), 0);
  }
  void point_snap_indicator_sits_on_the_point() {
    auto* c = make();
    line(c);
    c->set_snap_enabled(true);
    const QPointF end = c->screen_of(60, 10);
    c->hover(end.x() + 4, end.y() - 3);
    QVERIFY(c->snap_visible());
    QCOMPARE(c->snap_kind(), QString("point"));
    QVERIFY(near(c->snap_x(), end.x()));
    QVERIFY(near(c->snap_y(), end.y()));
    c->set_snap_enabled(false);
    c->hover(end.x() + 4, end.y() - 3);
    QVERIFY(!c->snap_visible());
  }
  void undo_redo_state_is_published() {
    auto* c = make();
    QSignalSpy spy(c, &SketchController::changed);
    QVERIFY(!c->can_undo());
    line(c);
    QVERIFY(spy.count() > 0);
    QVERIFY(c->can_undo());
    QVERIFY(c->undo());
    QCOMPARE(c->entity_count(), 0);
    QVERIFY(c->can_redo());
    QVERIFY(c->redo());
    QCOMPARE(c->entity_count(), 3);
  }
  void referenced_point_delete_reports_message() {
    auto* c = make();
    line(c);
    c->set_tool("select");
    tap(c, 60, 10);
    QVERIFY(c->has_selection());
    QVERIFY(!c->delete_selection());
    QCOMPARE(c->message(), QString("point_in_use"));
    QCOMPARE(c->entity_count(), 3);
    c->clear_selection();
    QVERIFY(!c->has_selection());
  }
  void length_dimension_drives_drawn_line() {
    auto* c = make();
    line(c);
    const auto id = lines(c).front();
    QSignalSpy spy(c, &SketchController::changed);
    const qulonglong dim = c->add_dimension("length", id);
    QVERIFY(dim != 0);
    QVERIFY(spy.count() > 0);
    QCOMPARE(c->dimensions().size(), 1);
    QCOMPARE(dimension(c)["kind"].toString(), QString("length"));
    QVERIFY(near(dimension(c)["value"].toDouble(), 50));
    QCOMPARE(dimension(c)["id"].toULongLong(), dim);

    QVERIFY(c->set_dimension(dim, 80));
    QVERIFY(std::abs(length(c, id) - 80) < sketchcad::kLengthTolerance);
    QVERIFY(near(dimension(c)["value"].toDouble(), 80));
    QVERIFY(c->message().isEmpty());
    QVERIFY(c->undo());
    QVERIFY(std::abs(length(c, id) - 50) < sketchcad::kLengthTolerance);
    QVERIFY(near(dimension(c)["value"].toDouble(), 50));
  }
  void angle_dimension_uses_degrees() {
    auto* c = make();
    line(c);
    c->set_tool("line");
    tap(c, 10, 30);
    tap(c, 40, 60);
    const auto ids = lines(c);
    const qulonglong dim = c->add_dimension("angle", ids[0], ids[1]);
    QVERIFY(dim != 0);
    QVERIFY(std::abs(dimension(c)["value"].toDouble() - 45) < 1e-9);
    QVERIFY(c->set_dimension(dim, 90));
    const auto& s = c->document().sketch();
    QVERIFY(std::abs(*sketchcad::measure(s, sketchcad::ConstraintKind::kAngle,
                                         ids[0], ids[1]) -
                     std::numbers::pi / 2) < sketchcad::kAngleTolerance);
    QVERIFY(c->set_dimension(dim, 180) == false);
    QCOMPARE(c->message(), QString("invalid_value"));
  }
  void rejected_dimensions_report_and_keep_state() {
    auto* c = make();
    line(c);
    const auto id = lines(c).front();
    const qulonglong first = c->add_dimension("length", id);
    QVERIFY(c->add_dimension("length", id) != 0);
    const auto before = c->document().sketch();
    const auto label = c->document().undo_label();

    QVERIFY(!c->set_dimension(first, -5));
    QCOMPARE(c->message(), QString("invalid_value"));
    QVERIFY(c->document().sketch() == before);
    QVERIFY(!c->set_dimension(first, 80));
    QCOMPARE(c->message(), QString("conflict"));
    QVERIFY(c->document().sketch() == before);
    QCOMPARE(c->document().undo_label(), label);

    QCOMPARE(c->add_dimension("teapot", id), 0ull);
    QCOMPARE(c->message(), QString("invalid_dimension"));
    QCOMPARE(c->add_dimension("radius", id), 0ull);
    QCOMPARE(c->message(), QString("invalid_dimension"));
    QVERIFY(c->document().sketch() == before);
  }
  void unknown_tool_name_is_ignored() {
    auto* c = make();
    c->set_tool("rectangle");
    c->set_tool("teapot");
    QCOMPARE(c->tool(), QString("rectangle"));
  }
};
QTEST_MAIN(SketchControllerTest)
#include "sketch_controller_test.moc"
