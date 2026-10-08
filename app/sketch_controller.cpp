#include "sketch_controller.h"

#include "sketchcad/dimension_layout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>

using sketchcad::Entity;
using sketchcad::EntityId;
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
  // Every command is solved; the status explains a rejected one.
  document_.set_commit_step([this](Sketch& sketch) {
    last_status_ = sketchcad::solve(sketch).status;
    return last_status_ == sketchcad::SolveStatus::kSolved;
  });
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
  conflict_entities_.clear();
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
  for (EntityId id : session_.selected())
    selected_path_ += entity_path(sketch, id, kSelectedMarkerPx);
  refresh_analysis();
  if (selected_constraint_ && !sketch.constraint(selected_constraint_))
    selected_constraint_ = 0;
  constraint_path_.clear();
  if (const auto c = sketch.constraint(selected_constraint_))
    for (EntityId id : {c->first, c->second})
      constraint_path_ += entity_path(sketch, id, kSelectedMarkerPx);
  refresh_dimensions();
  fixed_path_.clear();
  for (const auto& [id, c] : sketch.constraints())
    if (c.kind == sketchcad::ConstraintKind::kFix)
      fixed_path_ += entity_path(sketch, c.first, kSelectedMarkerPx);
  conflict_path_.clear();
  for (EntityId id : conflict_entities_)
    conflict_path_ += entity_path(sketch, id, kSelectedMarkerPx);
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

namespace {
constexpr std::array<std::pair<sketchcad::ConstraintKind, const char*>, 4>
    kDimensions{{
        {sketchcad::ConstraintKind::kLength, "length"},
        {sketchcad::ConstraintKind::kDistance, "distance"},
        {sketchcad::ConstraintKind::kAngle, "angle"},
        {sketchcad::ConstraintKind::kRadius, "radius"},
    }};
constexpr double kDegree = std::numbers::pi / 180;

QString status_message(sketchcad::SolveStatus status) {
  switch (status) {
    case sketchcad::SolveStatus::kUnsatisfied:
      return QStringLiteral("conflict");
    case sketchcad::SolveStatus::kInvalidInput:
      return QStringLiteral("invalid_geometry");
    case sketchcad::SolveStatus::kNumericalFailure:
      return QStringLiteral("numerical_failure");
    case sketchcad::SolveStatus::kDegenerate:
      return QStringLiteral("degenerate");
    case sketchcad::SolveStatus::kSolved:
      break;
  }
  return {};
}
}  // namespace

QVariantList SketchController::dimensions() const {
  QVariantList list;
  for (const auto& [id, c] : document_.sketch().constraints()) {
    for (const auto& [kind, name] : kDimensions) {
      if (kind != c.kind) continue;
      const bool angle = kind == sketchcad::ConstraintKind::kAngle;
      list.append(QVariantMap{
          {"id", QVariant::fromValue<qulonglong>(id)},
          {"kind", QString(name)},
          {"value", angle ? c.value / kDegree : c.value},
      });
    }
  }
  return list;
}

qulonglong SketchController::add_dimension(const QString& name,
                                           qulonglong first,
                                           qulonglong second) {
  std::optional<sketchcad::ConstraintKind> kind;
  for (const auto& [k, n] : kDimensions)
    if (name == n) kind = k;
  const auto value =
      kind ? sketchcad::measure(document_.sketch(), *kind, first, second)
           : std::nullopt;
  std::optional<sketchcad::EntityId> id;
  bool ok = false;
  // Validate on a copy so a rejected reference is not reported as a solve
  // error.
  Sketch probe = document_.sketch();
  if (!value || !probe.add_dimension(*kind, first, second, *value)) {
    message_ = QStringLiteral("invalid_dimension");
  } else {
    ok = document_.execute("Dimension", [&](Sketch& s) {
      id = s.add_dimension(*kind, first, second, *value);
      return id.has_value();
    });
    message_ = ok ? QString() : status_message(last_status_);
    highlight_rejected(ok, probe);
  }
  refresh_scene();
  emit changed();
  return ok ? *id : 0;
}

bool SketchController::set_dimension(qulonglong id, double value) {
  const auto c = document_.sketch().constraint(id);
  bool ok = false;
  if (!c || !sketchcad::is_dimension(c->kind)) {
    message_ = QStringLiteral("invalid_dimension");
  } else {
    if (c->kind == sketchcad::ConstraintKind::kAngle) value *= kDegree;
    Sketch probe = document_.sketch();
    if (!probe.set_dimension(id, value)) {
      message_ = QStringLiteral("invalid_value");
    } else {
      ok = document_.execute(
          "Dimension", [&](Sketch& s) { return s.set_dimension(id, value); });
      message_ = ok ? QString() : status_message(last_status_);
      highlight_rejected(ok, probe);
    }
  }
  refresh_scene();
  emit changed();
  return ok;
}

namespace {
struct Action {
  const char* key;
  sketchcad::ConstraintKind kind;
};
constexpr std::array<Action, 12> kActions{{
    {"coincident", sketchcad::ConstraintKind::kCoincident},
    {"horizontal", sketchcad::ConstraintKind::kHorizontal},
    {"vertical", sketchcad::ConstraintKind::kVertical},
    {"parallel", sketchcad::ConstraintKind::kParallel},
    {"perpendicular", sketchcad::ConstraintKind::kPerpendicular},
    {"tangent", sketchcad::ConstraintKind::kTangent},
    {"equal", sketchcad::ConstraintKind::kEqual},
    {"fix", sketchcad::ConstraintKind::kFix},
    {"length", sketchcad::ConstraintKind::kLength},
    {"distance", sketchcad::ConstraintKind::kDistance},
    {"angle", sketchcad::ConstraintKind::kAngle},
    {"radius", sketchcad::ConstraintKind::kRadius},
}};
QString key_of(sketchcad::ConstraintKind kind) {
  for (const auto& a : kActions)
    if (a.kind == kind) return a.key;
  return {};
}
// Value text: up to three decimals without trailing zeros.
QString format_value(double v) {
  QString text = QString::number(v, 'f', 3);
  while (text.endsWith('0')) text.chop(1);
  if (text.endsWith('.')) text.chop(1);
  return text;
}
}  // namespace

// Adds `action` on the current selection to `sketch`; the new ID or nullopt.
std::optional<EntityId> SketchController::add_action(Sketch& sketch,
                                                     const QString& key) const {
  const auto& sel = session_.selected();
  if (sel.empty()) return std::nullopt;
  const EntityId first = sel[0], second = sel.size() > 1 ? sel[1] : 0;
  for (const auto& a : kActions) {
    if (key != a.key) continue;
    if (!sketchcad::is_dimension(a.kind))
      return sketch.add_constraint(a.kind, first, second);
    const auto value = sketchcad::measure(sketch, a.kind, first, second);
    if (!value) return std::nullopt;
    return sketch.add_dimension(a.kind, first, second, *value);
  }
  return std::nullopt;
}

void SketchController::refresh_analysis() {
  const auto& sketch = document_.sketch();
  if (document_.revision() != analysed_revision_ || !analysed_) {
    diagnosis_ = sketchcad::diagnose(sketch);
    analysed_revision_ = document_.revision();
    analysed_ = true;
    applicable_selection_.reset();
  }
  if (applicable_selection_ != session_.selected()) {
    applicable_.clear();
    for (const auto& a : kActions) {
      // Actions that could only collapse geometry are not offered;
      // conflicting ones are, so the conflict can be shown.
      Sketch probe = sketch;
      if (add_action(probe, a.key) &&
          sketchcad::solve(probe).status != sketchcad::SolveStatus::kDegenerate)
        applicable_.append(a.key);
    }
    applicable_selection_ = session_.selected();
  }
}

void SketchController::highlight_rejected(bool ok, const Sketch& candidate) {
  conflict_entities_.clear();
  if (ok || last_status_ != sketchcad::SolveStatus::kUnsatisfied) return;
  const auto d = sketchcad::diagnose(candidate);
  std::vector<EntityId> ids = d.dependent;
  ids.insert(ids.end(), d.violated.begin(), d.violated.end());
  for (EntityId id : ids)
    if (const auto c = candidate.constraint(id))
      for (EntityId e : {c->first, c->second})
        if (e) conflict_entities_.push_back(e);
}

QString SketchController::entity_path(const Sketch& sketch, EntityId id,
                                      double marker_px) const {
  const auto entity = sketch.entity(id);
  if (!entity) return {};
  if (const auto* p = std::get_if<SketchPoint>(&*entity))
    return marker_path(p->position, marker_px);
  return curve_path(sketch, *entity);
}

QStringList SketchController::applicable() const { return applicable_; }

QVariantList SketchController::constraints() const {
  QVariantList list;
  const auto& d = diagnosis_;
  const auto has = [](const std::vector<EntityId>& ids, EntityId id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
  };
  for (const auto& [id, c] : document_.sketch().constraints()) {
    QVariantMap entry{
        {"id", QVariant::fromValue<qulonglong>(id)},
        {"kind", key_of(c.kind)},
        {"entities", QVariantList{QVariant::fromValue<qulonglong>(c.first),
                                  QVariant::fromValue<qulonglong>(c.second)}},
        {"dependent", has(d.dependent, id)},
        {"violated", has(d.violated, id)},
    };
    if (sketchcad::is_dimension(c.kind))
      entry["value"] = c.kind == sketchcad::ConstraintKind::kAngle
                           ? c.value / kDegree
                           : c.value;
    list.append(entry);
  }
  return list;
}

void SketchController::set_selected_constraint(qulonglong id) {
  selected_constraint_ = document_.sketch().constraint(id) ? id : 0;
  refresh_scene();
  emit changed();
}

QString SketchController::diagnosis() const {
  switch (diagnosis_.status) {
    case sketchcad::DiagnosisStatus::kConsistent:
      return QStringLiteral("consistent");
    case sketchcad::DiagnosisStatus::kRedundant:
      return QStringLiteral("redundant");
    case sketchcad::DiagnosisStatus::kConflicting:
      return QStringLiteral("conflicting");
    case sketchcad::DiagnosisStatus::kUnknown:
      break;
  }
  return QStringLiteral("unknown");
}

int SketchController::dof() const { return diagnosis_.dof.value_or(-1); }

QString SketchController::diagnosis_reason() const {
  switch (diagnosis_.reason) {
    case sketchcad::UnknownReason::kInvalidGeometry:
      return QStringLiteral("invalid_geometry");
    case sketchcad::UnknownReason::kNumericalFailure:
      return QStringLiteral("numerical_failure");
    case sketchcad::UnknownReason::kIllConditioned:
      return QStringLiteral("ill_conditioned");
    case sketchcad::UnknownReason::kTooLarge:
      return QStringLiteral("too_large");
    case sketchcad::UnknownReason::kNone:
      break;
  }
  return {};
}

QVariantList SketchController::dimension_labels() const {
  QVariantList list;
  const auto has = [](const std::vector<EntityId>& ids, EntityId id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
  };
  for (const auto& g : sketchcad::layout_dimensions(document_.sketch(), view_)) {
    QString text;
    switch (g.kind) {
      case sketchcad::ConstraintKind::kAngle:
        text = format_value(g.value / kDegree) + QStringLiteral("°");
        break;
      case sketchcad::ConstraintKind::kRadius:
        text = QStringLiteral("R") + format_value(g.value);
        break;
      default:
        text = format_value(g.value);
        break;
    }
    list.append(QVariantMap{
        {"id", QVariant::fromValue<qulonglong>(g.id)},
        {"x", g.text_position.x},
        {"y", g.text_position.y},
        {"angle", g.text_angle / kDegree},
        {"text", text},
        {"bad", has(diagnosis_.dependent, g.id) || has(diagnosis_.violated, g.id)}});
  }
  return list;
}

qulonglong SketchController::apply(const QString& key) {
  qulonglong result = 0;
  if (!applicable_.contains(key)) {
    message_ = QStringLiteral("invalid_action");
  } else {
    Sketch probe = document_.sketch();
    add_action(probe, key);
    std::optional<EntityId> id;
    const bool ok = document_.execute(key.toStdString(), [&](Sketch& s) {
      id = add_action(s, key);
      return id.has_value();
    });
    message_ = ok ? QString() : status_message(last_status_);
    highlight_rejected(ok, probe);
    if (ok) result = *id;
  }
  refresh_scene();
  emit changed();
  return result;
}

bool SketchController::remove_constraint(qulonglong id) {
  const bool ok =
      document_.sketch().constraint(id) &&
      document_.execute("Remove constraint",
                        [&](Sketch& s) { return s.erase(id); });
  message_.clear();
  if (ok) conflict_entities_.clear();
  refresh_scene();
  emit changed();
  return ok;
}

void SketchController::highlight_dependent() {
  conflict_entities_.clear();
  std::vector<EntityId> ids = diagnosis_.dependent;
  ids.insert(ids.end(), diagnosis_.violated.begin(), diagnosis_.violated.end());
  const Sketch& s = document_.sketch();
  for (EntityId id : ids)
    if (const auto c = s.constraint(id))
      for (EntityId e : {c->first, c->second})
        if (e) conflict_entities_.push_back(e);
  refresh_scene();
  emit changed();
}

QVariantList SketchController::selection_properties() const {
  const Sketch& sketch = document_.sketch();
  QVariantList rows;
  const auto row = [&](const QString& label, const QString& value,
                       const QString& unit = {}) {
    rows.push_back(QVariantMap{{"label", label}, {"value", value},
                               {"unit", unit}});
  };
  const QString mm = QStringLiteral("мм"), deg = QStringLiteral("°");
  const auto& selected = session_.selected();
  if (selected.empty()) {
    row(QStringLiteral("Объектов"), QString::number(entity_count()));
    row(QStringLiteral("Ограничений"),
        QString::number(sketch.constraints().size()));
    row(QStringLiteral("DOF"), dof() < 0 ? QStringLiteral("—")
                                         : QString::number(dof()));
    return rows;
  }
  if (selected.size() != 1) return rows;
  const auto entity = sketch.entity(selected.front());
  if (!entity) return rows;
  const auto at = [&](EntityId id) {
    return std::get<SketchPoint>(*sketch.entity(id)).position;
  };
  if (const auto* p = std::get_if<SketchPoint>(&*entity)) {
    row(QStringLiteral("X"), format_value(p->position.x), mm);
    row(QStringLiteral("Y"), format_value(p->position.y), mm);
  } else if (const auto* l = std::get_if<sketchcad::SketchLine>(&*entity)) {
    const Position a = at(l->start), b = at(l->end);
    double angle = std::atan2(b.y - a.y, b.x - a.x) / kDegree;
    angle = std::fmod(angle + 360, 180);
    if (std::abs(angle - 180) < 5e-4) angle = 0;
    row(QStringLiteral("Длина"), format_value(std::hypot(b.x - a.x, b.y - a.y)),
        mm);
    row(QStringLiteral("Угол"), format_value(angle), deg);
  } else if (const auto* c = std::get_if<sketchcad::SketchCircle>(&*entity)) {
    row(QStringLiteral("Радиус"), format_value(c->radius), mm);
    row(QStringLiteral("Диаметр"), format_value(2 * c->radius), mm);
  } else if (const auto* r = std::get_if<sketchcad::SketchArc>(&*entity)) {
    row(QStringLiteral("Радиус"), format_value(r->radius), mm);
    row(QStringLiteral("Угол дуги"), format_value(r->sweep_angle / kDegree),
        deg);
  }
  return rows;
}

QString SketchController::selection_title() const {
  const auto& selected = session_.selected();
  if (selected.empty()) return QStringLiteral("Эскиз");
  if (selected.size() > 1)
    return QStringLiteral("%1 объекта").arg(selected.size());
  const auto entity = document_.sketch().entity(selected.front());
  if (!entity) return {};
  if (std::holds_alternative<SketchPoint>(*entity))
    return QStringLiteral("Точка");
  if (std::holds_alternative<sketchcad::SketchLine>(*entity))
    return QStringLiteral("Линия");
  if (std::holds_alternative<sketchcad::SketchCircle>(*entity))
    return QStringLiteral("Окружность");
  return QStringLiteral("Дуга");
}

// ESKD dimension graphics (dimension-style.adoc) as two SVG layers.
void SketchController::refresh_dimensions() {
  dimension_paths(document_.sketch(), dimension_path_, dimension_arrows_path_);
  dimension_paths(session_.preview(), preview_dimension_path_,
                  preview_dimension_arrows_path_);
}

void SketchController::dimension_paths(const Sketch& sketch, QString& lines,
                                       QString& arrows) const {
  lines.clear();
  arrows.clear();
  for (const auto& g : sketchcad::layout_dimensions(sketch, view_)) {
    for (const auto& [a, b] : g.segments)
      lines += move_to(a) + line_to(b);
    for (const auto& arc : g.arcs) {
      // Pieces of at most half a turn; positive screen sweep is SVG
      // sweep-flag 1 (Y down).
      const int pieces = static_cast<int>(
          std::ceil(std::abs(arc.sweep) / std::numbers::pi - 1e-9));
      const auto at = [&](double angle) {
        return ScreenPoint{arc.center.x + arc.radius * std::cos(angle),
                           arc.center.y + arc.radius * std::sin(angle)};
      };
      lines += move_to(at(arc.start));
      for (int i = 1; i <= std::max(pieces, 1); ++i) {
        const ScreenPoint p =
            at(arc.start + arc.sweep * i / std::max(pieces, 1));
        lines += QStringLiteral("A %1 %1 0 0 %2 %3 %4 ")
                   .arg(num(arc.radius))
                   .arg(arc.sweep > 0 ? 1 : 0)
                   .arg(num(p.x), num(p.y));
      }
    }
    for (const auto& a : g.arrows)
      arrows +=
          move_to(a[0]) + line_to(a[1]) + line_to(a[2]) + QStringLiteral("Z ");
  }
}

void SketchController::set_auto_dimensions(bool enabled) {
  if (enabled == session_.auto_dimensions()) return;
  session_.set_auto_dimensions(enabled);
  emit changed();
}

QString SketchController::input_field() const {
  switch (session_.input_field()) {
    case sketchcad::InputField::kLength: return QStringLiteral("length");
    case sketchcad::InputField::kWidth: return QStringLiteral("width");
    case sketchcad::InputField::kHeight: return QStringLiteral("height");
    case sketchcad::InputField::kRadius: return QStringLiteral("radius");
    case sketchcad::InputField::kNone: break;
  }
  return {};
}

double SketchController::input_value() const { return session_.input_value(); }

bool SketchController::enter_value(double value) {
  const bool ok = session_.enter_value(value);
  message_ = ok ? QString() : QStringLiteral("invalid_value");
  refresh_scene();
  emit changed();
  return ok;
}

QVariantList SketchController::preview_dimension_labels() const {
  QVariantList list;
  for (const auto& g : sketchcad::layout_dimensions(session_.preview(), view_)) {
    const QString text = g.kind == sketchcad::ConstraintKind::kRadius
                             ? QStringLiteral("R") + format_value(g.value)
                             : format_value(g.value);
    list.append(QVariantMap{{"x", g.text_position.x},
                            {"y", g.text_position.y},
                            {"angle", g.text_angle / kDegree},
                            {"text", text}});
  }
  return list;
}
