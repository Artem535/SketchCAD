#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "sketchcad/sketch.h"

namespace sketchcad {
// Mutates a working copy; returning false rejects the whole command.
using Edit = std::function<bool(Sketch&)>;

// Single boundary for sketch changes: atomic commands, linear Undo/Redo
// history, grouped gestures and dirty state relative to the saved revision.
class Document {
 public:
  explicit Document(Sketch initial = {});

  const Sketch& sketch() const { return live_; }

  bool execute(std::string label, const Edit& edit);
  // Runs after every edit and gesture step; the solver hook for S01.
  void set_commit_step(Edit step);

  bool can_undo() const;
  bool can_redo() const;
  bool undo();
  bool redo();
  std::optional<std::string> undo_label() const;
  std::optional<std::string> redo_label() const;

  bool begin_gesture(std::string label);
  bool gesture_step(const Edit& edit);
  bool end_gesture();
  bool cancel_gesture();
  bool gesture_active() const { return gesture_label_.has_value(); }

  std::uint64_t revision() const;
  bool mark_saved();
  bool dirty() const;

 private:
  struct State {
    std::string label;  // Label of the command that produced this state.
    Sketch sketch;
    std::uint64_t revision;
  };

  std::optional<Sketch> apply(const Edit& edit) const;
  void record(std::string label);
  void restore(std::size_t position);

  std::vector<State> states_;
  std::size_t position_ = 0;
  Sketch live_;
  Edit commit_step_;
  std::optional<std::string> gesture_label_;
  std::uint64_t next_revision_ = 1;
  std::uint64_t saved_revision_ = 0;
};
}  // namespace sketchcad
