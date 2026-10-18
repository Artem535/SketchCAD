#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "sketchcad/sketch.h"
namespace sketchcad {
struct ProjectData {
  Sketch sketch;
  std::uint64_t revision = 0;
};
enum class ProjectError { kInvalidProject, kUnsupportedVersion, kTooLarge };
using ProjectDecodeResult = std::variant<ProjectData, ProjectError>;
inline constexpr std::size_t kProjectMaxBytes = 64 * 1024 * 1024;
inline constexpr std::size_t kProjectMaxRecords = 200000;
ProjectDecodeResult decode_project(std::string_view bytes);
std::optional<std::string> encode_project(const ProjectData&);
}  // namespace sketchcad
