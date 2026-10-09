#include "sketchcad/sketch_tool.h"

#include "sketchcad/diagnostics.h"
#include "sketchcad/dimension_layout.h"
#include "sketchcad/drag.h"
#include "sketchcad/solver.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sketchcad {
namespace {
constexpr double kTwoPi = 2 * std::numbers::pi;
// Sweeps closer than this to 0 or a full turn are treated as degenerate.
constexpr double kMinSweep = 1e-9;
constexpr std::size_t kMaxSelection = 2;

double distance(Position a, Position b) {
  return std::hypot(a.x - b.x, a.y - b.y);
}

// Counter-clockwise angle from `from` to `to` in [0, 2*pi).
double ccw_sweep(double from, double to) {
  double sweep = std::fmod(to - from, kTwoPi);
  if (sweep < 0) sweep += kTwoPi;
  return sweep;
}

// Adds driving dimensions at their measured values, skipping any that the
// diagnosis lists as dependent (redundant) or cannot judge.
void add_auto_dimensions(
    Sketch& sk, const std::vector<std::pair<ConstraintKind, EntityId>>& dims) {
  for (const auto& [kind, id] : dims) {
    const auto value = measure(sk, kind, id);
    if (!value) continue;
    Sketch trial = sk;
    const auto added = trial.add_dimension(kind, id, 0, *value);
    if (!added) continue;
    const Diagnosis d = diagnose(trial);
    if (d.status == DiagnosisStatus::kUnknown ||
        std::find(d.dependent.begin(), d.dependent.end(), *added) !=
            d.dependent.end())
      continue;
    sk = std::move(trial);
  }
}

// Points held by kOnCurve on any of `curves`, at their current positions.
std::map<EntityId, Position> riders_of(const Sketch& s,
                                       const std::vector<EntityId>& curves) {
  std::map<EntityId, Position> out;
  for (const auto& [id, c] : s.constraints())
    if (c.kind == ConstraintKind::kOnCurve &&
        std::find(curves.begin(), curves.end(), c.second) != curves.end())
      out[c.first] = std::get<SketchPoint>(*s.entity(c.first)).position;
  return out;
}

// `riders` moved by (dx, dy), added to `targets`.
void add_riders(std::map<EntityId, Position>& targets,
                const std::map<EntityId, Position>& riders, double dx,
                double dy) {
  for (const auto& [id, start] : riders)
    targets.try_emplace(id, Position{start.x + dx, start.y + dy});
}

// Constrains `point` onto each of `curves` (U04 snaps).
bool put_on(Sketch& s, EntityId point, const std::vector<EntityId>& curves) {
  for (EntityId curve : curves)
    if (!s.add_constraint(ConstraintKind::kOnCurve, point, curve)) return false;
  return true;
}

// Reuses a snapped point that still exists, otherwise creates one, on the
// curves it was snapped onto.
std::optional<EntityId> vertex_point(Sketch& s, Position p,
                                     std::optional<EntityId> existing,
                                     const std::vector<EntityId>& curves) {
  if (existing && s.entity(*existing)) return existing;
  const auto id = s.create_point(p);
  if (!id || !put_on(s, *id, curves)) return std::nullopt;
  return id;
}
}  // namespace

ToolSession::ToolSession(Document& document) : document_(document) {}

void ToolSession::set_tool(Tool tool) {
  cancel();
  if (tool != Tool::kSelect) selected_.clear();
  tool_ = tool;
}

SnapResult ToolSession::snapped(Position p,
                                std::optional<EntityId> exclude) const {
  return snap(document_.sketch(), p, snap_settings_, exclude);
}

void ToolSession::hover(Position p) {
  if (tool_ == Tool::kDimension) {
    hover_ = p;
    update_preview(p);
    return;
  }
  last_snap_ = snapped(p);
  hover_ = last_snap_->position;
  update_preview(last_snap_->position);
}

void ToolSession::press(Position p) {
  if (tool_ == Tool::kSelect) {
    dragged_point_.reset();
    curve_press_.reset();
    curve_start_.clear();
    riders_.clear();
    const auto hit = pick(document_.sketch(), p, pick_tolerance_mm_);
    if (!hit) {
      selected_.clear();
      return;
    }
    // Additive: reselecting moves to the end, a third entity drops the
    // oldest.
    std::erase(selected_, *hit);
    selected_.push_back(*hit);
    if (selected_.size() > kMaxSelection) selected_.erase(selected_.begin());
    const Sketch& sk = document_.sketch();
    const Entity e = *sk.entity(*hit);
    if (const auto* point = std::get_if<SketchPoint>(&e)) {
      dragged_point_ = hit;
      point_start_ = point->position;
      // Dragging a centre moves its circles and arcs, with their riders.
      std::vector<EntityId> centred;
      for (const auto& [id, other] : sk.entities())
        if ((std::holds_alternative<SketchCircle>(other) &&
             std::get<SketchCircle>(other).center == *hit) ||
            (std::holds_alternative<SketchArc>(other) &&
             std::get<SketchArc>(other).center == *hit))
          centred.push_back(id);
      riders_ = riders_of(sk, centred);
      return;
    }
    // A curve moves by its defining points: line ends or the centre.
    std::vector<EntityId> defining;
    if (const auto* l = std::get_if<SketchLine>(&e))
      defining = {l->start, l->end};
    else if (const auto* c = std::get_if<SketchCircle>(&e))
      defining = {c->center};
    else if (const auto* a = std::get_if<SketchArc>(&e))
      defining = {a->center};
    for (EntityId id : defining)
      curve_start_[id] = std::get<SketchPoint>(*sk.entity(id)).position;
    curve_press_ = p;
    riders_ = riders_of(sk, {*hit});
    return;
  }
  if (tool_ == Tool::kDimension) {
    press_dimension(p);
    return;
  }
  const SnapResult s = snapped(p);
  last_snap_ = s;
  hover_ = s.position;
  place(s);
}

std::optional<ToolSession::DimensionPick> ToolSession::dimension_pick() const {
  const Sketch& sk = document_.sketch();
  const auto& picks = dimension_picks_;
  if (picks.size() == 2) return DimensionPick{ConstraintKind::kDistance, picks[0], picks[1]};
  if (picks.size() != 1) return std::nullopt;
  const Entity e = *sk.entity(picks[0]);
  if (std::holds_alternative<SketchLine>(e))
    return DimensionPick{ConstraintKind::kLength, picks[0], 0};
  if (!std::holds_alternative<SketchPoint>(e))
    return DimensionPick{ConstraintKind::kRadius, picks[0], 0};
  return std::nullopt;
}

void ToolSession::press_dimension(Position p) {
  std::erase_if(dimension_picks_,
                [&](EntityId id) { return !document_.sketch().entity(id); });
  if (const auto d = dimension_pick()) {
    // Complete: this tap places the reference through `p`.
    if (document_.execute("Reference dimension", [&](Sketch& sk) {
          const auto value = measure(sk, d->kind, d->first, d->second);
          const auto id = value ? sk.add_dimension(d->kind, d->first,
                                                   d->second, *value, true)
                                : std::nullopt;
          if (!id) return false;
          const auto placement =
              dimension_placement_at(sk, *sk.constraint(*id), p);
          return !placement || sk.set_dimension_placement(*id, *placement);
        }))
      dimension_picks_.clear();
    update_preview(std::nullopt);
    return;
  }
  const auto hit = pick(document_.sketch(), p, pick_tolerance_mm_);
  if (!hit) return;
  const bool point =
      std::holds_alternative<SketchPoint>(*document_.sketch().entity(*hit));
  if (dimension_picks_.empty() ||
      (point && dimension_picks_.front() != *hit))
    dimension_picks_.push_back(*hit);
  update_preview(p);
}

// The picked geometry copied into the preview with the reference laid
// through `cursor`.
void ToolSession::preview_reference(Position cursor) {
  const Sketch& sk = document_.sketch();
  const auto at = [&](EntityId id) {
    return std::get<SketchPoint>(*sk.entity(id)).position;
  };
  const auto d = dimension_pick();
  if (!d) return;
  std::optional<EntityId> first, second;
  const Entity e = *sk.entity(d->first);
  if (d->kind == ConstraintKind::kDistance) {
    first = preview_.create_point(at(d->first));
    second = preview_.create_point(at(d->second));
  } else if (const auto* l = std::get_if<SketchLine>(&e)) {
    first = preview_.create_line(*preview_.create_point(at(l->start)),
                                 *preview_.create_point(at(l->end)));
  } else if (const auto* c = std::get_if<SketchCircle>(&e)) {
    first = preview_.create_circle(*preview_.create_point(at(c->center)),
                                   c->radius);
  } else if (const auto* a = std::get_if<SketchArc>(&e)) {
    first = preview_.create_arc(*preview_.create_point(at(a->center)),
                                a->radius, a->start_angle, a->sweep_angle);
  }
  if (!first) return;
  const auto value = measure(preview_, d->kind, *first, second.value_or(0));
  const auto id = value ? preview_.add_dimension(d->kind, *first,
                                                 second.value_or(0), *value,
                                                 true)
                        : std::nullopt;
  if (!id) return;
  if (const auto placement =
          dimension_placement_at(preview_, *preview_.constraint(*id), cursor))
    preview_.set_dimension_placement(*id, *placement);
}

// One drawing step at `s`, from a tap or from an entered value.
void ToolSession::place(const SnapResult& s) {
  // Exact match without snapping, snap tolerance with it.
  const double near =
      snap_settings_.enabled ? snap_settings_.point_tolerance_mm : 0;
  const auto close_to = [&](const Vertex& v) {
    return distance(s.position, v.position) <= near;
  };

  if (vertices_.empty()) {
    add_vertex(s);
  } else if (tool_ == Tool::kLine) {
    const Vertex start = vertices_.front();
    if (start.position != s.position &&
        document_.execute("Line", [&](Sketch& sk) {
          auto a = vertex_point(sk, start.position, start.point, start.curves);
          auto b = vertex_point(sk, s.position, s.point, s.curves);
          const auto line = a && b ? sk.create_line(*a, *b) : std::nullopt;
          if (!line) return false;
          // An end on a curve stretches with it, so no length (U04).
          if (auto_dimensions_ && start.curves.empty() && s.curves.empty())
            add_auto_dimensions(sk, {{ConstraintKind::kLength, *line}});
          return true;
        }))
      vertices_.clear();
  } else if (tool_ == Tool::kPolyline) {
    if (vertices_.size() >= 2 && close_to(vertices_.back()))
      commit_polyline(false);
    else if (vertices_.size() >= 3 && close_to(vertices_.front()))
      commit_polyline(true);
    else if (s.position != vertices_.back().position)
      add_vertex(s);
  } else if (tool_ == Tool::kRectangle) {
    const Position a = vertices_.front().position;
    Position b = s.position;
    if (locked_width_) b.x = a.x + *locked_width_;
    const double w = std::abs(b.x - a.x), h = std::abs(b.y - a.y);
    if (w > 0 && h > 0 &&
        document_.execute("Rectangle", [&](Sketch& sk) {
          const auto r = sk.create_rectangle(
              {std::min(a.x, b.x), std::min(a.y, b.y)}, w, h);
          if (!r) return false;
          // Corners snapped onto curves (a locked width moves the second).
          const auto corner = [&](Position at) -> std::optional<EntityId> {
            for (EntityId id : r->points)
              if (distance(std::get<SketchPoint>(*sk.entity(id)).position,
                           at) < 1e-9)
                return id;
            return std::nullopt;
          };
          const auto snap_corner = [&](Position at,
                                       const std::vector<EntityId>& on) {
            const auto id = corner(at);
            return !id || put_on(sk, *id, on);
          };
          if (!snap_corner(a, vertices_.front().curves) ||
              (!locked_width_ && !snap_corner(b, s.curves)))
            return false;
          const bool on_curve = !vertices_.front().curves.empty() ||
                                (!locked_width_ && !s.curves.empty());
          if (auto_dimensions_) {
            // Sides run bottom, right, top, left.
            if (!sk.add_constraint(ConstraintKind::kHorizontal, r->lines[0]) ||
                !sk.add_constraint(ConstraintKind::kVertical, r->lines[1]) ||
                !sk.add_constraint(ConstraintKind::kHorizontal, r->lines[2]) ||
                !sk.add_constraint(ConstraintKind::kVertical, r->lines[3]))
              return false;
            if (!on_curve)
              add_auto_dimensions(sk,
                                  {{ConstraintKind::kLength, r->lines[0]},
                                   {ConstraintKind::kLength, r->lines[1]}});
          }
          return true;
        }))
      vertices_.clear();
  } else if (tool_ == Tool::kCircle) {
    const Vertex center = vertices_.front();
    const double r = distance(center.position, s.position);
    if (r > 0 && document_.execute("Circle", [&](Sketch& sk) {
          auto c = vertex_point(sk, center.position, center.point, center.curves);
          const auto circle = c ? sk.create_circle(*c, r) : std::nullopt;
          if (!circle) return false;
          if (auto_dimensions_)
            add_auto_dimensions(sk, {{ConstraintKind::kRadius, *circle}});
          return true;
        }))
      vertices_.clear();
  } else if (tool_ == Tool::kArc) {
    const Vertex center = vertices_.front();
    if (vertices_.size() == 1) {
      if (distance(center.position, s.position) > 0) add_vertex(s);
    } else {
      const Position c = center.position, a = vertices_[1].position;
      const double start = std::atan2(a.y - c.y, a.x - c.x);
      const double sweep =
          ccw_sweep(start, std::atan2(s.position.y - c.y, s.position.x - c.x));
      if (distance(c, s.position) > 0 && sweep > kMinSweep &&
          sweep < kTwoPi - kMinSweep &&
          document_.execute("Arc", [&](Sketch& sk) {
            auto id = vertex_point(sk, c, center.point, center.curves);
            const auto arc =
                id ? sk.create_arc(*id, distance(c, a), start, sweep)
                   : std::nullopt;
            if (!arc) return false;
            if (auto_dimensions_)
              add_auto_dimensions(sk, {{ConstraintKind::kRadius, *arc}});
            return true;
          }))
        vertices_.clear();
    }
  }
  update_preview(in_progress() ? std::optional(s.position) : std::nullopt);
}

void ToolSession::drag(Position p) {
  if (tool_ == Tool::kSelect && curve_press_) {
    if (!dragging_) dragging_ = document_.begin_gesture("Move curve");
    if (!dragging_) return;
    const double dx = p.x - curve_press_->x, dy = p.y - curve_press_->y;
    std::map<EntityId, Position> targets;
    for (const auto& [id, start] : curve_start_)
      targets[id] = {start.x + dx, start.y + dy};
    add_riders(targets, riders_, dx, dy);
    document_.gesture_step([&](Sketch& sk) {
      return drag_points(sk, targets).status == SolveStatus::kSolved;
    });
    return;
  }
  if (tool_ != Tool::kSelect || !dragged_point_) return;
  if (!dragging_) dragging_ = document_.begin_gesture("Move point");
  if (!dragging_) return;
  // U01 snapping while dragging: points and the grid, no curves (U04).
  SnapSettings settings = snap_settings_;
  settings.curves = false;
  const SnapResult s = snap(document_.sketch(), p, settings, dragged_point_);
  last_snap_ = s;
  // Warm start from the last accepted state; a rejected step keeps it.
  document_.gesture_step([&](Sketch& sk) {
    std::map<EntityId, Position> targets{{*dragged_point_, s.position}};
    add_riders(targets, riders_, s.position.x - point_start_.x,
               s.position.y - point_start_.y);
    return drag_points(sk, targets).status == SolveStatus::kSolved;
  });
}

void ToolSession::release(Position) {
  if (dragging_) document_.end_gesture();
  dragging_ = false;
  dragged_point_.reset();
  curve_press_.reset();
  curve_start_.clear();
  riders_.clear();
}

bool ToolSession::finish() {
  return tool_ == Tool::kPolyline && vertices_.size() >= 2 &&
         commit_polyline(false);
}

bool ToolSession::cancel() {
  if (dragging_) {
    document_.cancel_gesture();
    dragging_ = false;
    dragged_point_.reset();
    curve_press_.reset();
    curve_start_.clear();
    riders_.clear();
    return true;
  }
  if (vertices_.empty() && dimension_picks_.empty()) return false;
  vertices_.clear();
  dimension_picks_.clear();
  update_preview(std::nullopt);
  return true;
}

DeleteResult ToolSession::delete_selection() {
  drop_stale_selection();
  if (selected_.empty()) return DeleteResult::kNothingSelected;
  const auto unconstrain = [](Sketch& sk, EntityId entity) {
    for (EntityId c : sk.constraints_of(entity)) sk.erase(c);
  };
  // Erases one entity with its constraints, then its defining points that no
  // other geometry uses (with theirs).
  const auto erase = [&](Sketch& sk, EntityId id) {
    const auto entity = sk.entity(id);
    if (!entity) return true;  // Already erased as a defining point.
    std::vector<EntityId> defining;
    if (const auto* l = std::get_if<SketchLine>(&*entity))
      defining = {l->start, l->end};
    else if (const auto* c = std::get_if<SketchCircle>(&*entity))
      defining = {c->center};
    else if (const auto* a = std::get_if<SketchArc>(&*entity))
      defining = {a->center};
    unconstrain(sk, id);
    if (!sk.erase(id)) return false;
    for (EntityId point : defining) {
      Sketch trial = sk;
      unconstrain(trial, point);
      if (trial.erase(point)) sk = std::move(trial);
    }
    return true;
  };
  const std::vector<EntityId> ids = selected_;
  const bool ok = document_.execute("Delete", [&](Sketch& sk) {
    // Curves before points, so a selected endpoint of a selected line goes.
    for (int pass = 0; pass < 2; ++pass)
      for (EntityId id : ids) {
        const auto e = sk.entity(id);
        if (e && std::holds_alternative<SketchPoint>(*e) == (pass == 1) &&
            !erase(sk, id))
          return false;
      }
    return true;
  });
  if (!ok) return DeleteResult::kPointInUse;
  selected_.clear();
  return DeleteResult::kDeleted;
}

bool ToolSession::toggle_construction() {
  drop_stale_selection();
  std::vector<EntityId> curves;
  bool any_off = false;
  for (EntityId id : selected_) {
    const Entity e = *document_.sketch().entity(id);
    if (std::holds_alternative<SketchPoint>(e)) continue;
    curves.push_back(id);
    any_off |= !std::visit([](const auto& c) { return c.construction; }, e);
  }
  if (curves.empty()) return false;
  return document_.execute("Construction", [&](Sketch& sk) {
    for (EntityId id : curves)
      if (!sk.set_construction(id, any_off)) return false;
    return true;
  });
}

bool ToolSession::undo() {
  cancel();
  const bool ok = document_.undo();
  drop_stale_selection();
  return ok;
}

bool ToolSession::redo() {
  cancel();
  const bool ok = document_.redo();
  drop_stale_selection();
  return ok;
}

void ToolSession::add_vertex(const SnapResult& s) {
  vertices_.push_back({s.position, s.point, s.curves});
}

bool ToolSession::commit_polyline(bool closed) {
  const std::vector<Vertex> vertices = vertices_;
  vertices_.clear();
  update_preview(std::nullopt);
  return document_.execute("Polyline", [&](Sketch& sk) {
    std::vector<EntityId> ids;
    for (const Vertex& v : vertices) {
      auto id = vertex_point(sk, v.position, v.point, v.curves);
      if (!id) return false;
      ids.push_back(*id);
    }
    if (closed) ids.push_back(ids.front());
    const auto free = [&](std::size_t i) {
      return vertices[i % vertices.size()].curves.empty();
    };
    std::vector<std::pair<ConstraintKind, EntityId>> dims;
    for (std::size_t i = 1; i < ids.size(); ++i) {
      const auto line = sk.create_line(ids[i - 1], ids[i]);
      if (!line) return false;
      if (free(i - 1) && free(i)) dims.push_back({ConstraintKind::kLength, *line});
    }
    if (auto_dimensions_) add_auto_dimensions(sk, dims);
    return true;
  });
}

void ToolSession::update_preview(std::optional<Position> cursor) {
  preview_ = Sketch();
  if (tool_ == Tool::kDimension) {
    if (cursor) preview_reference(*cursor);
    return;
  }
  if (vertices_.empty()) {
    locked_width_.reset();
    return;
  }
  if (cursor && tool_ == Tool::kRectangle && locked_width_)
    cursor->x = vertices_.front().position.x + *locked_width_;
  std::vector<Position> chain;
  for (const Vertex& v : vertices_) chain.push_back(v.position);
  if (cursor) chain.push_back(*cursor);
  std::vector<EntityId> points;
  for (Position p : chain) points.push_back(*preview_.create_point(p));
  const Position first = chain.front(), last = chain.back();
  switch (tool_) {
    case Tool::kLine:
    case Tool::kPolyline: {
      std::optional<EntityId> current;
      for (std::size_t i = 1; i < points.size(); ++i)
        if (chain[i - 1] != chain[i])
          current = preview_.create_line(points[i - 1], points[i]);
      // Live dimension of the segment being drawn.
      if (current && cursor) preview_dimension(ConstraintKind::kLength, *current);
      break;
    }
    case Tool::kRectangle:
      if (cursor) {
        preview_ = Sketch();
        const auto r = preview_.create_rectangle(
            {std::min(first.x, last.x), std::min(first.y, last.y)},
            std::abs(last.x - first.x), std::abs(last.y - first.y));
        if (r) {
          preview_dimension(ConstraintKind::kLength, r->lines[0]);
          preview_dimension(ConstraintKind::kLength, r->lines[1]);
        }
      }
      break;
    case Tool::kCircle:
      if (cursor)
        if (const auto c =
                preview_.create_circle(points.front(), distance(first, last)))
          preview_dimension(ConstraintKind::kRadius, *c);
      break;
    case Tool::kArc:
      if (chain.size() == 2) {
        if (const auto l = preview_.create_line(points[0], points[1]))
          preview_dimension(ConstraintKind::kLength, *l);
      } else if (chain.size() >= 3) {
        const Position a = chain[1];
        const double start = std::atan2(a.y - first.y, a.x - first.x);
        if (const auto arc = preview_.create_arc(
                points.front(), distance(first, a), start,
                ccw_sweep(start,
                          std::atan2(last.y - first.y, last.x - first.x))))
          preview_dimension(ConstraintKind::kRadius, *arc);
      }
      break;
    case Tool::kSelect:
    case Tool::kDimension:
    case Tool::kTrim:
    case Tool::kExtend:
      break;
  }
}

void ToolSession::drop_stale_selection() {
  std::erase_if(selected_,
                [&](EntityId id) { return !document_.sketch().entity(id); });
}
void ToolSession::preview_dimension(ConstraintKind kind, EntityId id) {
  if (const auto value = measure(preview_, kind, id))
    preview_.add_dimension(kind, id, 0, *value);
}

InputField ToolSession::input_field() const {
  if (vertices_.empty()) return InputField::kNone;
  switch (tool_) {
    case Tool::kLine:
    case Tool::kPolyline:
      return InputField::kLength;
    case Tool::kRectangle:
      return locked_width_ ? InputField::kHeight : InputField::kWidth;
    case Tool::kCircle:
      return InputField::kRadius;
    case Tool::kArc:
      return vertices_.size() == 1 ? InputField::kRadius : InputField::kNone;
    case Tool::kSelect:
    case Tool::kDimension:
    case Tool::kTrim:
    case Tool::kExtend:
      break;
  }
  return InputField::kNone;
}

double ToolSession::input_value() const {
  if (vertices_.empty() || !hover_) return 0;
  const Position from = vertices_.back().position;
  switch (input_field()) {
    case InputField::kLength:
    case InputField::kRadius:
      return distance(from, *hover_);
    case InputField::kWidth:
      return std::abs(hover_->x - vertices_.front().position.x);
    case InputField::kHeight:
      return std::abs(hover_->y - vertices_.front().position.y);
    case InputField::kNone:
      break;
  }
  return 0;
}

bool ToolSession::enter_value(double value) {
  const InputField field = input_field();
  if (field == InputField::kNone || !std::isfinite(value) || value <= 0)
    return false;
  const Position from = vertices_.back().position;
  const Position toward = hover_.value_or(Position{from.x + 1, from.y});
  const auto sign = [](double d) { return d < 0 ? -1.0 : 1.0; };
  if (field == InputField::kWidth) {
    locked_width_ = sign(toward.x - from.x) * value;
    update_preview(hover_);
    return true;
  }
  Position target;
  if (field == InputField::kHeight) {
    const Position a = vertices_.front().position;
    target = {a.x + *locked_width_, a.y + sign(toward.y - a.y) * value};
  } else {
    // Length or radius: along the direction to the hover, +X if none.
    const double d = distance(from, toward);
    const Position u = d > 0 ? Position{(toward.x - from.x) / d,
                                        (toward.y - from.y) / d}
                             : Position{1, 0};
    target = {from.x + u.x * value, from.y + u.y * value};
  }
  const std::size_t before = vertices_.size();
  const auto revision = document_.revision();
  place({target, SnapKind::kNone, std::nullopt});
  if (in_progress()) update_preview(hover_);
  return vertices_.size() != before || document_.revision() != revision;
}
}  // namespace sketchcad
