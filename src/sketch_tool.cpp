#include "sketchcad/sketch_tool.h"

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

// Reuses a snapped point that still exists, otherwise creates one.
std::optional<EntityId> vertex_point(Sketch& s, Position p,
                                     std::optional<EntityId> existing) {
  if (existing && s.entity(*existing)) return existing;
  return s.create_point(p);
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
  last_snap_ = snapped(p);
  update_preview(last_snap_->position);
}

void ToolSession::press(Position p) {
  if (tool_ == Tool::kSelect) {
    dragged_point_.reset();
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
    if (std::holds_alternative<SketchPoint>(*document_.sketch().entity(*hit)))
      dragged_point_ = hit;
    return;
  }
  const SnapResult s = snapped(p);
  last_snap_ = s;
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
          auto a = vertex_point(sk, start.position, start.point);
          auto b = vertex_point(sk, s.position, s.point);
          return a && b && sk.create_line(*a, *b).has_value();
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
    const Position a = vertices_.front().position, b = s.position;
    const double w = std::abs(b.x - a.x), h = std::abs(b.y - a.y);
    if (w > 0 && h > 0 &&
        document_.execute("Rectangle", [&](Sketch& sk) {
          return sk
              .create_rectangle({std::min(a.x, b.x), std::min(a.y, b.y)}, w, h)
              .has_value();
        }))
      vertices_.clear();
  } else if (tool_ == Tool::kCircle) {
    const Vertex center = vertices_.front();
    const double r = distance(center.position, s.position);
    if (r > 0 && document_.execute("Circle", [&](Sketch& sk) {
          auto c = vertex_point(sk, center.position, center.point);
          return c && sk.create_circle(*c, r).has_value();
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
            auto id = vertex_point(sk, c, center.point);
            return id &&
                   sk.create_arc(*id, distance(c, a), start, sweep).has_value();
          }))
        vertices_.clear();
    }
  }
  update_preview(in_progress() ? std::optional(s.position) : std::nullopt);
}

void ToolSession::drag(Position p) {
  if (tool_ != Tool::kSelect || !dragged_point_) return;
  if (!dragging_) dragging_ = document_.begin_gesture("Move point");
  if (!dragging_) return;
  const SnapResult s = snapped(p, dragged_point_);
  last_snap_ = s;
  document_.gesture_step(
      [&](Sketch& sk) { return sk.update_point(*dragged_point_, s.position); });
}

void ToolSession::release(Position) {
  if (dragging_) document_.end_gesture();
  dragging_ = false;
  dragged_point_.reset();
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
    return true;
  }
  if (vertices_.empty()) return false;
  vertices_.clear();
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
  vertices_.push_back({s.position, s.point});
}

bool ToolSession::commit_polyline(bool closed) {
  const std::vector<Vertex> vertices = vertices_;
  vertices_.clear();
  update_preview(std::nullopt);
  return document_.execute("Polyline", [&](Sketch& sk) {
    std::vector<EntityId> ids;
    for (const Vertex& v : vertices) {
      auto id = vertex_point(sk, v.position, v.point);
      if (!id) return false;
      ids.push_back(*id);
    }
    if (closed) ids.push_back(ids.front());
    for (std::size_t i = 1; i < ids.size(); ++i)
      if (!sk.create_line(ids[i - 1], ids[i])) return false;
    return true;
  });
}

void ToolSession::update_preview(std::optional<Position> cursor) {
  preview_ = Sketch();
  if (vertices_.empty()) return;
  std::vector<Position> chain;
  for (const Vertex& v : vertices_) chain.push_back(v.position);
  if (cursor) chain.push_back(*cursor);
  std::vector<EntityId> points;
  for (Position p : chain) points.push_back(*preview_.create_point(p));
  const Position first = chain.front(), last = chain.back();
  switch (tool_) {
    case Tool::kLine:
    case Tool::kPolyline:
      for (std::size_t i = 1; i < points.size(); ++i)
        if (chain[i - 1] != chain[i])
          preview_.create_line(points[i - 1], points[i]);
      break;
    case Tool::kRectangle:
      if (cursor) {
        preview_ = Sketch();
        preview_.create_rectangle({std::min(first.x, last.x),
                                   std::min(first.y, last.y)},
                                  std::abs(last.x - first.x),
                                  std::abs(last.y - first.y));
      }
      break;
    case Tool::kCircle:
      if (cursor) preview_.create_circle(points.front(), distance(first, last));
      break;
    case Tool::kArc:
      if (chain.size() == 2) {
        preview_.create_line(points[0], points[1]);
      } else if (chain.size() >= 3) {
        const Position a = chain[1];
        const double start = std::atan2(a.y - first.y, a.x - first.x);
        preview_.create_arc(
            points.front(), distance(first, a), start,
            ccw_sweep(start, std::atan2(last.y - first.y, last.x - first.x)));
      }
      break;
    case Tool::kSelect:
      break;
  }
}

void ToolSession::drop_stale_selection() {
  std::erase_if(selected_,
                [&](EntityId id) { return !document_.sketch().entity(id); });
}
}  // namespace sketchcad
