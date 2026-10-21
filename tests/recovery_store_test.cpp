#include "recovery_store.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
namespace {
const QString kExt = ".sketchcad-recovery";
sketchcad::ProjectData sample(double x = 1.25) {
  sketchcad::ProjectData data;
  data.sketch.create_point({x, -2.5}, true);
  data.revision = 9;
  return data;
}
QByteArray bytes(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
void put(const QString& path, const QByteArray& content) {
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  QCOMPARE(file.write(content), content.size());
}
QString uuid(int n) {
  return QStringLiteral("00000000-0000-4000-8000-%1")
      .arg(n, 12, 10, QChar('0'));
}
struct Writer : project_file::detail::AtomicWriter {
  bool short_write, fail_commit;
  Writer(bool s, bool c) : short_write(s), fail_commit(c) {}
  bool open(const QString&) override { return true; }
  qint64 write(const QByteArray& b) override {
    return short_write ? b.size() - 1 : b.size();
  }
  bool commit() override { return !fail_commit; }
};
}  // namespace
class RecoveryStoreTest : public QObject {
  Q_OBJECT
  QProcess* helper(const QString& root, const QString& mode,
                   const QString& id = {}) {
    auto* p = new QProcess(this);
    QStringList args{"--root", root, "--mode", mode};
    if (!id.isEmpty()) args << "--id" << id;
    p->start(RECOVERY_HELPER, args);
    return p;
  }
  QString line(QProcess* p) {
    if (!p->waitForReadyRead(10000)) return {};
    return QString::fromUtf8(p->readLine()).trimmed();
  }
  void kill(QProcess* p) {
    p->kill();
    p->waitForFinished(10000);
  }
 private slots:
  void write_discover_claim_and_adopt() {
    QTemporaryDir dir;
    recovery::Store a(dir.path());
    QVERIFY(a.start());
    QVERIFY(sketchcad::valid_session_id(a.session_id().toStdString()));
    QVERIFY(!a.has_checkpoint());
    const QUrl original = QUrl::fromLocalFile(
        dir.filePath(QString::fromUtf8("чертёж 角.sketchcad")));
    QVERIFY(!a.write(sample(), original));
    QCOMPARE(a.generation(), 1u);
    QVERIFY(!a.write(sample(2), original));
    QCOMPARE(a.generation(), 2u);
    QVERIFY(a.has_checkpoint());
    QVERIFY(QFile::exists(dir.filePath(a.session_id() + kExt)));
    QVERIFY(a.discover().empty());
    recovery::Store b(dir.path());
    QVERIFY(b.start());
    QVERIFY(b.discover().empty());  // a holds its lock.
    QVERIFY(std::get<recovery::Error>(b.claim(a.session_id())) ==
            recovery::Error::kLocked);
    QVERIFY(!b.claimed());
    const QString id = a.session_id();
    a.release();
    const auto found = b.discover();
    QCOMPARE(found.size(), 1u);
    QCOMPARE(found[0].session_id, id);
    QVERIFY(found[0].status == recovery::Status::kValid);
    QCOMPARE(QUrl(found[0].original_url), original);
    QVERIFY(found[0].captured_at.isValid());
    QVERIFY(qAbs(found[0].captured_at.secsTo(QDateTime::currentDateTimeUtc())) <
            60);
    const auto claimed = b.claim(id);
    QVERIFY(std::holds_alternative<sketchcad::RecoveryData>(claimed));
    QVERIFY(std::get<sketchcad::RecoveryData>(claimed).project.sketch ==
            sample(2).sketch);
    QVERIFY(b.discover().empty());  // claimed by b.
    const QString old = b.session_id();
    QVERIFY(!b.write(sample(3), {}));
    QVERIFY(!b.adopt());
    QVERIFY(!b.claimed());
    QCOMPARE(b.session_id(), id);
    QCOMPARE(b.generation(), 2u);
    QVERIFY(!QFile::exists(dir.filePath(old + kExt)));
    QVERIFY(!QFile::exists(dir.filePath(old + ".lock")));
    QVERIFY(!b.write(sample(4), {}));
    QCOMPARE(b.generation(), 3u);
    recovery::Store c(dir.path());
    QVERIFY(c.start());
    QVERIFY(c.discover().empty());
  }
  void ignores_unrelated_entries_and_reports_invalid_records() {
    QTemporaryDir dir, outside;
    recovery::Store a(dir.path());
    QVERIFY(a.start());
    QVERIFY(!a.write(sample(), {}));
    const QByteArray valid = bytes(dir.filePath(a.session_id() + kExt));
    a.release();
    QVERIFY(QFile::remove(dir.filePath(a.session_id() + kExt)));
    put(outside.filePath("target"), valid);
    QVERIFY(
        QFile::link(outside.filePath("target"), dir.filePath(uuid(1) + kExt)));
    put(dir.filePath("notes.txt"), "x");
    put(dir.filePath(uuid(2) + kExt + ".Ab12Cd"), valid);
    QVERIFY(QDir(dir.path()).mkdir(uuid(3) + kExt));
    put(dir.filePath(uuid(4).toUpper().replace("0000-4000", "ABCD-4000") +
                     kExt),
        valid);
    put(dir.filePath(uuid(5) + kExt), valid);  // session_id mismatch
    put(dir.filePath(uuid(6) + kExt), "garbage");
    QByteArray future = valid;
    future.replace("\"version\":1,\"session_id\"",
                   "\"version\":2,\"session_id\"");
    put(dir.filePath(uuid(7) + kExt), future);
    recovery::Store b(dir.path());
    QVERIFY(b.start());
    auto found = b.discover();
    QCOMPARE(found.size(), 3u);
    std::sort(found.begin(), found.end(),
              [](auto& l, auto& r) { return l.session_id < r.session_id; });
    QCOMPARE(found[0].session_id, uuid(5));
    QVERIFY(found[0].status == recovery::Status::kInvalid);
    QVERIFY(found[1].status == recovery::Status::kInvalid);
    QVERIFY(found[1].captured_at.isValid());
    QVERIFY(found[2].status == recovery::Status::kUnsupported);
    QVERIFY(std::get<recovery::Error>(b.claim(uuid(6))) ==
            recovery::Error::kInvalid);
    QVERIFY(b.claimed());
    QVERIFY(b.adopt() == recovery::Error::kInvalid);
    QVERIFY(!b.discard_claim());
    QVERIFY(!b.claimed());
    QVERIFY(!QFile::exists(dir.filePath(uuid(6) + kExt)));
    QCOMPARE(b.discover().size(), 2u);
    QVERIFY(std::get<recovery::Error>(b.claim(uuid(9))) ==
            recovery::Error::kMissing);
    QVERIFY(std::get<recovery::Error>(b.claim("../x")) ==
            recovery::Error::kInvalid);
    QVERIFY(!b.claimed());
    QVERIFY(QFile::exists(outside.filePath("target")));
  }
  void failed_writes_keep_previous_checkpoint() {
    QTemporaryDir dir;
    int mode = 0;
    recovery::Store a(
        dir.path(),
        [&]() -> std::unique_ptr<project_file::detail::AtomicWriter> {
          if (mode == 1) return std::make_unique<Writer>(true, false);
          if (mode == 2) return std::make_unique<Writer>(false, true);
          return project_file::detail::make_save_writer();
        });
    QVERIFY(a.start());
    QVERIFY(!a.write(sample(), {}));
    const QString path = dir.filePath(a.session_id() + kExt);
    const QByteArray before = bytes(path);
    for (mode = 1; mode <= 2; ++mode) {
      QVERIFY(a.write(sample(7), {}) == recovery::Error::kStorage);
      QCOMPARE(bytes(path), before);
      QCOMPARE(a.generation(), 1u);
    }
    mode = 0;
    QVERIFY(!a.retire());
    QVERIFY(!a.has_checkpoint());
    QVERIFY(!a.retire());
    QVERIFY(QDir(dir.path()).removeRecursively());
    QVERIFY(a.write(sample(), {}) == recovery::Error::kStorage);
  }
  void retire_leaves_other_sessions_untouched() {
    QTemporaryDir dir;
    recovery::Store a(dir.path()), b(dir.path());
    QVERIFY(a.start() && b.start());
    QVERIFY(!a.write(sample(), {}) && !b.write(sample(5), {}));
    const QString other = dir.filePath(b.session_id() + kExt);
    const QByteArray before = bytes(other);
    QVERIFY(!a.retire());
    QCOMPARE(bytes(other), before);
    a.release();
    QCOMPARE(bytes(other), before);
  }
  void child_process_lock_crash_and_second_crash() {
    QTemporaryDir dir;
    QProcess* p = helper(dir.path(), "checkpoint");
    const QString id = line(p);
    QVERIFY2(sketchcad::valid_session_id(id.toStdString()), qPrintable(id));
    recovery::Store b(dir.path()), c(dir.path());
    QVERIFY(b.start() && c.start());
    QVERIFY(b.discover().empty());
    QVERIFY(std::get<recovery::Error>(b.claim(id)) == recovery::Error::kLocked);
    kill(p);
    auto found = b.discover();
    QCOMPARE(found.size(), 1u);
    QCOMPARE(found[0].session_id, id);
    QCOMPARE(QUrl(found[0].original_url).toLocalFile(),
             QString::fromUtf8("/tmp/исходный.sketchcad"));
    const QByteArray before = bytes(dir.filePath(id + kExt));
    const auto claimed = b.claim(id);
    QVERIFY(std::holds_alternative<sketchcad::RecoveryData>(claimed));
    QCOMPARE(std::get<sketchcad::RecoveryData>(claimed)
                 .project.sketch.entities()
                 .size(),
             3u);
    QVERIFY(std::get<recovery::Error>(c.claim(id)) == recovery::Error::kLocked);
    b.drop_claim();
    QProcess* r = helper(dir.path(), "restore-crash", id);
    QCOMPARE(line(r), QString("adopted"));
    r->waitForFinished(10000);
    QVERIFY(r->exitStatus() == QProcess::CrashExit);
    found = c.discover();
    QCOMPARE(found.size(), 1u);
    QCOMPARE(found[0].session_id, id);
    QCOMPARE(bytes(dir.filePath(id + kExt)), before);
  }
};
QTEST_GUILESS_MAIN(RecoveryStoreTest)
#include "recovery_store_test.moc"
