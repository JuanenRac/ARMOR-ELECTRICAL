// ARMOR-ELECTRICAL - the node's network: the Ethernet port (the s3-eth board), the Wi-Fi station, the Wi-Fi access point, or a mix.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <string>
#include <vector>

#include "core/netplan.hpp"
#include "core/electrical_config.hpp"

namespace armor::network {

struct Status {
  std::string layout;                 // see netplan::to_text
  bool link_up = false;               // the Ethernet cable has a link, or the station is associated with its network
  bool ethernet_available = false;    // this board has an Ethernet port (the s3-eth profile)
  bool ethernet_ok = true;            // false: the W5500 did not answer
  std::string board;                  // "s3-wifi" or "s3-eth"
  bool has_ip = false;
  std::string ip, netmask, gateway, dns, mac;
  bool ap_active = false;
  bool ap_setup = false;              // the setup access point of a node that has no user yet
  std::string ap_ssid;
  int ap_channel = 0;
  int ap_clients = 0;
  bool sta_connected = false;
  std::string sta_ssid;
  int sta_rssi = 0;
};

// Builds the network from the settings and starts it. Returns false only when nothing at all could be started; the node then keeps reading its
// equipment and says why on the console.
bool start(const config::Settings& settings, const netplan::Plan& plan);

// Whether the node can be reached: the station has an address, or an access point is up (its own address is 192.168.4.1).
bool has_ip();
bool reachable();
Status status();

// One Wi-Fi network heard by a search.
struct ScanEntry {
  std::string ssid;
  int rssi = 0;
  int channel = 0;
  std::string security;   // open, wep, wpa, wpa2, wpa3, wpa2wpa3, enterprise
};
// Searches for Wi-Fi networks (up to 25, strongest first, one line per name). If the radio is running only as an access point it is briefly used as a
// station too (the clients of the access point may notice). False, with a code in `error`, when the radio is busy or the search fails.
bool scan(std::vector<ScanEntry>& out, std::string& error);

}  // namespace armor::network
