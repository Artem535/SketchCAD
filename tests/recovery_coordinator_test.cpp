#include "recovery_coordinator.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "project_session.h"
#include "sketch_controller.h"
namespace {
struct Writer : project_file::detail::AtomicWriter {
  bool open(const QString&) override { return true; }
  qint64 write(const QByteArray& b) override { return b.size(); }
  bool commit() override { return false; }
};
// A controller, file session and coordinator over a temporary root with a
// manual clock: tests move `now` and call poll().
struct Fixture {
  QTemporaryDir dir;
  std::int64_t now = 1000;
  bool fail_writes = false;
  SketchController c;
  ProjectSession p{c};
  std::unique_ptr<RecoveryCoordinator> r;
  Fixture() {
    c.set_viewport_size(1000, 700);
    c.set_snap_enabled(false);
    c.set_auto_dimensions(false);
    auto store = std::make_unique<recovery::Store>(
        dir.path(),
        [this]() -> std::unique_ptr<project_file::detail::AtomicWriter> {
          if (fail_writes) return std::make_unique<Writer>();
          return project_file::detail::make_save_writer();
        });
    store->start();
    r = std::make_unique<RecoveryCoordinator>(
        c, p, std::move(store),
        RecoveryCoordinator::Options{[this] { return now; }, false});
  }
  QString path() {
    return dir.filePath(r->store().session_id() + ".sketchcad-recovery");
  }
  bool exists() { return QFile::exists(path()); }
  std::optional<sketchcad::RecoveryData> file() {
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    const QByteArray bytes = f.readAll();
    auto decoded = sketchcad::decode_recovery(
        std::string_view(bytes.constData(), bytes.size()));
    if (!std::holds_alternative<sketchcad::RecoveryData>(decoded))
      return std::nullopt;
    return std::get<sketchcad::RecoveryData>(decoded);
  }
  void at(std::int64_t t) {
    now = t;
    r->poll();
  }
  void tap(double x, double y) {
    const QPointF s = c.screen_of(x, y);
    c.press(s.x(), s.y());
    c.release(s.x(), s.y());
  }
  void line(double y = 10) {
    c.set_tool("line");
    tap(10, y);
    tap(60, y);
    c.cancel();
  }
};
}  // namespace
class RecoveryCoordinatorTest : public QObject {
  Q_OBJECT
 private slots:
  void debounce_writes_five_seconds_after_commit() {
    Fixture f;
    QVERIFY(f.r->active());
    f.at(1000);
    QVERIFY(!f.exists());  // clean untitled document
    f.line();
    f.at(5999);
    QVERIFY(!f.exists());
    f.at(6000);
    QVERIFY(f.exists());
    const auto data = f.file();
    QVERIFY(data);
    QVERIFY(data->project.sketch == f.c.document().sketch());
    QCOMPARE(data->project.revision, f.c.document().revision());
    QVERIFY(data->meta.original_url.empty());
    QVERIFY(f.p.dirty());
    QVERIFY(f.c.can_undo());
    QVERIFY(!f.c.can_redo());
  }
  void continuous_commits_write_within_thirty_seconds() {
    Fixture f;
    f.c.set_tool("circle");
    for (int i = 0; i < 29; ++i) {
      f.now = 1000 + i * 1000;
      f.tap(i * 5, 0);
      f.tap(i * 5 + 1, 0);
      f.r->poll();
      QVERIFY2(!f.exists(), qPrintable(QString::number(i)));
    }
    f.at(30999);
    QVERIFY(!f.exists());
    f.at(31000);
    QVERIFY(f.exists());
  }
  void view_events_do_not_write_and_same_state_is_written_once() {
    Fixture f;
    f.line();
    f.at(6000);
    QVERIFY(f.exists());
    const auto generation = f.r->store().generation();
    f.c.zoom_at(500, 350, 1.5);
    f.c.pan(10, 10);
    f.c.hover(100, 100);
    f.c.set_tool("select");
    f.tap(35, 10);
    f.c.clear_selection();
    f.at(60000);
    QCOMPARE(f.r->store().generation(), generation);
  }
  void active_drag_writes_the_committed_state() {
    Fixture f;
    f.line();
    f.at(6000);
    f.c.set_tool("select");
    const QPointF from = f.c.screen_of(10, 10), to = f.c.screen_of(10, 30);
    f.c.press(from.x(), from.y());
    f.c.drag(to.x(), to.y());
    QVERIFY(f.c.document().gesture_active());
    f.c.release(to.x(), to.y());
    const auto moved = f.c.document().sketch();
    QVERIFY(f.c.undo());
    const auto before = f.c.document().sketch();
    QVERIFY(f.c.redo());
    f.c.press(to.x(), to.y());
    f.c.drag(from.x(), from.y());
    f.at(100000);
    QVERIFY(f.c.document().gesture_active());
    QVERIFY(f.file()->project.sketch == moved);
    QVERIFY(!(f.file()->project.sketch == before));
    QVERIFY(!(f.c.document().sketch() == moved));
  }
  void undo_to_saved_retires_and_redo_schedules_again() {
    Fixture f;
    f.line();
    const QUrl url = QUrl::fromLocalFile(f.dir.filePath("p.sketchcad"));
    f.p.request_save_as();
    f.p.accept_file(url);
    QVERIFY(!f.p.dirty());
    f.line(30);
    f.at(10000);
    QVERIFY(f.exists());
    QCOMPARE(QUrl(QString::fromStdString(f.file()->meta.original_url)), url);
    QVERIFY(f.c.undo());
    QVERIFY(!f.exists());
    f.at(50000);
    QVERIFY(!f.exists());
    QVERIFY(f.c.redo());
    f.at(54999);
    QVERIFY(!f.exists());
    f.at(55000);
    QVERIFY(f.exists());
  }
  void dirty_empty_sketch_is_checkpointed() {
    Fixture f;
    f.line();
    f.c.set_tool("select");
    for (auto x : {35, 10, 60}) {
      f.tap(x, 10);
      f.c.delete_selection();
    }
    QCOMPARE(f.c.entity_count(), 0);
    QVERIFY(f.p.dirty());
    f.at(100000);
    QVERIFY(f.exists());
    QCOMPARE(f.file()->project.sketch.entities().size(), 0u);
  }
  void failures_warn_once_and_retry_after_thirty_seconds() {
    Fixture f;
    QSignalSpy changed(f.r.get(), &RecoveryCoordinator::changed);
    f.fail_writes = true;
    f.line();
    f.at(6000);
    QCOMPARE(f.r->warning(), QString("autosave_failed"));
    QVERIFY(!f.exists());
    const int warned = changed.count();
    f.c.set_tool("circle");
    f.tap(80, 80);
    f.tap(85, 80);
    f.at(35999);
    f.at(36000);  // retry fails again: no new warning episode
    QCOMPARE(f.r->warning(), QString("autosave_failed"));
    QCOMPARE(changed.count(), warned);
    QCOMPARE(f.c.entity_count(), 5);
    f.fail_writes = false;
    f.at(65999);
    QVERIFY(!f.exists());
    f.at(66000);
    QVERIFY(f.exists());
    QVERIFY(f.r->warning().isEmpty());
  }
};
QTEST_GUILESS_MAIN(RecoveryCoordinatorTest)
#include "recovery_coordinator_test.moc"
