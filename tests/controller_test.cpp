#include "controller.h"

#include <QtTest>
class ControllerTest : public QObject {
  Q_OBJECT
 private slots:
  void successful_edit_notifies() {
    Controller c;
    QSignalSpy spy(&c, &Controller::geometry_changed);
    QVERIFY(c.resize(50, 30, false));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(c.points().size(), 4);
    QVERIFY(c.translate(5, 7));
    QCOMPARE(spy.count(), 2);
  }
  void rejected_edit_preserves_geometry() {
    Controller c;
    QVERIFY(c.resize(50, 30, false));
    auto p = c.points();
    QSignalSpy spy(&c, &Controller::geometry_changed);
    QVERIFY(!c.resize(-1, 30, true));
    QCOMPARE(c.points(), p);
    QCOMPARE(spy.count(), 0);
    QVERIFY(!c.conflict());
    QCOMPARE(c.points(), p);
    QVERIFY(c.status().contains("Rejected"));
  }
  void anchor_blocks_drag() {
    Controller c;
    QVERIFY(c.resize(50, 30, true));
    QVERIFY(c.anchored());
    auto p = c.points();
    QVERIFY(!c.translate(1, 2));
    QCOMPARE(c.points(), p);
  }
};
QTEST_GUILESS_MAIN(ControllerTest)
#include "controller_test.moc"
