#include "sketchcad/recovery_io.h"

#include <yyjson.h>

#include <memory>
#include <rfl.hpp>
#include <rfl/json.hpp>

#include "json_strict.h"

namespace sketchcad {
namespace {
using namespace json_strict;
struct WireRecovery {
  std::string format;
  unsigned version;
  std::string session_id;
  std::string generation;
  std::string captured_at;
  std::string original_url;
  std::string project_json;
};
bool hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }
bool digit(char c) { return c >= '0' && c <= '9'; }
int number(std::string_view s, std::size_t at, std::size_t length) {
  int result = 0;
  for (std::size_t i = at; i < at + length; ++i) result = result * 10 + (s[i] - '0');
  return result;
}
bool valid_meta(const RecoveryMeta& meta) {
  return valid_session_id(meta.session_id) &&
         meta.generation != UINT64_MAX && valid_captured_at(meta.captured_at);
}
}  // namespace

bool valid_session_id(std::string_view id) {
  if (id.size() != 36) return false;
  for (std::size_t i = 0; i < id.size(); ++i) {
    const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
    if (dash ? id[i] != '-' : !hex(id[i])) return false;
  }
  return true;
}

bool valid_captured_at(std::string_view t) {
  // YYYY-MM-DDTHH:MM:SS.mmmZ
  constexpr std::string_view pattern = "dddd-dd-ddTdd:dd:dd.dddZ";
  if (t.size() != pattern.size()) return false;
  for (std::size_t i = 0; i < t.size(); ++i)
    if (pattern[i] == 'd' ? !digit(t[i]) : t[i] != pattern[i]) return false;
  const int year = number(t, 0, 4), month = number(t, 5, 2),
            day = number(t, 8, 2);
  if (month < 1 || month > 12 || day < 1) return false;
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (day > days[month - 1] + (month == 2 && leap ? 1 : 0)) return false;
  return number(t, 11, 2) < 24 && number(t, 14, 2) < 60 &&
         number(t, 17, 2) < 60;
}

RecoveryDecodeResult decode_recovery(std::string_view bytes) {
  if (bytes.size() > kRecoveryMaxBytes) return RecoveryError::kTooLarge;
  const auto invalid = RecoveryError::kInvalid;
  try {
    std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(
        yyjson_read(bytes.data(), bytes.size(), 0), yyjson_doc_free);
    if (!doc) return invalid;
    auto* root = yyjson_doc_get_root(doc.get());
    if (!bounded_tree(root) || !yyjson_is_obj(root) ||
        text(yyjson_obj_get(root, "format")) != "SketchCADRecovery")
      return invalid;
    auto* version = yyjson_obj_get(root, "version");
    if (!yyjson_is_uint(version)) return invalid;
    if (yyjson_get_uint(version) != 1) return RecoveryError::kUnsupportedVersion;
    using T = Type;
    if (!shape(root, {{"format", T::kString},
                      {"version", T::kInteger},
                      {"session_id", T::kString},
                      {"generation", T::kString},
                      {"captured_at", T::kString},
                      {"original_url", T::kString},
                      {"project_json", T::kString}}))
      return invalid;
    auto parsed = rfl::json::read<WireRecovery, rfl::NoExtraFields>(
        rfl::json::InputVarType(root));
    if (!parsed) return invalid;
    auto& wire = parsed.value();
    const auto generation = decimal(wire.generation);
    RecoveryMeta meta{wire.session_id, generation.value_or(UINT64_MAX),
                      wire.captured_at, wire.original_url};
    if (!generation || !valid_meta(meta)) return invalid;
    auto project = decode_project(wire.project_json);
    if (auto* error = std::get_if<ProjectError>(&project))
      return *error == ProjectError::kTooLarge ? RecoveryError::kTooLarge
                                               : RecoveryError::kInvalidProject;
    return RecoveryData{std::move(meta),
                        std::get<ProjectData>(std::move(project))};
  } catch (const std::exception&) {
    return invalid;
  }
}

std::optional<std::string> encode_recovery(const RecoveryMeta& meta,
                                           const ProjectData& project) {
  if (!valid_meta(meta)) return std::nullopt;
  auto payload = encode_project(project);
  if (!payload) return std::nullopt;
  try {
    const WireRecovery wire{"SketchCADRecovery",
                            1,
                            meta.session_id,
                            std::to_string(meta.generation),
                            meta.captured_at,
                            meta.original_url,
                            std::move(*payload)};
    auto bytes = rfl::json::write(wire);
    if (bytes.size() > kRecoveryMaxBytes) return std::nullopt;
    return bytes;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}
}  // namespace sketchcad
