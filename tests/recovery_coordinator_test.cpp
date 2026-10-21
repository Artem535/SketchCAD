#include "recovery_coordinator.h"

#include <QDir>
#include <QFile>
#include <QProcess>
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
  QTemporaryDir own;
  QDir dir;
  std::int64_t now = 1000;
  bool fail_writes = false;
  SketchController c;
  ProjectSession p{c};
  std::unique_ptr<RecoveryCoordinator> r;
  explicit Fixture(const QString& root = {})
      : dir(root.isEmpty() ? own.path() : root) {
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
    p.set_recovery(r.get());
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
QByteArray bytes(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
// Makes deleting the checkpoint fail: a non-empty directory takes its name.
void block_cleanup(Fixture& f) {
  QFile::remove(f.path());
  QVERIFY(QDir().mkpath(f.path() + "/x"));
}
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
  void save_and_save_as_retire_and_failures_keep_checkpoint() {
    Fixture f;
    f.line();
    f.at(6000);
    QVERIFY(f.exists());
    QSignalSpy pick(&f.p, &ProjectSession::choose_save);
    f.p.request_save();
    QCOMPARE(pick.count(), 1);
    f.p.cancel_file();
    QVERIFY(f.exists());
    f.p.request_save_as();
    f.p.accept_file(
        QUrl::fromLocalFile(f.dir.filePath("missing/dir/p.sketchcad")));
    QVERIFY(f.exists());
    QVERIFY(f.p.dirty());
    f.p.request_save_as();
    f.p.accept_file(QUrl::fromLocalFile(f.dir.filePath("p.sketchcad")));
    QVERIFY(!f.p.dirty());
    QVERIFY(!f.exists());
    f.line(30);
    f.at(20000);
    QVERIFY(f.exists());
    f.p.request_save();
    QVERIFY(!f.exists());
    f.at(100000);
    QVERIFY(!f.exists());
  }
  void new_open_and_close_retire_after_validation() {
    Fixture f;
    const QString project = f.dir.filePath("p.sketchcad");
    f.line();
    f.p.request_save_as();
    f.p.accept_file(QUrl::fromLocalFile(project));
    f.line(30);
    f.at(10000);
    QVERIFY(f.exists());
    QFile bad(f.dir.filePath("bad.sketchcad"));
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("garbage");
    bad.close();
    QSignalSpy err(&f.p, &ProjectSession::error);
    f.p.request_open();
    f.p.answer_unsaved("discard");
    f.p.accept_file(QUrl::fromLocalFile(bad.fileName()));
    QCOMPARE(err.count(), 1);
    QVERIFY(f.exists());  // failed Open keeps the old checkpoint
    QVERIFY(f.p.dirty());
    f.p.request_open();
    f.p.answer_unsaved("discard");
    f.p.accept_file(QUrl::fromLocalFile(project));
    QVERIFY(!f.exists());
    QVERIFY(!f.p.dirty());
    f.line(50);
    f.at(20000);
    QVERIFY(f.exists());
    f.p.request_new();
    f.p.answer_unsaved("cancel");
    QVERIFY(f.exists());
    f.p.request_new();
    f.p.answer_unsaved("discard");
    QVERIFY(!f.exists());
    QCOMPARE(f.c.entity_count(), 0);
    f.line(70);
    f.at(30000);
    QVERIFY(f.exists());
    QSignalSpy closed(&f.p, &ProjectSession::close_ready);
    f.p.request_close();
    f.p.answer_unsaved("cancel");
    QVERIFY(f.exists());
    f.p.request_close();
    f.p.answer_unsaved("discard");
    QCOMPARE(closed.count(), 1);
    QVERIFY(!f.exists());
    QVERIFY(!f.r->active());
    f.line(90);  // a late edit after close must not recreate the file
    f.at(100000);
    QVERIFY(QDir(f.dir).entryList({"*.sketchcad-recovery"}).isEmpty());
  }
  void cleanup_failure_is_reported_and_blocks_close_until_retry() {
    Fixture f;
    f.line();
    f.at(6000);
    block_cleanup(f);
    QSignalSpy err(&f.p, &ProjectSession::error);
    QSignalSpy closed(&f.p, &ProjectSession::close_ready);
    f.p.request_save_as();
    f.p.accept_file(QUrl::fromLocalFile(f.dir.filePath("p.sketchcad")));
    QVERIFY(!f.p.dirty());  // the save itself succeeded
    QCOMPARE(err.count(), 1);
    QCOMPARE(err.last().first().toString(), QString("recovery_cleanup_failed"));
    QCOMPARE(f.r->warning(), QString("recovery_cleanup_failed"));
    f.p.request_close();
    QCOMPARE(closed.count(), 0);
    QCOMPARE(err.count(), 2);
    QVERIFY(!f.p.busy());
    f.p.request_new();
    QCOMPARE(err.count(), 3);
    QVERIFY(QDir(f.path()).removeRecursively());
    f.at(6000 + 30000);
    QVERIFY(f.r->warning().isEmpty());
    f.p.request_close();
    QCOMPARE(closed.count(), 1);
  }
  void restore_gives_dirty_untitled_copy_and_keeps_original() {
    QTemporaryDir root;
    const QString original =
        QDir(root.path()).filePath(QString::fromUtf8("деталь.sketchcad"));
    QString id;
    QByteArray saved;
    sketchcad::Sketch expected;
    {
      Fixture f(root.path());
      f.line();
      f.p.request_save_as();
      f.p.accept_file(QUrl::fromLocalFile(original));
      saved = bytes(original);
      f.line(30);
      f.at(10000);
      expected = f.c.document().sketch();
      id = f.r->store().session_id();
    }  // destroyed without close: as after a crash
    QVERIFY(
        QFile::exists(QDir(root.path()).filePath(id + ".sketchcad-recovery")));
    Fixture g(root.path());
    g.r->refresh();
    QCOMPARE(g.r->candidates().size(), 1);
    const auto row = g.r->candidates()[0].toMap();
    QCOMPARE(row["session_id"].toString(), id);
    QCOMPARE(row["source_name"].toString(),
             QString::fromUtf8("деталь.sketchcad"));
    QVERIFY(row["restorable"].toBool());
    g.line(50);
    g.p.request_restore(id);
    g.p.answer_unsaved("cancel");
    QCOMPARE(g.c.entity_count(), 3);
    QCOMPARE(g.r->candidates().size(), 1);
    g.p.request_restore(id);
    g.p.answer_unsaved("discard");
    QVERIFY(g.c.document().sketch() == expected);
    QVERIFY(g.p.dirty());
    QVERIFY(g.p.file_url().isEmpty());
    QVERIFY(!g.c.can_undo());
    QCOMPARE(g.p.display_name(),
             QString::fromUtf8("Восстановлено — деталь.sketchcad"));
    QCOMPARE(g.r->store().session_id(), id);
    QVERIFY(g.r->candidates().isEmpty());
    QVERIFY(g.exists());  // adopted, not deleted
    g.at(100000);
    QCOMPARE(g.r->store().generation(), 1u);  // already holds this state
    QSignalSpy pick(&g.p, &ProjectSession::choose_save);
    g.p.request_save();
    QCOMPARE(pick.count(), 1);
    QCOMPARE(pick.last().first().toUrl(),
             QUrl::fromLocalFile(QDir(root.path())
                                     .filePath(QString::fromUtf8(
                                         "деталь (восстановлено).sketchcad"))));
    QCOMPARE(bytes(original), saved);
    g.p.cancel_file();
    QVERIFY(g.exists());
    const QString copy = QDir(root.path()).filePath("copy.sketchcad");
    g.p.request_save();
    g.p.accept_file(QUrl::fromLocalFile(copy));
    QVERIFY(!g.p.dirty());
    QCOMPARE(g.p.display_name(), QString("copy.sketchcad"));
    QCOMPARE(bytes(original), saved);
    QVERIFY(!g.exists());
  }
  void invalid_or_locked_restore_changes_nothing() {
    QTemporaryDir root;
    const QString corrupt = "00000000-0000-4000-8000-000000000001";
    QFile file(QDir(root.path()).filePath(corrupt + ".sketchcad-recovery"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{}");
    file.close();
    Fixture other(root.path());
    other.line();
    other.at(6000);
    Fixture f(root.path());
    f.line();
    const auto before = f.c.document().sketch();
    QSignalSpy err(&f.p, &ProjectSession::error);
    f.r->refresh();
    QCOMPARE(f.r->candidates().size(), 1);  // the live `other` is hidden
    QVERIFY(!f.r->candidates()[0].toMap()["restorable"].toBool());
    for (const QString& id :
         {corrupt, other.r->store().session_id(),
          QString("00000000-0000-4000-8000-000000000009")}) {
      f.p.request_restore(id);
      f.p.answer_unsaved("discard");
      QVERIFY(f.c.document().sketch() == before);
      QVERIFY(f.c.can_undo());
      QVERIFY(!f.p.busy());
      QVERIFY(!f.r->store().claimed());
    }
    QCOMPARE(err.count(), 3);
    QVERIFY(other.exists());
    f.r->discard(corrupt);
    QVERIFY(!file.exists());
    QVERIFY(f.r->candidates().isEmpty());
    f.r->discard(other.r->store().session_id());
    QVERIFY(other.exists());
  }
  void crashed_child_process_is_restored() {
    QTemporaryDir root;
    QProcess child;
    child.start(RECOVERY_HELPER,
                {"--root", root.path(), "--mode", "checkpoint"});
    QVERIFY(child.waitForReadyRead(10000));
    const QString id = QString::fromUtf8(child.readLine()).trimmed();
    Fixture f(root.path());
    f.r->refresh();
    QVERIFY(f.r->candidates().isEmpty());
    child.kill();
    child.waitForFinished(10000);
    f.r->refresh();
    QCOMPARE(f.r->candidates().size(), 1);
    f.p.request_restore(id);
    QCOMPARE(f.c.entity_count(), 3);
    QVERIFY(f.p.dirty());
    QCOMPARE(f.p.display_name(),
             QString::fromUtf8("Восстановлено — исходный.sketchcad"));
  }
};
QTEST_GUILESS_MAIN(RecoveryCoordinatorTest)
#include "recovery_coordinator_test.moc"
