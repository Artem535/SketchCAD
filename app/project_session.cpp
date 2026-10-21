#include "project_session.h"

#include <QDir>
#include <QFileInfo>

#include "recovery_coordinator.h"
#include "sketch_controller.h"

ProjectSession::ProjectSession(SketchController& controller, QObject* parent)
    : QObject(parent),
      controller_(controller),
      document_(controller.document_) {
  connect(&controller_, &SketchController::changed, this,
          &ProjectSession::changed);
  connect(this, &ProjectSession::document_replaced, &controller_,
          &SketchController::reset_after_open);
}
QString ProjectSession::display_name() const {
  if (!file_url_.isEmpty())
    return QFileInfo(file_url_.toLocalFile()).fileName();
  if (!recovered_) return tr("Без имени");
  const QString source =
      recovered_from_.isLocalFile()
          ? QFileInfo(recovered_from_.toLocalFile()).fileName()
          : QString();
  return tr("Восстановлено — %1")
      .arg(source.isEmpty() ? tr("Без имени") : source);
}
QUrl ProjectSession::recovery_source() const {
  return file_url_.isEmpty() ? recovered_from_ : file_url_;
}
// A restored copy is first saved under a new name next to its source, never
// over the source path from the checkpoint metadata.
QUrl ProjectSession::suggested_save_url() const {
  if (!file_url_.isEmpty() || !recovered_ || !recovered_from_.isLocalFile())
    return file_url_;
  const QFileInfo source(recovered_from_.toLocalFile());
  return QUrl::fromLocalFile(source.dir().filePath(
      tr("%1 (восстановлено).sketchcad").arg(source.completeBaseName())));
}
bool ProjectSession::dirty() const { return document_.dirty(); }
bool ProjectSession::busy() const { return phase_ != Phase::kIdle; }
void ProjectSession::set_recovery(RecoveryCoordinator* recovery) {
  recovery_ = recovery;
}
void ProjectSession::request_restore(const QString& session_id) {
  if (busy() || !recovery_) return;
  restore_id_ = session_id;
  request(Action::kRestore);
}
void ProjectSession::request_new() { request(Action::kNew); }
void ProjectSession::request_open() { request(Action::kOpen); }
void ProjectSession::request_save() { request(Action::kSave); }
void ProjectSession::request_save_as() { request(Action::kSaveAs); }
void ProjectSession::request_close() { request(Action::kClose); }
void ProjectSession::request(Action action) {
  if (busy()) return;
  // Reserve the operation before emitting any controller notifications.
  phase_ = Phase::kConfirm;
  pending_ = action;
  controller_.prepare_file_operation();
  if (action != Action::kSave && action != Action::kSaveAs && dirty()) {
    emit changed();
    emit ask_unsaved();
    return;
  }
  proceed();
}
void ProjectSession::finish() {
  phase_ = Phase::kIdle;
  pending_ = Action::kNone;
  emit changed();
}
void ProjectSession::proceed() {
  switch (pending_) {
    case Action::kNew:
      if (!retire_for_replacement()) return;
      if (!document_.replace({}, 0)) {
        fail(project_file::Error::kInvalidProject);
        return;
      }
      file_url_ = QUrl{};
      recovered_ = false;
      recovered_from_ = QUrl{};
      if (recovery_) recovery_->installed(false);
      emit document_replaced();
      finish();
      return;
    case Action::kOpen:
      phase_ = Phase::kOpen;
      emit changed();
      emit choose_open();
      return;
    case Action::kClose:
      if (!retire_for_replacement()) return;
      if (recovery_) recovery_->shutdown();
      finish();
      emit close_ready();
      return;
    case Action::kRestore:
      restore();
      return;
    case Action::kSave:
      if (!file_url_.isEmpty()) {
        save(file_url_);
        return;
      }
      [[fallthrough]];
    case Action::kSaveAs:
      phase_ = Phase::kSave;
      emit changed();
      emit choose_save(suggested_save_url());
      return;
    case Action::kNone:
      finish();
      return;
  }
}
void ProjectSession::answer_unsaved(const QString& choice) {
  if (phase_ != Phase::kConfirm) return;
  if (choice == "cancel") {
    finish();
    return;
  }
  if (choice == "discard") {
    proceed();
    return;
  }
  if (choice != "save") return;
  if (!file_url_.isEmpty()) {
    save(file_url_);
    return;
  }
  phase_ = Phase::kSave;
  emit changed();
  emit choose_save(suggested_save_url());
}
void ProjectSession::accept_file(const QUrl& selected) {
  if (phase_ == Phase::kSave) {
    QUrl destination = selected;
    if (destination.isLocalFile() && !destination.toLocalFile().isEmpty() &&
        QFileInfo(destination.toLocalFile()).suffix().isEmpty())
      destination =
          QUrl::fromLocalFile(destination.toLocalFile() + ".sketchcad");
    save(destination);
    return;
  }
  if (phase_ != Phase::kOpen) return;
  auto result = project_file::read(selected);
  if (auto* error = std::get_if<project_file::Error>(&result)) {
    fail(*error);
    return;
  }
  auto data = std::get<sketchcad::ProjectData>(std::move(result));
  if (data.revision == UINT64_MAX) {
    fail(project_file::Error::kInvalidProject);
    return;
  }
  if (!retire_for_replacement()) return;
  if (!document_.replace(std::move(data.sketch), data.revision)) {
    fail(project_file::Error::kInvalidProject);
    return;
  }
  file_url_ = selected;
  recovered_ = false;
  recovered_from_ = QUrl{};
  if (recovery_) recovery_->installed(false);
  emit document_replaced();
  finish();
}
void ProjectSession::save(const QUrl& destination) {
  const auto error = project_file::write(
      destination, {document_.sketch(), document_.revision()});
  if (error) {
    fail(*error);
    return;
  }
  file_url_ = destination;
  recovered_ = false;
  recovered_from_ = QUrl{};
  document_.mark_saved();
  if (recovery_ && !recovery_->retire()) {
    // The project file is saved; only its recovery copy remains.
    fail(QStringLiteral("recovery_cleanup_failed"));
    return;
  }
  if (pending_ == Action::kNew || pending_ == Action::kOpen ||
      pending_ == Action::kClose || pending_ == Action::kRestore) {
    proceed();
    return;
  }
  finish();
}
bool ProjectSession::retire_for_replacement() {
  if (!recovery_ || recovery_->retire()) return true;
  fail(QStringLiteral("recovery_cleanup_failed"));
  return false;
}
// Restores the checkpoint chosen in the recovery list. Every failure leaves
// the current document, path and history unchanged.
void ProjectSession::restore() {
  auto claimed = recovery_->claim(restore_id_);
  if (auto* error = std::get_if<recovery::Error>(&claimed)) {
    fail(*error == recovery::Error::kLocked
             ? QStringLiteral("recovery_locked")
             : QStringLiteral("recovery_restore_failed"));
    recovery_->refresh();
    return;
  }
  auto data = std::get<sketchcad::RecoveryData>(std::move(claimed));
  if (data.project.revision == UINT64_MAX || !recovery_->adopt_claim()) {
    recovery_->drop_claim();
    fail(QStringLiteral("recovery_restore_failed"));
    recovery_->refresh();
    return;
  }
  // Cannot fail: gestures were cancelled and the revision checked above.
  document_.replace(std::move(data.project.sketch), data.project.revision,
                    false);
  file_url_ = QUrl{};
  recovered_ = true;
  recovered_from_ = QUrl(QString::fromStdString(data.meta.original_url));
  recovery_->installed(true);
  emit document_replaced();
  finish();
  recovery_->refresh();
}
void ProjectSession::cancel_file() {
  if (phase_ == Phase::kOpen || phase_ == Phase::kSave) finish();
}
void ProjectSession::fail(project_file::Error failure) {
  QString key;
  switch (failure) {
    case project_file::Error::kUnreadable:
      key = "project_unreadable";
      break;
    case project_file::Error::kInvalidProject:
      key = "project_invalid";
      break;
    case project_file::Error::kUnsupportedVersion:
      key = "project_version";
      break;
    case project_file::Error::kTooLarge:
      key = "project_too_large";
      break;
    case project_file::Error::kWriteFailed:
      key = "project_write_failed";
      break;
    case project_file::Error::kUnsupportedUrl:
      key = "project_unsupported_url";
      break;
  }
  fail(key);
}
void ProjectSession::fail(const QString& key) {
  finish();
  emit error(key);
}
