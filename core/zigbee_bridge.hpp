// ARMOR-ELECTRICAL - the Zigbee radio of the base board: what the node decides about it, without any hardware.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The node does not speak Zigbee. A coordinator module (a CC2652P with a Z-Stack coordinator firmware) sits on one of the node's serial lines and the node only lends that line to the
// network, byte for byte, over TCP, to ONE client: Zigbee2MQTT on the server (`serial: port: tcp://<node>:<port>`, adapter `zstack`). What the node decides is in this file:
//   - who may connect: the one IPv4 address named in the settings, nobody else (a coordinator that anyone could talk to is a house anyone could command);
//   - how the radio is reset, and how it is put into its serial boot loader to be given a new firmware (the order of the two lines and the pauses);
//   - what the panel shows of the bridge.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "electrical_config.hpp"
#include "json.hpp"
#include "net_text.hpp"

namespace armor::zigbee {

// True when `peer` (an IPv4 address in host order) is the client the settings name.
inline bool client_allowed(const std::string& allowed, std::uint32_t peer) {
  std::uint32_t wanted = 0;
  return net::parse_ipv4(allowed, wanted) && wanted == peer;
}

// One step of driving the radio's two control lines. A line is either pulled low or let go (the radio has its own pull-ups; the node never drives either line high).
struct Step {
  bool reset_low = false;
  bool bootloader_low = false;
  unsigned hold_ms = 0;   // how long to stay in this step before the next
};

// A plain reset: RESET_N low for a while, then let go with the boot line untouched.
// To the boot loader: the boot line is already low when RESET_N is released, and stays low until the radio has had time to look at it; then both are let go.
inline std::vector<Step> radio_sequence(bool bootloader) {
  if (!bootloader) return {{true, false, 50}, {false, false, 0}};
  return {{true, true, 50}, {false, true, 150}, {false, false, 0}};
}

struct Status {
  std::string state = "disabled";   // disabled, starting, error, waiting (no client), connected
  std::string error;                // why the bridge did not start: "uart", "pins", "listen"
  std::string client;               // the address of the client now connected
  std::uint64_t to_radio = 0, from_radio = 0;   // bytes the client sent to the radio and the radio sent to the client
  std::uint32_t connections = 0;    // clients accepted since the node started
  std::uint32_t refused = 0;        // connections closed at once because they came from another address
  std::uint64_t dropped = 0;        // bytes the radio sent while nobody was connected
  bool has_reset = false, has_bootloader = false;
};

inline std::string status_json(const config::ZigbeeSetting& setting, const Status& status) {
  json::Writer w;
  w.begin_object().field("enabled", setting.enabled).field("state", status.state).field("error", status.error).field("port", setting.port).field("baud", setting.baud)
      .field("allowed_client", setting.allowed_client).field("client", status.client)
      .key("to_radio").integer(static_cast<long long>(status.to_radio)).key("from_radio").integer(static_cast<long long>(status.from_radio))
      .key("connections").integer(status.connections).key("refused").integer(status.refused).key("dropped").integer(static_cast<long long>(status.dropped))
      .field("has_reset", status.has_reset).field("has_bootloader", status.has_bootloader).end_object();
  return w.str();
}

}  // namespace armor::zigbee
