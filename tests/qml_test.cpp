#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
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

 private slots:
  void initTestCase() { QQuickStyle::setStyle("Material"); }
  void draw_select_delete_undo_and_zoom() {
    SketchController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("sketch", &controller);
    engine.load(QUrl("qrc:/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    window_ = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    QVERIFY(window_);
    QVERIFY(QTest::qWaitForWindowExposed(window_));
    window_->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(window_));
    QVERIFY(item("sketchCanvas"));

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
};
QTEST_MAIN(QmlTest)
#include "qml_test.moc"
