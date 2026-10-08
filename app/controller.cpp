#include "controller.h"

#include <QVariantMap>
QVariantList Controller::points() const {
  QVariantList result;
  for (const auto& p : rectangle_.points())
    result.append(QVariantMap{{"id", p.id}, {"x", p.x}, {"y", p.y}});
  return result;
}
bool Controller::report(bool ok) {
  status_ = ok ? "Solved" : "Rejected: geometry preserved";
  emit status_changed();
  if (ok) emit geometry_changed();
  return ok;
}
bool Controller::resize(double width, double height, bool anchor) {
  const bool ok = rectangle_.solve(width, height, anchor);
  if (ok) {
    width_ = width;
    height_ = height;
  }
  return report(ok);
}
bool Controller::translate(double dx, double dy) {
  return report(rectangle_.translate(dx, dy));
}
bool Controller::conflict() {
  return report(
      rectangle_.solve(width_, height_, rectangle_.anchored(), width_ + 10));
}
