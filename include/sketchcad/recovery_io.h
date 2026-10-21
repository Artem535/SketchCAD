#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "sketchcad/project_io.h"
namespace sketchcad {
// Metadata of an automatic recovery checkpoint (autosave-recovery.adoc).
struct RecoveryMeta {
  std::string session_id;  // Lowercase canonical UUID.
  std::uint64_t generation = 0;
  std::string captured_at;  // UTC, YYYY-MM-DDTHH:MM:SS.mmmZ.
  std::string original_url;  // Informational only; never opened or written.
};
struct RecoveryData {
  RecoveryMeta meta;
  ProjectData project;
};
enum class RecoveryError {
  kInvalid,
  kUnsupportedVersion,
  kTooLarge,
  kInvalidProject
};
using RecoveryDecodeResult = std::variant<RecoveryData, RecoveryError>;
inline constexpr std::size_t kRecoveryMaxBytes = 128 * 1024 * 1024;
bool valid_session_id(std::string_view);
bool valid_captured_at(std::string_view);
RecoveryDecodeResult decode_recovery(std::string_view bytes);
// Returns nullopt for invalid metadata or a project F01 cannot encode.
std::optional<std::string> encode_recovery(const RecoveryMeta&,
                                           const ProjectData&);
}  // namespace sketchcad
