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
  QString text_of(const char* name) {
    QQuickItem* i = item(name);
    return i ? i->property("text").toString() : QString();
  }

 private slots:
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
    QTRY_VERIFY(shown("constraintPanel"));
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
