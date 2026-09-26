// ARMOR-ELECTRICAL - decides how the node's Wi-Fi is built from its settings: a station, an access point, both, or the setup access point.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The layouts:
//   kSetupAp           a node that has no user yet opens the access point "ARMOR-SETUP-xxxxxx", protected with the setup code, and nothing else
//                      (on the s3-eth board the cable works too: it is set up from either)
//   kStation           the node joins an existing network (the s3-wifi board's normal way in)
//   kStationWithAp     a station and an access point at once (the access point follows the station's channel)
//   kAccessPoint       only its own access point (a node in a place with no network of its own)
//   kEthernet          the s3-eth board on its cable, with DHCP or a fixed address; no Wi-Fi at all
//   kEthernetWithAp    the cable, and an access point of its own (192.168.4.x, the node's panel is on 192.168.4.1) to reach the node from a phone
#pragma once
#include <string>
#include <string_view>

#include "electrical_config.hpp"

namespace armor::netplan {

enum class Layout { kSetupAp, kStation, kStationWithAp, kAccessPoint, kEthernet, kEthernetWithAp };

struct AccessPointPlan {
  bool enabled = false;
  bool setup = false;   // the access point of a node that is waiting to be set up
  std::string ssid, password, country;
  config::WifiSecurity security = config::WifiSecurity::kWpa2;
  int channel = 1;
  bool hidden = false;
  int max_clients = 8;
  int tx_power_dbm = 15;
  int bandwidth_mhz = 20;
};

struct Plan {
  Layout layout = Layout::kStation;
  bool station = false;
  bool wired = false;   // the Ethernet port is up (a node that is being set up on the s3-eth board is wired too)
  AccessPointPlan ap;
  std::string hostname;
};

// A host name from the settings, or "armor-" + the node id (underscores become hyphens, at most 32 characters, never ending in a hyphen).
inline std::string hostname_for(const config::Settings& s) {
  if (!s.hostname.empty()) return s.hostname;
  std::string name = "armor-";
  for (const char c : s.node_id) name += c == '_' ? '-' : c;
  if (name.size() > 32) name.resize(32);
  while (!name.empty() && name.back() == '-') name.pop_back();
  return name;
}

inline std::string upper(std::string_view text) {
  std::string out(text);
  for (char& c : out) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  return out;
}

inline Plan plan_network(const config::Settings& s, bool setup_mode, std::string_view setup_code, std::string_view mac_tail, unsigned mac_sum) {
  Plan plan;
  plan.hostname = hostname_for(s);
  AccessPointPlan& ap = plan.ap;
  const bool wired = board::kHasEthernet && s.uplink == config::Uplink::kEthernet;
  plan.wired = wired;
  if (setup_mode) {
    ap.enabled = true;
    ap.setup = true;
    ap.ssid = "ARMOR-SETUP-" + upper(mac_tail);
    ap.password = std::string(setup_code);
    ap.security = config::WifiSecurity::kWpa2;
    ap.max_clients = 4;
  } else {
    plan.station = !wired && s.sta.enabled;
    if (s.ap.enabled) {
      ap.enabled = true;
      ap.ssid = s.ap.ssid;
      ap.password = s.ap.password;
      ap.security = s.ap.security;
      ap.hidden = s.ap.hidden;
      ap.max_clients = s.ap.max_clients;
    }
  }
  if (ap.enabled) {
    ap.channel = config::effective_channel(s.ap, mac_sum);
    ap.country = s.ap.country;
    ap.tx_power_dbm = s.ap.tx_power_dbm;
    ap.bandwidth_mhz = s.ap.bandwidth_mhz;
  }
  if (setup_mode) plan.layout = Layout::kSetupAp;
  else if (wired) plan.layout = ap.enabled ? Layout::kEthernetWithAp : Layout::kEthernet;
  else if (plan.station) plan.layout = ap.enabled ? Layout::kStationWithAp : Layout::kStation;
  else plan.layout = Layout::kAccessPoint;
  return plan;
}

inline const char* to_text(Layout layout) {
  switch (layout) {
    case Layout::kSetupAp: return "setup-ap";
    case Layout::kStation: return "wifi-station";
    case Layout::kStationWithAp: return "wifi-station+ap";
    case Layout::kAccessPoint: return "wifi-ap";
    case Layout::kEthernet: return "ethernet";
    case Layout::kEthernetWithAp: return "ethernet+ap";
  }
  return "wifi-station";
}

}  // namespace armor::netplan
