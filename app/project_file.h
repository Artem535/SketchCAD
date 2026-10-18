#pragma once
#include <QByteArray>
#include <QUrl>
#include <optional>
#include <variant>

#include "sketchcad/project_io.h"
namespace project_file {
enum class Error {
  kUnreadable,
  kInvalidProject,
  kUnsupportedVersion,
  kTooLarge,
  kWriteFailed,
  kUnsupportedUrl
};
using ReadResult = std::variant<sketchcad::ProjectData, Error>;
ReadResult read(const QUrl&);
std::optional<Error> write(const QUrl&, const sketchcad::ProjectData&);
namespace detail {
class AtomicWriter {
 public:
  virtual ~AtomicWriter() = default;
  virtual bool open(const QString&) = 0;
  virtual qint64 write(const QByteArray&) = 0;
  virtual bool commit() = 0;
};
std::optional<Error> write_with(const QUrl&, const sketchcad::ProjectData&,
                                AtomicWriter&);
}  // namespace detail
}  // namespace project_file
