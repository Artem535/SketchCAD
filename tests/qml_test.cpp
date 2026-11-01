#include <QtQuickTest/quicktest.h>

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QInputDevice>
#include <QPointingDevice>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QtTest>

#include "project_session.h"
#include "recovery_coordinator.h"
#include "sketch_controller.h"
class QmlTest : public QObject {
  Q_OBJECT
  QQuickWindow* window_ = nullptr;
  std::unique_ptr<QTemporaryDir> scratch_;
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

  // Recovery checkpoints always live in a temporary root, never the user's.
  void open(QQmlApplicationEngine& engine, SketchController& controller,
            QString recovery_root = {}, recovery::WriterFactory writer = {},
            std::function<std::int64_t()> now = {}) {
    if (recovery_root.isEmpty()) {
      scratch_ = std::make_unique<QTemporaryDir>();
      recovery_root = scratch_->path();
    }
    engine.rootContext()->setContextProperty("sketch", &controller);
    auto* project = new ProjectSession(controller, &controller);
    auto store =
        std::make_unique<recovery::Store>(recovery_root, std::move(writer));
    store->start();
    auto* recovery = new RecoveryCoordinator(
        controller, *project, std::move(store),
        RecoveryOptions{std::move(now), !now}, &controller);
    project->set_recovery(recovery);
    engine.rootContext()->setContextProperty("project", project);
    engine.rootContext()->setContextProperty("recovery", recovery);
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
  bool popup_open(const char* name) {
    QObject* popup = window_->findChild<QObject*>(name);
    return popup && popup->property("visible").toBool();
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
  void recovery_list_discard_later_menu_and_restore() {
    QTemporaryDir root;
    const QString source =
        QDir(root.path()).filePath(QString::fromUtf8("деталь.sketchcad"));
    sketchcad::Sketch saved;
    {
      recovery::Store crashed(root.path());
      QVERIFY(crashed.start());
      const auto a = *saved.create_point({10, 40});
      const auto b = *saved.create_point({60, 40});
      saved.create_line(a, b);
      QVERIFY(!crashed.write({saved, 4}, QUrl::fromLocalFile(source)));
    }
    const QString corrupt =
        QDir(root.path())
            .filePath(
                "00000000-0000-4000-8000-000000000001.sketchcad-recovery");
    QFile bad(corrupt);
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("{}");
    bad.close();
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller, root.path());
    QTRY_VERIFY(popup_open("recoveryDialog"));
    QTRY_VERIFY(item("recoveryRow_1"));
    // Display order follows time; find the rows by name.
    const int valid =
        text_of("recoveryName_0") == QString::fromUtf8("деталь.sketchcad") ? 0
                                                                           : 1;
    const int broken = 1 - valid;
    const auto name = [](const char* prefix, int i) {
      return (QByteArray(prefix) + QByteArray::number(i));
    };
    QCOMPARE(text_of(name("recoveryName_", valid)),
             QString::fromUtf8("деталь.sketchcad"));
    QCOMPARE(text_of(name("recoveryName_", broken)),
             QString::fromUtf8("Без имени"));
    QVERIFY(item(name("recoveryRestore_", valid))->isEnabled());
    QVERIFY(!item(name("recoveryRestore_", broken))->isEnabled());
    QVERIFY(text_of(name("recoveryStatus_", broken))
                .contains(QString::fromUtf8("повреждена")));
    QTest::qWait(300);
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_f02_recovery_list.png"));
    click(item(name("recoveryDiscard_", broken)));
    QTRY_VERIFY(!item("recoveryRow_1"));
    QVERIFY(!QFile::exists(corrupt));
    click(item("recoveryLater"));
    QTRY_VERIFY(!popup_open("recoveryDialog"));
    // "Not now" keeps the crashed copy; this session has no copy yet.
    QCOMPARE(QDir(root.path()).entryList({"*.sketchcad-recovery"}).size(), 1);
    click(item("moreButton"));
    QTRY_VERIFY(engine.rootObjects()
                    .front()
                    ->findChild<QObject*>("moreMenu")
                    ->property("visible")
                    .toBool());
    QTest::qWait(200);
    click(item("fileRecovery"));
    QTRY_VERIFY(popup_open("recoveryDialog"));
    QTRY_VERIFY(item("recoveryRestore_0"));
    QTest::qWait(200);
    click(item("recoveryRestore_0"));
    QTRY_VERIFY(!popup_open("recoveryDialog"));
    QTRY_COMPARE(controller.entity_count(), 3);
    QVERIFY(controller.document().sketch() == saved);
    QCOMPARE(
        window_->title(),
        QString::fromUtf8("Восстановлено — деталь.sketchcad * — SketchCAD"));
    QVERIFY(!QFile::exists(source));
    QTest::qWait(300);
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_f02_restored.png"));
  }
  void autosave_failure_shows_a_banner() {
    std::int64_t now = 0;
    SketchController controller;
    QQmlApplicationEngine engine;
    struct Fail : project_file::detail::AtomicWriter {
      bool open(const QString&) override { return false; }
      qint64 write(const QByteArray&) override { return -1; }
      bool commit() override { return false; }
    };
    open(
        engine, controller, {}, [] { return std::make_unique<Fail>(); },
        [&] { return now; });
    QVERIFY(!shown("autosaveWarning"));
    click(item("tool_line"));
    tap(controller, 10, 40);
    tap(controller, 60, 40);
    now = 6000;
    auto* recovery = qobject_cast<RecoveryCoordinator*>(
        engine.rootContext()->contextProperty("recovery").value<QObject*>());
    recovery->poll();
    QTRY_VERIFY(shown("autosaveWarning"));
    QVERIFY(
        text_of("autosaveWarningText").contains(QString::fromUtf8("копию")));
  }
  void project_file_commands_save_new_open_and_cancel() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    auto* project = qobject_cast<ProjectSession*>(
        engine.rootContext()->contextProperty("project").value<QObject*>());
    QVERIFY(project);
    QTemporaryDir dir;
    const auto url = QUrl::fromLocalFile(
        dir.filePath(QString::fromUtf8("Проверка размеров.sketchcad")));
    click(item("tool_line"));
    tap(controller, 10, 40);
    tap(controller, 60, 40);
    click(item("tool_dimension"));
    tap(controller, 10, 40);
    tap(controller, 60, 40);
    tap(controller, 35, 55);
    QTRY_COMPARE(controller.dimensions().size(), 1);
    const auto dimension_id =
        controller.dimensions().front().toMap()["id"].toULongLong();
    QVERIFY(controller.document().sketch().constraint(dimension_id)->reference);
    QVERIFY(controller.document()
                .sketch()
                .constraint(dimension_id)
                ->placement.has_value());
    const auto saved_sketch = controller.document().sketch();
    QVERIFY(project->dirty());
    click(item("moreButton"));
    QVERIFY(item("fileSaveAs"));
    click(item("fileSaveAs"));
    QVERIFY(project->busy());
    project->accept_file(url);
    QVERIFY(!project->dirty());
    QCOMPARE(text_of("projectName"),
             QString::fromUtf8("Проверка размеров.sketchcad"));
    click(item("undoButton"));
    QVERIFY(project->dirty());
    click(item("moreButton"));
    click(item("fileNew"));
    const auto guarded_revision = controller.document().revision();
    QTest::keySequence(window_, QKeySequence(QKeySequence::Redo));
    QCOMPARE(controller.document().revision(), guarded_revision);
    click(item("unsavedCancel"));
    QVERIFY(project->dirty());
    QVERIFY(!project->busy());
    QTRY_VERIFY(!engine.rootObjects()
                     .front()
                     ->findChild<QObject*>("unsavedDialog")
                     ->property("visible")
                     .toBool());
    click(item("moreButton"));
    QTRY_VERIFY(engine.rootObjects()
                    .front()
                    ->findChild<QObject*>("moreMenu")
                    ->property("visible")
                    .toBool());
    QTest::qWait(200);
    click(item("fileNew"));
    QTRY_VERIFY(project->busy());
    click(item("unsavedDiscard"));
    QTRY_VERIFY(!project->busy());
    QTRY_VERIFY(!engine.rootObjects()
                     .front()
                     ->findChild<QObject*>("unsavedDialog")
                     ->property("visible")
                     .toBool());
    QCOMPARE(controller.entity_count(), 0);
    QVERIFY(!project->dirty());
    click(item("moreButton"));
    click(item("fileOpen"));
    QVERIFY(project->busy());
    project->accept_file(url);
    QCOMPARE(controller.entity_count(), 3);
    QVERIFY(!project->dirty());
    QVERIFY(!controller.can_undo());
    QVERIFY(controller.document().sketch() == saved_sketch);
    click(item("moreButton"));
    click(item("fileSave"));
    QVERIFY(!project->busy());
    QCOMPARE(text_of("projectState"),
             QString::fromUtf8("Сохранено · объектов: 3"));
    QTRY_VERIFY(!engine.rootObjects()
                     .front()
                     ->findChild<QObject*>("moreMenu")
                     ->property("visible")
                     .toBool());
    const auto center = controller.screen_of(35, 40);
    controller.zoom_at(center.x(), center.y(), 0.5);
    QTRY_COMPARE(controller.dimension_labels().size(), 1);
    QTRY_VERIFY(item("dimensionLabel_0"));
    QTest::qWait(300);
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_f01_opened.png"));
  }
  void chamfer_tool_edits_a_rectangle_with_numeric_input() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    controller.set_auto_dimensions(true);
    click(item("tool_rectangle"));
    tap(controller, 20, 20);
    tap(controller, 60, 50);
    QVERIFY(item("tool_chamfer"));
    QQuickItem* rail = item("toolRailFlick");
    QVERIFY(rail);
    rail->setProperty(
        "contentY", rail->property("contentHeight").toReal() - rail->height());
    QTest::qWait(20);
    click(item("tool_chamfer"));
    QCOMPARE(controller.tool(), QString("chamfer"));
    QVERIFY(item("chamferPrompt"));
    QCOMPARE(item("chamferPrompt")->property("text").toString(),
             QString("Выберите первый отрезок"));
    QVERIFY(!item("chamferDistance")->isVisible());
    const auto before = controller.document().sketch();
    const auto revision = controller.document().revision();
    tap(controller, 40, 20);
    QCOMPARE(item("chamferPrompt")->property("text").toString(),
             QString("Выберите второй отрезок"));
    QVERIFY(!item("chamferDistance")->isVisible());
    tap(controller, 20, 35);
    QCOMPARE(item("chamferPrompt")->property("text").toString(),
             QString("Задайте отступ и подтвердите"));
    QVERIFY(item("chamferDistance")->isVisible());
    QVERIFY(item("finishShape")->isVisible());
    // A filled chip, not a bare caption over the grid (U11 acceptance).
    const QColor chip = color_of("chamferDistance_background");
    QVERIFY(chip.isValid());
    QCOMPARE(chip.alpha(), 255);
    click(item("chamferDistance"));
    QTRY_VERIFY(item("dimensionEditor"));
    type("100");
    click(item("applyDimension"));
    QTRY_VERIFY(!shown("dimensionEditor"));
    QTRY_VERIFY(!item("finishShape")->isEnabled());
    QCOMPARE(controller.message(), QString("chamfer_too_long"));
    QVERIFY(shown("toast"));
    QVERIFY(controller.edit_preview_path().isEmpty());
    QVERIFY(controller.document().sketch() == before);
    QTest::qWait(200);
    click(item("chamferDistance"));
    QTRY_VERIFY(item("dimensionEditor"));
    click(item("key_3"));
    click(item("applyDimension"));
    QTRY_COMPARE(controller.input_value(), 3.0);
    QVERIFY(!shown("toast"));
    QTRY_VERIFY(item("finishShape")->isEnabled());
    QTRY_VERIFY(!shown("dimensionEditor"));
    QVERIFY(controller.document().sketch() == before);
    QCOMPARE(controller.document().revision(), revision);
    QTest::qWait(100);
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u11_chamfer_preview.png"));
    click(item("finishShape"));
    QCOMPARE(controller.document().undo_label(), std::string("Chamfer"));
    std::size_t lines = 0;
    for (const auto& [id, e] : controller.document().sketch().entities())
      lines += std::holds_alternative<sketchcad::SketchLine>(e);
    QCOMPARE(lines, 5u);
    QCOMPARE(controller.dimensions().size(), 4);
    QVERIFY(window_->property("helpKeys").toStringList().contains("chamfer"));
    QTest::qWait(100);
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u11_chamfer.png"));
  }

  void chamfer_keyboard_confirmation_and_cancel() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    controller.set_tool("rectangle");
    tap(controller, 20, 20);
    tap(controller, 60, 50);
    const auto before = controller.document().sketch();
    controller.set_tool("chamfer");
    tap(controller, 40, 20);
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!controller.in_progress());
    QVERIFY(controller.document().sketch() == before);
    tap(controller, 40, 20);
    tap(controller, 20, 35);
    QVERIFY(!controller.edit_preview_path().isEmpty());
    click(item("closeContext"));
    QTRY_VERIFY(!controller.in_progress());
    QVERIFY(controller.document().sketch() == before);
    tap(controller, 40, 20);
    tap(controller, 20, 35);
    // Typing opens the keypad. Enter accepts the size without committing.
    QTest::keyClick(window_, Qt::Key_4);
    QTRY_VERIFY(shown("dimensionEditor"));
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_VERIFY(!shown("dimensionEditor"));
    QCOMPARE(controller.input_value(), 4.0);
    QVERIFY(controller.document().sketch() == before);
    QTest::qWait(200);
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_COMPARE(controller.document().undo_label(), std::string("Chamfer"));
    controller.undo();
    QVERIFY(controller.document().sketch() == before);
  }

  void chamfer_preview_grip_and_dimensions_work_with_mouse_and_keypad() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    controller.set_tool("rectangle");
    tap(controller, 20, 20);
    tap(controller, 60, 50);
    controller.set_tool("chamfer");
    tap(controller, 40, 20);
    tap(controller, 20, 35);
    const auto before = controller.document().sketch();
    const auto revision = controller.document().revision();
    QQuickItem* grip = item("chamferHandle");
    QVERIFY(grip);
    QVERIFY(grip->isVisible());
    QVERIFY(grip->width() >= 56 && grip->height() >= 56);
    QCOMPARE(controller.preview_dimension_labels().size(), 2);
    const auto at =
        grip->mapToScene({grip->width() / 2, grip->height() / 2}).toPoint();
    const auto origin = controller.screen_of(20, 20);
    const double initial = controller.input_value();
    QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, at);
    QTest::mouseMove(window_, at + QPoint(12, -12), 20);
    QTest::mouseMove(window_, at + QPoint(32, -32), 20);
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier,
                        at + QPoint(32, -32));
    QVERIFY(controller.input_value() > initial);
    QVERIFY(controller.document().sketch() == before);
    QCOMPARE(controller.document().revision(), revision);
    QCOMPARE(controller.screen_of(20, 20), origin);
    QVERIFY(!controller.document().gesture_active());
    for (int i = 0; i < 2; ++i) {
      const auto name = "previewDimensionLabel_" + QByteArray::number(i);
      click(item(name.constData()));
      QTRY_VERIFY(shown("dimensionEditor"));
      click(item(i == 0 ? "key_3" : "key_4"));
      click(item("applyDimension"));
      QTRY_VERIFY(!shown("dimensionEditor"));
      QCOMPARE(controller.input_value(), i == 0 ? 3.0 : 4.0);
      for (const auto& label : controller.preview_dimension_labels())
        QCOMPARE(label.toMap()["text"].toString(),
                 i == 0 ? QString("3") : QString("4"));
      QVERIFY(controller.document().sketch() == before);
      QTest::qWait(200);
    }
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u11_chamfer_grip.png"));
    click(item("finishShape"));
    QCOMPARE(controller.document().undo_label(), std::string("Chamfer"));
    QVERIFY(!grip->isVisible());
    QVERIFY(controller.preview_dimension_labels().empty());
    controller.undo();
    QVERIFY(controller.document().sketch() == before);
  }

  void chamfer_grip_touch_resizes_in_finger_pan_mode_without_panning_data() {
    QTest::addColumn<QString>("corner_tool");
    QTest::newRow("chamfer") << QString("chamfer");
    QTest::newRow("fillet") << QString("fillet");
  }
  void chamfer_grip_touch_resizes_in_finger_pan_mode_without_panning() {
    QFETCH(QString, corner_tool);
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    controller.set_tool("rectangle");
    tap(controller, 20, 20);
    tap(controller, 60, 50);
    controller.set_tool(corner_tool);
    tap(controller, 40, 20);
    tap(controller, 20, 35);
    controller.set_finger_draws(false);
    const auto before = controller.document().sketch();
    const auto origin = controller.screen_of(20, 20);
    const auto initial = controller.input_value();
    auto* grip =
        item(corner_tool == "fillet" ? "filletHandle" : "chamferHandle");
    QVERIFY(grip);
    QVERIFY(grip->isVisible());
    const auto at =
        grip->mapToScene({grip->width() / 2, grip->height() / 2}).toPoint();
    std::unique_ptr<QPointingDevice> touch{QTest::createTouchDevice()};
    QVERIFY(QQuickTest::qWaitForPolish(window_));
    auto sequence = QTest::touchEvent(window_, touch.get(), false);
    sequence.press(0, at, window_).commit();
    QTest::qWait(20);
    sequence.move(0, at + QPoint(15, -15), window_).commit();
    QTest::qWait(20);
    sequence.move(0, at + QPoint(40, -40), window_).commit();
    QTest::qWait(20);
    sequence.release(0, at + QPoint(40, -40), window_).commit();
    QVERIFY(controller.input_value() > initial);
    QCOMPARE(controller.screen_of(20, 20), origin);
    QVERIFY(controller.document().sketch() == before);
    click(item("closeContext"));
    QVERIFY(!controller.in_progress());
    QVERIFY(!grip->isVisible());
    QVERIFY(controller.preview_dimension_labels().empty());
    QVERIFY(controller.document().sketch() == before);
  }

  void chamfer_grip_perpendicular_drag_at_limit_does_not_change_size() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    controller.set_tool("rectangle");
    tap(controller, 20, 20);
    tap(controller, 60, 50);
    controller.set_tool("chamfer");
    tap(controller, 40, 20);
    tap(controller, 20, 35);
    QVERIFY(controller.resize_chamfer(10000, -10000));
    const double limit = controller.input_value();
    auto* grip = item("chamferHandle");
    QVERIFY(grip);
    QVERIFY(QQuickTest::qWaitForPolish(window_));
    const auto at =
        grip->mapToScene({grip->width() / 2, grip->height() / 2}).toPoint();
    QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, at);
    QTest::mouseMove(window_, at + QPoint(12, 12), 20);
    QTest::mouseMove(window_, at + QPoint(32, 32), 20);
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier,
                        at + QPoint(32, 32));
    QVERIFY(std::abs(controller.input_value() - limit) < 1e-8);
  }

  void fillet_tool_radius_grip_keypad_confirm_and_undo() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    controller.set_auto_dimensions(true);
    controller.set_tool("rectangle");
    tap(controller, 20, 20);
    tap(controller, 60, 50);
    const auto before = controller.document().sketch();
    auto* rail = item("toolRailFlick");
    QVERIFY(rail);
    rail->setProperty(
        "contentY", rail->property("contentHeight").toReal() - rail->height());
    QTest::qWait(20);
    QVERIFY(item("tool_fillet"));
    click(item("tool_fillet"));
    QCOMPARE(controller.tool(), QString("fillet"));
    tap(controller, 40, 20);
    tap(controller, 20, 35);
    QVERIFY(item("filletPrompt"));
    QVERIFY(item("filletRadius")->isVisible());
    auto* grip = item("filletHandle");
    QVERIFY(grip);
    QVERIFY(grip->isVisible());
    QCOMPARE(controller.preview_dimension_labels().size(), 1);
    const auto initial = controller.input_value();
    const auto origin = controller.screen_of(20, 20);
    const auto at = grip->mapToScene({28, 28}).toPoint();
    QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, at);
    QTest::mouseMove(window_, at + QPoint(12, -12), 20);
    QTest::mouseMove(window_, at + QPoint(24, -24), 20);
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier,
                        at + QPoint(24, -24));
    QVERIFY(controller.input_value() > initial);
    QCOMPARE(controller.screen_of(20, 20), origin);
    QVERIFY(controller.document().sketch() == before);
    click(item("previewDimensionLabel_0"));
    QTRY_VERIFY(shown("dimensionEditor"));
    click(item("key_3"));
    click(item("applyDimension"));
    QTRY_VERIFY(!shown("dimensionEditor"));
    QCOMPARE(controller.input_value(), 3.0);
    QVERIFY(controller.document().sketch() == before);
    QTest::qWait(200);
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u12_fillet_preview.png"));
    click(item("finishShape"));
    QCOMPARE(controller.document().undo_label(), std::string("Fillet"));
    std::size_t arcs = 0;
    for (const auto& [id, e] : controller.document().sketch().entities())
      arcs += std::holds_alternative<sketchcad::SketchArc>(e);
    QCOMPARE(arcs, 1u);
    QTest::qWait(100);
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u12_fillet.png"));
    controller.undo();
    QVERIFY(controller.document().sketch() == before);
    tap(controller, 40, 20);
    tap(controller, 20, 35);
    click(item("closeContext"));
    QVERIFY(!controller.in_progress());
    QVERIFY(controller.document().sketch() == before);
  }

  void wheel_over_tool_rail_does_not_zoom() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    QQuickItem* rail = item("toolRailFlick");
    QVERIFY(rail);
    const double scale = controller.scale();
    const QPointF at = rail->mapToScene(
        {rail->width() / 2, rail->height() / 2});
    // Scrolls down past the end of the list, as in U11 acceptance.
    for (int i = 0; i < 12; ++i) {
      QWheelEvent wheel(at, window_->mapToGlobal(at), QPoint(), QPoint(0, -120),
                        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                        false);
      QCoreApplication::sendEvent(window_, &wheel);
      QTest::qWait(20);
    }
    QCOMPARE(controller.scale(), scale);
    QTRY_VERIFY(rail->property("contentY").toReal() > 0);
  }
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
        if (std::abs(a.position.y - 40) < 1e-6 &&
            std::abs(b.position.y - 40) < 1e-6)
          QVERIFY(std::abs(std::max(a.position.x, b.position.x) - 30) < 1e-6);
      }
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u04_trim.png"));
  }
  void extend_reaches_a_boundary_continuation() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    click(item("tool_line"));
    tap(controller, 10, 60);
    tap(controller, 30, 50);
    tap(controller, 60, 60);
    tap(controller, 60, 80);
    QQuickItem* rail = item("toolRailFlick");
    QVERIFY(rail);
    rail->setProperty("contentY", rail->property("contentHeight").toDouble() -
                                      rail->height());
    click(item("tool_extend"));
    QCOMPARE(controller.tool(), QString("extend"));
    const auto p = controller.screen_of(30, 50);
    controller.hover(p.x(), p.y());
    QVERIFY(!controller.edit_preview_path().isEmpty());
    tap(controller, 30, 50);
    QCOMPARE(controller.document().undo_label(), std::string("Extend"));
    bool reached = false;
    const auto& s = controller.document().sketch();
    for (const auto& [id, e] : s.entities())
      if (const auto* point = std::get_if<sketchcad::SketchPoint>(&e))
        if (std::abs(point->position.x - 60) < 1e-6 &&
            std::abs(point->position.y - 35) < 1e-6)
          reached = true;
    QVERIFY(reached);
    QTest::qWait(100);
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u04_extend_continuation.png"));
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
        QVERIFY2(std::abs(c.position.x - 56) < 0.5,
                 qPrintable(QString::number(c.position.x)));
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
    QCOMPARE(controller.document().undo_label(),
             std::optional<std::string>("Move dimension"));
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
    const QStringList actions{
        "coincident", "horizontal", "vertical", "parallel",    "perpendicular",
        "tangent",    "equal",      "fix",      "length",      "distance",
        "angle",      "radius",     "on_curve", "construction"};
    const QStringList keys = window_->property("helpKeys").toStringList();
    QCOMPARE(keys.size(), 32);
    for (const QString& key :
         actions + QStringList{"constraint", "dimension", "dof", "defined",
                               "redundant", "conflict", "snap", "finger",
                               "autodim", "input", "modes", "reference", "trim",
                               "extend", "chamfer", "fillet", "profiles",
                               "merge_vertices"}) {
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
    QCOMPARE(text_of("helpTitle"),
             window_->property("helpTitles").toMap()["length"].toString());
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
    QCOMPARE(text_of("helpTitle"),
             window_->property("helpTitles").toMap()["redundant"].toString());
    QTest::qWait(300);
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u07_help.png"));
  }
  void collapse_short_edge_through_keypad() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    controller.set_tool("rectangle");
    tap(controller, 20, 20);
    tap(controller, 70, 40);
    controller.set_tool("chamfer");
    tap(controller, 45, 40);
    tap(controller, 20, 35);
    QVERIFY(controller.enter_value(10.51));
    QVERIFY(controller.finish());
    tap(controller, 45, 20);
    tap(controller, 20, 24);
    QVERIFY(controller.enter_value(9.48992));
    QVERIFY(controller.finish());
    const auto before = controller.document().sketch();
    click(item("moreButton"));
    QTRY_VERIFY(shown("mergeVerticesAction"));
    click(item("mergeVerticesAction"));
    QTRY_VERIFY(shown("dimensionEditor"));
    click(item("applyDimension"));
    QVERIFY(shown("dimensionEditor"));
    QVERIFY(controller.document().sketch() == before);
    click(item("collapseShortEdges"));
    QTRY_VERIFY(shown("collapseDimensionsNotice"));
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u13_collapse_keypad.png"));
    click(item("applyDimension"));
    QTRY_VERIFY(!shown("dimensionEditor"));
    QVERIFY(controller.profile_ready());
    QVERIFY(controller.undo());
    QVERIFY(controller.document().sketch() == before);
    QVERIFY(controller.redo());
    QVERIFY(controller.profile_ready());
  }

  void profiles_and_merge_vertices_from_pill_and_menu() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    window_->setProperty("darkTheme", false);
    click(item("snapToggle"));
    QVERIFY(!shown("profileStatus"));

    // Draw an almost-closed 4-segment loop with ~0.5 mm corner gaps.
    click(item("tool_line"));
    tap(controller, 20.0, 20.0);
    tap(controller, 69.6, 20.0);
    tap(controller, 70.0, 20.3);
    tap(controller, 70.0, 59.6);
    tap(controller, 69.7, 60.0);
    tap(controller, 20.4, 60.0);
    tap(controller, 20.0, 59.7);
    tap(controller, 20.0, 20.4);
    click(item("tool_select"));

    QTRY_VERIFY(shown("profileStatus"));
    QVERIFY(!controller.profile_ready());
    QCOMPARE(controller.open_endpoint_count(), 8);
    QVERIFY(!controller.open_endpoints_path().isEmpty());
    QCOMPARE(color_of("profileStatus_background"), QColor("#fbe5e3"));
    QTest::qWait(100);
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u13_profiles_open.png"));

    // Menu action opens keypad prefilled with default tolerance (0.5 mm);
    // a tiny tolerance keeps the keypad open with an inline message.
    click(item("moreButton"));
    QTRY_VERIFY(shown("mergeVerticesAction"));
    click(item("mergeVerticesAction"));
    QTRY_VERIFY(shown("dimensionEditor"));
    QCOMPARE(text_of("dimensionValue"), QString("0.5"));
    QVERIFY(shown("collapseShortEdges"));
    QVERIFY(!item("collapseShortEdges")->property("checked").toBool());
    click(item("collapseShortEdges"));
    QVERIFY(item("collapseShortEdges")->property("checked").toBool());
    QVERIFY(shown("collapseDimensionsNotice"));
    type("0.1");
    click(item("applyDimension"));
    QVERIFY(shown("dimensionEditor"));
    QVERIFY(!text_of("dimensionError").isEmpty());
    click(item("cancelDimension"));
    QTRY_VERIFY(!shown("dimensionEditor"));

    // Tapping the open-gap pill opens the tolerance keypad and merges corners.
    click(item("profileStatus"));
    QTRY_VERIFY(shown("dimensionEditor"));
    QVERIFY(!item("collapseShortEdges")->property("checked").toBool());
    type("1");
    click(item("applyDimension"));
    QTRY_VERIFY(!shown("dimensionEditor"));
    QCOMPARE(controller.document().undo_label(), std::string("Merge vertices"));
    QVERIFY(controller.profile_ready());
    QCOMPARE(controller.profile_count(), 1);
    QCOMPARE(color_of("profileStatus_background"), QColor("#e2f4ee"));

    // Add a circular hole inside and fillet a corner: profile stays closed.
    click(item("tool_circle"));
    tap(controller, 45, 40);
    tap(controller, 52, 40);
    QCOMPARE(controller.hole_count(), 1);
    QVERIFY(controller.profile_ready());
    auto* rail = item("toolRailFlick");
    QVERIFY(rail);
    rail->setProperty(
        "contentY", rail->property("contentHeight").toReal() - rail->height());
    QTest::qWait(20);
    click(item("tool_fillet"));
    tap(controller, 45, 20);
    tap(controller, 20, 40);
    click(item("finishShape"));
    QVERIFY(controller.profile_ready());
    QCOMPARE(controller.profile_count(), 1);
    QCOMPARE(controller.hole_count(), 1);
    QTest::qWait(100);
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u13_profiles_closed.png"));
  }

  void profile_controls_fit_the_minimum_tablet_width() {
    SketchController controller;
    QQmlApplicationEngine engine;
    open(engine, controller);
    window_->resize(720, 800);
    QTest::qWait(30);
    controller.set_tool("rectangle");
    auto at = controller.screen_of(20, 20);
    controller.press(at.x(), at.y());
    controller.release(at.x(), at.y());
    at = controller.screen_of(60, 50);
    controller.press(at.x(), at.y());
    controller.release(at.x(), at.y());
    QTRY_VERIFY(controller.profile_ready());
    QVERIFY(QQuickTest::qWaitForPolish(window_));
    const auto* pill = item("profileStatus");
    const auto* dof = item("dofStatus");
    const auto* menu = item("moreButton");
    QVERIFY(pill && dof && menu);
    const auto pill_at = pill->mapToScene({0, 0});
    const auto dof_at = dof->mapToScene({0, 0});
    const auto menu_at = menu->mapToScene({0, 0});
    QVERIFY(pill_at.x() >= 0);
    QVERIFY(pill_at.x() + pill->width() <= dof_at.x());
    QVERIFY(dof_at.x() + dof->width() <= menu_at.x());
    QVERIFY(menu_at.x() + menu->width() <= window_->width());
    QVERIFY(window_->grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/sketch_u13_profiles_compact.png"));
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
