#include "sketchcad/document.h"

#include <utility>

namespace sketchcad {
Document::Document(Sketch initial) : live_(std::move(initial)) {
  states_.push_back({"", live_, 0});
}

// Edits never touch the live state directly, so a rejected or throwing edit
// leaves the document unchanged.
std::optional<Sketch> Document::apply(const Edit& edit) const {
  Sketch working = live_;
  if (!edit(working)) return std::nullopt;
  if (commit_step_ && !commit_step_(working)) return std::nullopt;
  return working;
}

bool Document::execute(std::string label, const Edit& edit) {
  if (gesture_active()) return false;
  std::optional<Sketch> result = apply(edit);
  if (!result) return false;
  const bool changed = result->entities() != live_.entities();
  // Assignment keeps the higher watermark, so IDs consumed by a no-op edit
  // are not issued again.
  live_ = std::move(*result);
  if (changed) record(std::move(label));
  return true;
}

void Document::set_commit_step(Edit step) { commit_step_ = std::move(step); }

bool Document::can_undo() const { return !gesture_active() && position_ > 0; }

bool Document::can_redo() const {
  return !gesture_active() && position_ + 1 < states_.size();
}

bool Document::undo() {
  if (!can_undo()) return false;
  restore(position_ - 1);
  return true;
}

bool Document::redo() {
  if (!can_redo()) return false;
  restore(position_ + 1);
  return true;
}

std::optional<std::string> Document::undo_label() const {
  if (position_ == 0) return std::nullopt;
  return states_[position_].label;
}

std::optional<std::string> Document::redo_label() const {
  if (position_ + 1 >= states_.size()) return std::nullopt;
  return states_[position_ + 1].label;
}

bool Document::begin_gesture(std::string label) {
  if (gesture_active()) return false;
  gesture_label_ = std::move(label);
  return true;
}

bool Document::gesture_step(const Edit& edit) {
  if (!gesture_active()) return false;
  std::optional<Sketch> result = apply(edit);
  if (!result) return false;
  live_ = std::move(*result);
  return true;
}

bool Document::end_gesture() {
  if (!gesture_active()) return false;
  std::string label = std::move(*gesture_label_);
  gesture_label_.reset();
  if (live_.entities() != states_[position_].sketch.entities())
    record(std::move(label));
  return true;
}

bool Document::cancel_gesture() {
  if (!gesture_active()) return false;
  gesture_label_.reset();
  live_ = states_[position_].sketch;
  return true;
}

std::uint64_t Document::revision() const { return states_[position_].revision; }

bool Document::mark_saved() {
  if (gesture_active()) return false;
  saved_revision_ = revision();
  return true;
}

bool Document::dirty() const {
  if (revision() != saved_revision_) return true;
  return gesture_active() &&
         live_.entities() != states_[position_].sketch.entities();
}

// Appends the live state as a new step and discards the redo branch.
void Document::record(std::string label) {
  states_.erase(states_.begin() + static_cast<std::ptrdiff_t>(position_) + 1,
                states_.end());
  states_.push_back({std::move(label), live_, next_revision_++});
  position_ = states_.size() - 1;
}

void Document::restore(std::size_t position) {
  position_ = position;
  live_ = states_[position].sketch;
}
}  // namespace sketchcad
