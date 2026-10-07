#pragma once
#include <QObject>
#include <QVariantList>

#include "sketchcad/rectangle.h"
class Controller : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList points READ points NOTIFY geometry_changed)
  Q_PROPERTY(bool anchored READ anchored NOTIFY geometry_changed)
  Q_PROPERTY(QString status READ status NOTIFY status_changed)
 public:
  explicit Controller(QObject* parent = nullptr) : QObject(parent) {}
  QVariantList points() const;
  bool anchored() const { return rectangle_.anchored(); }
  QString status() const { return status_; }
  Q_INVOKABLE bool resize(double width, double height, bool anchor);
  Q_INVOKABLE bool translate(double dx, double dy);
  Q_INVOKABLE bool conflict();
 signals:
  void geometry_changed();
  void status_changed();

 private:
  sketchcad::Rectangle rectangle_;
  double width_ = 50, height_ = 30;
  QString status_ = "Ready";
  bool report(bool ok);
};
