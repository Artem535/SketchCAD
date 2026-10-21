#pragma once
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include "recovery_store.h"
#include "sketchcad/autosave_schedule.h"
class SketchController;
class ProjectSession;
struct RecoveryOptions {
  std::function<std::int64_t()> now_ms;  // Monotonic; default steady clock.
  bool use_timer = true;                 // false: tests call poll().
};
// Writes committed document changes to this session's recovery checkpoint
// and lists the checkpoints of crashed sessions (autosave-recovery.adoc).
class RecoveryCoordinator : public QObject {
  Q_OBJECT
  // Empty, autosave_failed or recovery_cleanup_failed.
  Q_PROPERTY(QString warning READ warning NOTIFY changed)
  // {session_id, source_name, captured_at, status, restorable}.
  Q_PROPERTY(QVariantList candidates READ candidates NOTIFY changed)
  Q_PROPERTY(bool active READ active NOTIFY changed)
 public:
  using Options = RecoveryOptions;
  RecoveryCoordinator(SketchController&, ProjectSession&,
                      std::unique_ptr<recovery::Store>, Options = {},
                      QObject* parent = nullptr);
  ~RecoveryCoordinator() override;

  QString warning() const { return warning_; }
  QVariantList candidates() const { return candidates_; }
  bool active() const;
  recovery::Store& store() { return *store_; }

  // Writes the checkpoint when due and retries a failed cleanup.
  Q_INVOKABLE void poll();
  Q_INVOKABLE void refresh();
  Q_INVOKABLE void discard(const QString& session_id);

  // For ProjectSession. retire() deletes this session's checkpoint; false
  // keeps it, shows the cleanup warning and retries later.
  bool retire();
  std::variant<sketchcad::RecoveryData, recovery::Error> claim(
      const QString& session_id);
  bool adopt_claim();
  void drop_claim();
  // A different document was installed; `restored` when it came from the
  // adopted checkpoint, which therefore already holds its state.
  void installed(bool restored);
  // Normal close: no further checkpoints, the session lock is released.
  void shutdown();

 signals:
  void changed();
  void error(QString key);

 private:
  using Key = std::pair<std::uint64_t, std::uint64_t>;  // epoch, revision
  void observe();
  void arm();
  void set_warning(const QString&);
  std::int64_t now() const;

  SketchController& controller_;
  ProjectSession& project_;
  std::unique_ptr<recovery::Store> store_;
  Options options_;
  QTimer timer_;
  sketchcad::AutosaveSchedule schedule_;
  std::uint64_t epoch_ = 0;
  std::optional<Key> observed_;
  std::optional<Key> written_;
  std::optional<std::int64_t> cleanup_at_;
  bool shut_down_ = false;
  QString warning_;
  QVariantList candidates_;
};
