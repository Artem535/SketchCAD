#pragma once
#include <QDateTime>
#include <QString>
#include <QUrl>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include "project_file.h"
#include "sketchcad/recovery_io.h"
class QLockFile;
// Recovery checkpoints of running and crashed application sessions
// (autosave-recovery.adoc). Every session owns `<uuid>.sketchcad-recovery`
// and holds `<uuid>.lock` directly below the managed root for its lifetime.
namespace recovery {
enum class Status { kValid, kInvalid, kUnsupported, kTooLarge, kUnreadable };
enum class Error { kStorage, kLocked, kMissing, kInvalid, kExhausted };
struct Candidate {
  QString session_id;
  QDateTime captured_at;  // Checkpoint time, or file time when unreadable.
  QString original_url;   // Informational only.
  Status status = Status::kInvalid;
};
using WriterFactory =
    std::function<std::unique_ptr<project_file::detail::AtomicWriter>()>;
class Store {
 public:
  explicit Store(QString root, WriterFactory writer = {});
  ~Store();  // Releases locks; keeps checkpoint files for recovery.
  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;

  // AppLocalDataLocation/recovery; the application name must be set first.
  static QString default_root();
  QString root() const { return root_; }

  // Creates the root, a new session UUID and holds its lock.
  bool start();
  QString session_id() const { return session_id_; }
  bool has_checkpoint() const;
  std::uint64_t generation() const { return generation_; }

  std::optional<Error> write(const sketchcad::ProjectData&,
                             const QUrl& original);
  // Deletes this session's checkpoint; the lock stays held.
  std::optional<Error> retire();
  // Releases this session's and any claimed lock; files stay.
  void release();

  // Checkpoints of inactive sessions; never this session's or a claimed one.
  std::vector<Candidate> discover() const;

  // Holds the candidate's lock and rereads it. kLocked/kMissing hold no
  // claim; kInvalid keeps the claim so that the record can be discarded.
  std::variant<sketchcad::RecoveryData, Error> claim(const QString& session_id);
  bool claimed() const { return claim_lock_ != nullptr; }
  // The valid claimed checkpoint becomes this session's; the previous own
  // checkpoint is deleted and its lock released.
  std::optional<Error> adopt();
  // Deletes the claimed checkpoint and its temporary files, then releases it.
  std::optional<Error> discard_claim();
  void drop_claim();

 private:
  QString file_path(const QString& id) const;
  QString lock_path(const QString& id) const;
  std::unique_ptr<QLockFile> try_lock(const QString& id) const;

  QString root_;
  WriterFactory writer_;
  QString session_id_;
  std::unique_ptr<QLockFile> lock_;
  std::uint64_t generation_ = 0;
  QString claim_id_;
  std::unique_ptr<QLockFile> claim_lock_;
  std::optional<std::uint64_t> claim_generation_;  // Set for a valid claim.
};
QString status_key(Status);
}  // namespace recovery
