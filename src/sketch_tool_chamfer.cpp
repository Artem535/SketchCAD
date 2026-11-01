#include <algorithm>
#include <cmath>

#include "sketch_corner.h"
#include "sketchcad/sketch_tool.h"
#include "sketchcad/solver.h"

namespace sketchcad {
namespace {
std::string chamfer_message(ChamferStatus status) {
  switch (status) {
    case ChamferStatus::kReady:
      return {};
    case ChamferStatus::kNotCorner:
      return "chamfer_not_corner";
    case ChamferStatus::kInvalidDistance:
      return "invalid_value";
    case ChamferStatus::kTooLong:
      return "chamfer_too_long";
    case ChamferStatus::kCollinear:
      return "chamfer_collinear";
    case ChamferStatus::kEqualLength:
      return "chamfer_equal_length";
  }
  return "chamfer_not_corner";
}
}  // namespace

std::optional<EntityId> ToolSession::chamfer_corner() const {
  if (chamfer_lines_.size() != 2) return std::nullopt;
  const auto first = document_.sketch().entity(chamfer_lines_[0]);
  const auto second = document_.sketch().entity(chamfer_lines_[1]);
  if (!first || !second) return std::nullopt;
  const auto* a = std::get_if<SketchLine>(&*first);
  const auto* b = std::get_if<SketchLine>(&*second);
  if (!a || !b) return std::nullopt;
  if (a->start == b->start || a->start == b->end) return a->start;
  if (a->end == b->start || a->end == b->end) return a->end;
  return std::nullopt;
}

void ToolSession::update_chamfer_preview() {
  edit_preview_.reset();
  preview_ = Sketch();
  const auto corner = chamfer_corner();
  if (tool_ == Tool::kFillet) {
    const auto status =
        corner ? fillet_status(document_.sketch(), *corner, fillet_radius_)
               : FilletStatus::kNotCorner;
    switch (status) {
      case FilletStatus::kReady:
        message_.clear();
        break;
      case FilletStatus::kNotCorner:
        message_ = "fillet_not_corner";
        break;
      case FilletStatus::kInvalidRadius:
        message_ = "invalid_value";
        break;
      case FilletStatus::kTooLong:
        message_ = "fillet_too_long";
        break;
      case FilletStatus::kCollinear:
        message_ = "fillet_collinear";
        break;
      case FilletStatus::kEqualLength:
        message_ = "fillet_equal_length";
        break;
    }
    if (status != FilletStatus::kReady) return;
    edit_preview_ = fillet_preview(document_.sketch(), *corner, fillet_radius_);
    if (!edit_preview_) return;
    const auto center = *preview_.create_point(edit_preview_->center);
    const auto arc = preview_.create_arc(
        center, fillet_radius_, edit_preview_->start, edit_preview_->sweep);
    if (arc)
      preview_.add_dimension(ConstraintKind::kRadius, *arc, 0, fillet_radius_);
  } else {
    const auto status =
        corner ? chamfer_status(document_.sketch(), *corner, chamfer_distance_)
               : ChamferStatus::kNotCorner;
    message_ = chamfer_message(status);
    if (status != ChamferStatus::kReady) return;
    edit_preview_ =
        chamfer_preview(document_.sketch(), *corner, chamfer_distance_);
    if (!edit_preview_) return;
    const auto p = *preview_.create_point(*chamfer_corner_position());
    for (const Position end : {edit_preview_->a, edit_preview_->b}) {
      const auto q = *preview_.create_point(end);
      preview_.add_dimension(ConstraintKind::kDistance, p, q,
                             chamfer_distance_);
    }
  }
}

std::optional<Position> ToolSession::chamfer_corner_position() const {
  if (!is_corner_tool()) return std::nullopt;
  const auto corner = chamfer_corner();
  if (!corner) return std::nullopt;
  return std::get<SketchPoint>(*document_.sketch().entity(*corner)).position;
}

bool ToolSession::resize_chamfer(Position delta) {
  if (!std::isfinite(delta.x) || !std::isfinite(delta.y) || !edit_preview_)
    return false;
  const auto origin = chamfer_corner_position();
  if (!origin) return false;
  const auto piece = *edit_preview_;
  double& size = tool_ == Tool::kFillet ? fillet_radius_ : chamfer_distance_;
  Position anchor{(piece.a.x + piece.b.x) / 2, (piece.a.y + piece.b.y) / 2};
  if (piece.is_arc) {
    const double angle = piece.start + piece.sweep / 2;
    anchor = {piece.center.x + piece.radius * std::cos(angle),
              piece.center.y + piece.radius * std::sin(angle)};
  }
  const Position v{(anchor.x - origin->x) / size,
                   (anchor.y - origin->y) / size};
  const double v2 = v.x * v.x + v.y * v.y;
  if (!(v2 > 0)) return false;
  const double target = size + (delta.x * v.x + delta.y * v.y) / v2;
  if (!std::isfinite(target)) return false;
  double upper = INFINITY;
  for (const auto id : chamfer_lines_) {
    const auto line = std::get<SketchLine>(*document_.sketch().entity(id));
    const auto a =
        std::get<SketchPoint>(*document_.sketch().entity(line.start));
    const auto b = std::get<SketchPoint>(*document_.sketch().entity(line.end));
    upper = std::min(upper, std::hypot(a.position.x - b.position.x,
                                       a.position.y - b.position.y));
  }
  // Keep both shortened lines and the chamfer itself nondegenerate.
  const double spread =
      std::hypot(piece.a.x - piece.b.x, piece.a.y - piece.b.y) / size;
  const double setback_factor =
      std::hypot(piece.a.x - origin->x, piece.a.y - origin->y) / size;
  const double lower =
      2 * detail::kCornerLengthMargin *
      std::max({1., 1 / spread, 1 / setback_factor});
  upper = (upper - 2 * detail::kCornerLengthMargin) / setback_factor;
  if (!(upper > lower)) return false;
  const double next = std::clamp(target, lower, upper);
  if (next == size) return false;
  size = next;
  update_chamfer_preview();
  return true;
}

void ToolSession::press_chamfer(Position p) {
  last_snap_.reset();
  if (chamfer_lines_.size() == 2) {
    update_chamfer_preview();
    return;
  }
  const auto hit = pick_curve(document_.sketch(), p, pick_tolerance_mm_);
  if (!hit ||
      !std::holds_alternative<SketchLine>(*document_.sketch().entity(*hit)) ||
      (!chamfer_lines_.empty() && chamfer_lines_.front() == *hit)) {
    message_ =
        tool_ == Tool::kFillet ? "fillet_pick_line" : "chamfer_pick_line";
    return;
  }
  chamfer_lines_.push_back(*hit);
  if (chamfer_lines_.size() != 2) return;
  update_chamfer_preview();
  // A long retained distance can be repaired in the distance step. Other
  // rejections need a different second line; retain the first pick.
  if (!message_.empty() && !message_.ends_with("_too_long"))
    chamfer_lines_.pop_back();
}

bool ToolSession::can_finish() const {
  if (is_corner_tool())
    return chamfer_lines_.size() == 2 && edit_preview_.has_value();
  return tool_ == Tool::kPolyline && vertices_.size() >= 2;
}

bool ToolSession::finish_chamfer() {
  if (chamfer_lines_.size() != 2) return false;
  update_chamfer_preview();
  const auto corner = chamfer_corner();
  if (!can_finish() || !corner) return false;
  const bool rounding = tool_ == Tool::kFillet;
  if (!document_.execute(rounding ? "Fillet" : "Chamfer", [&](Sketch& sk) {
        return rounding ? fillet(sk, *corner, fillet_radius_)
                        : chamfer(sk, *corner, chamfer_distance_);
      })) {
    message_ = rounding ? "fillet_failed" : "chamfer_failed";
    return false;
  }
  cancel();
  return true;
}
}  // namespace sketchcad
