// A.R.M.O.R. - the settings every field node has, whatever it measures or guards: its identity, the way in (Ethernet or Wi-Fi), the access point, the broker, the clock, the panel's mode, Bluetooth,
// the language and the periodic restart. Read, checked and written here, once, for the electrical node and the alarm node (and for any other that adopts it).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// One document (JSON) holds everything that differs from node to node, so the same firmware image serves every node and nothing secret has to be compiled in. This file only reads, checks and
// writes that document; it touches no hardware, so all of it is tested on a computer. Passwords are never written back to the panel: a section sent without a password (or with an empty one)
// keeps the stored one, and "password_set" tells the panel that there is one.
#pragma once
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "board_s3.hpp"
#include "config_common.hpp"
#include "json.hpp"
#include "net_text.hpp"
#include "node_id.hpp"

namespace armor::config {

constexpr int kVersion = 1;

enum class WifiSecurity { kOpen, kWpa2, kWpa3, kWpa2Wpa3 };
// The panel over plain HTTP only, over HTTP and HTTPS (a certificate the node made for itself), or over HTTPS only (port 80 sends the browser to HTTPS).
enum class WebMode { kHttp, kBoth, kHttps };
enum class BleMode { kOff, kSetup, kAlways };   // when the node listens to a phone over Bluetooth
// How the node reaches the network: over the Ethernet cable (only the s3-eth board has one) or as a Wi-Fi station.
enum class Uplink { kWifi, kEthernet };

// The address of the Ethernet port: DHCP, or a fixed address, mask, gateway and DNS.
struct IpSettings {
  bool dhcp = true;
  std::string address, netmask = "255.255.255.0", gateway, dns1, dns2;
};

struct AccessPoint {
  bool enabled = false;
  std::string ssid = "ARMOR-ELECTRICAL";
  WifiSecurity security = WifiSecurity::kWpa2;
  std::string password;
  int channel = 0;  // 0: one of 1, 6 or 11 chosen from the node's MAC
  bool hidden = false;
  int max_clients = 8;
  int tx_power_dbm = 15;
  int bandwidth_mhz = 20;
  std::string country = "ES";  // two capital letters: sets which channels and how much power the radio may use
};

struct Network { std::string ssid, password; };
constexpr std::size_t kMaxBackupNetworks = 3;

struct Station {
  bool enabled = false;
  std::string ssid, password;
  // Tried in order, after the network above, whenever the current one cannot be joined for a while (main/network.cpp); never while the
  // network above still works. The same Wi-Fi password rules apply to each.
  std::vector<Network> backup;
};

struct Broker { std::string uri, username, password; };
constexpr std::size_t kMaxBackupBrokers = 2;

struct Mqtt {
  bool enabled = false;  // a node that was never configured has no broker yet
  std::string uri, username, password;
  int heartbeat_s = 10;  // the keep-alive of the connection is twice this
  // Tried in order, after the broker above, whenever it cannot be reached for a while (main/mqtt_link.cpp); never while it still works.
  std::vector<Broker> backup;
};

// What time the node believes it is: a time server on the Internet (or the browser's clock when that is off) and the zone the local time is
// shown in. The zone is a POSIX TZ rule, so summer time changes by itself ("CET-1CEST,M3.5.0,M10.5.0/3" is Spain); "UTC0" is no offset.
struct Clock {
  bool ntp_enabled = true;
  std::string ntp = "pool.ntp.org";
  std::string zone = "UTC0";
};

struct NodeSettings {
  std::string node_id;
  std::string node_name;
  Uplink uplink = board::kHasEthernet ? Uplink::kEthernet : Uplink::kWifi;   // the board's own way in until the panel says otherwise
  IpSettings ip;
  std::string hostname;  // empty: "armor-" + the node id
  AccessPoint ap;
  Station sta;
  Mqtt mqtt;
  Clock time;
  WebMode web = WebMode::kBoth;
  BleMode ble = BleMode::kSetup;   // "setup": only while the node has no user; "always"; "off": the Bluetooth stack is not even started
  std::string language = "en";
  // A periodic, unconditional restart (disconnect_before_restart() then esp_restart()), independent of any fault: 0 means never. One of
  // {0, 1, 2, 3, 4, 6, 12, 24, 48} hours (auto_restart_hours_is_valid()).
  int auto_restart_hours = 0;
};

inline const char* to_text(WifiSecurity v) {
  switch (v) { case WifiSecurity::kOpen: return "open"; case WifiSecurity::kWpa2: return "wpa2"; case WifiSecurity::kWpa3: return "wpa3"; case WifiSecurity::kWpa2Wpa3: return "wpa2wpa3"; }
  return "wpa2";
}
inline const char* to_text(Uplink v) { return v == Uplink::kEthernet ? "ethernet" : "wifi"; }
inline const char* to_text(BleMode v) { return v == BleMode::kAlways ? "always" : v == BleMode::kOff ? "off" : "setup"; }
inline const char* to_text(WebMode v) { return v == WebMode::kHttps ? "https" : v == WebMode::kHttp ? "http" : "both"; }


inline int effective_channel(const AccessPoint& ap, unsigned mac_sum) {
  if (ap.channel >= 1 && ap.channel <= 13) return ap.channel;
  constexpr int kNonOverlapping[3] = {1, 6, 11};
  return kNonOverlapping[mac_sum % 3];
}


// A POSIX TZ rule: letters, digits, signs, commas, dots, colons, slashes and angle brackets, nothing else (it goes to setenv()).
inline bool time_zone_is_valid(std::string_view zone) {
  if (zone.empty() || zone.size() > 48) return false;
  for (const char c : zone) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == ',' || c == '.' || c == ':' || c == '/' || c == '<' || c == '>';
    if (!ok) return false;
  }
  return true;
}

inline bool language_is_known(std::string_view code) {
  for (const char* known : {"en", "es", "de", "fr", "it", "ja", "zh"}) if (code == known) return true;
  return false;
}

inline bool auto_restart_hours_is_valid(int hours) {
  for (const int known : {0, 1, 2, 3, 4, 6, 12, 24, 48}) if (hours == known) return true;
  return false;
}

inline bool broker_uri_is_valid(std::string_view uri) {
  std::string_view rest;
  if (uri.substr(0, 7) == "mqtt://") rest = uri.substr(7);
  else if (uri.substr(0, 8) == "mqtts://") rest = uri.substr(8);
  else return false;
  const std::size_t colon = rest.find(':');
  const std::string_view host = rest.substr(0, colon);
  if (!net::valid_host(host)) return false;
  if (colon == std::string_view::npos) return true;
  const std::string_view port = rest.substr(colon + 1);
  if (port.empty() || port.size() > 5) return false;
  unsigned value = 0;
  for (const char c : port) { if (c < '0' || c > '9') return false; value = value * 10 + static_cast<unsigned>(c - '0'); }
  return value >= 1 && value <= 65535;
}


// `mac_tail` is the last three bytes of the node's MAC as six lowercase hexadecimal digits: it makes the first identity unique.
inline void node_defaults(NodeSettings& n, std::string_view mac_tail) {
  n.node_id = "electrical-" + std::string(mac_tail);
  n.node_name = n.node_id;
}

// ---- reading --------------------------------------------------------------------------------------------------------------------

// Applies the members of a document that every node has (node, uplink, ip, ap, sta, mqtt, web, ble, time, ui, system) on top of `s`; what the document omits stays as it was.
inline void node_read(const json::Value& document, NodeSettings& s, Problems& problems) {
  using namespace detail;
  if (const json::Value* node = document.get("node"); node != nullptr && node->is_object()) {
    read_text(*node, "id", s.node_id, kMaxNodeIdLength, "node.id", problems);
    read_text(*node, "name", s.node_name, 48, "node.name", problems);
    read_text(*node, "hostname", s.hostname, 32, "node.hostname", problems);
  }
  if (document.get("uplink") != nullptr && !read_choice<Uplink>(document, "uplink", {{"wifi", Uplink::kWifi}, {"ethernet", Uplink::kEthernet}}, s.uplink)) bad(problems, "uplink", "invalid");
  if (const json::Value* ip = document.get("ip"); ip != nullptr && ip->is_object()) {
    read_bool(*ip, "dhcp", s.ip.dhcp, "ip.dhcp", problems);
    read_text(*ip, "address", s.ip.address, 15, "ip.address", problems);
    read_text(*ip, "netmask", s.ip.netmask, 15, "ip.netmask", problems);
    read_text(*ip, "gateway", s.ip.gateway, 15, "ip.gateway", problems);
    read_text(*ip, "dns1", s.ip.dns1, 15, "ip.dns1", problems);
    read_text(*ip, "dns2", s.ip.dns2, 15, "ip.dns2", problems);
  }
  if (const json::Value* ap = document.get("ap"); ap != nullptr && ap->is_object()) {
    read_bool(*ap, "enabled", s.ap.enabled, "ap.enabled", problems);
    read_text(*ap, "ssid", s.ap.ssid, 32, "ap.ssid", problems);
    if (!read_choice<WifiSecurity>(*ap, "security", {{"open", WifiSecurity::kOpen}, {"wpa2", WifiSecurity::kWpa2}, {"wpa3", WifiSecurity::kWpa3}, {"wpa2wpa3", WifiSecurity::kWpa2Wpa3}}, s.ap.security)) bad(problems, "ap.security", "invalid");
    read_secret(*ap, "password", s.ap.password, "ap.password", problems);
    read_int(*ap, "channel", s.ap.channel, 0, 13, "ap.channel", problems);
    read_bool(*ap, "hidden", s.ap.hidden, "ap.hidden", problems);
    read_int(*ap, "max_clients", s.ap.max_clients, 1, 10, "ap.max_clients", problems);
    read_int(*ap, "tx_power_dbm", s.ap.tx_power_dbm, 2, 20, "ap.tx_power_dbm", problems);
    read_int(*ap, "bandwidth_mhz", s.ap.bandwidth_mhz, 20, 40, "ap.bandwidth_mhz", problems);
    read_text(*ap, "country", s.ap.country, 2, "ap.country", problems);
  }
  if (const json::Value* sta = document.get("sta"); sta != nullptr && sta->is_object()) {
    read_bool(*sta, "enabled", s.sta.enabled, "sta.enabled", problems);
    read_text(*sta, "ssid", s.sta.ssid, 32, "sta.ssid", problems);
    read_secret(*sta, "password", s.sta.password, "sta.password", problems);
    if (const json::Value* backup = sta->get("backup"); backup != nullptr) {
      if (!backup->is_array()) bad(problems, "sta.backup", "invalid");
      // A "backup" the document sends replaces the stored list, never adds to it - a save after removing one in the panel must not
      // leave the one just removed behind (found for real: deleting backup brokers and saving brought them straight back).
      else {
        // The panel never holds a stored password (it only learns that there is one), so an entry that arrives without one is the same
        // network as before and keeps its password; one with a new name is a different network and starts without.
        const std::vector<Network> before = std::move(s.sta.backup);
        s.sta.backup.clear();
        // More than fit is never fatal: an older or hand-edited document with extra entries loses only the ones past the limit, not
        // the whole node (a broker and the rest of the settings have nothing to do with how many backup networks were once saved).
        for (std::size_t i = 0; i < backup->items.size() && i < kMaxBackupNetworks; ++i) {
          const json::Value& item = backup->items[i];
          const std::string base = "sta.backup." + std::to_string(i) + ".";
          Network network;
          if (!item.is_object()) { bad(problems, base + "ssid", "invalid"); continue; }
          read_text(item, "ssid", network.ssid, 32, base + "ssid", problems);
          for (const Network& old : before) if (old.ssid == network.ssid) { network.password = old.password; break; }
          read_secret(item, "password", network.password, base + "password", problems);
          s.sta.backup.push_back(network);
        }
      }
    }
  }
  if (const json::Value* mqtt = document.get("mqtt"); mqtt != nullptr && mqtt->is_object()) {
    read_bool(*mqtt, "enabled", s.mqtt.enabled, "mqtt.enabled", problems);
    read_text(*mqtt, "uri", s.mqtt.uri, 160, "mqtt.uri", problems);
    read_text(*mqtt, "username", s.mqtt.username, 64, "mqtt.username", problems);
    read_secret(*mqtt, "password", s.mqtt.password, "mqtt.password", problems);
    read_int(*mqtt, "heartbeat_s", s.mqtt.heartbeat_s, 2, 300, "mqtt.heartbeat_s", problems);
    read_text(*mqtt, "ntp", s.time.ntp, 64, "mqtt.ntp", problems);   // where older documents kept it
    if (const json::Value* backup = mqtt->get("backup"); backup != nullptr) {
      if (!backup->is_array()) bad(problems, "mqtt.backup", "invalid");
      // A "backup" the document sends replaces the stored list, never adds to it (see sta.backup above - the same bug, found on the
      // broker page: removing backup brokers and saving brought them straight back).
      else {
        // Same as the backup networks: an entry that arrives without a password is the same account as before (same user on the same
        // slot or the same address) and keeps it.
        const std::vector<Broker> before = std::move(s.mqtt.backup);
        s.mqtt.backup.clear();
        // Same as sta.backup above: more entries than fit just lose the extras, not the rest of the node's settings.
        for (std::size_t i = 0; i < backup->items.size() && i < kMaxBackupBrokers; ++i) {
          const json::Value& item = backup->items[i];
          const std::string base = "mqtt.backup." + std::to_string(i) + ".";
          Broker broker;
          if (!item.is_object()) { bad(problems, base + "uri", "invalid"); continue; }
          read_text(item, "uri", broker.uri, 160, base + "uri", problems);
          read_text(item, "username", broker.username, 64, base + "username", problems);
          for (std::size_t k = 0; k < before.size(); ++k) {
            if (before[k].username == broker.username && (k == i || before[k].uri == broker.uri)) { broker.password = before[k].password; break; }
          }
          read_secret(item, "password", broker.password, base + "password", problems);
          s.mqtt.backup.push_back(broker);
        }
      }
    }
  }
  if (const json::Value* web = document.get("web"); web != nullptr && web->is_object()) {
    if (!read_choice<WebMode>(*web, "mode", {{"http", WebMode::kHttp}, {"both", WebMode::kBoth}, {"https", WebMode::kHttps}}, s.web)) bad(problems, "web.mode", "invalid");
  }
  if (const json::Value* ble = document.get("ble"); ble != nullptr && ble->is_object()) {
    if (!read_choice<BleMode>(*ble, "mode", {{"off", BleMode::kOff}, {"setup", BleMode::kSetup}, {"always", BleMode::kAlways}}, s.ble)) bad(problems, "ble.mode", "invalid");
  }
  if (const json::Value* time = document.get("time"); time != nullptr && time->is_object()) {
    read_bool(*time, "ntp_enabled", s.time.ntp_enabled, "time.ntp_enabled", problems);
    read_text(*time, "ntp", s.time.ntp, 64, "time.ntp", problems);
    read_text(*time, "zone", s.time.zone, 48, "time.zone", problems);
  }
  if (const json::Value* ui = document.get("ui"); ui != nullptr && ui->is_object()) read_text(*ui, "language", s.language, 4, "ui.language", problems);
  if (const json::Value* system = document.get("system"); system != nullptr && system->is_object()) read_int(*system, "auto_restart_hours", s.auto_restart_hours, 0, 48, "system.auto_restart_hours", problems);
}

// ---- checking -------------------------------------------------------------------------------------------------------------------

inline void node_validate(const NodeSettings& s, Problems& problems) {
  using detail::bad;
  if (!node_id_is_valid(s.node_id)) bad(problems, "node.id", "invalid");
  std::size_t name_characters = 0;
  if (s.node_name.empty()) bad(problems, "node.name", "required");
  else if (!net::valid_utf8(s.node_name, &name_characters) || name_characters > 48) bad(problems, "node.name", "invalid");
  if (!language_is_known(s.language)) bad(problems, "ui.language", "invalid");
  if (!auto_restart_hours_is_valid(s.auto_restart_hours)) bad(problems, "system.auto_restart_hours", "invalid");
  if (!s.hostname.empty() && !net::valid_hostname(s.hostname)) bad(problems, "node.hostname", "invalid");

  // the way in: the Ethernet cable (only the s3-eth board has one), with DHCP or a fixed address, or Wi-Fi
  if (s.uplink == Uplink::kEthernet && !board::kHasEthernet) bad(problems, "uplink", "not_available");
  if (!s.ip.dhcp) {   // a fixed address is for whichever connection the node uses, the cable or the Wi-Fi station
    std::uint32_t address = 0, mask = 0, gateway = 0, dns = 0;
    const bool address_ok = net::parse_ipv4(s.ip.address, address), mask_ok = net::parse_ipv4(s.ip.netmask, mask) && net::valid_netmask(mask);
    if (!address_ok || !net::usable_host_address(address)) bad(problems, "ip.address", s.ip.address.empty() ? "required" : "invalid");
    if (!mask_ok) bad(problems, "ip.netmask", s.ip.netmask.empty() ? "required" : "invalid");
    if (address_ok && mask_ok && net::is_network_or_broadcast(address, mask)) bad(problems, "ip.address", "invalid");
    if (!net::parse_ipv4(s.ip.gateway, gateway) || !net::usable_host_address(gateway)) bad(problems, "ip.gateway", s.ip.gateway.empty() ? "required" : "invalid");
    else if (address_ok && mask_ok && !net::same_subnet(address, gateway, mask)) bad(problems, "ip.gateway", "outside_subnet");
    else if (address_ok && gateway == address) bad(problems, "ip.gateway", "conflict");
    if (!s.ip.dns1.empty() && !net::parse_ipv4(s.ip.dns1, dns)) bad(problems, "ip.dns1", "invalid");
    if (!s.ip.dns2.empty() && !net::parse_ipv4(s.ip.dns2, dns)) bad(problems, "ip.dns2", "invalid");
  }

  // Wi-Fi: a node on Wi-Fi has no other way in, so at least one of its two ways in must be on
  if (s.ap.enabled) {
    if (!net::valid_ssid(s.ap.ssid)) bad(problems, "ap.ssid", s.ap.ssid.empty() ? "required" : "invalid");
    if (s.ap.security != WifiSecurity::kOpen && !net::valid_wpa_passphrase(s.ap.password)) bad(problems, "ap.password", s.ap.password.empty() ? "required" : "invalid_key");
  }
  if (s.ap.country.size() != 2 || !(s.ap.country[0] >= 'A' && s.ap.country[0] <= 'Z' && s.ap.country[1] >= 'A' && s.ap.country[1] <= 'Z')) bad(problems, "ap.country", "invalid");
  if (s.sta.enabled) {
    if (!net::valid_ssid(s.sta.ssid)) bad(problems, "sta.ssid", s.sta.ssid.empty() ? "required" : "invalid");
    if (!s.sta.password.empty() && !net::valid_wpa_passphrase(s.sta.password)) bad(problems, "sta.password", "invalid_key");
    for (std::size_t i = 0; i < s.sta.backup.size(); ++i) {
      const std::string base = "sta.backup." + std::to_string(i) + ".";
      const Network& network = s.sta.backup[i];
      if (!net::valid_ssid(network.ssid)) bad(problems, base + "ssid", network.ssid.empty() ? "required" : "invalid");
      if (!network.password.empty() && !net::valid_wpa_passphrase(network.password)) bad(problems, base + "password", "invalid_key");
    }
  }
  if (s.uplink == Uplink::kWifi && !s.ap.enabled && !s.sta.enabled) bad(problems, "sta.enabled", "required");

  // clock
  if (s.time.ntp_enabled && !net::valid_host(s.time.ntp)) bad(problems, "time.ntp", "invalid");
  if (!time_zone_is_valid(s.time.zone)) bad(problems, "time.zone", "invalid");

  // broker
  if (s.mqtt.enabled) {
    if (s.mqtt.uri.empty()) bad(problems, "mqtt.uri", "required");
    else if (!broker_uri_is_valid(s.mqtt.uri)) bad(problems, "mqtt.uri", "invalid");
    for (std::size_t i = 0; i < s.mqtt.backup.size(); ++i) {
      const std::string base = "mqtt.backup." + std::to_string(i) + ".";
      if (!broker_uri_is_valid(s.mqtt.backup[i].uri)) bad(problems, base + "uri", s.mqtt.backup[i].uri.empty() ? "required" : "invalid");
    }
  }

}

// ---- writing --------------------------------------------------------------------------------------------------------------------

// The first members of a document (the version, the identity, the way in, the access point, the Wi-Fi and the broker); `secrets`: true writes the passwords (for flash storage), false replaces
// them with "password_set" flags (for the panel). A node writes its own sections between this and node_write_tail().
inline void node_write_head(json::Writer& w, const NodeSettings& s, bool secrets) {
  w.field("v", kVersion);
  w.key("node").begin_object().field("id", s.node_id).field("name", s.node_name).field("hostname", s.hostname).end_object();
  w.field("uplink", to_text(s.uplink));
  w.key("ip").begin_object().field("dhcp", s.ip.dhcp).field("address", s.ip.address).field("netmask", s.ip.netmask).field("gateway", s.ip.gateway)
      .field("dns1", s.ip.dns1).field("dns2", s.ip.dns2).end_object();
  w.key("ap").begin_object().field("enabled", s.ap.enabled).field("ssid", s.ap.ssid).field("security", to_text(s.ap.security));
  if (secrets) w.field("password", s.ap.password); else w.field("password_set", !s.ap.password.empty());
  w.field("channel", s.ap.channel).field("hidden", s.ap.hidden).field("max_clients", s.ap.max_clients).field("tx_power_dbm", s.ap.tx_power_dbm)
      .field("bandwidth_mhz", s.ap.bandwidth_mhz).field("country", s.ap.country).end_object();
  w.key("sta").begin_object().field("enabled", s.sta.enabled).field("ssid", s.sta.ssid);
  if (secrets) w.field("password", s.sta.password); else w.field("password_set", !s.sta.password.empty());
  w.key("backup").begin_array();
  for (const Network& network : s.sta.backup) {
    w.begin_object().field("ssid", network.ssid);
    if (secrets) w.field("password", network.password); else w.field("password_set", !network.password.empty());
    w.end_object();
  }
  w.end_array();
  w.end_object();
  w.key("mqtt").begin_object().field("enabled", s.mqtt.enabled).field("uri", s.mqtt.uri).field("username", s.mqtt.username);
  if (secrets) w.field("password", s.mqtt.password); else w.field("password_set", !s.mqtt.password.empty());
  w.field("heartbeat_s", s.mqtt.heartbeat_s);
  w.key("backup").begin_array();
  for (const Broker& broker : s.mqtt.backup) {
    w.begin_object().field("uri", broker.uri).field("username", broker.username);
    if (secrets) w.field("password", broker.password); else w.field("password_set", !broker.password.empty());
    w.end_object();
  }
  w.end_array();
  w.end_object();
}

// The last members: the panel's mode, Bluetooth, the language, the clock and the periodic restart.
inline void node_write_tail(json::Writer& w, const NodeSettings& s) {
  w.key("web").begin_object().field("mode", to_text(s.web)).end_object();
  w.key("ble").begin_object().field("mode", to_text(s.ble)).end_object();
  w.key("ui").begin_object().field("language", s.language).end_object();
  w.key("time").begin_object().field("ntp_enabled", s.time.ntp_enabled).field("ntp", s.time.ntp).field("zone", s.time.zone).end_object();
  w.key("system").begin_object().field("auto_restart_hours", s.auto_restart_hours).end_object();
}

}  // namespace armor::config
