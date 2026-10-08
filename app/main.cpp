#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>

#include "controller.h"
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  Controller controller;
  if (!controller.resize(50, 30, false)) return 2;
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("sketch", &controller);
  engine.load(QUrl("qrc:/Main.qml"));
  if (engine.rootObjects().isEmpty()) return 1;
  if (app.arguments().contains("--smoke"))
    QTimer::singleShot(500, &app, [&] {
      const bool ok = controller.translate(5, 7) && !controller.conflict() &&
                      controller.resize(50, 30, true) &&
                      !controller.translate(1, 1);
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
