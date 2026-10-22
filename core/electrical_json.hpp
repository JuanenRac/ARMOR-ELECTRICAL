// ARMOR-ELECTRICAL - the message a node publishes (contract version 0, see docs/ELECTRICAL_MESSAGES.md).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
//   armor/electrical/<node>/state     one message per node, every few seconds; JSON, no retained flag.
//   kind "electrical": the state of the node's switches (when it has any) and one entry per measured channel: AC or DC, voltage, current, power (positive when it draws from the network), energy, and for AC the
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

/// A channel the node makes from one of its own analog inputs (core/analog.hpp): only what the input measures is set.
struct AnalogChannel {
  std::string id, label;
  bool ac = false;
  bool has_voltage = false, has_current = false, has_power = false, has_energy = false;
  bool power_estimated = false;     // the power and the energy were made from a current and a nominal voltage (apparent power), not measured
  double voltage_v = 0, current_a = 0, power_w = 0, energy_kwh = 0;
};

/// The message of a node from the meters that have a fresh reading. `extras` (by channel id) adds the state of a switch and an alarm code; a meter's own alarm flag is
/// always reported. At most sixteen channels go into one message. `switching_enabled` says whether this node's firmware may switch at all. `switches` is the already
/// serialised array of the node's switches (`SwitchSet::switches_json`), or empty for a node that has none. `analog` are the channels of the node's own analog inputs, after the
/// meters' (the message holds sixteen channels in all). `now_ms` is the node's uptime clock: when it is given (not zero) each meter channel also says how old its reading is
/// (`reading_age_ms`) and how many requests failed since boot (`errors`), so the server can tell "0 W" from "no data".
inline std::string message_json(const std::string& node_id, std::uint64_t timestamp_ms, bool switching_enabled, const std::vector<const MeterState*>& meters,
                                const std::vector<std::pair<std::string, ChannelExtra>>& extras = {}, const std::string& switches = "",
                                const std::vector<AnalogChannel>& analog = {}, std::uint64_t now_ms = 0) {
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
    if (now_ms != 0) {
      w.key("reading_age_ms").integer(static_cast<long long>(now_ms >= meter->reading_at_ms ? now_ms - meter->reading_at_ms : 0));
      w.key("errors").integer(static_cast<long long>(meter->replies_bad) + static_cast<long long>(meter->timeouts));
    }
    for (const auto& extra : extras) {
      if (extra.first != meter->config.channel) continue;
      if (extra.second.state != SwitchState::kNone) w.field("state", state_name(extra.second.state));
      if (!extra.second.alarm_code.empty()) w.field("alarm_code", extra.second.alarm_code);
    }
    w.field("alarm", r.alarm);
    w.end_object();
    ++written;
  }
  for (const AnalogChannel& input : analog) {
    if (written >= 16) break;
    w.begin_object().field("id", input.id).field("domain", input.ac ? "ac" : "dc");
    if (!input.label.empty()) w.field("label", input.label);
    if (input.has_voltage) w.key("voltage_v").number(input.voltage_v, 2);
    if (input.has_current) w.key("current_a").number(input.current_a, 3);
    if (input.has_power) w.key("power_w").number(input.power_w, 1);
    if (input.has_energy) w.key("energy_kwh").number(input.energy_kwh, 3);
    if (input.power_estimated) w.field("power_estimated", true);
    w.field("alarm", false);
    w.end_object();
    ++written;
  }
  w.end_array();
  if (!switches.empty()) w.key("switches").raw(switches);
  w.end_object();
  return w.str();
}

}  // namespace armor::electrical
