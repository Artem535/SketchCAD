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
    // U01-U07 cases predate automatic dimensions (U08).
    c->set_auto_dimensions(false);
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
  // Line A (10,10)-(60,10), line B (10,30)-(40,60), circle at (80,40) r 10.
  void scene(SketchController* c) {
    line(c);
    c->set_tool("line");
    tap(c, 10, 30);
    tap(c, 40, 60);
    c->set_tool("circle");
    tap(c, 80, 40);
    tap(c, 90, 40);
    c->set_tool("select");
  }
  QStringList pick(SketchController* c,
                   std::initializer_list<std::pair<double, double>> taps) {
    c->clear_selection();
    for (const auto& [x, y] : taps) tap(c, x, y);
    return c->applicable();
  }
  void applicable_actions_follow_selection() {
    auto* c = make();
    scene(c);
    using L = QStringList;
    QCOMPARE(pick(c, {}), L{});
    // U04: "construction" for any curve, "on_curve" for a point and a curve.
    QCOMPARE(pick(c, {{35, 10}}),
             (L{"horizontal", "vertical", "length", "construction"}));
    QCOMPARE(pick(c, {{35, 10}, {25, 45}}),
             (L{"parallel", "perpendicular", "equal", "angle", "construction"}));
    QCOMPARE(pick(c, {{10, 10}}), L{"fix"});
    // Coincident ends of one line would collapse it, so it is not offered.
    QCOMPARE(pick(c, {{10, 10}, {60, 10}}),
             (L{"horizontal", "vertical", "distance"}));
    QCOMPARE(pick(c, {{10, 30}, {35, 10}}),
             (L{"on_curve", "distance", "construction"}));
    QCOMPARE(pick(c, {{35, 10}, {10, 30}}),
             (L{"on_curve", "distance", "construction"}));
    QCOMPARE(pick(c, {{90, 40}}), (L{"radius", "construction"}));
    QCOMPARE(pick(c, {{90, 40}, {35, 10}}), (L{"tangent", "construction"}));
  }
  void curve_snaps_and_construction_layer() {
    auto* c = make();
    line(c);  // (10, 10) - (60, 10)
    c->set_tool("line");
    tap(c, 35, -10);
    tap(c, 35, 30);
    c->set_snap_enabled(true);
    QPointF p = c->screen_of(35.3, 10.2);
    c->hover(p.x(), p.y());
    QCOMPARE(c->snap_kind(), QString("intersection"));
    p = c->screen_of(20, 10.3);
    c->hover(p.x(), p.y());
    QCOMPARE(c->snap_kind(), QString("curve"));
    QVERIFY(near(c->snap_y(), c->screen_of(20, 10).y()));
    // Grid on curves: the default view has a 2 mm grid step.
    p = c->screen_of(20.6, 10.2);
    c->hover(p.x(), p.y());
    QVERIFY(near(c->snap_x(), c->screen_of(20.6, 10).x()));
    c->set_grid_on_curves(true);
    QVERIFY(c->grid_on_curves());
    c->hover(p.x(), p.y());
    QCOMPARE(c->snap_kind(), QString("curve"));
    QVERIFY(near(c->snap_x(), c->screen_of(20, 10).x()));
    c->set_grid_on_curves(false);
    c->cancel();
    c->set_snap_enabled(false);

    c->set_tool("select");
    tap(c, 20, 10);
    QVERIFY(c->applicable().contains("construction"));
    const QString before = c->geometry_path();
    QVERIFY(c->construction_path().isEmpty());
    QVERIFY(c->apply("construction") != 0);
    QCOMPARE(c->document().undo_label(), QString("Construction"));
    QVERIFY(!c->construction_path().isEmpty());
    QVERIFY(before.contains(c->construction_path()));
    QVERIFY(!c->geometry_path().contains(c->construction_path()));
    QVERIFY(c->undo());
    QVERIFY(c->construction_path().isEmpty());
    QCOMPARE(c->geometry_path(), before);
  }
  void apply_adds_constraint_and_updates_diagnosis() {
    auto* c = make();
    line(c);
    c->set_tool("select");
    QCOMPARE(c->dof(), 4);
    QCOMPARE(c->diagnosis(), QString("consistent"));
    tap(c, 35, 10);
    const qulonglong id = c->apply("horizontal");
    QVERIFY(id != 0);
    QCOMPARE(c->constraints().size(), 1);
    const auto entry = c->constraints().front().toMap();
    QCOMPARE(entry["kind"].toString(), QString("horizontal"));
    QCOMPARE(entry["id"].toULongLong(), id);
    QCOMPARE(c->dof(), 3);
    c->set_selected_constraint(id);
    QVERIFY(!c->constraint_path().isEmpty());
    QVERIFY(c->undo());
    QVERIFY(c->constraints().isEmpty());
    QCOMPARE(c->dof(), 4);
    QVERIFY(c->constraint_path().isEmpty());
  }
  void non_applicable_actions_are_not_sent() {
    auto* c = make();
    line(c);
    c->set_tool("select");
    tap(c, 35, 10);
    const auto revision = c->document().revision();
    QCOMPARE(c->apply("radius"), 0ull);
    QCOMPARE(c->message(), QString("invalid_action"));
    QCOMPARE(c->apply("teapot"), 0ull);
    QCOMPARE(c->document().revision(), revision);
    QVERIFY(!c->document().can_redo());
  }
  void conflict_is_highlighted_and_rolled_back() {
    auto* c = make();
    line(c);
    c->set_tool("select");
    tap(c, 35, 10);
    const qulonglong first = c->apply("length");
    QVERIFY(first != 0);
    QVERIFY(c->apply("length") != 0);
    QCOMPARE(c->diagnosis(), QString("redundant"));
    c->highlight_dependent();
    QVERIFY(!c->conflict_path().isEmpty());
    c->clear_selection();
    QVERIFY(c->conflict_path().isEmpty());

    const auto before = c->document().sketch();
    QVERIFY(!c->set_dimension(first, 80));
    QCOMPARE(c->message(), QString("conflict"));
    QVERIFY(!c->conflict_path().isEmpty());
    QVERIFY(c->document().sketch() == before);
    c->clear_selection();
    QVERIFY(c->conflict_path().isEmpty());
  }
  void remove_constraint_is_undoable() {
    auto* c = make();
    line(c);
    c->set_tool("select");
    tap(c, 35, 10);
    const qulonglong id = c->apply("vertical");
    QVERIFY(id != 0);
    QVERIFY(c->remove_constraint(id));
    QVERIFY(c->constraints().isEmpty());
    QVERIFY(!c->remove_constraint(id));
    QVERIFY(c->undo());
    QCOMPARE(c->constraints().size(), 1);
  }
  void dimension_labels_follow_geometry() {
    auto* c = make();
    line(c);
    c->set_tool("select");
    tap(c, 35, 10);
    const qulonglong id = c->apply("length");
    QCOMPARE(c->dimension_labels().size(), 1);
    auto label = c->dimension_labels().front().toMap();
    QCOMPARE(label["id"].toULongLong(), id);
    QCOMPARE(label["text"].toString(), QString("50"));
    const QPointF mid = c->screen_of(35, 10);
    QVERIFY(std::abs(label["x"].toDouble() - mid.x()) < 1);
    QVERIFY(std::abs(label["y"].toDouble() - mid.y()) < 40);
    QVERIFY(c->set_dimension(id, 12.5));
    label = c->dimension_labels().front().toMap();
    QCOMPARE(label["text"].toString(), QString("12.5"));
  }
  void collapsing_actions_are_not_offered() {
    auto* c = make();
    line(c);
    c->set_tool("select");
    tap(c, 35, 10);
    QVERIFY(c->apply("horizontal") != 0);
    QVERIFY(!c->applicable().contains("vertical"));
    QCOMPARE(c->apply("vertical"), 0ull);
    QCOMPARE(c->message(), QString("invalid_action"));
    c->clear_selection();
    tap(c, 10, 10);
    tap(c, 60, 10);
    QVERIFY(!c->applicable().contains("coincident"));
    QVERIFY(c->applicable().contains("distance"));
  }
  void drag_keeps_constraints_and_moves_tied_geometry() {
    auto* c = make();
    line(c);
    c->set_tool("select");
    tap(c, 35, 10);
    QVERIFY(c->apply("horizontal") != 0);
    c->clear_selection();
    const auto id = lines(c).front();
    const auto l = std::get<SketchLine>(*c->document().sketch().entity(id));
    const QPointF from = c->screen_of(10, 10), to = c->screen_of(10, 16);
    c->press(from.x(), from.y());
    c->drag((from.x() + to.x()) / 2, (from.y() + to.y()) / 2);
    c->drag(to.x(), to.y());
    c->release(to.x(), to.y());
    const auto& s = c->document().sketch();
    QVERIFY(std::abs(point_of(s, l.start).y - 16) < 1e-3);
    QVERIFY(std::abs(point_of(s, l.end).y - 16) < 1e-3);
    QVERIFY(std::abs(point_of(s, l.end).x - 60) < 1e-3);
    QCOMPARE(c->diagnosis(), QString("consistent"));
  }
  // Inspector rows as "label=value unit" strings.
  QStringList rows(SketchController* c) {
    QStringList out;
    for (const QVariant& v : c->selection_properties()) {
      const auto m = v.toMap();
      out << QString("%1=%2 %3")
                 .arg(m["label"].toString(), m["value"].toString(),
                      m["unit"].toString())
                 .trimmed();
    }
    return out;
  }
  void inspector_describes_the_selection() {
    auto* c = make();
    QCOMPARE(c->selection_title(), QString("Эскиз"));
    QCOMPARE(rows(c), (QStringList{"Объектов=0", "Ограничений=0", "DOF=0"}));
    line(c);  // (10,10)-(60,10)
    c->set_tool("circle");
    tap(c, 100, 50);
    tap(c, 112.5, 50);
    c->set_tool("arc");
    tap(c, 100, 0);
    tap(c, 110, 0);
    tap(c, 100, 10);
    c->set_tool("select");
    QCOMPARE(rows(c).first(), QString("Объектов=7"));

    tap(c, 35, 10);
    QCOMPARE(c->selection_title(), QString("Линия"));
    QCOMPARE(rows(c), (QStringList{"Длина=50 мм", "Угол=0 °"}));
    c->clear_selection();
    tap(c, 10, 10);
    QCOMPARE(c->selection_title(), QString("Точка"));
    QCOMPARE(rows(c), (QStringList{"X=10 мм", "Y=10 мм"}));
    c->clear_selection();
    tap(c, 100, 62.5);
    QCOMPARE(c->selection_title(), QString("Окружность"));
    QCOMPARE(rows(c), (QStringList{"Радиус=12.5 мм", "Диаметр=25 мм"}));
    c->clear_selection();
    const QPointF on_arc = c->screen_of(100 + 10 * std::cos(0.7),
                                        10 * std::sin(0.7));
    c->press(on_arc.x(), on_arc.y());
    c->release(on_arc.x(), on_arc.y());
    QCOMPARE(c->selection_title(), QString("Дуга"));
    QCOMPARE(rows(c), (QStringList{"Радиус=10 мм", "Угол дуги=90 °"}));
    tap(c, 35, 10);
    QCOMPARE(c->selection_title(), QString("2 объекта"));
    QVERIFY(rows(c).isEmpty());
  }
  void fixed_points_are_published() {
    auto* c = make();
    line(c);
    c->set_tool("select");
    QVERIFY(c->fixed_path().isEmpty());
    tap(c, 10, 10);
    QVERIFY(c->apply("fix") != 0);
    QVERIFY(!c->fixed_path().isEmpty());
    QVERIFY(c->undo());
    QVERIFY(c->fixed_path().isEmpty());
  }
  void dimensions_are_drawn_with_lines_and_arrows() {
    auto* c = make();
    line(c);
    c->set_tool("select");
    tap(c, 35, 10);
    QVERIFY(c->dimension_path().isEmpty());
    QVERIFY(c->apply("length") != 0);
    QVERIFY(!c->dimension_path().isEmpty());
    QVERIFY(!c->dimension_arrows_path().isEmpty());
    const auto label = c->dimension_labels().front().toMap();
    QVERIFY(label.contains("angle"));
    QCOMPARE(label["angle"].toDouble(), 0.0);
    QVERIFY(c->undo());
    QVERIFY(c->dimension_path().isEmpty());
    QVERIFY(c->dimension_arrows_path().isEmpty());
  }
  void dynamic_input_and_live_dimensions() {
    auto* c = make();
    QCOMPARE(c->input_field(), QString());
    c->set_tool("line");
    QCOMPARE(c->input_field(), QString());
    QVERIFY(!c->enter_value(10));
    const QPointF a = c->screen_of(10, 10), b = c->screen_of(30, 10);
    c->press(a.x(), a.y());
    c->release(a.x(), a.y());
    c->hover(b.x(), b.y());
    QCOMPARE(c->input_field(), QString("length"));
    QVERIFY(std::abs(c->input_value() - 20) < 1e-6);
    QCOMPARE(c->preview_dimension_labels().size(), 1);
    QVERIFY(!c->preview_dimension_path().isEmpty());
    QVERIFY(!c->preview_dimension_arrows_path().isEmpty());
    QCOMPARE(c->preview_dimension_labels().front().toMap()["text"].toString(),
             QString("20"));
    QVERIFY(!c->enter_value(-5));
    QCOMPARE(c->message(), QString("invalid_value"));
    QVERIFY(c->enter_value(45));
    QVERIFY(!c->in_progress());
    QCOMPARE(lines(c).size(), std::size_t{1});
    QVERIFY(std::abs(length(c, lines(c).front()) - 45) < 1e-9);
    QVERIFY(c->preview_dimension_labels().isEmpty());
    QVERIFY(c->preview_dimension_path().isEmpty());
    QVERIFY(c->dimensions().isEmpty());  // Auto dimensions are off here.
  }
  void auto_dimensions_follow_the_setting() {
    auto* c = make();
    QVERIFY(!c->auto_dimensions());
    c->set_auto_dimensions(true);
    QVERIFY(c->auto_dimensions());
    line(c);
    QCOMPARE(c->dimensions().size(), 1);
    QCOMPARE(dimension(c)["kind"].toString(), QString("length"));
    QCOMPARE(c->dof(), 3);
    QVERIFY(c->undo());
    QVERIFY(c->dimensions().isEmpty());
    c->set_auto_dimensions(false);
    line(c);
    QVERIFY(c->dimensions().isEmpty());
  }
  void dimensions_are_moved_by_dragging() {
    auto* c = make();
    line(c);  // (10,10)-(60,10)
    c->set_tool("select");
    tap(c, 35, 10);
    const qulonglong id = c->apply("length");
    QVERIFY(id != 0);
    QCOMPARE(dimension(c)["placed"].toBool(), false);
    const auto revision = c->document().revision();
    QVERIFY(!c->begin_dimension_drag(999));
    QVERIFY(c->begin_dimension_drag(id));
    const QPointF below = c->screen_of(30, 2), further = c->screen_of(20, 0);
    c->drag_dimension(below.x(), below.y());
    c->drag_dimension(further.x(), further.y());
    c->end_dimension_drag();
    const auto placement = c->document().sketch().constraint(id)->placement;
    QVERIFY(placement);
    QVERIFY(std::abs(std::abs(placement->offset) - 10) < 1e-6);
    QVERIFY(std::abs(placement->along - 0.2) < 1e-6);
    QCOMPARE(dimension(c)["placed"].toBool(), true);
    QVERIFY(c->document().revision() != revision);
    QVERIFY(c->undo());
    QVERIFY(!c->document().sketch().constraint(id)->placement);
    QVERIFY(c->redo());

    // Cancel restores the previous placement and adds no step.
    const auto before = c->document().sketch().constraint(id)->placement;
    const auto steps = c->document().revision();
    QVERIFY(c->begin_dimension_drag(id));
    const QPointF above = c->screen_of(50, 30);
    c->drag_dimension(above.x(), above.y());
    c->cancel_dimension_drag();
    QCOMPARE(c->document().sketch().constraint(id)->placement, before);
    QCOMPARE(c->document().revision(), steps);
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
