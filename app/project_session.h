#pragma once
#include <QObject>
#include <QUrl>

#include "project_file.h"
#include "sketchcad/document.h"
class SketchController;
class ProjectSession : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString display_name READ display_name NOTIFY changed)
  Q_PROPERTY(QUrl file_url READ file_url NOTIFY changed)
  Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
 public:
  explicit ProjectSession(SketchController&, QObject* parent = nullptr);
  QString display_name() const;
  QUrl file_url() const { return file_url_; }
  bool dirty() const;
  bool busy() const;
  Q_INVOKABLE void request_new();
  Q_INVOKABLE void request_open();
  Q_INVOKABLE void request_save();
  Q_INVOKABLE void request_save_as();
  Q_INVOKABLE void request_close();
  Q_INVOKABLE void answer_unsaved(const QString& choice);
  Q_INVOKABLE void accept_file(const QUrl& url);
  Q_INVOKABLE void cancel_file();
 signals:
  void changed();
  void ask_unsaved();
  void choose_open();
  void choose_save(QUrl suggested_url);
  void close_ready();
  void error(QString key);
  void document_replaced();

 private:
  enum class Action { kNone, kNew, kOpen, kSave, kSaveAs, kClose };
  enum class Phase { kIdle, kConfirm, kOpen, kSave };
  void request(Action);
  void proceed();
  void save(const QUrl&);
  void finish();
  void fail(project_file::Error);
  SketchController& controller_;
  sketchcad::Document& document_;
  QUrl file_url_;
  Action pending_ = Action::kNone;
  Phase phase_ = Phase::kIdle;
};
