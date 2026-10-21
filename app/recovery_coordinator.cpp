#include "recovery_coordinator.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocale>
#include <QLoggingCategory>
#include <algorithm>
#include <chrono>

#include "project_session.h"
#include "sketch_controller.h"

Q_LOGGING_CATEGORY(lc_recovery, "sketchcad.recovery")

namespace {
const QString kAutosaveFailed = QStringLiteral("autosave_failed");
const QString kCleanupFailed = QStringLiteral("recovery_cleanup_failed");
std::int64_t steady_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
      .count();
}
QString error_key(recovery::Error error) {
  switch (error) {
    case recovery::Error::kLocked:
      return QStringLiteral("recovery_locked");
    case recovery::Error::kMissing:
      return QStringLiteral("recovery_missing");
    case recovery::Error::kInvalid:
      return QStringLiteral("recovery_invalid");
    case recovery::Error::kStorage:
    case recovery::Error::kExhausted:
      return QStringLiteral("recovery_storage");
  }
  return QStringLiteral("recovery_storage");
}
}  // namespace

RecoveryCoordinator::RecoveryCoordinator(SketchController& controller,
                                         ProjectSession& project,
                                         std::unique_ptr<recovery::Store> store,
                                         Options options, QObject* parent)
    : QObject(parent),
      controller_(controller),
      project_(project),
      store_(std::move(store)),
      options_(std::move(options)) {
  if (!options_.now_ms) options_.now_ms = steady_ms;
  timer_.setSingleShot(true);
  connect(&timer_, &QTimer::timeout, this, &RecoveryCoordinator::poll);
  connect(&controller_, &SketchController::changed, this,
          &RecoveryCoordinator::observe);
  observe();
}

RecoveryCoordinator::~RecoveryCoordinator() = default;

bool RecoveryCoordinator::active() const {
  return !shut_down_ && !store_->session_id().isEmpty();
}

std::int64_t RecoveryCoordinator::now() const { return options_.now_ms(); }

void RecoveryCoordinator::set_warning(const QString& warning) {
  if (warning_ == warning) return;
  warning_ = warning;
  emit changed();
}

// Runs on every controller notification; only a new committed state
// (document epoch and revision) schedules a checkpoint.
void RecoveryCoordinator::observe() {
  if (!active()) return;
  const auto& document = controller_.document();
  if (!document.committed_dirty()) {
    schedule_.clear();
    observed_.reset();
    if (warning_ == kAutosaveFailed) set_warning({});
    if (store_->has_checkpoint() && !cleanup_at_) retire();
    arm();
    return;
  }
  const Key key{epoch_, document.revision()};
  if (observed_ == key) return;
  observed_ = key;
  if (written_ == key)
    schedule_.clear();
  else
    schedule_.changed(now());
  arm();
}

void RecoveryCoordinator::arm() {
  if (!options_.use_timer) return;
  std::optional<std::int64_t> at = schedule_.deadline();
  if (cleanup_at_) at = at ? std::min(*at, *cleanup_at_) : *cleanup_at_;
  if (!at || !active()) {
    timer_.stop();
    return;
  }
  timer_.start(static_cast<int>(std::clamp<std::int64_t>(
      *at - now(), 0, sketchcad::AutosaveSchedule::kMaxDelayMs)));
}

void RecoveryCoordinator::poll() {
  if (!active()) return;
  const std::int64_t time = now();
  if (cleanup_at_ && time >= *cleanup_at_ &&
      !controller_.document().committed_dirty())
    retire();
  if (!schedule_.due(time)) {
    arm();
    return;
  }
  const auto& document = controller_.document();
  if (!document.committed_dirty()) {
    schedule_.clear();
    arm();
    return;
  }
  QElapsedTimer elapsed;
  elapsed.start();
  const auto failure = store_->write(
      {document.committed(), document.revision()}, project_.recovery_source());
  qCDebug(lc_recovery) << "checkpoint" << (failure ? "failed" : "written")
                       << "in" << elapsed.elapsed() << "ms";
  if (failure) {
    if (schedule_.failed(time)) set_warning(kAutosaveFailed);
  } else {
    written_ = Key{epoch_, document.revision()};
    cleanup_at_.reset();
    schedule_.written();
    if (warning_ == kAutosaveFailed) set_warning({});
  }
  arm();
}

bool RecoveryCoordinator::retire() {
  if (!active()) return true;
  schedule_.clear();
  written_.reset();
  observed_.reset();
  if (store_->retire()) {
    cleanup_at_ = now() + sketchcad::AutosaveSchedule::kRetryMs;
    set_warning(kCleanupFailed);
    arm();
    return false;
  }
  cleanup_at_.reset();
  if (warning_ == kCleanupFailed || warning_ == kAutosaveFailed)
    set_warning({});
  arm();
  return true;
}

void RecoveryCoordinator::refresh() {
  auto found = store_->discover();
  std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
    if (a.captured_at != b.captured_at) return a.captured_at > b.captured_at;
    return a.session_id < b.session_id;
  });
  QVariantList rows;
  for (const auto& candidate : found) {
    const QUrl source(candidate.original_url);
    rows.append(QVariantMap{
        {"session_id", candidate.session_id},
        {"source_name", source.isLocalFile()
                            ? QFileInfo(source.toLocalFile()).fileName()
                            : QString()},
        {"captured_at", QLocale().toString(candidate.captured_at.toLocalTime(),
                                           QLocale::ShortFormat)},
        {"status", recovery::status_key(candidate.status)},
        {"restorable", candidate.status == recovery::Status::kValid}});
  }
  candidates_ = rows;
  emit changed();
}

void RecoveryCoordinator::discard(const QString& session_id) {
  const auto claimed = store_->claim(session_id);
  if (!store_->claimed()) {
    emit error(error_key(std::get<recovery::Error>(claimed)));
  } else if (const auto failure = store_->discard_claim()) {
    store_->drop_claim();
    emit error(QStringLiteral("recovery_discard_failed"));
  }
  refresh();
}

std::variant<sketchcad::RecoveryData, recovery::Error>
RecoveryCoordinator::claim(const QString& session_id) {
  auto claimed = store_->claim(session_id);
  if (std::holds_alternative<recovery::Error>(claimed)) store_->drop_claim();
  return claimed;
}

bool RecoveryCoordinator::adopt_claim() {
  if (store_->adopt()) {
    store_->drop_claim();
    return false;
  }
  cleanup_at_.reset();
  return true;
}

void RecoveryCoordinator::drop_claim() { store_->drop_claim(); }

void RecoveryCoordinator::installed(bool restored) {
  ++epoch_;
  schedule_.clear();
  observed_.reset();
  written_.reset();
  if (restored) {
    written_ = Key{epoch_, controller_.document().revision()};
    observed_ = written_;
  }
  arm();
}

void RecoveryCoordinator::shutdown() {
  timer_.stop();
  schedule_.clear();
  shut_down_ = true;
  store_->release();
  emit changed();
}
