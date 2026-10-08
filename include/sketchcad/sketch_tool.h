#pragma once

#include <optional>
#include <vector>

#include "sketchcad/document.h"
#include "sketchcad/sketch_view.h"

namespace sketchcad {
enum class Tool { kSelect, kLine, kPolyline, kRectangle, kCircle, kArc };

enum class DeleteResult { kDeleted, kNothingSelected, kPointInUse };

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
  bool in_progress() const { return !vertices_.empty(); }

  std::optional<EntityId> selection() const { return selection_; }
  void clear_selection() { selection_.reset(); }
  DeleteResult delete_selection();

  bool undo();
  bool redo();

  // Unfinished shape plus the rubber band to the last hover position.
  const Sketch& preview() const { return preview_; }
  const std::optional<SnapResult>& last_snap() const { return last_snap_; }

 private:
  struct Vertex {
    Position position;
    std::optional<EntityId> point;
  };

  SnapResult snapped(Position, std::optional<EntityId> exclude = {}) const;
  void add_vertex(const SnapResult&);
  bool commit_polyline(bool closed);
  void update_preview(std::optional<Position> cursor);
  void drop_stale_selection();

  Document& document_;
  Tool tool_ = Tool::kSelect;
  SnapSettings snap_settings_;
  double pick_tolerance_mm_ = 4;
  std::vector<Vertex> vertices_;
  std::optional<EntityId> selection_;
  std::optional<EntityId> dragged_point_;
  bool dragging_ = false;
  std::optional<SnapResult> last_snap_;
  Sketch preview_;
};
}  // namespace sketchcad
