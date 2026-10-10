// A.R.M.O.R. - what every node's settings file needs and none of them decides: a problem found in a document, and the small readers that fill a field from JSON and say what was wrong.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// Pure: no hardware. The settings of each node (core/<kind>_config.hpp) and the base board's own (core/io_config.hpp) are written with these.
#pragma once
#include <array>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "board_s3.hpp"
#include "json.hpp"

namespace armor::config {

constexpr std::size_t kMaxPasswordText = 64;

struct Problem {
  std::string path;  // "ports.2.baud"
  std::string code;  // "required", "too_long", "range", "invalid", "conflict", "reserved" ...
};
using Problems = std::vector<Problem>;


// A device name: it becomes a piece of an MQTT topic (the same rule as the contract's device name).
inline bool valid_device_name(std::string_view name) {
  if (name.empty() || name.size() > 32 || name.front() == '-' || name.front() == '_') return false;
  for (const char c : name) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  return true;
}


namespace detail {
template <typename E>
bool read_choice(const json::Value& parent, const char* name, std::initializer_list<std::pair<const char*, E>> options, E& target) {
  const json::Value* member = parent.get(name);
  if (member == nullptr) return true;
  if (!member->is_string()) return false;
  for (const auto& option : options) if (member->text == option.first) { target = option.second; return true; }
  return false;
}

inline void bad(Problems& problems, std::string path, const char* code) { problems.push_back({std::move(path), code}); }

inline void read_text(const json::Value& parent, const char* name, std::string& target, std::size_t longest, const std::string& path, Problems& problems) {
  const json::Value* member = parent.get(name);
  if (member == nullptr) return;
  if (!member->is_string()) { bad(problems, path, "invalid"); return; }
  if (member->text.size() > longest) { bad(problems, path, "too_long"); return; }
  target = member->text;
}

// A secret: absent or empty keeps the stored one; "<name>_clear": true erases it.
inline void read_secret(const json::Value& parent, const char* name, std::string& target, const std::string& path, Problems& problems) {
  if (parent.bool_or(std::string(name) + "_clear", false)) { target.clear(); return; }
  const json::Value* member = parent.get(name);
  if (member == nullptr) return;
  if (!member->is_string()) { bad(problems, path, "invalid"); return; }
  if (member->text.empty()) return;
  if (member->text.size() > kMaxPasswordText) { bad(problems, path, "too_long"); return; }
  target = member->text;
}

inline void read_bool(const json::Value& parent, const char* name, bool& target, const std::string& path, Problems& problems) {
  const json::Value* member = parent.get(name);
  if (member == nullptr) return;
  if (!member->is_bool()) { bad(problems, path, "invalid"); return; }
  target = member->boolean;
}

inline void read_double(const json::Value& parent, const char* name, double& target, double lowest, double highest, const std::string& path, Problems& problems) {
  const json::Value* member = parent.get(name);
  if (member == nullptr) return;
  if (!member->is_number()) { bad(problems, path, "invalid"); return; }
  if (!(member->number >= lowest && member->number <= highest)) { bad(problems, path, "range"); return; }   // also false for NaN
  target = member->number;
}

inline void read_int(const json::Value& parent, const char* name, int& target, long long lowest, long long highest, const std::string& path, Problems& problems) {
  const json::Value* member = parent.get(name);
  if (member == nullptr) return;
  if (!member->is_number()) { bad(problems, path, "invalid"); return; }
  const long long value = parent.integer_or(name, lowest - 1, lowest, highest);
  if (value < lowest || value > highest) { bad(problems, path, "range"); return; }
  target = static_cast<int>(value);
}
}  // namespace detail


namespace detail {
// The pins already claimed, to catch two uses of one GPIO.
struct PinClaims {
  std::array<std::string, board::kLastGpio + 1> owner;
  bool claim(int gpio, const std::string& who) {
    if (gpio < 0 || gpio > board::kLastGpio) return true;
    if (!owner[static_cast<std::size_t>(gpio)].empty()) return false;
    owner[static_cast<std::size_t>(gpio)] = who;
    return true;
  }
};
}  // namespace detail


}  // namespace armor::config
