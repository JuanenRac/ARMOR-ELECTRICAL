// ARMOR-ELECTRICAL - the health report every node says about itself, in the same words for every family. No hardware.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// A node publishes, now and then, one message to armor/health/<node> (contract node_health.schema.json in ARMOR-COMMON): how long it has been up and why it last restarted, the memory it has
// (now and at its worst), the link it reaches the network by, how often it has connected to the broker, how many of its tasks are short of stack and the last error it logged. The server keeps
// the last few reports and warns of a node that is short of memory, restarts too often or keeps losing the broker (it judges; the node only tells). Everything here is a pure function of
// numbers and text handed in, so it is tested on a computer; main/node_health.cpp reads the real values and sends the message.
#pragma once
#include <cstdint>
#include <string>

#include "json.hpp"
#include "node_id.hpp"

namespace armor::nodehealth {

enum class Link { kNone, kWifi, kEthernet };

inline const char* link_text(Link link) { return link == Link::kWifi ? "wifi" : link == Link::kEthernet ? "ethernet" : "none"; }

// The reset reasons of ESP-IDF (esp_reset_reason_t, numbered as there) in the words of the contract.
inline const char* reset_reason_text(int esp_reset_reason) {
  switch (esp_reset_reason) {
    case 1: return "power_on";
    case 2: return "external";
    case 3: return "software";
    case 4: return "panic";
    case 5: return "int_wdt";
    case 6: return "task_wdt";
    case 7: return "other_wdt";
    case 8: return "deep_sleep";
    case 9: return "brownout";
    case 10: return "sdio";
    case 11: return "usb";
    case 12: return "jtag";
    case 13: return "efuse";
    case 14: return "power_glitch";
    case 15: return "cpu_lockup";
    default: return "unknown";
  }
}

// A restart nobody asked for: a crash, a watchdog, a sag of the supply. (A power-on, the reset pin and a restart the node itself chose are normal.)
inline bool is_abnormal_reset(const std::string& reason) {
  return reason == "panic" || reason == "int_wdt" || reason == "task_wdt" || reason == "other_wdt" || reason == "brownout" || reason == "power_glitch" || reason == "cpu_lockup";
}

struct LinkInfo {
  Link link = Link::kNone;
  bool up = false;
  int rssi_dbm = 0;   // only meaningful on a Wi-Fi link that is up
};

// Which link the node reaches the network by. A cable with a carrier is the link when the board has one; otherwise the station when it has joined; a board with no cable is a Wi-Fi board whose
// station has not joined (down); a board with a cable and no carrier is an Ethernet link that is down. `up` also needs an address.
inline LinkInfo choose_link(bool ethernet_available, bool link_up, bool has_ip, bool sta_connected, int sta_rssi) {
  LinkInfo info;
  if (ethernet_available && link_up && !sta_connected) { info.link = Link::kEthernet; info.up = has_ip; return info; }
  if (sta_connected) {
    info.link = Link::kWifi;
    info.up = has_ip;
    info.rssi_dbm = sta_rssi > 0 ? 0 : (sta_rssi < -120 ? -120 : sta_rssi);
    return info;
  }
  info.link = ethernet_available ? Link::kEthernet : Link::kWifi;
  return info;
}

constexpr std::size_t kMaxErrorChars = 96;

// One line of the log as an error text: the ESP-IDF format is "E (12345) tag: text", possibly wrapped in colour codes; the result is "tag: text", printable ASCII, cut to 96 characters.
inline bool error_text_of_line(const std::string& raw, std::string& out) {
  std::string line;
  for (std::size_t i = 0; i < raw.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(raw[i]);
    if (c == 0x1b) {   // a colour code: ESC [ ... m
      while (i + 1 < raw.size() && raw[i + 1] != 'm') ++i;
      ++i;
      continue;
    }
    if (c == '\r' || c == '\n') break;
    line += (c >= 32 && c < 127) ? static_cast<char>(c) : '?';
  }
  if (line.size() < 6 || line[0] != 'E' || line[1] != ' ' || line[2] != '(') return false;
  const std::size_t close = line.find(") ");
  if (close == std::string::npos || close > 14) return false;
  std::string text = line.substr(close + 2);
  while (!text.empty() && text.back() == ' ') text.pop_back();
  if (text.empty()) return false;
  if (text.size() > kMaxErrorChars) text.resize(kMaxErrorChars);
  out = text;
  return true;
}

// The last error line of a piece of log (the console text the node keeps), or "".
inline std::string last_error_of_log(const std::string& log) {
  std::string found;
  std::size_t start = 0;
  while (start <= log.size()) {
    std::size_t end = log.find('\n', start);
    if (end == std::string::npos) end = log.size();
    std::string text;
    if (end > start && error_text_of_line(log.substr(start, end - start), text)) found = text;
    if (end == log.size()) break;
    start = end + 1;
  }
  return found;
}

struct Report {
  std::string node_id;
  std::string family;            // radar, solar, electrical, alarm or hmi
  std::string firmware;          // 0.3.6 or 0.8.0.1
  std::string board;             // may be empty
  std::uint64_t timestamp_ms = 0;
  std::uint64_t uptime_s = 0;
  std::string reset_reason = "unknown";
  std::uint64_t heap_free = 0;
  std::uint64_t heap_min = 0;
  LinkInfo link;
  std::uint32_t broker_reconnects = 0;
  int low_stack_tasks = 0;
  std::string last_error;        // may be empty
  // What the server has made of the node (see control_gate.hpp): whether it works, since when and for whom, the maintenance flag. Said only when `has_control` is set.
  bool has_control = false;
  std::string control_state = "running";   // running, paused or standby
  std::uint64_t control_since_ms = 0;      // wall clock when the pause or standby began, 0 if unknown
  std::string control_by;                  // who asked for it, may be empty
  long long control_until_s = -1;          // seconds until a pause ends, -1 when not paused
  bool maintenance = false;
  long long maintenance_until_s = -1;
};

inline bool valid_family(const std::string& family) { return family == "radar" || family == "solar" || family == "electrical" || family == "alarm" || family == "hmi"; }

inline std::string topic(const std::string& node_id) { return node_id_is_valid(node_id) ? "armor/health/" + node_id : ""; }

// The message. The least heap is never reported above the heap free (the contract says so), a signal strength goes only with a Wi-Fi link that is up, and a link of none is down.
inline std::string to_json(const Report& r) {
  json::Writer w;
  w.begin_object().field("kind", "node_health").field("node_id", r.node_id).key("timestamp_ms").integer(static_cast<long long>(r.timestamp_ms));
  w.field("family", r.family).field("firmware", r.firmware);
  if (!r.board.empty()) w.field("board", r.board.substr(0, 32));
  w.key("uptime_s").integer(static_cast<long long>(r.uptime_s));
  w.field("reset_reason", r.reset_reason);
  const std::uint64_t heap_min = r.heap_min > r.heap_free ? r.heap_free : r.heap_min;
  w.key("heap_free").integer(static_cast<long long>(r.heap_free)).key("heap_min").integer(static_cast<long long>(heap_min));
  const bool up = r.link.link != Link::kNone && r.link.up;
  w.field("link", link_text(r.link.link)).field("link_up", up);
  if (r.link.link == Link::kWifi && up) w.key("rssi_dbm").integer(r.link.rssi_dbm);
  w.key("broker_reconnects").integer(static_cast<long long>(r.broker_reconnects));
  w.key("low_stack_tasks").integer(r.low_stack_tasks < 0 ? 0 : (r.low_stack_tasks > 64 ? 64 : r.low_stack_tasks));
  if (!r.last_error.empty()) w.field("last_error", r.last_error.substr(0, kMaxErrorChars));
  if (r.has_control) {
    // Since and by go only with a pause or a standby, the time left only with a pause, the maintenance time only with the flag on (the contract says so).
    const bool away = r.control_state == "paused" || r.control_state == "standby";
    w.field("control_state", away ? r.control_state : std::string("running"));
    if (away) {
      w.key("control_since_ms").integer(static_cast<long long>(r.control_since_ms));
      if (!r.control_by.empty() && r.control_by.size() <= 64) w.field("control_by", r.control_by);
      if (r.control_state == "paused" && r.control_until_s >= 0) w.key("control_until_s").integer(r.control_until_s > 86400 ? 86400 : r.control_until_s);
    }
    w.field("maintenance", r.maintenance);
    if (r.maintenance && r.maintenance_until_s >= 0) w.key("maintenance_until_s").integer(r.maintenance_until_s > 86400 ? 86400 : r.maintenance_until_s);
  }
  w.end_object();
  return w.str();
}

}  // namespace armor::nodehealth
