#include "project_session.h"

#include <QFileInfo>

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
  return file_url_.isEmpty() ? tr("Без имени")
                             : QFileInfo(file_url_.toLocalFile()).fileName();
}
bool ProjectSession::dirty() const { return document_.dirty(); }
bool ProjectSession::busy() const { return phase_ != Phase::kIdle; }
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
      if (!document_.replace({}, 0)) {
        fail(project_file::Error::kInvalidProject);
        return;
      }
      file_url_ = QUrl{};
      emit document_replaced();
      finish();
      return;
    case Action::kOpen:
      phase_ = Phase::kOpen;
      emit changed();
      emit choose_open();
      return;
    case Action::kClose:
      finish();
      emit close_ready();
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
      emit choose_save(file_url_);
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
  emit choose_save({});
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
  if (!document_.replace(std::move(data.sketch), data.revision)) {
    fail(project_file::Error::kInvalidProject);
    return;
  }
  file_url_ = selected;
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
  document_.mark_saved();
  if (pending_ == Action::kNew || pending_ == Action::kOpen ||
      pending_ == Action::kClose) {
    proceed();
    return;
  }
  finish();
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
  finish();
  emit error(key);
}
