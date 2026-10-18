#include "project_file.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
class ProjectFileTest : public QObject {
  Q_OBJECT
 private slots:
  void unicode_round_trip_and_overwrite() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QUrl url=QUrl::fromLocalFile(dir.filePath(QString::fromUtf8("эскиз 角.sketchcad")));
    sketchcad::ProjectData data; data.sketch.create_point({1.25,-2.5},true); data.revision=42;
    QVERIFY(!project_file::write(url,data));
    const auto result=project_file::read(url);
    QVERIFY(std::holds_alternative<sketchcad::ProjectData>(result));
    const auto restored=std::get<sketchcad::ProjectData>(result);
    QVERIFY(restored.sketch==data.sketch); QCOMPARE(restored.revision,42u);
    data.sketch.create_point({9,8});
    QVERIFY(!project_file::write(url,data));
    QVERIFY(std::get<sketchcad::ProjectData>(project_file::read(url)).sketch==data.sketch);
  }
  void errors_and_commit_failures_preserve_old_file() {
    QTemporaryDir dir; const auto path=dir.filePath("old.sketchcad");
    QFile old(path); QVERIFY(old.open(QIODevice::WriteOnly)); QCOMPARE(old.write("previous"),8); old.close();
    struct Fail : project_file::detail::AtomicWriter {
      bool short_write=false; bool committed=false;
      bool open(const QString&) override {return true;}
      qint64 write(const QByteArray& b) override {return short_write?b.size()-1:b.size();}
      bool commit() override {committed=true; return false;}
    } fail;
    const auto url=QUrl::fromLocalFile(path);
    QVERIFY(project_file::detail::write_with(url,{},fail).has_value());
    QVERIFY(fail.committed);
    fail.short_write=true; fail.committed=false;
    QVERIFY(project_file::detail::write_with(url,{},fail).has_value());
    QVERIFY(!fail.committed);
    QVERIFY(old.open(QIODevice::ReadOnly)); QCOMPARE(old.readAll(),QByteArray("previous")); old.close();
    QVERIFY(project_file::write(QUrl::fromLocalFile(dir.filePath("missing/file.sketchcad")),{}).has_value());
    QCOMPARE(std::get<project_file::Error>(project_file::read(QUrl("https://example.org/x"))),project_file::Error::kUnsupportedUrl);
    QCOMPARE(std::get<project_file::Error>(project_file::read(url)),project_file::Error::kInvalidProject);
    QVERIFY(old.open(QIODevice::WriteOnly)); old.write("{\"format\":\"SketchCAD\",\"version\":2,\"units\":\"mm\",\"revision\":\"0\",\"next_id\":\"1\",\"entities\":[],\"constraints\":[]}"); old.close();
    QCOMPARE(std::get<project_file::Error>(project_file::read(url)),project_file::Error::kUnsupportedVersion);
    QVERIFY(old.open(QIODevice::WriteOnly)); QVERIFY(old.resize(sketchcad::kProjectMaxBytes+1)); old.close();
    QCOMPARE(std::get<project_file::Error>(project_file::read(url)),project_file::Error::kTooLarge);
  }
};
QTEST_GUILESS_MAIN(ProjectFileTest)
#include "project_file_test.moc"
