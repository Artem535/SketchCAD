// Child process for recovery crash and lock tests. Prints one line, then
// waits to be killed or aborts, never exiting normally.
#include <QCoreApplication>
#include <QThread>
#include <cstdio>
#include <cstdlib>

#include "recovery_store.h"
namespace {
void say(const QString& line) {
  std::printf("%s\n", qPrintable(line));
  std::fflush(stdout);
}
[[noreturn]] void wait_forever() {
  for (;;) QThread::sleep(1);
}
}  // namespace
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QStringList args = app.arguments();
  const auto value = [&](const QString& name) {
    const auto at = args.indexOf(name);
    return at >= 0 && at + 1 < args.size() ? args[at + 1] : QString();
  };
  recovery::Store store(value("--root"));
  if (!store.start()) return 2;
  const QString mode = value("--mode");
  if (mode == "hold") {
    say(store.session_id());
    wait_forever();
  }
  if (mode == "checkpoint") {
    sketchcad::Sketch sketch;
    const auto a = *sketch.create_point({1.5, 2.5});
    const auto b = *sketch.create_point({11.5, 2.5});
    sketch.create_line(a, b);
    if (store.write({sketch, 5},
                    QUrl::fromLocalFile("/tmp/исходный.sketchcad")))
      return 3;
    say(store.session_id());
    wait_forever();
  }
  if (mode == "restore-crash") {
    const auto claimed = store.claim(value("--id"));
    if (!std::holds_alternative<sketchcad::RecoveryData>(claimed) ||
        store.adopt())
      return 4;
    say("adopted");
    std::abort();
  }
  return 1;
}
