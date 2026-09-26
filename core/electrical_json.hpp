// ARMOR-ELECTRICAL - the message a node publishes (contract version 0, see docs/ELECTRICAL_MESSAGES.md).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
//   armor/electrical/<node>/state     one message per node, every few seconds; JSON, no retained flag.
//   kind "electrical": one entry per measured channel: AC or DC, voltage, current, power (positive when it draws from the network), energy, and for AC the
//                      frequency and the power factor; the state of a switch as the node sees it; an alarm. It carries a state and never a command.
// `node_id` and a channel id are lowercase letters, digits, - and _ (the rule of the whole of A.R.M.O.R.); `timestamp_ms` is the node's clock.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "json.hpp"
#include "pzem_bus.hpp"

namespace armor::electrical {

inline bool valid_name(const std::string& text, std::size_t longest) {
  if (text.empty() || text.size() > longest) return false;
  if (text.front() == '-' || text.front() == '_') return false;
  for (char c : text) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  return true;
}

inline std::string topic(const std::string& node_id) {
  return valid_name(node_id, 64) ? "armor/electrical/" + node_id + "/state" : "";
}

/// What a switch shows: `kClosed` and `kOpen` are what the node SEES (the auxiliary contact), `kUnknown` when it does not see it.
enum class SwitchState { kNone, kClosed, kOpen, kUnknown };

struct ChannelExtra {
  SwitchState state = SwitchState::kNone;
  std::string alarm_code;      // "" when the channel says nothing about an alarm code
};

inline const char* state_name(SwitchState state) { return state == SwitchState::kClosed ? "closed" : state == SwitchState::kOpen ? "open" : "unknown"; }

/// The message of a node from the meters that have a fresh reading. `extras` (by channel id) adds the state of a switch and an alarm code; a meter's own alarm flag is
/// always reported. At most sixteen channels go into one message. `switching_enabled` says whether this node's firmware may switch at all.
inline std::string message_json(const std::string& node_id, std::uint64_t timestamp_ms, bool switching_enabled, const std::vector<const MeterState*>& meters,
                                const std::vector<std::pair<std::string, ChannelExtra>>& extras = {}) {
  json::Writer w;
  w.begin_object().field("kind", "electrical").field("node_id", node_id).key("timestamp_ms").integer(static_cast<long long>(timestamp_ms));
  w.field("switching_enabled", switching_enabled);
  w.key("channels").begin_array();
  std::size_t written = 0;
  for (const MeterState* meter : meters) {
    if (meter == nullptr || written >= 16) continue;
    const pzem::Reading& r = meter->reading;
    const bool ac = meter->config.model == pzem::Model::kAc;
    w.begin_object().field("id", meter->config.channel).field("domain", ac ? "ac" : "dc");
    if (!meter->config.label.empty()) w.field("label", meter->config.label);
    w.key("voltage_v").number(r.voltage_v, ac ? 1 : 2).key("current_a").number(r.current_a, ac ? 3 : 2).key("power_w").number(r.power_w, 1).key("energy_kwh").number(r.energy_kwh, 3);
    if (r.has_frequency) w.key("frequency_hz").number(r.frequency_hz, 1);
    if (r.has_power_factor) w.key("power_factor").number(r.power_factor, 2);
    for (const auto& extra : extras) {
      if (extra.first != meter->config.channel) continue;
      if (extra.second.state != SwitchState::kNone) w.field("state", state_name(extra.second.state));
      if (!extra.second.alarm_code.empty()) w.field("alarm_code", extra.second.alarm_code);
    }
    w.field("alarm", r.alarm);
    w.end_object();
    ++written;
  }
  w.end_array().end_object();
  return w.str();
}

}  // namespace armor::electrical
