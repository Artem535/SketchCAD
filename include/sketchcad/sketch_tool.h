#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "sketchcad/document.h"
#include "sketchcad/sketch_edit.h"
#include "sketchcad/sketch_view.h"

namespace sketchcad {
enum class Tool {
  kSelect,
  kLine,
  kPolyline,
  kRectangle,
  kCircle,
  kArc,
  // Reference dimensions (U10).
  kDimension,
  // U04 part 2.
  kTrim,
  kExtend,
  kChamfer,
  kFillet,
};

enum class DeleteResult { kDeleted, kNothingSelected, kPointInUse };

// Value the next typed number sets while drawing (default-dimensions.adoc).
enum class InputField { kNone, kLength, kWidth, kHeight, kRadius, kDistance };

// Drawing and selection state machine over a Document, in world mm.
// Every finished shape is one Document command; unfinished shapes exist
// only in preview().
class ToolSession {
 public:
  explicit ToolSession(Document& document);

  Tool tool() const { return tool_; }
  bool is_corner_tool() const {
    return tool_ == Tool::kChamfer || tool_ == Tool::kFillet;
  }
  // Switching the tool cancels the shape in progress.
  void set_tool(Tool);

  void set_snap(const SnapSettings& settings) { snap_settings_ = settings; }
  const SnapSettings& snap_settings() const { return snap_settings_; }
  void set_pick_tolerance(double mm) { pick_tolerance_mm_ = mm; }

  // Pointer input. `press` is a tap for drawing tools; `drag` and `release`
  // move a selected point with the select tool.
  void hover(Position);
  void press(Position);
  void drag(Position);
  void release(Position);

  // Clears interaction state before a file operation, retaining user settings.
  void reset();
  bool finish();
  bool can_finish() const;
  bool cancel();

  // Driving dimensions added to every finished shape (on by default).
  void set_auto_dimensions(bool enabled) { auto_dimensions_ = enabled; }
  bool auto_dimensions() const { return auto_dimensions_; }

  // Dynamic input: the field the next value sets, its current preview value,
  // and entering it (> 0 and finite).
  InputField input_field() const;
  double input_value() const;
  bool enter_value(double value);
  // Preview-only equal setback adjustment by a world-coordinate drag delta.
  bool resize_chamfer(Position delta);
  std::optional<Position> chamfer_corner_position() const;
  const std::string& message() const { return message_; }
  bool in_progress() const {
    return !vertices_.empty() || !dimension_picks_.empty() ||
           !chamfer_lines_.empty();
  }

  // Most recently selected entity.
  std::optional<EntityId> selection() const {
    if (selected_.empty()) return std::nullopt;
    return selected_.back();
  }
  // Up to two selected entities in tap order.
  const std::vector<EntityId>& selected() const {
    return is_corner_tool() ? chamfer_lines_ : selected_;
  }
  void clear_selection() { selected_.clear(); }
  DeleteResult delete_selection();
  // Toggles the construction flag of the selected curves in one command:
  // on if any of them was off (sketch-editing.adoc). False without curves.
  bool toggle_construction();

  bool undo();
  bool redo();

  // Unfinished shape plus the rubber band to the last hover position.
  const Sketch& preview() const { return preview_; }
  const std::optional<SnapResult>& last_snap() const { return last_snap_; }
  // Trim/extend: the piece the next tap would remove or add.
  const std::optional<CurvePiece>& edit_preview() const { return edit_preview_; }

 private:
  struct Vertex {
    Position position;
    std::optional<EntityId> point;
    // Curves the vertex was snapped onto: a new point gets kOnCurve on each.
    std::vector<EntityId> curves = {};
  };

  // Dimension tool (U10): the reference to add once the picks are complete.
  struct DimensionPick {
    ConstraintKind kind;
    EntityId first;
    EntityId second;
  };
  std::optional<DimensionPick> dimension_pick() const;
  void press_dimension(Position);
  void preview_reference(Position cursor);

  void press_chamfer(Position);
  std::optional<EntityId> chamfer_corner() const;
  void update_chamfer_preview();
  bool finish_chamfer();

  SnapResult snapped(Position, std::optional<EntityId> exclude = {}) const;
  void place(const SnapResult&);
  void preview_dimension(ConstraintKind kind, EntityId id);
  void add_vertex(const SnapResult&);
  bool commit_polyline(bool closed);
  void update_preview(std::optional<Position> cursor);
  void drop_stale_selection();

  Document& document_;
  Tool tool_ = Tool::kSelect;
  SnapSettings snap_settings_;
  double pick_tolerance_mm_ = 4;
  std::vector<Vertex> vertices_;
  std::vector<EntityId> selected_;
  std::optional<EntityId> dragged_point_;
  // Curve drag (U03b): press position and the defining points' starts.
  std::optional<Position> curve_press_;
  std::map<EntityId, Position> curve_start_;
  // Points on the moved curve (kOnCurve) that ride along, with the dragged
  // point's or curve's start for their displacement.
  std::map<EntityId, Position> riders_;
  Position point_start_{0, 0};
  bool dragging_ = false;
  bool auto_dimensions_ = true;
  std::optional<Position> hover_;
  std::optional<double> locked_width_;  // Rectangle, signed.
  std::optional<SnapResult> last_snap_;
  std::vector<EntityId> dimension_picks_;
  std::optional<CurvePiece> edit_preview_;
  std::vector<EntityId> chamfer_lines_;
  double chamfer_distance_ = 2;
  double fillet_radius_ = 2;
  std::string message_;
  Sketch preview_;
};
}  // namespace sketchcad
