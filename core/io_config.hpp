// A.R.M.O.R. - the settings of the base board that carries the ESP32-S3 module: the Zigbee radio, the relay outputs and whether the broker may move the relays.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The electrical node and the alarm node use the same board with different parts populated, so these sections of their settings documents are the same: they are read, checked and written
// here, once. Pure: no hardware. `io_validate` is given the caller's own `claim(gpio, who, path)`, which knows the pins the board reserves and the ones already taken.
#pragma once
#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "base_board.hpp"
#include "config_common.hpp"
#include "net_text.hpp"

namespace armor::config {

// The Zigbee radio module on the base board: a coordinator the node does not interpret. The node lends its serial line to ONE client of the network (Zigbee2MQTT on the
// server) over TCP, and only to the address named here.
struct ZigbeeSetting {
  bool enabled = false;
  int rx = -1;                 // the GPIO that receives the radio's TX
  int tx = -1;                 // the GPIO that sends to the radio's RX
  int reset = -1;              // drives the radio's RESET_N (active low); -1: not wired
  int bootloader = -1;         // drives the radio's serial-boot pin (active low); -1: not wired
  int baud = 115200;           // what the radio's firmware speaks (a Z-Stack coordinator: 115200)
  int port = 8888;             // the TCP port the node listens on
  std::string allowed_client;  // the one IPv4 address that may connect (the server that runs Zigbee2MQTT): required while the bridge is on
  std::string fallback_client; // optionally a second address (an alarm panel) that may take the radio over while the first has been away for fallback_after_s; empty: none
  int fallback_after_s = 30;   // how long the first client must have been away before the second may connect
};

// One relay output: the node drives the pin, whatever is behind it (a driver, the relay, the load) is the installer's. Every relay starts open (off) when the node starts.
struct RelaySetting {
  bool enabled = false;
  std::string name;        // lowercase letters, digits, - and _ ; the same rule as a channel
  std::string label;
  int pin = -1;
  bool active_low = false; // the relay closes when the pin is low (an opto-isolated board often does)
};

// Whether the broker may move the relays. OFF by default: a relay then answers only to the panel. On, each relay is a device of the device layer (core/relay_remote.hpp) and
// falls back to open after `link_timeout_s` seconds without the broker (0: never).
struct Remote {
  bool relays = false;
  int link_timeout_s = 0;
};

// The speeds the Zigbee radio's firmware may use (a Z-Stack coordinator: 115200; the maker's own firmware: 230400).
inline const std::vector<int>& zigbee_bauds() {
  static const std::vector<int> speeds{115200, 230400};
  return speeds;
}

struct IoSettings {
  ZigbeeSetting zigbee;
  std::array<RelaySetting, board::kRelayCount> relays;
  Remote remote;
};

// The pins a node that was never configured looks at first: the board's own.
inline void io_defaults(IoSettings& io) {
  IoSettings& s = io;
  s.zigbee.rx = board::kZigbeePins.rx; s.zigbee.tx = board::kZigbeePins.tx; s.zigbee.reset = board::kZigbeePins.reset; s.zigbee.bootloader = board::kZigbeePins.bootloader;
  for (std::size_t i = 0; i < board::kRelayCount; ++i) { s.relays[i].name = "relay" + std::to_string(i + 1); s.relays[i].pin = board::kRelayPins[i]; }
}

// The "zigbee", "relays" and "remote" sections of a settings document, applied on top of `io`.
inline void io_read(const json::Value& document, IoSettings& io, Problems& problems) {
  using namespace detail;
  IoSettings& s = io;
  if (const json::Value* zigbee = document.get("zigbee"); zigbee != nullptr && zigbee->is_object()) {
    read_bool(*zigbee, "enabled", s.zigbee.enabled, "zigbee.enabled", problems);
    read_int(*zigbee, "rx", s.zigbee.rx, -1, board::kLastGpio, "zigbee.rx", problems);
    read_int(*zigbee, "tx", s.zigbee.tx, -1, board::kLastGpio, "zigbee.tx", problems);
    read_int(*zigbee, "reset", s.zigbee.reset, -1, board::kLastGpio, "zigbee.reset", problems);
    read_int(*zigbee, "bootloader", s.zigbee.bootloader, -1, board::kLastGpio, "zigbee.bootloader", problems);
    read_int(*zigbee, "baud", s.zigbee.baud, 1200, 1000000, "zigbee.baud", problems);
    read_int(*zigbee, "port", s.zigbee.port, 1, 65535, "zigbee.port", problems);
    read_text(*zigbee, "allowed_client", s.zigbee.allowed_client, 15, "zigbee.allowed_client", problems);
    read_text(*zigbee, "fallback_client", s.zigbee.fallback_client, 15, "zigbee.fallback_client", problems);
    read_int(*zigbee, "fallback_after_s", s.zigbee.fallback_after_s, 5, 600, "zigbee.fallback_after_s", problems);
  }
  if (const json::Value* relays = document.get("relays"); relays != nullptr) {
    if (!relays->is_array() || relays->items.size() > board::kRelayCount) bad(problems, "relays", "invalid");
    else for (std::size_t i = 0; i < relays->items.size(); ++i) {
      const json::Value& item = relays->items[i];
      const std::string base = "relays." + std::to_string(i) + ".";
      if (!item.is_object()) { bad(problems, base + "enabled", "invalid"); continue; }
      RelaySetting& relay = s.relays[i];
      read_bool(item, "enabled", relay.enabled, base + "enabled", problems);
      read_text(item, "name", relay.name, 32, base + "name", problems);
      read_text(item, "label", relay.label, 40, base + "label", problems);
      read_int(item, "pin", relay.pin, -1, board::kLastGpio, base + "pin", problems);
      read_bool(item, "active_low", relay.active_low, base + "active_low", problems);
    }
  }
  if (const json::Value* remote = document.get("remote"); remote != nullptr && remote->is_object()) {
    read_bool(*remote, "relays", s.remote.relays, "remote.relays", problems);
    read_int(*remote, "link_timeout_s", s.remote.link_timeout_s, 0, 86400, "remote.link_timeout_s", problems);
  }
}

// `claim(gpio, who, path)` takes a pin for a use or says why it cannot (reserved by the board, or already taken); `broker_enabled` is whether the node has a broker to talk to.
template <typename Claim>
inline void io_validate(const IoSettings& io, bool broker_enabled, Claim&& claim, Problems& problems) {
  using detail::bad;
  const IoSettings& s = io;
  // the Zigbee radio: its own serial line, lent to one client of the network
  if (s.zigbee.enabled) {
    if (s.zigbee.rx < 0) bad(problems, "zigbee.rx", "required"); else claim(s.zigbee.rx, "zigbee", "zigbee.rx");
    if (s.zigbee.tx < 0) bad(problems, "zigbee.tx", "required"); else claim(s.zigbee.tx, "zigbee", "zigbee.tx");
    if (s.zigbee.reset >= 0) claim(s.zigbee.reset, "zigbee", "zigbee.reset");
    if (s.zigbee.bootloader >= 0) claim(s.zigbee.bootloader, "zigbee", "zigbee.bootloader");
    if (std::find(zigbee_bauds().begin(), zigbee_bauds().end(), s.zigbee.baud) == zigbee_bauds().end()) bad(problems, "zigbee.baud", "range");
    if (s.zigbee.port < 1024 || s.zigbee.port > 65535) bad(problems, "zigbee.port", "range");
    std::uint32_t client = 0;
    if (s.zigbee.allowed_client.empty()) bad(problems, "zigbee.allowed_client", "required");
    else if (!net::parse_ipv4(s.zigbee.allowed_client, client) || !net::usable_host_address(client)) bad(problems, "zigbee.allowed_client", "invalid");
    std::uint32_t second = 0;
    if (!s.zigbee.fallback_client.empty()) {
      if (!net::parse_ipv4(s.zigbee.fallback_client, second) || !net::usable_host_address(second)) bad(problems, "zigbee.fallback_client", "invalid");
      else if (second == client) bad(problems, "zigbee.fallback_client", "conflict");
    }
  }

  // the relay outputs: each has its own name and its own pin, which may not be reserved
  std::vector<std::string> relay_names;
  for (std::size_t i = 0; i < board::kRelayCount; ++i) {
    const RelaySetting& relay = s.relays[i];
    if (!relay.enabled) continue;
    const std::string base = "relays." + std::to_string(i) + ".";
    if (!valid_device_name(relay.name)) bad(problems, base + "name", relay.name.empty() ? "required" : "invalid");
    else if (std::find(relay_names.begin(), relay_names.end(), relay.name) != relay_names.end()) bad(problems, base + "name", "conflict");
    else relay_names.push_back(relay.name);
    if (relay.pin < 0) bad(problems, base + "pin", "required"); else claim(relay.pin, "relay" + std::to_string(i + 1), base + "pin");
    std::size_t label_characters = 0;
    if (!relay.label.empty() && (!net::valid_utf8(relay.label, &label_characters) || label_characters > 40)) bad(problems, base + "label", "invalid");
  }

  // the broker moving the relays: it needs a broker, and a fall back time is 0 (never) or at least ten seconds
  if (s.remote.relays && !broker_enabled) bad(problems, "remote.relays", "needs_broker");
  if (s.remote.link_timeout_s != 0 && s.remote.link_timeout_s < 10) bad(problems, "remote.link_timeout_s", "range");

}

inline void io_write(json::Writer& w, const IoSettings& io) {
  const IoSettings& s = io;
  w.key("zigbee").begin_object().field("enabled", s.zigbee.enabled).field("rx", s.zigbee.rx).field("tx", s.zigbee.tx).field("reset", s.zigbee.reset).field("bootloader", s.zigbee.bootloader)
      .field("baud", s.zigbee.baud).field("port", s.zigbee.port).field("allowed_client", s.zigbee.allowed_client).field("fallback_client", s.zigbee.fallback_client).field("fallback_after_s", s.zigbee.fallback_after_s).end_object();
  w.key("relays").begin_array();
  for (const RelaySetting& relay : s.relays) {
    w.begin_object().field("enabled", relay.enabled).field("name", relay.name).field("label", relay.label).field("pin", relay.pin).field("active_low", relay.active_low).end_object();
  }
  w.end_array();
  w.key("remote").begin_object().field("relays", s.remote.relays).field("link_timeout_s", s.remote.link_timeout_s).end_object();
}

}  // namespace armor::config
