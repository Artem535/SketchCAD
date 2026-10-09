#pragma once

#include <map>
#include <optional>
#include <vector>

#include "sketchcad/document.h"
#include "sketchcad/sketch_view.h"

namespace sketchcad {
enum class Tool { kSelect, kLine, kPolyline, kRectangle, kCircle, kArc };

enum class DeleteResult { kDeleted, kNothingSelected, kPointInUse };

// Value the next typed number sets while drawing (default-dimensions.adoc).
enum class InputField { kNone, kLength, kWidth, kHeight, kRadius };

// Drawing and selection state machine over a Document, in world mm.
// Every finished shape is one Document command; unfinished shapes exist
// only in preview().
class ToolSession {
 public:
  explicit ToolSession(Document& document);

  Tool tool() const { return tool_; }
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

  bool finish();
  bool cancel();

  // Driving dimensions added to every finished shape (on by default).
  void set_auto_dimensions(bool enabled) { auto_dimensions_ = enabled; }
  bool auto_dimensions() const { return auto_dimensions_; }

  // Dynamic input: the field the next value sets, its current preview value,
  // and entering it (> 0 and finite).
  InputField input_field() const;
  double input_value() const;
  bool enter_value(double value);
  bool in_progress() const { return !vertices_.empty(); }

  // Most recently selected entity.
  std::optional<EntityId> selection() const {
    if (selected_.empty()) return std::nullopt;
    return selected_.back();
  }
  // Up to two selected entities in tap order.
  const std::vector<EntityId>& selected() const { return selected_; }
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

 private:
  struct Vertex {
    Position position;
    std::optional<EntityId> point;
    // Curves the vertex was snapped onto: a new point gets kOnCurve on each.
    std::vector<EntityId> curves = {};
  };

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
  Sketch preview_;
};
}  // namespace sketchcad
