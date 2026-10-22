// ARMOR-ELECTRICAL - the diagnostic package of a node: what an administrator may take from it to find out why it misbehaves, and never a secret. No hardware.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The server asks for it with the `diagnostics` order (the same arm and execute as every order, detail `package` or `log`) and the node answers inside the `diagnostics` of its result: the
// memory as it is now, the task list with the least stack each one has had, why the node last started (and whether it crashed), the versions it runs, the log it keeps (the newest
// few kilobytes), the settings it runs on, and the summary of the core dump when one is waiting in flash.
//
// The settings are the delicate part, so they are built the other way round from "everything but the passwords":
//   1. only the leaves of an EXPLICIT list of names are carried (`allowed_leaf`), as `path -> short text` (a number or a flag is written as text): the mode of the network, whether DHCP is on, the intervals, the
//      language, whether a service is enabled ... never a name or address of the house, never a user name, never a credential;
//   2. a path that contains a word of a second list (`secret_path`: pass, secret, token, key, pin, psk, cred, auth, cert, pem, private, session, cookie, user, salt, hash, code, otp, seed) is
//      dropped even if its leaf is allowed;
//   3. the address of a broker is carried as scheme, host and port only (`safe_uri`): what stands before the host in it (a user name and password) never leaves;
//   4. every value that sits under a secret path in the node's full settings is collected (`collect_secrets`) and the finished package is washed of those values wherever they appear (the
//      log too): a password echoed by a log line is replaced by `***`;
//   5. the log is washed of what looks like a credential (`password=...`, `token: ...`, a user and password in an address, a long run of hexadecimal digits).
// The package is bounded (`kMaxPackageBytes`): the log gives way first.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "boot_report.hpp"
#include "control_gate.hpp"
#include "json.hpp"
#include "memory_budget.hpp"
#include "task_health.hpp"

namespace armor::diag {

constexpr std::size_t kMaxLogBytes = 5000;
constexpr std::size_t kMaxTasks = 40;
constexpr std::size_t kMaxConfigEntries = 64;
constexpr std::size_t kMaxValueChars = 64;
constexpr std::size_t kMaxPackageBytes = 15000;
constexpr std::size_t kMaxSensors = 8;

using Detail = control::Detail;
using control::detail_name;

inline std::string lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return out;
}

inline bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

// The second list: a path that says any of these is never carried and its value is washed from the package.
inline bool secret_path(const std::string& path) {
  static const char* const words[] = {"pass", "secret", "token", "key", "pin", "psk", "cred", "auth", "cert", "pem", "private", "session", "cookie", "user", "salt", "hash", "code", "otp", "seed", "bearer", "login"};
  const std::string low = lower(path);
  for (const char* word : words) if (contains(low, word)) return true;
  return false;
}

// The first list: the leaves (the last name of a path) that may be carried. An unknown leaf is NOT carried.
inline bool allowed_leaf(const std::string& leaf) {
  static const char* const names[] = {
      "v", "enabled", "mode", "dhcp", "hidden", "channel", "bandwidth_mhz", "tx_power_dbm", "country", "language", "zone", "security", "heartbeat_s", "telemetry_ms", "period_s", "poll_s",
      "auto_restart_hours", "ntp_enabled", "tls", "baud", "kind", "type", "model", "profile", "dialect", "max_clients", "link_timeout_s", "write_enabled", "parallel", "cells_per_cycle",
      "debounce_ms", "pulse_ms", "initial_on", "invert", "pull", "merge_mm", "yaw_deg", "pitch_deg", "lux_fallback", "veml7700", "ui", "uri_host"};
  for (const char* name : names) if (leaf == name) return true;
  return false;
}

// "mqtts://user:pass@broker.local:8883/path?x=1" -> "mqtts://broker.local:8883". Anything that is not scheme://host[:port]... gives "".
inline std::string safe_uri(const std::string& uri) {
  const std::size_t scheme_end = uri.find("://");
  if (scheme_end == std::string::npos || scheme_end == 0 || scheme_end > 8) return std::string();
  for (std::size_t i = 0; i < scheme_end; ++i) if (!((uri[i] >= 'a' && uri[i] <= 'z') || (uri[i] >= 'A' && uri[i] <= 'Z'))) return std::string();
  std::size_t start = scheme_end + 3;
  std::size_t end = uri.find_first_of("/?#", start);
  if (end == std::string::npos) end = uri.size();
  std::string authority = uri.substr(start, end - start);
  const std::size_t at = authority.rfind('@');
  if (at != std::string::npos) authority = authority.substr(at + 1);   // what stands before the host is never carried
  if (authority.empty() || authority.size() > 80) return std::string();
  for (const char c : authority) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == ':' || c == '[' || c == ']')) return std::string();
  return lower(uri.substr(0, scheme_end)) + "://" + authority;
}

struct Entry {
  std::string path;
  std::string text;      // the value as JSON text: always a quoted string
};

namespace detail {
inline std::string printable(std::string_view text, std::size_t limit) {
  std::string out;
  for (const char c : text) {
    if (out.size() >= limit) break;
    const unsigned char u = static_cast<unsigned char>(c);
    out += (u >= 32 && u < 127) ? c : '?';
  }
  return out;
}

inline void walk(const json::Value& value, const std::string& path, std::size_t depth, std::vector<Entry>& kept, std::vector<std::string>& secrets, bool& truncated) {
  if (depth > 8) return;
  if (value.is_object()) {
    for (std::size_t i = 0; i < value.names.size(); ++i) walk(value.items[i], path.empty() ? value.names[i] : path + "." + value.names[i], depth + 1, kept, secrets, truncated);
    return;
  }
  if (value.is_array()) {
    for (std::size_t i = 0; i < value.items.size() && i < 16; ++i) walk(value.items[i], path + "." + std::to_string(i), depth + 1, kept, secrets, truncated);
    return;
  }
  const std::size_t dot = path.rfind('.');
  const std::string leaf = dot == std::string::npos ? path : path.substr(dot + 1);
  const bool secret = secret_path(path);
  if (secret) {
    if (value.is_string() && value.text.size() >= 4 && value.text.size() <= 256) secrets.push_back(value.text);
    return;
  }
  if (path.empty()) return;
  // The address of a broker: scheme, host and port only, under a name of its own.
  if (leaf == "uri" && value.is_string()) {
    const std::string safe = safe_uri(value.text);
    // the user name and the password that may stand in it are washed from everything else
    const std::size_t scheme_end = value.text.find("://");
    if (scheme_end != std::string::npos) {
      const std::size_t start = scheme_end + 3;
      std::size_t end = value.text.find_first_of("/?#", start);
      if (end == std::string::npos) end = value.text.size();
      const std::string authority = value.text.substr(start, end - start);
      const std::size_t at = authority.rfind('@');
      if (at != std::string::npos) {
        const std::string userinfo = authority.substr(0, at);
        if (userinfo.size() >= 4) secrets.push_back(userinfo);
        const std::size_t colon = userinfo.find(':');
        if (colon != std::string::npos && userinfo.size() - colon - 1 >= 4) secrets.push_back(userinfo.substr(colon + 1));
      }
    }
    if (!safe.empty() && kept.size() < kMaxConfigEntries) kept.push_back({path.substr(0, path.size() - 3) + "uri_host", json::quote(safe)});
    return;
  }
  if (!allowed_leaf(leaf)) return;
  if (kept.size() >= kMaxConfigEntries) { truncated = true; return; }
  // every value is carried as short text ("true", "30", "station"): a console shows them, it does not compute with them
  if (value.is_bool()) kept.push_back({path, json::quote(value.boolean ? "true" : "false")});
  else if (value.is_number()) { json::Writer w; w.number(value.number); kept.push_back({path, json::quote(w.str())}); }
  else if (value.is_string()) kept.push_back({path, json::quote(printable(value.text, kMaxValueChars))});
}
}  // namespace detail

// The settings of the node as they are kept (the text of `config::to_json(settings, true)`): what the package may carry, and every value found under a secret path.
// False only when the text is not a JSON document.
inline bool collect(const std::string& full_settings_json, std::vector<Entry>& kept, std::vector<std::string>& secrets, bool* truncated = nullptr) {
  json::Value doc;
  if (!json::parse(full_settings_json, doc) || !doc.is_object()) return false;
  bool cut = false;
  detail::walk(doc, std::string(), 0, kept, secrets, cut);
  std::sort(kept.begin(), kept.end(), [](const Entry& a, const Entry& b) { return a.path < b.path; });
  if (truncated != nullptr) *truncated = cut;
  return true;
}

inline std::string config_object(const std::vector<Entry>& kept) {
  json::Writer w;
  w.begin_object();
  for (const Entry& entry : kept) w.key(entry.path).raw(entry.text);
  w.end_object();
  return w.str();
}

// Every occurrence of a known secret in `text` becomes `***`. A value shorter than four characters is not washed (it would eat ordinary words).
inline std::string wash(std::string text, const std::vector<std::string>& secrets) {
  for (const std::string& secret : secrets) {
    if (secret.size() < 4) continue;
    std::size_t at = 0;
    while ((at = text.find(secret, at)) != std::string::npos) { text.replace(at, secret.size(), "***"); at += 3; }
    // inside a JSON text a value with a quote, a backslash or a control character is written escaped: wash that form too
    std::string escaped = json::quote(secret);
    escaped = escaped.substr(1, escaped.size() - 2);
    if (escaped != secret) {
      at = 0;
      while ((at = text.find(escaped, at)) != std::string::npos) { text.replace(at, escaped.size(), "***"); at += 3; }
    }
  }
  return text;
}

namespace detail {
inline bool credential_word(const std::string& low, std::size_t at, std::size_t& value_start) {
  static const char* const words[] = {"password", "passwd", "pass", "secret", "token", "psk", "apikey", "api_key", "authorization", "bearer", "cookie", "pin", "credential", "login"};
  for (const char* word : words) {
    const std::size_t n = std::char_traits<char>::length(word);
    if (low.compare(at, n, word) != 0) continue;
    if (at > 0) {
      const char before = low[at - 1];
      if ((before >= 'a' && before <= 'z') || (before >= '0' && before <= '9')) continue;   // inside another word
    }
    std::size_t i = at + n;
    while (i < low.size() && (low[i] == '"' || low[i] == '\'')) ++i;
    // "password=x", "token: x", "bearer x"
    if (i < low.size() && (low[i] == '=' || low[i] == ':')) {
      ++i;
      while (i < low.size() && (low[i] == ' ' || low[i] == '"' || low[i] == '\'')) ++i;
      // "Authorization: Bearer <value>" and "Authorization: Basic <value>": the scheme is not the secret, what follows it is
      if (n == 13) {
        for (const char* scheme : {"bearer ", "basic "}) {
          const std::size_t m = std::char_traits<char>::length(scheme);
          if (low.compare(i, m, scheme) == 0) { i += m; break; }
        }
      }
      value_start = i;
      return true;
    }
    if (std::char_traits<char>::length(word) == 6 && low.compare(at, 6, "bearer") == 0 && i < low.size() && low[i] == ' ') { value_start = i + 1; return true; }
  }
  return false;
}
}  // namespace detail

// The log as the package carries it: the newest `limit` bytes, from the start of a line, printable characters only, washed of credentials.
inline std::string sanitize_log(std::string_view raw, std::size_t limit, const std::vector<std::string>& secrets) {
  std::string_view tail = raw.size() > limit ? raw.substr(raw.size() - limit) : raw;
  if (raw.size() > limit) {
    const std::size_t newline = tail.find('\n');
    if (newline != std::string_view::npos) tail = tail.substr(newline + 1);
  }
  std::string text;
  text.reserve(tail.size());
  for (std::size_t i = 0; i < tail.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(tail[i]);
    if (c == 0x1b) {   // a colour code: ESC [ ... m
      while (i + 1 < tail.size() && tail[i + 1] != 'm') ++i;
      ++i;
      continue;
    }
    if (c == '\n') text += '\n';
    else if (c == '\r' || c == '\t') text += ' ';
    else text += (c >= 32 && c < 127) ? static_cast<char>(c) : '?';
  }
  const std::string low = lower(text);
  std::string out;
  out.reserve(text.size());
  std::size_t i = 0;
  while (i < text.size()) {
    std::size_t value_start = 0;
    if (detail::credential_word(low, i, value_start)) {
      std::size_t end = value_start;
      while (end < text.size() && text[end] != '\n' && text[end] != ' ' && text[end] != ',' && text[end] != ';' && text[end] != '"' && text[end] != '\'') ++end;
      out.append(text, i, value_start - i);
      out += "***";
      i = end;
      continue;
    }
    // a user and a password in an address: scheme://user:pass@host
    if (text.compare(i, 3, "://") == 0) {
      const std::size_t stop = text.find_first_of("/ \n", i + 3);
      const std::size_t limit_end = stop == std::string::npos ? text.size() : stop;
      const std::size_t at = text.find('@', i + 3);
      if (at != std::string::npos && at < limit_end) {
        out += "://***@";
        i = at + 1;
        continue;
      }
    }
    // a long run of hexadecimal digits: a token, a hash, a key
    if (std::isxdigit(static_cast<unsigned char>(text[i])) != 0 && (i == 0 || std::isalnum(static_cast<unsigned char>(text[i - 1])) == 0)) {
      std::size_t end = i;
      while (end < text.size() && std::isxdigit(static_cast<unsigned char>(text[end])) != 0) ++end;
      if (end - i >= 32 && (end == text.size() || std::isalnum(static_cast<unsigned char>(text[end])) == 0)) { out += "<hex>"; i = end; continue; }
    }
    out += text[i++];
  }
  return wash(out, secrets);
}

struct Sensor {
  std::string name;      // up to 32 characters
  std::string version;   // up to 32 characters
};

struct CoreSummary {
  bool has_summary = false;
  std::string task;                       // the task that crashed
  std::string reason;                     // what the panic handler said ("Guru Meditation Error: ... (LoadProhibited)"), may be empty
  std::string pc;                         // "0x4200abcd"
  std::vector<std::string> backtrace;     // up to 16 addresses, "0x..."
  std::string app_sha256;                 // the first 16 hexadecimal digits of the image that crashed
};

struct Inputs {
  std::string firmware, idf, board;
  std::uint64_t uptime_s = 0;
  std::string reset_reason = "unknown";
  unsigned guard_restarts_in_hour = 0;
  membudget::Snapshot memory;
  std::uint64_t heap_total_free = 0, heap_total_min = 0;
  std::vector<health::TaskInfo> tasks;
  bool has_boot = false;
  bootreport::Report boot;
  bool has_gate = false;
  bootreport::GateInfo gate;
  std::vector<Sensor> sensors;
  std::string full_settings_json;         // config::to_json(settings, true): used to take the allowed leaves and to learn the secrets; never written out
  bool coredump_present = false;
  std::uint32_t coredump_size = 0;
  CoreSummary core;
  std::string log;                        // the raw log text
};

inline bool hex_text(const std::string& text, std::size_t limit) {
  if (text.size() < 3 || text.size() > limit || text[0] != '0' || text[1] != 'x') return false;
  for (std::size_t i = 2; i < text.size(); ++i) if (!std::isxdigit(static_cast<unsigned char>(text[i]))) return false;
  return true;
}

inline void write_core(json::Writer& w, const Inputs& in) {
  w.key("coredump").begin_object().field("present", in.coredump_present);
  if (in.coredump_present && in.coredump_size > 0) w.key("size").integer(static_cast<long long>(in.coredump_size));
  if (in.coredump_present && in.core.has_summary) {
    w.key("summary").begin_object().field("task", detail::printable(in.core.task, 16));
    if (!in.core.reason.empty()) w.field("reason", detail::printable(in.core.reason, 96));
    if (hex_text(in.core.pc, 18)) w.field("pc", in.core.pc);
    w.key("backtrace").begin_array();
    std::size_t n = 0;
    for (const std::string& address : in.core.backtrace) if (n < 16 && hex_text(address, 18)) { w.string(address); ++n; }
    w.end_array();
    if (!in.core.app_sha256.empty()) w.field("app_sha256", detail::printable(in.core.app_sha256, 16));
    w.end_object();
  }
  w.end_object();
}

// The `package` member of the diagnostics answer. `kLog` carries the log alone, `kPackage` everything; `kSummary` is not a package (the plain diagnostics need none) and gives "".
inline std::string package_json(const Inputs& in, Detail detail) {
  if (detail == Detail::kSummary) return std::string();
  std::vector<Entry> kept;
  std::vector<std::string> secrets;
  bool cut = false;
  const bool settings_read = collect(in.full_settings_json, kept, secrets, &cut);
  // The fixed parts first, then the log with whatever room is left.
  auto build = [&](std::size_t log_limit) {
    json::Writer w;
    w.begin_object().field("detail", detail_name(detail));
    if (detail == Detail::kPackage) {
      w.key("heap").begin_object();
      w.key("internal_free").integer(in.memory.internal_free);
      w.key("internal_min").integer(in.memory.internal_min > in.memory.internal_free ? in.memory.internal_free : in.memory.internal_min);
      w.key("internal_largest").integer(in.memory.internal_largest > in.memory.internal_free ? in.memory.internal_free : in.memory.internal_largest);
      w.key("total_free").integer(static_cast<long long>(in.heap_total_free));
      w.key("total_min").integer(static_cast<long long>(in.heap_total_min > in.heap_total_free ? in.heap_total_free : in.heap_total_min));
      if (in.memory.has_psram()) w.key("psram_free").integer(in.memory.psram_free > in.memory.psram_total ? in.memory.psram_total : in.memory.psram_free);
      w.end_object();
      std::vector<health::TaskInfo> tasks = in.tasks;
      std::sort(tasks.begin(), tasks.end(), [](const health::TaskInfo& a, const health::TaskInfo& b) { return a.stack_free != b.stack_free ? a.stack_free < b.stack_free : a.name < b.name; });
      w.key("tasks").begin_array();
      for (std::size_t i = 0; i < tasks.size() && i < kMaxTasks; ++i) {
        w.begin_object().field("name", detail::printable(tasks[i].name, 15)).key("stack_free").integer(tasks[i].stack_free).key("priority").integer(tasks[i].priority < 0 ? 0 : (tasks[i].priority > 63 ? 63 : tasks[i].priority));
        const char state = tasks[i].state;
        w.field("state", std::string(1, (state == 'R' || state == 'r' || state == 'B' || state == 'S' || state == 'D') ? state : '?')).end_object();
      }
      w.end_array();
      w.key("resets").begin_object().field("reason", in.reset_reason).key("uptime_s").integer(static_cast<long long>(in.uptime_s)).key("guard_restarts_in_hour").integer(in.guard_restarts_in_hour > 3 ? 3 : in.guard_restarts_in_hour).end_object();
      if (in.has_boot && in.boot.dirty) { w.key("boot"); bootreport::write_json(w, in.boot); }
      if (in.has_gate) { w.key("memory_gate"); bootreport::write_gate_json(w, in.gate); }
      w.key("versions").begin_object().field("firmware", in.firmware).field("idf", detail::printable(in.idf, 32));
      if (!in.board.empty()) w.field("board", detail::printable(in.board, 32));
      w.key("sensors").begin_array();
      for (std::size_t i = 0; i < in.sensors.size() && i < kMaxSensors; ++i) w.begin_object().field("name", detail::printable(in.sensors[i].name, 32)).field("version", detail::printable(in.sensors[i].version, 32)).end_object();
      w.end_array().end_object();
      w.key("config").raw(settings_read ? config_object(kept) : std::string("{}"));
      write_core(w, in);
    }
    const std::string log = sanitize_log(in.log, log_limit, secrets);
    w.field("log", log);
    w.field("truncated", cut || in.log.size() > log_limit).end_object();
    return wash(w.str(), secrets);
  };
  std::size_t limit = kMaxLogBytes;
  std::string out = build(limit);
  while (out.size() > kMaxPackageBytes && limit > 256) {
    limit = limit * 2 / 3;
    out = build(limit);
  }
  return out;
}

}  // namespace armor::diag
