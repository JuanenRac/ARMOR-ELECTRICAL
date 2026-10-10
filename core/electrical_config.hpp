// ARMOR-ELECTRICAL - the settings of an electrical node: what the web panel edits, what is stored in flash and what the firmware obeys.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// One document (JSON) holds everything that differs from node to node, so the same firmware image serves every node and nothing secret has to be
// compiled in. This file only reads, checks and writes that document; it touches no hardware, so all of it is tested on a computer. Passwords are never
// written back to the panel: a section sent without a password (or with an empty one) keeps the stored one, and "password_set" tells the panel that
// there is one.
//
// A node has ONE serial line (a hardware UART) with up to sixteen energy meters on it, each with its own address and the channel it makes in the message.
// Nothing here can make a node write to a meter. The base board adds three more things the same document sets: a Zigbee radio on its own serial line that the node hands to
// the network (a serial-to-TCP bridge for Zigbee2MQTT), up to eight relay outputs and up to eight analog inputs (a voltage or a current, read by the ADC).
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "board_s3.hpp"
#include "json.hpp"
#include "net_text.hpp"
#include "node_id.hpp"

namespace armor::config {

constexpr int kVersion = 1;
constexpr std::size_t kMaxPasswordText = 64;
constexpr std::size_t kMeterCount = 16;
constexpr std::size_t kMaxChannels = 16;   // what one message carries: the meters and the analog inputs together

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

// The serial line the meters share (a hardware UART; the PZEM meters talk 9600 baud 8N1).
struct BusSetting {
  int rx = -1;        // the GPIO that receives the meters' TX line
  int tx = -1;        // the GPIO that sends to the meters' RX line
  int baud = 9600;
  int poll_s = 2;     // seconds between two rounds of readings
};

// One meter on the line: which channel of the message it makes, and how to talk to it.
struct MeterSetting {
  bool enabled = false;
  std::string channel;     // the channel id in the message (armor/electrical/<node>/state): lowercase letters, digits, - and _
  std::string label;       // its name, or empty
  std::string model = "ac";   // "ac" (PZEM-004T v3) or "dc" (PZEM-017)
  int address = 1;         // its Modbus address on the line (1 to 247)
};

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
  std::string allowed_client;  // the one IPv4 address that may connect: required while the bridge is on
};

// One relay output: the node drives the pin, whatever is behind it (a driver, the relay, the load) is the installer's. Every relay starts open (off) when the node starts.
struct RelaySetting {
  bool enabled = false;
  std::string name;        // lowercase letters, digits, - and _ ; the same rule as a channel
  std::string label;
  int pin = -1;
  bool active_low = false; // the relay closes when the pin is low (an opto-isolated board often does)
};

// One analog input: what the ADC sees on the pin becomes a channel of the node's message.
//   quantity "voltage" or "current": what the sensor measures.   mode "dc": the average, "ac": the effective (RMS) value around the average.
//   scale: the quantity per volt at the pin (a divider of 1:11 is 11 V/V, a 30 A sensor that gives 1 V is 30 A/V). offset_v: the volts the pin shows at zero (a sensor
//   with a mid-supply bias); only for "dc", since "ac" takes it from the signal. zero_below: a reading under it (in the quantity's unit) is shown as 0 (the noise floor).
//   nominal_voltage_v: for a current only, the voltage the circuit is assumed to have, to give a power and an energy (0: none, only the current is published).
struct AnalogSetting {
  bool enabled = false;
  std::string channel;
  std::string label;
  int pin = -1;
  std::string quantity = "voltage";
  std::string mode = "dc";
  double scale = 1.0;
  double offset_v = 0.0;
  double zero_below = 0.0;
  double nominal_voltage_v = 0.0;
};

// What time the node believes it is: a time server on the Internet (or the browser's clock when that is off) and the zone the local time is
// shown in. The zone is a POSIX TZ rule, so summer time changes by itself ("CET-1CEST,M3.5.0,M10.5.0/3" is Spain); "UTC0" is no offset.
struct Clock {
  bool ntp_enabled = true;
  std::string ntp = "pool.ntp.org";
  std::string zone = "UTC0";
};

struct Settings {
  std::string node_id;
  std::string node_name;
  Uplink uplink = board::kHasEthernet ? Uplink::kEthernet : Uplink::kWifi;   // the board's own way in until the panel says otherwise
  IpSettings ip;
  std::string hostname;  // empty: "armor-" + the node id
  AccessPoint ap;
  Station sta;
  Mqtt mqtt;
  Clock time;
  BusSetting bus;
  std::array<MeterSetting, kMeterCount> meters;
  ZigbeeSetting zigbee;
  std::array<RelaySetting, board::kRelayCount> relays;
  std::array<AnalogSetting, board::kAnalogCount> analogs;
  WebMode web = WebMode::kBoth;
  BleMode ble = BleMode::kSetup;   // "setup": only while the node has no user; "always"; "off": the Bluetooth stack is not even started
  std::string language = "en";
  // A periodic, unconditional restart (disconnect_before_restart() then esp_restart()), independent of any fault: 0 means never. One of
  // {0, 1, 2, 3, 4, 6, 12, 24, 48} hours (auto_restart_hours_is_valid()).
  int auto_restart_hours = 0;
};

struct Problem {
  std::string path;  // "ports.2.baud"
  std::string code;  // "required", "too_long", "range", "invalid", "conflict", "reserved" ...
};
using Problems = std::vector<Problem>;

inline const char* to_text(WifiSecurity v) {
  switch (v) { case WifiSecurity::kOpen: return "open"; case WifiSecurity::kWpa2: return "wpa2"; case WifiSecurity::kWpa3: return "wpa3"; case WifiSecurity::kWpa2Wpa3: return "wpa2wpa3"; }
  return "wpa2";
}
inline const char* to_text(Uplink v) { return v == Uplink::kEthernet ? "ethernet" : "wifi"; }
inline const char* to_text(BleMode v) { return v == BleMode::kAlways ? "always" : v == BleMode::kOff ? "off" : "setup"; }
inline const char* to_text(WebMode v) { return v == WebMode::kHttps ? "https" : v == WebMode::kHttp ? "http" : "both"; }

// The speeds the line may use.
inline const std::vector<int>& allowed_bauds() {
  static const std::vector<int> speeds{1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
  return speeds;
}

// The speeds the Zigbee radio's firmware may use (a Z-Stack coordinator: 115200; the maker's own firmware: 230400).
inline const std::vector<int>& zigbee_bauds() {
  static const std::vector<int> speeds{115200, 230400};
  return speeds;
}

// ---- the defaults of a node that has never been configured ---------------------------------------------------------------------

// `mac_tail` is the last three bytes of the node's MAC as six lowercase hexadecimal digits: it makes the first identity unique.
inline Settings default_settings(std::string_view mac_tail) {
  Settings s;
  s.node_id = "electrical-" + std::string(mac_tail);
  s.node_name = s.node_id;
  s.bus.rx = board::kDefaultPins[0].rx;
  s.bus.tx = board::kDefaultPins[0].tx;
  for (std::size_t i = 0; i < kMeterCount; ++i) s.meters[i].channel = "meter" + std::to_string(i + 1);
  s.zigbee.rx = board::kZigbeePins.rx; s.zigbee.tx = board::kZigbeePins.tx; s.zigbee.reset = board::kZigbeePins.reset; s.zigbee.bootloader = board::kZigbeePins.bootloader;
  for (std::size_t i = 0; i < board::kRelayCount; ++i) { s.relays[i].name = "relay" + std::to_string(i + 1); s.relays[i].pin = board::kRelayPins[i]; }
  for (std::size_t i = 0; i < board::kAnalogCount; ++i) { s.analogs[i].channel = "analog" + std::to_string(i + 1); s.analogs[i].pin = board::kAnalogPins[i]; }
  return s;
}

inline int effective_channel(const AccessPoint& ap, unsigned mac_sum) {
  if (ap.channel >= 1 && ap.channel <= 13) return ap.channel;
  constexpr int kNonOverlapping[3] = {1, 6, 11};
  return kNonOverlapping[mac_sum % 3];
}

// ---- reading -------------------------------------------------------------------------------------------------------------------

namespace detail {
template <typename E>
bool read_choice(const json::Value& parent, const char* name, std::initializer_list<std::pair<const char*, E>> options, E& target) {
  const json::Value* member = parent.get(name);
  if (member == nullptr) return true;
  if (!member->is_string()) return false;
  for (const auto& option : options) if (member->text == option.first) { target = option.second; return true; }
  return false;
}

inline void bad(Problems& problems, std::string path, const char* code) { problems.push_back({std::move(path), code}); }

inline void read_text(const json::Value& parent, const char* name, std::string& target, std::size_t longest, const std::string& path, Problems& problems) {
  const json::Value* member = parent.get(name);
  if (member == nullptr) return;
  if (!member->is_string()) { bad(problems, path, "invalid"); return; }
  if (member->text.size() > longest) { bad(problems, path, "too_long"); return; }
  target = member->text;
}

// A secret: absent or empty keeps the stored one; "<name>_clear": true erases it.
inline void read_secret(const json::Value& parent, const char* name, std::string& target, const std::string& path, Problems& problems) {
  if (parent.bool_or(std::string(name) + "_clear", false)) { target.clear(); return; }
  const json::Value* member = parent.get(name);
  if (member == nullptr) return;
  if (!member->is_string()) { bad(problems, path, "invalid"); return; }
  if (member->text.empty()) return;
  if (member->text.size() > kMaxPasswordText) { bad(problems, path, "too_long"); return; }
  target = member->text;
}

inline void read_bool(const json::Value& parent, const char* name, bool& target, const std::string& path, Problems& problems) {
  const json::Value* member = parent.get(name);
  if (member == nullptr) return;
  if (!member->is_bool()) { bad(problems, path, "invalid"); return; }
  target = member->boolean;
}

inline void read_double(const json::Value& parent, const char* name, double& target, double lowest, double highest, const std::string& path, Problems& problems) {
  const json::Value* member = parent.get(name);
  if (member == nullptr) return;
  if (!member->is_number()) { bad(problems, path, "invalid"); return; }
  if (!(member->number >= lowest && member->number <= highest)) { bad(problems, path, "range"); return; }   // also false for NaN
  target = member->number;
}

inline void read_int(const json::Value& parent, const char* name, int& target, long long lowest, long long highest, const std::string& path, Problems& problems) {
  const json::Value* member = parent.get(name);
  if (member == nullptr) return;
  if (!member->is_number()) { bad(problems, path, "invalid"); return; }
  const long long value = parent.integer_or(name, lowest - 1, lowest, highest);
  if (value < lowest || value > highest) { bad(problems, path, "range"); return; }
  target = static_cast<int>(value);
}
}  // namespace detail

// Applies the members present in `document` on top of `settings`; whatever the document omits stays as it was. The result is checked
// with validate(): read_settings() only reports a member of the wrong type or size.
inline void read_settings(const json::Value& document, Settings& s, Problems& problems) {
  using namespace detail;
  if (!document.is_object()) { bad(problems, "", "invalid"); return; }
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
  if (const json::Value* bus = document.get("bus"); bus != nullptr && bus->is_object()) {
    read_int(*bus, "rx", s.bus.rx, -1, board::kLastGpio, "bus.rx", problems);
    read_int(*bus, "tx", s.bus.tx, -1, board::kLastGpio, "bus.tx", problems);
    read_int(*bus, "baud", s.bus.baud, 1200, 115200, "bus.baud", problems);
    read_int(*bus, "poll_s", s.bus.poll_s, 1, 3600, "bus.poll_s", problems);
  }
  if (const json::Value* meters = document.get("meters"); meters != nullptr) {
    if (!meters->is_array() || meters->items.size() > kMeterCount) bad(problems, "meters", "invalid");
    else for (std::size_t i = 0; i < meters->items.size(); ++i) {
      const json::Value& item = meters->items[i];
      const std::string base = "meters." + std::to_string(i) + ".";
      if (!item.is_object()) { bad(problems, base + "enabled", "invalid"); continue; }
      MeterSetting& meter = s.meters[i];
      read_bool(item, "enabled", meter.enabled, base + "enabled", problems);
      read_text(item, "channel", meter.channel, 32, base + "channel", problems);
      read_text(item, "label", meter.label, 40, base + "label", problems);
      read_text(item, "model", meter.model, 4, base + "model", problems);
      read_int(item, "address", meter.address, 1, 247, base + "address", problems);
    }
  }
  if (const json::Value* zigbee = document.get("zigbee"); zigbee != nullptr && zigbee->is_object()) {
    read_bool(*zigbee, "enabled", s.zigbee.enabled, "zigbee.enabled", problems);
    read_int(*zigbee, "rx", s.zigbee.rx, -1, board::kLastGpio, "zigbee.rx", problems);
    read_int(*zigbee, "tx", s.zigbee.tx, -1, board::kLastGpio, "zigbee.tx", problems);
    read_int(*zigbee, "reset", s.zigbee.reset, -1, board::kLastGpio, "zigbee.reset", problems);
    read_int(*zigbee, "bootloader", s.zigbee.bootloader, -1, board::kLastGpio, "zigbee.bootloader", problems);
    read_int(*zigbee, "baud", s.zigbee.baud, 1200, 1000000, "zigbee.baud", problems);
    read_int(*zigbee, "port", s.zigbee.port, 1, 65535, "zigbee.port", problems);
    read_text(*zigbee, "allowed_client", s.zigbee.allowed_client, 15, "zigbee.allowed_client", problems);
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
  if (const json::Value* analogs = document.get("analogs"); analogs != nullptr) {
    if (!analogs->is_array() || analogs->items.size() > board::kAnalogCount) bad(problems, "analogs", "invalid");
    else for (std::size_t i = 0; i < analogs->items.size(); ++i) {
      const json::Value& item = analogs->items[i];
      const std::string base = "analogs." + std::to_string(i) + ".";
      if (!item.is_object()) { bad(problems, base + "enabled", "invalid"); continue; }
      AnalogSetting& input = s.analogs[i];
      read_bool(item, "enabled", input.enabled, base + "enabled", problems);
      read_text(item, "channel", input.channel, 32, base + "channel", problems);
      read_text(item, "label", input.label, 40, base + "label", problems);
      read_int(item, "pin", input.pin, -1, board::kLastGpio, base + "pin", problems);
      read_text(item, "quantity", input.quantity, 8, base + "quantity", problems);
      read_text(item, "mode", input.mode, 4, base + "mode", problems);
      read_double(item, "scale", input.scale, -100000.0, 100000.0, base + "scale", problems);
      read_double(item, "offset_v", input.offset_v, -5.0, 5.0, base + "offset_v", problems);
      read_double(item, "zero_below", input.zero_below, 0.0, 100000.0, base + "zero_below", problems);
      read_double(item, "nominal_voltage_v", input.nominal_voltage_v, 0.0, 1000.0, base + "nominal_voltage_v", problems);
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

// ---- checking ------------------------------------------------------------------------------------------------------------------

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

// A device name: it becomes a piece of an MQTT topic (the same rule as the contract's device name).
inline bool valid_device_name(std::string_view name) {
  if (name.empty() || name.size() > 32 || name.front() == '-' || name.front() == '_') return false;
  for (const char c : name) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  return true;
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

namespace detail {
// The pins already claimed, to catch two uses of one GPIO.
struct PinClaims {
  std::array<std::string, board::kLastGpio + 1> owner;
  bool claim(int gpio, const std::string& who) {
    if (gpio < 0 || gpio > board::kLastGpio) return true;
    if (!owner[static_cast<std::size_t>(gpio)].empty()) return false;
    owner[static_cast<std::size_t>(gpio)] = who;
    return true;
  }
};
}  // namespace detail

inline Problems validate(const Settings& s) {
  using detail::bad;
  Problems problems;
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

  // the line and its meters: the pins may not be reserved nor shared, each meter has its own channel and its own address
  detail::PinClaims claims;
  const auto claim = [&](int gpio, const std::string& who, const std::string& path) {
    if (!board::assignable(gpio)) { bad(problems, path, "reserved"); return; }
    if (!claims.claim(gpio, who)) bad(problems, path, "conflict");
  };
  std::vector<std::string> channels;
  std::vector<int> addresses;
  bool any = false;
  for (std::size_t i = 0; i < kMeterCount; ++i) {
    const MeterSetting& meter = s.meters[i];
    if (!meter.enabled) continue;
    any = true;
    const std::string base = "meters." + std::to_string(i) + ".";
    if (!valid_device_name(meter.channel)) bad(problems, base + "channel", meter.channel.empty() ? "required" : "invalid");
    else if (std::find(channels.begin(), channels.end(), meter.channel) != channels.end()) bad(problems, base + "channel", "conflict");
    else channels.push_back(meter.channel);
    if (meter.model != "ac" && meter.model != "dc") bad(problems, base + "model", "invalid");
    if (meter.address < 1 || meter.address > 247) bad(problems, base + "address", "range");
    else if (std::find(addresses.begin(), addresses.end(), meter.address) != addresses.end()) bad(problems, base + "address", "conflict");
    else addresses.push_back(meter.address);
    std::size_t label_characters = 0;
    if (!meter.label.empty() && (!net::valid_utf8(meter.label, &label_characters) || label_characters > 40)) bad(problems, base + "label", "invalid");
  }
  if (any) {
    if (s.bus.rx < 0) bad(problems, "bus.rx", "required"); else claim(s.bus.rx, "bus", "bus.rx");
    if (s.bus.tx < 0) bad(problems, "bus.tx", "required"); else claim(s.bus.tx, "bus", "bus.tx");
    if (std::find(allowed_bauds().begin(), allowed_bauds().end(), s.bus.baud) == allowed_bauds().end()) bad(problems, "bus.baud", "range");
    if (s.bus.poll_s < 1 || s.bus.poll_s > 3600) bad(problems, "bus.poll_s", "range");
  }

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

  // the analog inputs: a channel of the message each, so the names are shared with the meters' and the message has room for all
  std::size_t analog_count = 0;
  for (std::size_t i = 0; i < board::kAnalogCount; ++i) {
    const AnalogSetting& input = s.analogs[i];
    if (!input.enabled) continue;
    ++analog_count;
    const std::string base = "analogs." + std::to_string(i) + ".";
    if (!valid_device_name(input.channel)) bad(problems, base + "channel", input.channel.empty() ? "required" : "invalid");
    else if (std::find(channels.begin(), channels.end(), input.channel) != channels.end()) bad(problems, base + "channel", "conflict");
    else channels.push_back(input.channel);
    if (input.pin < 0) bad(problems, base + "pin", "required");
    else if (!board::analog_capable(input.pin)) bad(problems, base + "pin", board::assignable(input.pin) ? "not_analog" : "reserved");
    else if (!claims.claim(input.pin, "analog" + std::to_string(i + 1))) bad(problems, base + "pin", "conflict");
    if (input.quantity != "voltage" && input.quantity != "current") bad(problems, base + "quantity", "invalid");
    if (input.mode != "dc" && input.mode != "ac") bad(problems, base + "mode", "invalid");
    if (input.scale == 0.0) bad(problems, base + "scale", "range");
    if (input.mode == "ac" && input.offset_v != 0.0) bad(problems, base + "offset_v", "invalid");   // an alternating signal is centred by itself
    if (input.nominal_voltage_v != 0.0 && input.quantity != "current") bad(problems, base + "nominal_voltage_v", "invalid");
    std::size_t label_characters = 0;
    if (!input.label.empty() && (!net::valid_utf8(input.label, &label_characters) || label_characters > 40)) bad(problems, base + "label", "invalid");
  }
  std::size_t meter_count = 0;
  for (const MeterSetting& meter : s.meters) if (meter.enabled) ++meter_count;
  if (meter_count + analog_count > kMaxChannels) bad(problems, "analogs", "too_many");
  return problems;
}

// ---- writing -------------------------------------------------------------------------------------------------------------------

// `secrets`: true writes the passwords (for flash storage); false replaces them with "password_set" flags (for the panel).
inline std::string to_json(const Settings& s, bool secrets) {
  json::Writer w;
  w.begin_object();
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
  w.key("bus").begin_object().field("rx", s.bus.rx).field("tx", s.bus.tx).field("baud", s.bus.baud).field("poll_s", s.bus.poll_s).end_object();
  w.key("meters").begin_array();
  for (const MeterSetting& meter : s.meters) {
    w.begin_object().field("enabled", meter.enabled).field("channel", meter.channel).field("label", meter.label).field("model", meter.model).field("address", meter.address).end_object();
  }
  w.end_array();
  w.key("zigbee").begin_object().field("enabled", s.zigbee.enabled).field("rx", s.zigbee.rx).field("tx", s.zigbee.tx).field("reset", s.zigbee.reset).field("bootloader", s.zigbee.bootloader)
      .field("baud", s.zigbee.baud).field("port", s.zigbee.port).field("allowed_client", s.zigbee.allowed_client).end_object();
  w.key("relays").begin_array();
  for (const RelaySetting& relay : s.relays) {
    w.begin_object().field("enabled", relay.enabled).field("name", relay.name).field("label", relay.label).field("pin", relay.pin).field("active_low", relay.active_low).end_object();
  }
  w.end_array();
  w.key("analogs").begin_array();
  for (const AnalogSetting& input : s.analogs) {
    w.begin_object().field("enabled", input.enabled).field("channel", input.channel).field("label", input.label).field("pin", input.pin).field("quantity", input.quantity).field("mode", input.mode)
        .key("scale").number(input.scale).key("offset_v").number(input.offset_v).key("zero_below").number(input.zero_below).key("nominal_voltage_v").number(input.nominal_voltage_v).end_object();
  }
  w.end_array();
  w.key("web").begin_object().field("mode", to_text(s.web)).end_object();
  w.key("ble").begin_object().field("mode", to_text(s.ble)).end_object();
  w.key("ui").begin_object().field("language", s.language).end_object();
  w.key("time").begin_object().field("ntp_enabled", s.time.ntp_enabled).field("ntp", s.time.ntp).field("zone", s.time.zone).end_object();
  w.key("system").begin_object().field("auto_restart_hours", s.auto_restart_hours).end_object();
  w.end_object();
  return w.str();
}

// Reads a stored or received document on top of `base` and checks the result. Nothing is applied unless `problems` stays empty.
inline bool load(std::string_view text, const Settings& base, Settings& out, Problems& problems) {
  json::Value document;
  if (!json::parse(text, document)) { problems.push_back({"", "not_json"}); return false; }
  out = base;
  read_settings(document, out, problems);
  if (problems.empty()) problems = validate(out);
  return problems.empty();
}

}  // namespace armor::config
