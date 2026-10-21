#include "recovery_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QStandardPaths>
#include <QTimeZone>
#include <QUuid>

namespace recovery {
namespace {
const QString kExtension = QStringLiteral(".sketchcad-recovery");

// A managed checkpoint: a regular file directly below the root whose
// basename is a canonical session UUID. Symbolic links never qualify.
bool regular(const QFileInfo& info) {
  return info.exists() && !info.isSymLink() && info.isFile();
}

struct Read {
  Status status = Status::kUnreadable;
  std::optional<sketchcad::RecoveryData> data;
};

Read read_checkpoint(const QString& path, const QString& id) {
  Read result;
  QFile file(path);
  if (!regular(QFileInfo(path)) || !file.open(QIODevice::ReadOnly))
    return result;
  if (file.size() > static_cast<qint64>(sketchcad::kRecoveryMaxBytes)) {
    result.status = Status::kTooLarge;
    return result;
  }
  const QByteArray bytes =
      file.read(static_cast<qint64>(sketchcad::kRecoveryMaxBytes) + 1);
  if (file.error() != QFileDevice::NoError) return result;
  auto decoded = sketchcad::decode_recovery(
      std::string_view(bytes.constData(), bytes.size()));
  if (auto* error = std::get_if<sketchcad::RecoveryError>(&decoded)) {
    switch (*error) {
      case sketchcad::RecoveryError::kUnsupportedVersion:
        result.status = Status::kUnsupported;
        break;
      case sketchcad::RecoveryError::kTooLarge:
        result.status = Status::kTooLarge;
        break;
      case sketchcad::RecoveryError::kInvalid:
      case sketchcad::RecoveryError::kInvalidProject:
        result.status = Status::kInvalid;
        break;
    }
    return result;
  }
  auto& data = std::get<sketchcad::RecoveryData>(decoded);
  if (QString::fromStdString(data.meta.session_id) != id) {
    result.status = Status::kInvalid;
    return result;
  }
  result.status = Status::kValid;
  result.data = std::move(data);
  return result;
}

QString utc_now() {
  return QDateTime::currentDateTimeUtc().toString(
      QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzzZ"));
}
}  // namespace

Store::Store(QString root, WriterFactory writer)
    : root_(std::move(root)), writer_(std::move(writer)) {
  if (!writer_) writer_ = project_file::detail::make_save_writer;
}

Store::~Store() { release(); }

QString Store::default_root() {
  return QStandardPaths::writableLocation(
             QStandardPaths::AppLocalDataLocation) +
         QStringLiteral("/recovery");
}

QString Store::file_path(const QString& id) const {
  return QDir(root_).filePath(id + kExtension);
}

QString Store::lock_path(const QString& id) const {
  return QDir(root_).filePath(id + QStringLiteral(".lock"));
}

// Long-lived locks: never stale by age; Qt still removes the lock of a
// dead process on this host.
std::unique_ptr<QLockFile> Store::try_lock(const QString& id) const {
  auto lock = std::make_unique<QLockFile>(lock_path(id));
  lock->setStaleLockTime(0);
  if (!lock->tryLock(0)) return nullptr;
  return lock;
}

bool Store::start() {
  if (lock_ || root_.isEmpty() || !QDir().mkpath(root_)) return false;
  const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  lock_ = try_lock(id);
  if (!lock_) return false;
  session_id_ = id;
  generation_ = 0;
  // Locks left by dead sessions that never wrote a checkpoint: acquiring
  // such a lock removes the stale file and releasing it deletes ours.
  const auto locks =
      QDir(root_).entryInfoList({QStringLiteral("*.lock")},
                                QDir::Files | QDir::Hidden | QDir::NoSymLinks);
  for (const QFileInfo& info : locks) {
    const QString other = info.completeBaseName();
    if (other != session_id_ &&
        sketchcad::valid_session_id(other.toStdString()) &&
        !QFileInfo(file_path(other)).exists())
      try_lock(other);
  }
  return true;
}

bool Store::has_checkpoint() const {
  return lock_ && regular(QFileInfo(file_path(session_id_)));
}

std::optional<Error> Store::write(const sketchcad::ProjectData& project,
                                  const QUrl& original) {
  if (!lock_) return Error::kStorage;
  if (generation_ + 1 == UINT64_MAX) return Error::kExhausted;
  const sketchcad::RecoveryMeta meta{session_id_.toStdString(), generation_ + 1,
                                     utc_now().toStdString(),
                                     original.toString().toStdString()};
  const auto encoded = sketchcad::encode_recovery(meta, project);
  if (!encoded) return Error::kInvalid;
  const QByteArray bytes(encoded->data(),
                         static_cast<qsizetype>(encoded->size()));
  auto writer = writer_();
  if (!writer || !writer->open(file_path(session_id_)) ||
      writer->write(bytes) != bytes.size() || !writer->commit())
    return Error::kStorage;
  generation_ = meta.generation;
  return std::nullopt;
}

std::optional<Error> Store::retire() {
  if (!lock_) return std::nullopt;
  const QString path = file_path(session_id_);
  if (!QFileInfo(path).exists() && !QFileInfo(path).isSymLink())
    return std::nullopt;
  if (!QFile::remove(path)) return Error::kStorage;
  return std::nullopt;
}

void Store::release() {
  drop_claim();
  lock_.reset();  // QLockFile unlocks and removes its file.
}

std::vector<Candidate> Store::discover() const {
  std::vector<Candidate> found;
  const QDir dir(root_);
  const auto entries = dir.entryInfoList(
      {QStringLiteral("*") + kExtension},
      QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks);
  for (const QFileInfo& info : entries) {
    const QString id = info.fileName().chopped(kExtension.size());
    if (!sketchcad::valid_session_id(id.toStdString()) || !regular(info) ||
        id == session_id_ || id == claim_id_)
      continue;
    const auto lock = try_lock(id);
    if (!lock) continue;  // A live session owns it.
    const Read read = read_checkpoint(info.filePath(), id);
    Candidate candidate;
    candidate.session_id = id;
    candidate.status = read.status;
    candidate.captured_at = info.lastModified(QTimeZone::UTC);
    if (read.data) {
      candidate.captured_at = QDateTime::fromString(
          QString::fromStdString(read.data->meta.captured_at),
          Qt::ISODateWithMs);
      candidate.original_url =
          QString::fromStdString(read.data->meta.original_url);
    }
    found.push_back(std::move(candidate));
  }
  return found;
}

std::variant<sketchcad::RecoveryData, Error> Store::claim(const QString& id) {
  drop_claim();
  if (!sketchcad::valid_session_id(id.toStdString()) || id == session_id_)
    return Error::kInvalid;
  auto lock = try_lock(id);
  if (!lock) return Error::kLocked;
  if (!regular(QFileInfo(file_path(id)))) return Error::kMissing;
  claim_id_ = id;
  claim_lock_ = std::move(lock);
  Read read = read_checkpoint(file_path(id), id);
  if (!read.data) return Error::kInvalid;
  claim_generation_ = read.data->meta.generation;
  return std::move(*read.data);
}

std::optional<Error> Store::adopt() {
  if (!claim_lock_ || !claim_generation_) return Error::kInvalid;
  if (const auto error = retire()) return error;
  lock_ = std::move(claim_lock_);  // Releases the previous own lock.
  session_id_ = claim_id_;
  generation_ = *claim_generation_;
  claim_id_.clear();
  claim_generation_.reset();
  return std::nullopt;
}

std::optional<Error> Store::discard_claim() {
  if (!claim_lock_) return Error::kInvalid;
  if (!QFile::remove(file_path(claim_id_)) &&
      QFileInfo(file_path(claim_id_)).exists())
    return Error::kStorage;
  // Temporary files QSaveFile left behind when that session crashed.
  const auto leftovers =
      QDir(root_).entryInfoList({claim_id_ + kExtension + QStringLiteral(".*")},
                                QDir::Files | QDir::Hidden | QDir::NoSymLinks);
  for (const QFileInfo& info : leftovers) QFile::remove(info.filePath());
  drop_claim();
  return std::nullopt;
}

void Store::drop_claim() {
  claim_lock_.reset();
  claim_id_.clear();
  claim_generation_.reset();
}

QString status_key(Status status) {
  switch (status) {
    case Status::kValid:
      return QStringLiteral("valid");
    case Status::kInvalid:
      return QStringLiteral("invalid");
    case Status::kUnsupported:
      return QStringLiteral("unsupported");
    case Status::kTooLarge:
      return QStringLiteral("too_large");
    case Status::kUnreadable:
      return QStringLiteral("unreadable");
  }
  return QStringLiteral("invalid");
}
}  // namespace recovery
