#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>
#include <cmath>

#include "controller.h"
class QmlTest : public QObject {
  Q_OBJECT
 private slots:
  void edit_drag_anchor_and_conflict() {
    Controller controller;
    QVERIFY(controller.resize(50, 30, false));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("sketch", &controller);
    engine.load(QUrl("qrc:/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto item = [&](const char* name) {
      return window->findChild<QQuickItem*>(name);
    };
    auto click = [&](QQuickItem* target) {
      QVERIFY(target);
      QTest::mouseClick(
          window, Qt::LeftButton, Qt::NoModifier,
          target->mapToScene(QPointF(target->width() / 2, target->height() / 2))
              .toPoint());
    };
    auto* width = item("widthInput");
    QVERIFY(width);
    width->setProperty("text", "70");
    click(item("applyDimensions"));
    auto points = controller.points();
    auto x = [](const QVariant& p) { return p.toMap()["x"].toDouble(); };
    auto y = [](const QVariant& p) { return p.toMap()["y"].toDouble(); };
    QVERIFY(std::abs(x(points[1]) - x(points[0]) - 70) < 1e-7);
    auto* canvas = item("sketchCanvas");
    QVERIFY(canvas);
    const QPoint corner =
        canvas
            ->mapToScene(QPointF(
                canvas->property("originX").toDouble() + x(points[0]) * 4,
                canvas->property("originY").toDouble() - y(points[0]) * 4))
            .toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, corner);
    QTest::mouseMove(window, corner + QPoint(40, -20), 20);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier,
                        corner + QPoint(40, -20));
    QTRY_VERIFY(std::abs(x(controller.points()[0]) - x(points[0]) - 10) < 1e-7);
    QVERIFY(std::abs(y(controller.points()[0]) - y(points[0]) - 5) < 1e-7);
    points = controller.points();
    click(item("conflictProbe"));
    QCOMPARE(controller.points(), points);
    QVERIFY(controller.status().startsWith("Rejected"));
    click(item("anchorOrigin"));
    QVERIFY(controller.anchored());
    QVERIFY(std::abs(x(controller.points()[0])) < 1e-7);
    QVERIFY(std::abs(y(controller.points()[0])) < 1e-7);
  }
};
QTEST_MAIN(QmlTest)
#include "qml_test.moc"
