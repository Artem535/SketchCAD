#include "sketchcad/autosave_schedule.h"

#include <algorithm>

namespace sketchcad {
void AutosaveSchedule::changed(std::int64_t now_ms) {
  if (!first_) first_ = now_ms;
  last_ = now_ms;
}

void AutosaveSchedule::clear() {
  first_.reset();
  failed_at_.reset();
}

void AutosaveSchedule::written() { clear(); }

bool AutosaveSchedule::failed(std::int64_t now_ms) {
  const bool episode = !failed_at_;
  failed_at_ = now_ms;
  return episode;
}

std::optional<std::int64_t> AutosaveSchedule::deadline() const {
  if (!first_) return std::nullopt;
  std::int64_t at = std::min(last_ + kDebounceMs, *first_ + kMaxDelayMs);
  if (failed_at_) at = std::max(at, *failed_at_ + kRetryMs);
  return at;
}

bool AutosaveSchedule::due(std::int64_t now_ms) const {
  const auto at = deadline();
  return at && now_ms >= *at;
}
}  // namespace sketchcad
