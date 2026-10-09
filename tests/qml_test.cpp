#include <QColor>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QInputDevice>
#include <QWheelEvent>
#include <QtQuickTest/quicktest.h>
#include <QtTest>

#include "sketch_controller.h"
class QmlTest : public QObject {
  Q_OBJECT
  QQuickWindow* window_ = nullptr;
  // Walks the visual tree: Repeater delegates are not QObject children.
  static QQuickItem* find(QQuickItem* root, const QString& name) {
    if (root->objectName() == name) return root;
    for (QQuickItem* child : root->childItems())
      if (QQuickItem* found = find(child, name)) return found;
    return nullptr;
  }
  QQuickItem* item(const char* name) {
    return find(window_->contentItem(), name);
  }
  void click(QQuickItem* target) {
    QVERIFY(target);
    QVERIFY(QQuickTest::qWaitForPolish(window_));
    QVERIFY(target->isVisible());
    QTest::mouseClick(
        window_, Qt::LeftButton, Qt::NoModifier,
        target->mapToScene(QPointF(target->width() / 2, target->height() / 2))
            .toPoint());
  }
  QPoint scene_of(SketchController& c, double x_mm, double y_mm) {
    return item("sketchCanvas")->mapToScene(c.screen_of(x_mm, y_mm)).toPoint();
  }
  void tap(SketchController& c, double x_mm, double y_mm) {
    QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier,
                      scene_of(c, x_mm, y_mm));
  }

  void open(QQmlApplicationEngine& engine, SketchController& controller) {
    engine.rootContext()->setContextProperty("sketch", &controller);
    engine.load(QUrl("qrc:/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    window_ = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    QVERIFY(window_);
    QVERIFY(QTest::qWaitForWindowExposed(window_));
    window_->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(window_));
    QVERIFY(item("sketchCanvas"));
    // Scenarios before U08 assume no automatic dimensions.
    controller.set_auto_dimensions(false);
  }
  double line_length(const SketchController& c) {
    const auto& s = c.document().sketch();
    for (const auto& [id, e] : s.entities())
      if (const auto* l = std::get_if<sketchcad::SketchLine>(&e)) {
        const auto a = std::get<sketchcad::SketchPoint>(*s.entity(l->start));
        const auto b = std::get<sketchcad::SketchPoint>(*s.entity(l->end));
        return std::hypot(b.position.x - a.position.x,
                          b.position.y - a.position.y);
      }
    return 0;
  }
  void type(const QString& digits) {
    for (QChar ch : digits) {
      const QByteArray name =
          ch == '.' ? QByteArray("key_dot") : "key_" + QString(ch).toLatin1();
      click(item(name.constData()));
    }
  }
  // Closed popups leave the scene, so "not found" also means hidden.
  bool shown(const char* name) {
    QQuickItem* i = item(name);
    return i && i->isVisible();
  }
  QColor color_of(const char* name) {
    QQuickItem* i = item(name);
    return i ? i->property("color").value<QColor>() : QColor();
  }
  // Light and dark tokens of tablet-shell.adoc.
  struct Palette {
    const char* bg;
    const char* surface;
    const char* canvas;
    const char* primary_container;
  };
  void expect_palette(const Palette& p) {
    QCOMPARE(window_->color(), QColor(p.bg));
    QCOMPARE(color_of("appBar"), QColor(p.surface));
    QCOMPARE(color_of("toolRail"), QColor(p.surface));
    QCOMPARE(color_of("canvasBackground"), QColor(p.canvas));
    QCOMPARE(color_of("zoomControl"), QColor(p.surface));
    QCOMPARE(color_of("bottomStatus"), QColor(p.surface));
    QCOMPARE(color_of("mode_sketch_background"), QColor(p.primary_container));
  }
  QString text_of(const char* name) {
    QQuickItem* i = item(name);
    return i ? i->property("text").toString() : QString();
  }

 private slots:
  void trim_tool_cuts_at_the_crossing() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    click(item("tool_line"));
    tap(controller, 10, 40);
    tap(controller, 60, 40);
    tap(controller, 30, 20);
    tap(controller, 30, 60);
    QVERIFY(item("tool_extend"));
    click(item("tool_trim"));
    tap(controller, 50, 40);
    QCOMPARE(controller.document().undo_label(), std::string("Trim"));
    const auto& s = controller.document().sketch();
    for (const auto& [id, e] : s.entities())
      if (const auto* l = std::get_if<sketchcad::SketchLine>(&e)) {
        const auto a = std::get<sketchcad::SketchPoint>(*s.entity(l->start));
        const auto b = std::get<sketchcad::SketchPoint>(*s.entity(l->end));
        // The horizontal line now ends at the crossing.
        if (std::abs(a.position.y - 40) < 1e-6 && std::abs(b.position.y - 40) < 1e-6)
          QVERIFY(std::abs(std::max(a.position.x, b.position.x) - 30) < 1e-6);
      }
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u04_trim.png"));
  }
  void dimension_tool_draws_a_reference() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    click(item("tool_line"));
    tap(controller, 10, 60);
    tap(controller, 60, 60);
    click(item("tool_dimension"));
    tap(controller, 10, 60);
    tap(controller, 60, 60);
    tap(controller, 35, 70);
    QTRY_VERIFY(item("dimensionLabel_0"));
    const QVariantMap label = controller.dimension_labels().front().toMap();
    QVERIFY(label["text"].toString().endsWith('*'));
    click(item("tool_select"));
    click(item("dimensionLabel_0"));
    QTest::qWait(300);
    QVERIFY(!shown("dimensionEditor"));
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u10_reference.png"));
  }
  void circle_is_dragged_by_its_outline() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    click(item("snapToggle"));
    click(item("tool_circle"));
    tap(controller, 50, 50);
    tap(controller, 70, 50);
    click(item("tool_select"));
    const QPoint from = scene_of(controller, 50, 30);
    const QPoint to = scene_of(controller, 56, 30);
    QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, from);
    for (int i = 1; i <= 8; ++i)
      QTest::mouseMove(window_, from + (to - from) * i / 8);
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, to);
    const auto& s = controller.document().sketch();
    for (const auto& [id, e] : s.entities())
      if (const auto* k = std::get_if<sketchcad::SketchCircle>(&e)) {
        const auto c = std::get<sketchcad::SketchPoint>(*s.entity(k->center));
        QVERIFY2(std::abs(c.position.x - 56) < 0.5, qPrintable(QString::number(c.position.x)));
        QVERIFY(std::abs(c.position.y - 50) < 0.5);
      }
    QCOMPARE(controller.document().undo_label(), std::string("Move curve"));
  }
  void snaps_create_on_curve_and_construction() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    click(item("tool_line"));
    tap(controller, 10, 10);
    tap(controller, 60, 60);
    tap(controller, 10, 60);
    tap(controller, 60, 10);
    tap(controller, 80, 20);
    QTest::mouseMove(window_, scene_of(controller, 35.4, 35.3));
    QTRY_COMPARE(controller.snap_kind(), QString("intersection"));
    QVERIFY(shown("snapIndicator"));
    tap(controller, 35.4, 35.3);
    int on_curve = 0;
    for (const auto& [id, c] : controller.document().sketch().constraints())
      on_curve += c.kind == sketchcad::ConstraintKind::kOnCurve;
    QCOMPARE(on_curve, 2);
    click(item("moreButton"));
    QTRY_VERIFY(shown("gridOnCurvesToggle"));
    click(item("gridOnCurvesToggle"));
    QTRY_VERIFY(controller.grid_on_curves());
    QTRY_VERIFY(!shown("gridOnCurvesToggle"));

    click(item("tool_select"));
    tap(controller, 20, 20);
    QTRY_VERIFY(shown("action_construction"));
    click(item("action_construction"));
    QTRY_VERIFY(!controller.construction_path().isEmpty());
    QCOMPARE(controller.document().undo_label(), QString("Construction"));
    controller.clear_selection();
    QTRY_VERIFY(!shown("action_construction"));
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u04_snaps.png"));
  }
  void dimension_is_moved_with_the_mouse() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    click(item("snapToggle"));
    click(item("tool_line"));
    tap(controller, 10, 60);
    tap(controller, 60, 60);
    click(item("tool_select"));
    tap(controller, 35, 60);
    click(item("action_length"));
    QTRY_VERIFY(shown("dimensionEditor"));
    click(item("cancelDimension"));
    QTRY_VERIFY(!shown("dimensionEditor"));
    QTest::keyClick(window_, Qt::Key_Escape);
    const qulonglong id =
        controller.dimensions().front().toMap()["id"].toULongLong();

    // Drag the number below the line.
    QQuickItem* label = item("dimensionLabel_0");
    QVERIFY(label);
    QVERIFY(QQuickTest::qWaitForPolish(window_));
    const QPoint from =
        label->mapToScene({label->width() / 2, label->height() / 2}).toPoint();
    const QPoint to = scene_of(controller, 35, 50);
    QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, from);
    for (int i = 1; i <= 8; ++i)
      QTest::mouseMove(window_, from + (to - from) * i / 8);
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, to);
    const auto placement =
        controller.document().sketch().constraint(id)->placement;
    QVERIFY2(placement, "dimension placed");
    QVERIFY(placement->offset < -5);
    QCOMPARE(controller.document().undo_label(), std::optional<std::string>("Move dimension"));
    QVERIFY(!shown("dimensionEditor"));
    QCOMPARE(controller.entity_count(), 3);  // Nothing selected or drawn.

    // A plain click still opens the editor.
    click(item("dimensionLabel_0"));
    QTRY_VERIFY(shown("dimensionEditor"));
    click(item("cancelDimension"));
    QTRY_VERIFY(!shown("dimensionEditor"));
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u09_moved.png"));
  }
  void live_and_automatic_dimensions() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    controller.set_auto_dimensions(true);
    click(item("snapToggle"));
    click(item("tool_line"));
    tap(controller, 10, 60);
    QTest::mouseMove(window_, scene_of(controller, 30, 60));
    QCOMPARE(controller.input_field(), QString("length"));
    QTRY_VERIFY(item("previewDimensionLabel_0"));
    QTest::keyClick(window_, Qt::Key_4);
    QTRY_VERIFY(shown("dimensionEditor"));
    QTest::keyClick(window_, Qt::Key_0);
    QCOMPARE(text_of("dimensionValue"), QString("40"));
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_VERIFY(!shown("dimensionEditor"));
    QVERIFY(!controller.in_progress());
    QCOMPARE(controller.dimensions().size(), 1);
    QVERIFY(std::abs(line_length(controller) - 40) < 1e-6);

    // A tap on the live dimension opens the editor with the current value.
    tap(controller, 10, 90);
    QTest::mouseMove(window_, scene_of(controller, 40, 90));
    QTRY_VERIFY(item("previewDimensionLabel_0"));
    QTest::qWait(50);
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u08_drawing.png"));
    click(item("previewDimensionLabel_0"));
    QTRY_VERIFY(shown("dimensionEditor"));
    QVERIFY(!text_of("dimensionValue").isEmpty());
    click(item("cancelDimension"));
    QTRY_VERIFY(!shown("dimensionEditor"));
    QVERIFY(controller.in_progress());
    QTest::keyClick(window_, Qt::Key_Escape);
    QVERIFY(!controller.in_progress());

    // The overflow menu turns automatic dimensions off.
    click(item("moreButton"));
    QTRY_VERIFY(shown("autoDimensionsToggle"));
    click(item("autoDimensionsToggle"));
    QTRY_VERIFY(!controller.auto_dimensions());
  }
  void term_help() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    const QStringList actions{"coincident", "horizontal", "vertical",
                              "parallel", "perpendicular", "tangent",
                              "equal", "fix", "length", "distance",
                              "angle", "radius", "on_curve",
                              "construction"};
    const QStringList keys = window_->property("helpKeys").toStringList();
    QCOMPARE(keys.size(), 26);
    for (const QString& key : actions + QStringList{"constraint", "dimension",
                                                     "dof", "defined",
                                                     "redundant", "conflict",
                                                     "snap", "finger", "autodim",
                                                     "input", "modes", "reference"}) {
      QVERIFY2(keys.contains(key), qPrintable(key));
      QVariant hint;
      QVERIFY(QMetaObject::invokeMethod(window_, "helpHint",
                                        Q_RETURN_ARG(QVariant, hint),
                                        Q_ARG(QVariant, key)));
      QVERIFY2(!hint.toString().isEmpty(), qPrintable(key));
    }

    // DOF: entry plus the current state in words.
    click(item("help_dof"));
    QTRY_VERIFY(shown("helpPopup"));
    QCOMPARE(text_of("helpTitle"), QString("Степени свободы (DOF)"));
    QVERIFY(!text_of("helpText").isEmpty());
    QVERIFY(text_of("helpState").contains("Сейчас"));
    click(item("closeHelp"));
    QTRY_VERIFY(!shown("helpPopup"));

    // Glossary lists every term.
    click(item("help_glossary"));
    QTRY_VERIFY(shown("helpPopup"));
    for (const QString& key : keys)
      QVERIFY2(item(("helpEntry_" + key).toLatin1().constData()),
               qPrintable(key));
    click(item("closeHelp"));
    QTRY_VERIFY(!shown("helpPopup"));

    // Context bar help lists exactly the applicable actions.
    click(item("snapToggle"));
    click(item("tool_line"));
    tap(controller, 10, 60);
    tap(controller, 60, 60);
    click(item("tool_select"));
    tap(controller, 35, 60);
    click(item("help_actions"));
    QTRY_VERIFY(shown("helpPopup"));
    for (const QString& key : actions) {
      const bool listed =
          item(("helpEntry_" + key).toLatin1().constData()) != nullptr;
      QCOMPARE(listed, controller.applicable().contains(key));
    }
    click(item("closeHelp"));
    QTRY_VERIFY(!shown("helpPopup"));

    // Inspector: section help, row hints, badges.
    click(item("action_horizontal"));
    click(item("constraintsToggle"));
    QTRY_VERIFY(shown("inspector"));
    QTRY_COMPARE(item("inspector")->x() + item("inspector")->width(),
                 window_->width() - 8.0);
    QVERIFY(!text_of("constraintHint_0").isEmpty());
    click(item("help_constraints"));
    QTRY_VERIFY(shown("helpPopup"));
    QVERIFY(item("helpEntry_constraint"));
    QVERIFY(item("helpEntry_dimension"));
    click(item("closeHelp"));
    QTRY_VERIFY(!shown("helpPopup"));
    click(item("action_length"));
    QTRY_VERIFY(shown("dimensionEditor"));
    click(item("help_dimension"));
    QTRY_VERIFY(shown("helpPopup"));
    QCOMPARE(text_of("helpTitle"), window_->property("helpTitles")
                                       .toMap()["length"].toString());
    click(item("closeHelp"));
    QTRY_VERIFY(!shown("helpPopup"));
    click(item("applyDimension"));
    QTRY_VERIFY(!shown("dimensionEditor"));
    click(item("action_length"));  // Redundant second length.
    QTRY_VERIFY(shown("dimensionEditor"));
    click(item("applyDimension"));
    QTRY_VERIFY(!shown("dimensionEditor"));
    QTRY_VERIFY(item("constraintBadge_1"));
    click(item("constraintBadge_1"));
    QTRY_VERIFY(shown("helpPopup"));
    QCOMPARE(text_of("helpTitle"), window_->property("helpTitles")
                                       .toMap()["redundant"].toString());
    QTest::qWait(300);
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u07_help.png"));
  }
  void dimensions_in_eskd_style() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    window_->setProperty("darkTheme", false);
    click(item("snapToggle"));
    click(item("tool_rectangle"));
    tap(controller, 20, 40);
    tap(controller, 80, 70);
    click(item("tool_select"));
    for (const auto& [x, y] : {std::pair{50.0, 40.0}, std::pair{80.0, 55.0}}) {
      QTest::keyClick(window_, Qt::Key_Escape);
      tap(controller, x, y);
      click(item("action_length"));
      QTRY_VERIFY(shown("dimensionEditor"));
      click(item("cancelDimension"));
      QTRY_VERIFY(!shown("dimensionEditor"));
    }
    QTest::keyClick(window_, Qt::Key_Escape);
    QCOMPARE(controller.dimensions().size(), 2);
    QVERIFY(!controller.dimension_path().isEmpty());
    // The vertical dimension's label is rotated to read bottom to top.
    QTRY_VERIFY(item("dimensionLabel_1"));
    QCOMPARE(item("dimensionLabel_1")->rotation(), -90.0);
    click(item("dimensionLabel_0"));
    QTRY_VERIFY(shown("dimensionEditor"));
    click(item("cancelDimension"));
    QTRY_VERIFY(!shown("dimensionEditor"));
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u06_light.png"));
    window_->setProperty("darkTheme", true);
    QTest::qWait(100);
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u06_dark.png"));
  }
  void shell_follows_mockup_theme() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    const Palette light{"#f4f6f9", "#ffffff", "#f7f9fb", "#dbe9fb"};
    const Palette dark{"#12191f", "#1c262f", "#161e25", "#1f3b57"};
    // The start theme follows the platform; force a known one.
    window_->setProperty("darkTheme", false);
    expect_palette(light);

    for (const char* mode : {"mode_part", "mode_assembly", "mode_drawing"}) {
      QVERIFY2(item(mode), mode);
      QVERIFY2(!item(mode)->isEnabled(), mode);
    }
    QVERIFY(item("mode_sketch")->property("checked").toBool());

    QVERIFY(text_of("bottomStatus").contains("мм"));
    QVERIFY(text_of("bottomStatus").contains("сетка"));
    click(item("snapToggle"));
    QTRY_VERIFY(text_of("bottomStatus").contains("выкл"));

    // Free DOF uses the primary container; a fully fixed line is ok.
    click(item("tool_line"));
    tap(controller, 10, 60);
    tap(controller, 60, 60);
    click(item("tool_select"));
    QCOMPARE(color_of("dofStatus_background"), QColor("#dbe9fb"));
    tap(controller, 10, 60);
    click(item("action_fix"));
    QTest::keyClick(window_, Qt::Key_Escape);
    tap(controller, 60, 60);
    click(item("action_fix"));
    QTRY_COMPARE(controller.dof(), 0);
    QCOMPARE(color_of("dofStatus_background"), QColor("#e2f4ee"));

    // Inspector: properties of the selected line, zoom control moves left.
    QQuickItem* zoom = item("zoomControl");
    const double zoom_x = zoom->mapToScene({0, 0}).x();
    QTest::keyClick(window_, Qt::Key_Escape);
    tap(controller, 35, 60);
    click(item("constraintsToggle"));
    QTRY_VERIFY(shown("inspector"));
    QTRY_COMPARE(item("inspector")->x() + item("inspector")->width(),
                 window_->width() - 8.0);  // Slide-in finished.
    QCOMPARE(color_of("inspector"), QColor("#ffffff"));
    QTRY_COMPARE(text_of("inspectorTitle"), QString("Линия"));
    QCOMPARE(text_of("property_0"), QString("Длина"));
    QCOMPARE(text_of("propertyValue_0"), QString("50"));
    QTRY_VERIFY(zoom->mapToScene({0, 0}).x() < zoom_x - 300);
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u05_light.png"));

    // A rejected command shows an error toast.
    QCOMPARE(controller.apply("vertical"), 0ull);  // Both ends are fixed.
    QCOMPARE(controller.message(), QString("conflict"));
    QTRY_VERIFY(shown("toast"));
    QVERIFY(!text_of("toast").isEmpty());
    QCOMPARE(color_of("toast"), QColor("#b3261e"));

    // Dark theme from the overflow menu, and back.
    click(item("moreButton"));
    QTRY_VERIFY(shown("darkThemeToggle"));
    click(item("darkThemeToggle"));
    QTRY_COMPARE(window_->color(), QColor(dark.bg));
    expect_palette(dark);
    QCOMPARE(color_of("inspector"), QColor(dark.surface));
    QTest::qWait(400);
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u05_dark.png"));
    click(item("moreButton"));
    QTRY_VERIFY(shown("darkThemeToggle"));
    click(item("darkThemeToggle"));
    QTRY_COMPARE(window_->color(), QColor(light.bg));
  }
  void drag_keeps_horizontal_line() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    click(item("snapToggle"));
    click(item("tool_line"));
    tap(controller, 10, 60);
    tap(controller, 60, 60);
    click(item("tool_select"));
    tap(controller, 35, 60);
    click(item("action_horizontal"));
    QCOMPARE(controller.constraints().size(), 1);
    QTest::keyClick(window_, Qt::Key_Escape);

    const QPoint from = scene_of(controller, 10, 60);
    const QPoint to = scene_of(controller, 10, 70);
    QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, from);
    for (int i = 1; i <= 5; ++i)
      QTest::mouseMove(window_, from + (to - from) * i / 5);
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, to);

    const auto& s = controller.document().sketch();
    for (const auto& [id, e] : s.entities())
      if (const auto* l = std::get_if<sketchcad::SketchLine>(&e)) {
        const auto a = std::get<sketchcad::SketchPoint>(*s.entity(l->start));
        const auto b = std::get<sketchcad::SketchPoint>(*s.entity(l->end));
        QVERIFY2(std::abs(a.position.y - b.position.y) < 1e-6,
                 "line stays horizontal");
        // The dragged endpoint reaches the pointer (pixel rounding aside),
        // the other one follows it vertically.
        QVERIFY2(std::abs(a.position.y - 70) < 0.3, "dragged point at pointer");
        QVERIFY2(std::abs(b.position.y - 70) < 0.3, "other endpoint follows");
        QVERIFY(std::abs(b.position.x - 60) < 0.5);
      }
    QVERIFY(controller.document().undo_label() == "Move point");
  }
  void initTestCase() { QQuickStyle::setStyle("Material"); }
  void draw_select_delete_undo_and_zoom() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);

    click(item("snapToggle"));
    QVERIFY(!controller.snap_enabled());
    click(item("tool_line"));
    QCOMPARE(controller.tool(), QString("line"));
    tap(controller, 10, 60);
    QVERIFY(controller.in_progress());
    QVERIFY(item("contextBar")->isVisible());
    QTest::keyClick(window_, Qt::Key_Escape);
    QVERIFY(!controller.in_progress());
    QCOMPARE(controller.entity_count(), 0);

    tap(controller, 10, 60);
    tap(controller, 60, 60);
    QCOMPARE(controller.entity_count(), 3);

    click(item("tool_select"));
    tap(controller, 35, 60);
    QVERIFY(controller.has_selection());
    click(item("deleteSelection"));
    QCOMPARE(controller.entity_count(), 0);
    click(item("undoButton"));
    QCOMPARE(controller.entity_count(), 3);

    click(item("tool_rectangle"));
    tap(controller, 30, 30);
    tap(controller, 70, 80);
    QCOMPARE(controller.entity_count(), 11);

    // Wayland reports laptop touchpads as their own device type; the tool
    // handler must accept them like a mouse (synthetic touchpad input
    // needs private QPA API, so the handler configuration is checked).
    auto* tool_point = item("sketchCanvas")->findChild<QObject*>("toolPoint");
    QVERIFY(tool_point);
    const auto accepts = [&](QInputDevice::DeviceType type) {
      return (tool_point->property("acceptedDevices").toInt() &
              static_cast<int>(type)) != 0;
    };
    QVERIFY(accepts(QInputDevice::DeviceType::Mouse));
    QVERIFY(accepts(QInputDevice::DeviceType::TouchPad));
    QVERIFY(accepts(QInputDevice::DeviceType::Stylus));
    QVERIFY(accepts(QInputDevice::DeviceType::TouchScreen));
    click(item("fingerToggle"));
    QVERIFY(!controller.finger_draws());
    QVERIFY(!accepts(QInputDevice::DeviceType::TouchScreen));
    QVERIFY(accepts(QInputDevice::DeviceType::TouchPad));

    const double scale = controller.scale();
    const QPointF at = scene_of(controller, 50, 50);
    QWheelEvent wheel(at, window_->mapToGlobal(at), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(window_, &wheel);
    QTRY_VERIFY(controller.scale() > scale);
    click(item("fitView"));

    const QString shot =
        QCoreApplication::applicationDirPath() + "/sketch_u01.png";
    QVERIFY(window_->grabWindow().save(shot));
  }
  void constrain_dimension_conflict_and_panel() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    click(item("snapToggle"));
    click(item("tool_line"));
    tap(controller, 10, 60);
    tap(controller, 60, 60);
    click(item("tool_select"));
    tap(controller, 35, 60);
    QVERIFY(text_of("dofStatus").contains("4"));

    click(item("action_horizontal"));
    QCOMPARE(controller.constraints().size(), 1);
    QVERIFY(text_of("dofStatus").contains("3"));
    QVERIFY2(!item("action_radius"), "inapplicable actions are not offered");

    click(item("action_length"));
    QTRY_VERIFY(shown("dimensionEditor"));
    QCOMPARE(text_of("dimensionValue"), QString("50"));
    type("75");
    QCOMPARE(text_of("dimensionValue"), QString("75"));
    click(item("applyDimension"));
    QTRY_VERIFY(!shown("dimensionEditor"));
    QVERIFY(std::abs(line_length(controller) - 75) < 1e-6);

    // A second length at 75 is redundant; changing it to 90 conflicts.
    click(item("action_length"));
    QTRY_VERIFY(shown("dimensionEditor"));
    type("90");
    click(item("applyDimension"));
    QCOMPARE(controller.message(), QString("conflict"));
    QVERIFY(shown("dimensionEditor"));
    QVERIFY(!text_of("dimensionError").isEmpty());
    QVERIFY(!controller.conflict_path().isEmpty());
    QVERIFY(std::abs(line_length(controller) - 75) < 1e-6);
    QTest::qWait(400);  // Let the popup's enter transition finish.
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u02_conflict.png"));
    click(item("cancelDimension"));
    QTRY_VERIFY(!shown("dimensionEditor"));

    click(item("undoButton"));
    QCOMPARE(controller.constraints().size(), 2);

    click(item("constraintsToggle"));
    QTRY_VERIFY(shown("inspector"));
    QTRY_COMPARE(item("inspector")->x() + item("inspector")->width(),
                 window_->width() - 8.0);  // Slide-in finished.
    click(item("deleteConstraint_0"));
    QCOMPARE(controller.constraints().size(), 1);

    click(item("dimensionLabel_0"));
    QTRY_VERIFY(shown("dimensionEditor"));
    QCOMPARE(text_of("dimensionValue"), QString("75"));
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!shown("dimensionEditor"));

    const QString shot =
        QCoreApplication::applicationDirPath() + "/sketch_u02.png";
    QVERIFY(window_->grabWindow().save(shot));
  }
};
QTEST_MAIN(QmlTest)
#include "qml_test.moc"
