#include "sketch_controller.h"

#include <array>
#include <cmath>
#include <numbers>
#include <utility>

using sketchcad::Entity;
using sketchcad::Position;
using sketchcad::ScreenPoint;
using sketchcad::Sketch;
using sketchcad::SketchArc;
using sketchcad::SketchCircle;
using sketchcad::SketchLine;
using sketchcad::SketchPoint;
using sketchcad::SnapKind;
using sketchcad::Tool;

namespace {
// Screen-space sizes, converted to millimetres at the current zoom.
constexpr double kPickPx = 16;
constexpr double kSnapPx = 12;
constexpr double kPointMarkerPx = 3;
constexpr double kSelectedMarkerPx = 6;
constexpr int kMajorEvery = 5;

constexpr std::array<std::pair<Tool, const char*>, 6> kTools{{
    {Tool::kSelect, "select"},
    {Tool::kLine, "line"},
    {Tool::kPolyline, "polyline"},
    {Tool::kRectangle, "rectangle"},
    {Tool::kCircle, "circle"},
    {Tool::kArc, "arc"},
}};

QString num(double v) { return QString::number(v, 'f', 2); }
QString move_to(ScreenPoint p) {
  return QStringLiteral("M %1 %2 ").arg(num(p.x), num(p.y));
}
QString line_to(ScreenPoint p) {
  return QStringLiteral("L %1 %2 ").arg(num(p.x), num(p.y));
}
}  // namespace

SketchController::SketchController(QObject* parent) : QObject(parent) {
  sync_tolerances();
  refresh_scene();
}

QString SketchController::tool() const {
  for (const auto& [tool, name] : kTools)
    if (tool == session_.tool()) return name;
  return {};
}

void SketchController::set_tool(const QString& name) {
  for (const auto& [tool, tool_name] : kTools) {
    if (name != tool_name) continue;
    if (tool != session_.tool()) {
      session_.set_tool(tool);
      snap_hint_ = false;
      message_.clear();
      refresh_scene();
      emit changed();
    }
    return;
  }
}

bool SketchController::snap_visible() const {
  const auto& s = session_.last_snap();
  return snap_enabled_ && snap_hint_ && s && s->kind != SnapKind::kNone;
}

double SketchController::snap_x() const {
  const auto& s = session_.last_snap();
  return s ? view_.to_screen(s->position).x : 0;
}

double SketchController::snap_y() const {
  const auto& s = session_.last_snap();
  return s ? view_.to_screen(s->position).y : 0;
}

QString SketchController::snap_kind() const {
  if (!snap_visible()) return {};
  return session_.last_snap()->kind == SnapKind::kPoint ? "point" : "grid";
}

void SketchController::set_snap_enabled(bool enabled) {
  if (enabled == snap_enabled_) return;
  snap_enabled_ = enabled;
  sync_tolerances();
  emit changed();
}

void SketchController::set_finger_draws(bool enabled) {
  if (enabled == finger_draws_) return;
  finger_draws_ = enabled;
  emit changed();
}

int SketchController::entity_count() const {
  return static_cast<int>(document_.sketch().entities().size());
}

void SketchController::set_viewport_size(double width, double height) {
  if (!(width > 0 && height > 0)) return;
  const bool first = !(width_ > 0 && height_ > 0);
  width_ = width;
  height_ = height;
  if (first) {
    fit();
    return;
  }
  refresh_grid();
  emit changed();
}

void SketchController::hover(double x, double y) {
  session_.hover(world(x, y));
  snap_hint_ = session_.tool() != Tool::kSelect;
  refresh_scene();
  emit changed();
}

void SketchController::press(double x, double y) {
  message_.clear();
  session_.press(world(x, y));
  snap_hint_ = session_.tool() != Tool::kSelect;
  refresh_scene();
  emit changed();
}

void SketchController::drag(double x, double y) {
  if (session_.tool() != Tool::kSelect || !session_.selection()) return;
  session_.drag(world(x, y));
  snap_hint_ = true;
  refresh_scene();
  emit changed();
}

void SketchController::release(double x, double y) {
  session_.release(world(x, y));
  if (session_.tool() == Tool::kSelect) snap_hint_ = false;
  refresh_scene();
  emit changed();
}

void SketchController::pan(double dx, double dy) {
  view_.pan(dx, dy);
  refresh_grid();
  refresh_scene();
  emit changed();
}

void SketchController::zoom_at(double x, double y, double factor) {
  view_.zoom_at({x, y}, factor);
  sync_tolerances();
  refresh_grid();
  refresh_scene();
  emit changed();
}

void SketchController::fit() {
  if (!(width_ > 0 && height_ > 0)) return;
  view_.fit(sketchcad::bounds(document_.sketch()), width_, height_);
  sync_tolerances();
  refresh_grid();
  refresh_scene();
  emit changed();
}

bool SketchController::finish() {
  const bool ok = session_.finish();
  refresh_scene();
  emit changed();
  return ok;
}

bool SketchController::cancel() {
  const bool ok = session_.cancel();
  snap_hint_ = false;
  refresh_scene();
  emit changed();
  return ok;
}

void SketchController::clear_selection() {
  session_.clear_selection();
  message_.clear();
  refresh_scene();
  emit changed();
}

bool SketchController::delete_selection() {
  const auto result = session_.delete_selection();
  message_ = result == sketchcad::DeleteResult::kPointInUse
                 ? QStringLiteral("point_in_use")
                 : QString();
  refresh_scene();
  emit changed();
  return result == sketchcad::DeleteResult::kDeleted;
}

bool SketchController::undo() {
  const bool ok = session_.undo();
  message_.clear();
  refresh_scene();
  emit changed();
  return ok;
}

bool SketchController::redo() {
  const bool ok = session_.redo();
  message_.clear();
  refresh_scene();
  emit changed();
  return ok;
}

QPointF SketchController::screen_of(double x_mm, double y_mm) const {
  const ScreenPoint p = view_.to_screen({x_mm, y_mm});
  return {p.x, p.y};
}

QPointF SketchController::world_of(double x, double y) const {
  const Position p = world(x, y);
  return {p.x, p.y};
}

Position SketchController::world(double x, double y) const {
  return view_.to_world({x, y});
}

void SketchController::sync_tolerances() {
  const double s = view_.scale();
  session_.set_pick_tolerance(kPickPx / s);
  session_.set_snap({snap_enabled_, sketchcad::grid_step_for_scale(s),
                     kSnapPx / s});
}

// Arcs are split into pieces of at most half a turn so SVG never has to
// resolve an ambiguous or degenerate endpoint pair. Positive world sweep is
// counter-clockwise, which is sweep-flag 0 on the Y-down screen.
static QString arc_path(const sketchcad::ViewTransform& view, Position c,
                        double r, double start, double sweep) {
  const int pieces =
      std::max(1, static_cast<int>(std::ceil(std::abs(sweep) /
                                             std::numbers::pi - 1e-9)));
  const double rp = r * view.scale();
  const auto at = [&](double a) {
    return view.to_screen({c.x + r * std::cos(a), c.y + r * std::sin(a)});
  };
  QString path = move_to(at(start));
  for (int i = 1; i <= pieces; ++i) {
    const ScreenPoint p = at(start + sweep * i / pieces);
    path += QStringLiteral("A %1 %1 0 0 %2 %3 %4 ")
                .arg(num(rp), sweep > 0 ? "0" : "1", num(p.x), num(p.y));
  }
  return path;
}

QString SketchController::curve_path(const Sketch& sketch,
                                     const Entity& entity) const {
  const auto position = [&](sketchcad::EntityId id) {
    return std::get<SketchPoint>(*sketch.entity(id)).position;
  };
  if (const auto* l = std::get_if<SketchLine>(&entity))
    return move_to(view_.to_screen(position(l->start))) +
           line_to(view_.to_screen(position(l->end)));
  if (const auto* c = std::get_if<SketchCircle>(&entity))
    return arc_path(view_, position(c->center), c->radius, 0,
                    2 * std::numbers::pi);
  if (const auto* a = std::get_if<SketchArc>(&entity))
    return arc_path(view_, position(a->center), a->radius, a->start_angle,
                    a->sweep_angle);
  return {};
}

QString SketchController::marker_path(Position p, double radius_px) const {
  const ScreenPoint s = view_.to_screen(p);
  const QString r = num(radius_px);
  return QStringLiteral("M %1 %2 A %3 %3 0 0 0 %4 %2 A %3 %3 0 0 0 %1 %2 Z ")
      .arg(num(s.x + radius_px), num(s.y), r, num(s.x - radius_px));
}

void SketchController::refresh_scene() {
  const Sketch& sketch = document_.sketch();
  geometry_path_.clear();
  points_path_.clear();
  for (const auto& [id, entity] : sketch.entities()) {
    if (const auto* p = std::get_if<SketchPoint>(&entity))
      points_path_ += marker_path(p->position, kPointMarkerPx);
    else
      geometry_path_ += curve_path(sketch, entity);
  }
  selected_path_.clear();
  if (const auto id = session_.selection()) {
    if (const auto entity = sketch.entity(*id)) {
      if (const auto* p = std::get_if<SketchPoint>(&*entity))
        selected_path_ = marker_path(p->position, kSelectedMarkerPx);
      else
        selected_path_ = curve_path(sketch, *entity);
    }
  }
  preview_path_.clear();
  const Sketch& preview = session_.preview();
  for (const auto& [id, entity] : preview.entities())
    if (!std::holds_alternative<SketchPoint>(entity))
      preview_path_ += curve_path(preview, entity);
}

void SketchController::refresh_grid() {
  grid_minor_path_.clear();
  grid_major_path_.clear();
  axes_path_.clear();
  if (!(width_ > 0 && height_ > 0)) return;
  const double step = sketchcad::grid_step_for_scale(view_.scale());
  const Position lo = world(0, height_), hi = world(width_, 0);
  const auto add = [&](double index, bool vertical) {
    const double v = index * step;
    const ScreenPoint a = vertical ? view_.to_screen({v, lo.y})
                                   : view_.to_screen({lo.x, v});
    const ScreenPoint b = vertical ? view_.to_screen({v, hi.y})
                                   : view_.to_screen({hi.x, v});
    const QString segment = move_to(a) + line_to(b);
    if (index == 0) axes_path_ += segment;
    else if (std::fmod(index, kMajorEvery) == 0) grid_major_path_ += segment;
    else grid_minor_path_ += segment;
  };
  for (double i = std::ceil(lo.x / step); i * step <= hi.x; ++i) add(i, true);
  for (double i = std::ceil(lo.y / step); i * step <= hi.y; ++i) add(i, false);
}
