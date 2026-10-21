#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include "project_session.h"
#include "recovery_coordinator.h"
#include "sketch_controller.h"
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  // Before any standard path lookup: names the recovery directory.
  QCoreApplication::setApplicationName("SketchCAD");
  QQuickStyle::setStyle("Material");
  SketchController controller;
  ProjectSession project(controller);
  const int root_at = app.arguments().indexOf("--recovery-root");
  auto store = std::make_unique<recovery::Store>(
      root_at >= 0 && root_at + 1 < app.arguments().size()
          ? app.arguments()[root_at + 1]
          : recovery::Store::default_root());
  if (!store->start())
    qWarning("Recovery checkpoints are unavailable in %s",
             qPrintable(store->root()));
  RecoveryCoordinator recovery(controller, project, std::move(store));
  project.set_recovery(&recovery);
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("sketch", &controller);
  engine.rootContext()->setContextProperty("project", &project);
  engine.rootContext()->setContextProperty("recovery", &recovery);
  engine.load(QUrl("qrc:/Main.qml"));
  if (engine.rootObjects().isEmpty()) return 1;
  if (app.arguments().contains("--smoke"))
    QTimer::singleShot(500, &app, [&] {
      controller.set_snap_enabled(false);
      controller.set_tool("line");
      const QPointF a = controller.screen_of(10, 10);
      const QPointF b = controller.screen_of(40, 20);
      controller.press(a.x(), a.y());
      controller.release(a.x(), a.y());
      controller.press(b.x(), b.y());
      controller.release(b.x(), b.y());
      const bool ok = controller.entity_count() == 3 && controller.undo() &&
                      controller.entity_count() == 0 && controller.redo() &&
                      controller.entity_count() == 3;
      app.exit(ok ? 0 : 3);
    });
  const int index = app.arguments().indexOf("--screenshot");
  if (index >= 0 && index + 1 < app.arguments().size())
    QTimer::singleShot(300, &app, [&] {
      auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
      const bool ok =
          window && window->grabWindow().save(app.arguments()[index + 1]);
      if (!ok) app.exit(4);
    });
  return app.exec();
}
