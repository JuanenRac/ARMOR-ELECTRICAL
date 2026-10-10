// ARMOR-ELECTRICAL - the relays as devices of the device layer: the topics, the command words and the fall back to open when the broker is lost.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The convention is the one the radar node uses for the pins its panel maps: a relay is a device of A.R.M.O.R.'s device layer (ARMOR-SERVER), named after the node,
//   armor/device/<node>/<relay>/state          {"on":true}                       what the relay is (published when it changes and every half minute)
//   armor/device/<node>/<relay>/availability   online                            sent with it
//   armor/device/<node>/<relay>/set            ON | OFF | TOGGLE                 what the server (or anyone the broker allows) tells it
// In Studio the device is added with the "ARMOR node" connection and the name <node>/<relay>; the server then switches it with its own rules (a risk that asks first, only an
// administrator for the critical ones). Three independent things must all agree before a relay moves by a command: the node's setting `remote.relays` (off by default), the broker's
// ACL (DEVOPS `mqtt_identity.sh electrical-relays <node> on`, off by default) and the device the operator created in the server. Nothing here touches a pin.
#pragma once
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

#include "config_common.hpp"
#include "json.hpp"
#include "node_id.hpp"

namespace armor::relays {

enum class Action { kNone, kOn, kOff, kToggle };

inline std::string lower_trimmed(std::string_view text) {
  std::size_t first = 0, last = text.size();
  while (first < last && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
  while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
  std::string out(text.substr(first, last - first));
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

// The word of a command. Anything else (a number of pulses, a level, a sentence) is not a command to a relay.
inline Action parse_command(std::string_view payload) {
  const std::string word = lower_trimmed(payload);
  if (word == "on" || word == "true" || word == "1") return Action::kOn;
  if (word == "off" || word == "false" || word == "0") return Action::kOff;
  if (word == "toggle") return Action::kToggle;
  return Action::kNone;
}

// armor/device/<node>/<relay>/<suffix>, or "" when the node id or the relay's name is not valid.
inline std::string topic(std::string_view node_id, std::string_view relay, std::string_view suffix) {
  if (!node_id_is_valid(node_id) || !config::valid_device_name(relay) || (suffix != "state" && suffix != "set" && suffix != "availability")) return "";
  return "armor/device/" + std::string(node_id) + "/" + std::string(relay) + "/" + std::string(suffix);
}

// The single subscription that covers every command topic of the node.
inline std::string command_filter(std::string_view node_id) { return node_id_is_valid(node_id) ? "armor/device/" + std::string(node_id) + "/+/set" : ""; }

// The relay's name in a command topic of this node, or "" when the topic is not one (another node's, a state topic, a deeper path).
inline std::string relay_of_command_topic(std::string_view node_id, std::string_view topic_text) {
  const std::string prefix = "armor/device/" + std::string(node_id) + "/";
  if (!node_id_is_valid(node_id) || topic_text.substr(0, prefix.size()) != prefix) return "";
  const std::string_view rest = topic_text.substr(prefix.size());
  if (rest.size() < 5 || rest.substr(rest.size() - 4) != "/set") return "";
  const std::string_view name = rest.substr(0, rest.size() - 4);
  return config::valid_device_name(name) ? std::string(name) : "";
}

inline std::string report(bool on) {
  json::Writer w;
  w.begin_object().field("on", on).end_object();
  return w.str();
}

// How long the broker has been out of reach. A relay may be told to fall back to open after a while without it (a node cut off from the network must not leave a load closed forever):
// `remote.link_timeout_s`, 0 meaning never.
class LinkWatch {
 public:
  void update(bool up, std::uint64_t now_ms) {
    if (up) { lost_ = false; return; }
    if (!lost_) { lost_ = true; since_ms_ = now_ms; }
  }
  bool lost() const { return lost_; }
  std::uint64_t lost_for_ms(std::uint64_t now_ms) const { return lost_ ? now_ms - since_ms_ : 0; }
 private:
  bool lost_ = false;
  std::uint64_t since_ms_ = 0;
};

inline bool must_open(int link_timeout_s, std::uint64_t lost_for_ms) { return link_timeout_s > 0 && lost_for_ms >= static_cast<std::uint64_t>(link_timeout_s) * 1000ULL; }

}  // namespace armor::relays
