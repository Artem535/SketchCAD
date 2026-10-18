#include "project_file.h"

#include <QFile>
#include <QSaveFile>
#include <memory>

namespace project_file {
namespace {
class SaveWriter final : public detail::AtomicWriter {
 public:
  bool open(const QString& path) override {
    file_ = std::make_unique<QSaveFile>(path);
    file_->setDirectWriteFallback(false);
    return file_->open(QIODevice::WriteOnly);
  }
  qint64 write(const QByteArray& bytes) override { return file_->write(bytes); }
  bool commit() override { return file_->commit(); }
 private:
  std::unique_ptr<QSaveFile> file_;
};
bool local(const QUrl& url) {
  return url.isLocalFile() && !url.toLocalFile().isEmpty() && !url.hasQuery() && !url.hasFragment();
}
}  // namespace
ReadResult read(const QUrl& url) {
  if (!local(url)) return Error::kUnsupportedUrl;
  QFile file(url.toLocalFile());
  if (!file.open(QIODevice::ReadOnly)) return Error::kUnreadable;
  if (file.size() > static_cast<qint64>(sketchcad::kProjectMaxBytes)) return Error::kTooLarge;
  const QByteArray bytes=file.read(static_cast<qint64>(sketchcad::kProjectMaxBytes)+1);
  if(file.error()!=QFileDevice::NoError) return Error::kUnreadable;
  auto decoded=sketchcad::decode_project(std::string_view(bytes.constData(),bytes.size()));
  if(auto* data=std::get_if<sketchcad::ProjectData>(&decoded)) return std::move(*data);
  switch(std::get<sketchcad::ProjectError>(decoded)) {
    case sketchcad::ProjectError::kUnsupportedVersion: return Error::kUnsupportedVersion;
    case sketchcad::ProjectError::kTooLarge: return Error::kTooLarge;
    case sketchcad::ProjectError::kInvalidProject: return Error::kInvalidProject;
  }
  return Error::kInvalidProject;
}
std::optional<Error> detail::write_with(const QUrl& url,const sketchcad::ProjectData& data,AtomicWriter& writer) {
  if (!local(url)) return Error::kUnsupportedUrl;
  const auto encoded=sketchcad::encode_project(data);
  if(!encoded) return Error::kInvalidProject;
  const QByteArray bytes(encoded->data(),static_cast<qsizetype>(encoded->size()));
  if(!writer.open(url.toLocalFile()) || writer.write(bytes)!=bytes.size() || !writer.commit()) return Error::kWriteFailed;
  return std::nullopt;
}
std::optional<Error> write(const QUrl& url,const sketchcad::ProjectData& data) {
  SaveWriter writer;
  return detail::write_with(url,data,writer);
}
}  // namespace project_file
