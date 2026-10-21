#pragma once
// Strict JSON checks shared by the project and recovery codecs: canonical
// decimal identifiers, duplicate keys, nesting depth and exact object shape.
#include <yyjson.h>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace sketchcad::json_strict {
inline std::optional<std::uint64_t> decimal(std::string_view text) {
  if (text.empty() || (text.size() > 1 && text[0] == '0') || text[0] < '0' ||
      text[0] > '9')
    return std::nullopt;
  std::uint64_t number = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), number);
  if (error != std::errc() || end != text.data() + text.size())
    return std::nullopt;
  return number;
}
inline std::string_view text(yyjson_val* v) {
  return yyjson_is_str(v)
             ? std::string_view(yyjson_get_str(v), yyjson_get_len(v))
             : std::string_view();
}
inline bool bounded_tree(yyjson_val* v, unsigned depth = 1) {
  if (depth > 16) return false;
  std::size_t i, count;
  yyjson_val* key;
  yyjson_val* value;
  if (yyjson_is_obj(v)) {
    std::unordered_set<std::string_view> keys;
    yyjson_obj_foreach(v, i, count, key, value) {
      if (!keys.insert(text(key)).second || !bounded_tree(value, depth + 1))
        return false;
    }
  } else if (yyjson_is_arr(v)) {
    yyjson_arr_foreach(v, i, count, value) {
      if (!bounded_tree(value, depth + 1)) return false;
    }
  }
  return true;
}
enum class Type {
  kString,
  kNumber,
  kBoolean,
  kInteger,
  kObject,
  kArray,
  kNullableObject
};
using Field = std::pair<std::string_view, Type>;
inline bool shape(yyjson_val* v, std::initializer_list<Field> fields) {
  if (!yyjson_is_obj(v) || yyjson_obj_size(v) != fields.size()) return false;
  for (const auto& [name, type] : fields) {
    auto* value = yyjson_obj_getn(v, name.data(), name.size());
    if (!value) return false;
    switch (type) {
      case Type::kString:
        if (!yyjson_is_str(value)) return false;
        break;
      case Type::kNumber:
        if (!yyjson_is_num(value) || !std::isfinite(yyjson_get_num(value)))
          return false;
        break;
      case Type::kBoolean:
        if (!yyjson_is_bool(value)) return false;
        break;
      case Type::kInteger:
        if (!yyjson_is_int(value)) return false;
        break;
      case Type::kObject:
        if (!yyjson_is_obj(value)) return false;
        break;
      case Type::kArray:
        if (!yyjson_is_arr(value)) return false;
        break;
      case Type::kNullableObject:
        if (!yyjson_is_null(value) && !yyjson_is_obj(value)) return false;
        break;
    }
  }
  return true;
}
}  // namespace sketchcad::json_strict
