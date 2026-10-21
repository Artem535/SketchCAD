#pragma once
#include <cstdint>
#include <optional>

namespace sketchcad {
// When the next recovery checkpoint is due (autosave-recovery.adoc): 5 s
// after the last committed change, at most 30 s after the first unwritten
// one, and not sooner than 30 s after a storage failure. Times are in
// milliseconds of any monotonic clock.
class AutosaveSchedule {
 public:
  static constexpr std::int64_t kDebounceMs = 5000;
  static constexpr std::int64_t kMaxDelayMs = 30000;
  static constexpr std::int64_t kRetryMs = 30000;

  // A new committed state awaits a checkpoint.
  void changed(std::int64_t now_ms);
  // Nothing to write: clean, retired or already written. Ends a failure
  // episode.
  void clear();
  // The pending state was written.
  void written();
  // Returns true when this failure starts a new failure episode.
  bool failed(std::int64_t now_ms);
  bool failing() const { return failed_at_.has_value(); }
  std::optional<std::int64_t> deadline() const;
  bool due(std::int64_t now_ms) const;

 private:
  std::optional<std::int64_t> first_;
  std::int64_t last_ = 0;
  std::optional<std::int64_t> failed_at_;
};
}  // namespace sketchcad
