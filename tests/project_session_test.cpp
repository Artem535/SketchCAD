#include "project_session.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "sketch_controller.h"
class ProjectSessionTest : public QObject {
  Q_OBJECT
  void line(SketchController& c) {
    c.set_viewport_size(1000, 700);
    c.set_snap_enabled(false);
    c.set_auto_dimensions(false);
    c.set_tool("line");
    for (auto p : {c.screen_of(10, 10), c.screen_of(60, 10)}) {
      c.press(p.x(), p.y());
      c.release(p.x(), p.y());
    }
  }
 private slots:
  void new_dirty_cancel_discard_and_reentrant() {
    SketchController c;
    ProjectSession p(c);
    line(c);
    QSignalSpy ask(&p, &ProjectSession::ask_unsaved),
        close(&p, &ProjectSession::close_ready);
    const auto before = c.document().sketch();
    p.request_new();
    QCOMPARE(ask.count(), 1);
    QVERIFY(p.busy());
    p.request_close();
    QCOMPARE(ask.count(), 1);
    QCOMPARE(close.count(), 0);
    p.answer_unsaved("cancel");
    QVERIFY(!p.busy());
    QVERIFY(c.document().sketch() == before);
    QVERIFY(p.dirty());
    p.request_new();
    p.answer_unsaved("discard");
    QCOMPARE(c.entity_count(), 0);
    QVERIFY(!p.dirty());
    QVERIFY(!c.document().can_undo());
    QVERIFY(p.file_url().isEmpty());
    p.request_close();
    QCOMPARE(close.count(), 1);
  }
  void save_as_cancel_failure_and_saved_revision() {
    QTemporaryDir dir;
    SketchController c;
    ProjectSession p(c);
    line(c);
    QSignalSpy pick(&p, &ProjectSession::choose_save),
        err(&p, &ProjectSession::error);
    p.request_save();
    QCOMPARE(pick.count(), 1);
    QVERIFY(p.busy());
    p.cancel_file();
    QVERIFY(p.dirty());
    QVERIFY(p.file_url().isEmpty());
    p.request_save_as();
    p.accept_file(QUrl::fromLocalFile(dir.filePath("missing/file")));
    QCOMPARE(err.count(), 1);
    QVERIFY(p.dirty());
    QVERIFY(p.file_url().isEmpty());
    QVERIFY(!p.busy());
    p.request_save_as();
    p.accept_file(
        QUrl::fromLocalFile(dir.filePath(QString::fromUtf8("эскиз"))));
    QVERIFY(!p.dirty());
    QVERIFY(p.file_url().toLocalFile().endsWith(".sketchcad"));
    QVERIFY(QFile::exists(p.file_url().toLocalFile()));
    const auto saved_url = p.file_url();
    QVERIFY(c.undo());
    QVERIFY(p.dirty());
    QVERIFY(c.redo());
    QVERIFY(!p.dirty());
    p.request_save();
    QVERIFY(!p.busy());
    QVERIFY(!p.dirty());
    QCOMPARE(p.file_url(), saved_url);
  }
  void untitled_close_save_cancel_and_success() {
    QTemporaryDir dir;
    SketchController c;
    ProjectSession p(c);
    line(c);
    QSignalSpy close(&p, &ProjectSession::close_ready),
        pick(&p, &ProjectSession::choose_save);
    p.request_close();
    p.answer_unsaved("save");
    QCOMPARE(pick.count(), 1);
    p.cancel_file();
    QCOMPARE(close.count(), 0);
    QVERIFY(p.dirty());
    p.request_close();
    p.answer_unsaved("save");
    p.accept_file(QUrl::fromLocalFile(dir.filePath("project.sketchcad")));
    QCOMPARE(close.count(), 1);
    QVERIFY(!p.dirty());
  }
  void failed_open_and_successful_open_preserve_contract() {
    QTemporaryDir dir;
    SketchController c;
    ProjectSession p(c);
    line(c);
    const auto before = c.document().sketch();
    const auto revision = c.document().revision();
    QSignalSpy choose(&p, &ProjectSession::choose_open),
        err(&p, &ProjectSession::error),
        replaced(&p, &ProjectSession::document_replaced);
    p.request_open();
    p.answer_unsaved("discard");
    QCOMPARE(choose.count(), 1);
    p.accept_file(QUrl::fromLocalFile(dir.filePath("missing.sketchcad")));
    QCOMPARE(err.count(), 1);
    QVERIFY(c.document().sketch() == before);
    QCOMPARE(c.document().revision(), revision);
    QVERIFY(c.document().can_undo());
    QVERIFY(p.dirty());
    sketchcad::ProjectData other;
    other.sketch.create_point({7, 8});
    other.revision = 42;
    const auto url = QUrl::fromLocalFile(dir.filePath("other.sketchcad"));
    QVERIFY(!project_file::write(url, other));
    p.request_open();
    p.answer_unsaved("discard");
    p.accept_file(url);
    QCOMPARE(replaced.count(), 1);
    QVERIFY(c.document().sketch() == other.sketch);
    QCOMPARE(c.document().revision(), 42u);
    QVERIFY(!c.document().can_undo());
    QVERIFY(!p.dirty());
    QCOMPARE(p.file_url(), url);
    QVERIFY(!c.has_selection());
    QVERIFY(!c.in_progress());
    QCOMPARE(c.tool(), QString("select"));
  }
  void file_command_cancels_transient_drawing() {
    SketchController c;
    ProjectSession p(c);
    c.set_viewport_size(1000, 700);
    c.set_tool("line");
    auto point = c.screen_of(10, 10);
    c.press(point.x(), point.y());
    c.release(point.x(), point.y());
    QVERIFY(c.in_progress());
    p.request_save_as();
    QVERIFY(!c.in_progress());
    p.cancel_file();
    QVERIFY(!c.document().gesture_active());
  }
  void save_before_new_and_open_cancel() {
    QTemporaryDir dir;
    SketchController c;
    ProjectSession p(c);
    line(c);
    const auto url = QUrl::fromLocalFile(dir.filePath("explicit.custom"));
    p.request_save_as();
    p.accept_file(url);
    QCOMPARE(p.file_url(), url);
    c.undo();
    QVERIFY(p.dirty());
    p.request_new();
    p.answer_unsaved("save");
    QVERIFY(!p.dirty());
    QVERIFY(p.file_url().isEmpty());
    QCOMPARE(c.entity_count(), 0);
    auto saved = project_file::read(url);
    QVERIFY(std::holds_alternative<sketchcad::ProjectData>(saved));
    QVERIFY(std::get<sketchcad::ProjectData>(saved).sketch.entities().empty());
    p.request_open();
    p.cancel_file();
    QVERIFY(!p.busy());
    QVERIFY(p.file_url().isEmpty());
  }
  void file_command_rolls_back_drag_and_stale_release() {
    SketchController c;
    ProjectSession p(c);
    line(c);
    c.set_tool("select");
    const auto before = c.document().sketch();
    const auto start = c.screen_of(10, 10), to = c.screen_of(15, 15);
    c.press(start.x(), start.y());
    c.drag(to.x(), to.y());
    QVERIFY(c.document().gesture_active());
    p.request_save_as();
    QVERIFY(!c.document().gesture_active());
    QVERIFY(c.document().sketch() == before);
    p.cancel_file();
    p.request_new();
    p.answer_unsaved("discard");
    c.release(to.x(), to.y());
    QCOMPARE(c.entity_count(), 0);
    QVERIFY(!p.dirty());
  }
};
QTEST_GUILESS_MAIN(ProjectSessionTest)
#include "project_session_test.moc"
