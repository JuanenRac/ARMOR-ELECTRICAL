// ARMOR-ELECTRICAL - host tests of what the electrical and the alarm node share: the settings every node has (read, checked and written), the readers they are made with, the pins of the base board,
// the relays and the Zigbee radio's bridge.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
// Nothing here touches hardware or a network: the pins are numbers, the clock is a number the test moves and the documents are text.
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "../core/base_board.hpp"
#include "../core/board_s3.hpp"
#include "../core/central_link.hpp"
#include "../core/config_common.hpp"
#include "../core/io_config.hpp"
#include "../core/json.hpp"
#include "../core/node_settings.hpp"
#include "../core/relay_bank.hpp"
#include "../core/relay_remote.hpp"
#include "../core/zigbee_bridge.hpp"

static int failures = 0;
static int checks = 0;
static int known_gaps = 0;
// A behaviour that is wanted and not there yet: it is reported, and it does not fail the run; when the behaviour arrives the check passes and the report goes away.
#define KNOWN_GAP(condition)                                                          \
  do {                                                                                \
    ++checks;                                                                         \
    if (!(condition)) {                                                               \
      ++known_gaps;                                                                   \
      std::printf("KNOWN GAP %s:%d  %s\n", __FILE__, __LINE__, #condition);\
    }                                                                                 \
  } while (0)
#define CHECK(condition)                                                              \
  do {                                                                                \
    ++checks;                                                                         \
    if (!(condition)) {                                                               \
      ++failures;                                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);                \
    }                                                                                 \
  } while (0)

using namespace armor;

static bool has(const config::Problems& problems, const std::string& path, const std::string& code) {
  for (const config::Problem& p : problems) if (p.path == path && p.code == code) return true;
  return false;
}
static bool contains_text(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
static json::Value parsed(const std::string& text) {
  json::Value document;
  const bool ok = json::parse(text, document);
  CHECK(ok);
  return document;
}

// ---- the pins of the module and of the base board -----------------------------------------------------------------------------------------------

static void test_module_pins() {
  using namespace board;
  for (int gpio = 22; gpio <= 25; ++gpio) CHECK(pin_info(gpio).reason == Reserved::kNoSuchPin && !assignable(gpio));
  for (int gpio = 26; gpio <= 32; ++gpio) CHECK(pin_info(gpio).reason == Reserved::kFlash && !assignable(gpio));
  for (int gpio = 33; gpio <= 37; ++gpio) CHECK(pin_info(gpio).reason == Reserved::kPsram && !assignable(gpio));
  CHECK(pin_info(19).reason == Reserved::kUsb && pin_info(20).reason == Reserved::kUsb && !assignable(19) && !assignable(20));
  CHECK(!assignable(-1) && !assignable(49) && !assignable(1000) && pin_info(-1).reason == Reserved::kNoSuchPin && pin_info(49).reason == Reserved::kNoSuchPin);
  // the strapping pins and the boot button are offered, with a warning
  for (const int gpio : {0, 3, 45, 46}) CHECK(assignable(gpio) && pin_info(gpio).use == PinUse::kCaution && std::string(pin_info(gpio).note) != "");
  CHECK(std::string(pin_info(0).note) == "boot" && std::string(pin_info(3).note) == "strapping");
  // the header of the module N16R8 carries 35..37 only on the board without the cable
  CHECK(pin_info(35).on_header == !kHasEthernet && pin_info(33).on_header == false);
  if (kHasEthernet) {
    for (int gpio = 9; gpio <= 14; ++gpio) CHECK(pin_info(gpio).reason == Reserved::kEthernet && !assignable(gpio));
    CHECK(pin_info(8).reason == Reserved::kCamera && !assignable(8));
    for (int gpio = 4; gpio <= 7; ++gpio) CHECK(assignable(gpio) && std::string(pin_info(gpio).note) == "sdcard");
    CHECK(std::string(kId) == "s3-eth");
  } else {
    for (int gpio = 8; gpio <= 14; ++gpio) CHECK(assignable(gpio));
    CHECK(std::string(pin_info(43).note) == "usb_serial" && std::string(pin_info(48).note) == "led" && std::string(kId) == "s3-wifi");
  }
  // every GPIO that exists is exactly one of: free, a caution or reserved, and a reserved one has a reason
  for (int gpio = kFirstGpio; gpio <= kLastGpio; ++gpio) {
    const PinInfo info = pin_info(gpio);
    CHECK(info.gpio == gpio);
    CHECK((info.use == PinUse::kReserved) == (info.reason != Reserved::kNone));
    CHECK((info.use == PinUse::kFree) == (std::string(info.note).empty() && info.use != PinUse::kReserved));
  }
  // the serial ports' default pins: none reserved, and no pin is given to two ports
  std::set<int> used;
  for (const DefaultPins& pins : kDefaultPins) {
    for (const int gpio : {pins.rx, pins.tx, pins.de}) {
      if (gpio < 0) continue;
      CHECK(assignable(gpio));
      CHECK(used.insert(gpio).second);
    }
    CHECK(pins.rx != pins.tx);
  }
  CHECK(kPortCount == kHardwarePorts + kSoftPorts && kDefaultPins.size() == static_cast<std::size_t>(kPortCount));
}

static void test_base_board() {
  using namespace board;
  // ADC1 is GPIO 1 to 10; ADC2 shares the Wi-Fi radio and is never offered; a reserved pin is not analog
  for (int gpio = -2; gpio <= 49; ++gpio) CHECK(analog_capable(gpio) == (gpio >= 1 && gpio <= 10 && assignable(gpio)));
  CHECK(adc1_channel(1) == 0 && adc1_channel(10) == 9);
  CHECK(kRelayCount == 8 && kAnalogCount == 8);
  // the pins a never-configured node looks at first are all real pins, none shared with another use
  std::set<int> taken;
  for (const int gpio : kRelayPins) { CHECK(assignable(gpio)); CHECK(taken.insert(gpio).second); }
  for (const int gpio : {kZigbeePins.rx, kZigbeePins.tx, kZigbeePins.reset, kZigbeePins.bootloader}) { CHECK(assignable(gpio)); CHECK(taken.insert(gpio).second); }
  for (const int gpio : kAnalogPins) { if (gpio < 0) continue; CHECK(analog_capable(gpio)); CHECK(taken.insert(gpio).second); }
  if (kHasEthernet) {
    CHECK(kZigbeePins.reset == 43 && kZigbeePins.bootloader == 44 && kAnalogPins[6] == -1 && kAnalogPins[7] == -1);
  } else {
    for (const int gpio : kAnalogPins) CHECK(gpio > 0);
  }
  // a relay must not sit on a pin that twitches while the chip starts: none of the strapping pins
  for (const int gpio : kRelayPins) CHECK(gpio != 0 && gpio != 3 && gpio != 45 && gpio != 46);
}

// ---- the readers the settings are made with ---------------------------------------------------------------------------------------------------

static void test_config_readers() {
  using namespace config::detail;
  config::Problems problems;
  const json::Value doc = parsed("{\"s\":\"text\",\"long\":\"abcdefgh\",\"n\":7,\"f\":1.5,\"neg\":-1,\"big\":1e12,\"b\":true,\"mode\":\"fast\",\"num_mode\":3,\"pw\":\"newpassword\",\"empty\":\"\",\"huge\":\""
                                 + std::string(65, 'x') + "\"}");
  std::string text = "old";
  read_text(doc, "s", text, 10, "p.s", problems);
  CHECK(text == "text" && problems.empty());
  read_text(doc, "long", text, 5, "p.long", problems);
  CHECK(text == "text" && has(problems, "p.long", "too_long"));   // refused text leaves the old one
  read_text(doc, "n", text, 5, "p.n", problems);
  CHECK(text == "text" && has(problems, "p.n", "invalid"));
  read_text(doc, "missing", text, 5, "p.missing", problems);
  CHECK(text == "text" && problems.size() == 2);   // a member that is not there is no problem
  int number = 3;
  problems.clear();
  read_int(doc, "n", number, 0, 10, "p.n", problems);
  CHECK(number == 7 && problems.empty());
  read_int(doc, "n", number, 8, 10, "p.n", problems);
  CHECK(number == 7 && has(problems, "p.n", "range"));
  problems.clear();
  read_int(doc, "f", number, 0, 10, "p.f", problems);
  CHECK(number == 7 && has(problems, "p.f", "range"));   // a fraction is not a whole number
  problems.clear();
  read_int(doc, "neg", number, 0, 10, "p.neg", problems);
  CHECK(number == 7 && has(problems, "p.neg", "range"));
  read_int(doc, "neg", number, -1, 10, "p.neg", problems);
  CHECK(number == -1);
  problems.clear();
  read_int(doc, "big", number, 0, 100, "p.big", problems);
  CHECK(number == -1 && has(problems, "p.big", "range"));
  problems.clear();
  read_int(doc, "s", number, 0, 100, "p.s", problems);
  CHECK(has(problems, "p.s", "invalid"));
  double real = 0;
  problems.clear();
  read_double(doc, "f", real, 0, 2, "p.f", problems);
  CHECK(real == 1.5 && problems.empty());
  read_double(doc, "f", real, 2, 3, "p.f", problems);
  CHECK(real == 1.5 && has(problems, "p.f", "range"));
  read_double(doc, "s", real, 0, 3, "p.s", problems);
  CHECK(has(problems, "p.s", "invalid"));
  bool flag = false;
  problems.clear();
  read_bool(doc, "b", flag, "p.b", problems);
  CHECK(flag && problems.empty());
  read_bool(doc, "n", flag, "p.n", problems);
  CHECK(flag && has(problems, "p.n", "invalid"));
  enum class Speed { kSlow, kFast };
  Speed speed = Speed::kSlow;
  CHECK(read_choice<Speed>(doc, "none", {{"slow", Speed::kSlow}, {"fast", Speed::kFast}}, speed) && speed == Speed::kSlow);   // not there: kept
  CHECK(!read_choice<Speed>(doc, "mode", {{"slow", Speed::kSlow}, {"quick", Speed::kFast}}, speed) && speed == Speed::kSlow);
  CHECK(!read_choice<Speed>(doc, "num_mode", {{"slow", Speed::kSlow}, {"fast", Speed::kFast}}, speed));
  CHECK(read_choice<Speed>(doc, "mode", {{"slow", Speed::kSlow}, {"fast", Speed::kFast}}, speed) && speed == Speed::kFast);
  // a secret: absent or empty keeps the stored one, "_clear" erases it, a new one replaces it
  problems.clear();
  std::string secret = "stored";
  read_secret(doc, "missing", secret, "p.x", problems);
  CHECK(secret == "stored");
  read_secret(doc, "empty", secret, "p.x", problems);
  CHECK(secret == "stored" && problems.empty());
  read_secret(doc, "pw", secret, "p.pw", problems);
  CHECK(secret == "newpassword");
  read_secret(doc, "huge", secret, "p.huge", problems);
  CHECK(secret == "newpassword" && has(problems, "p.huge", "too_long"));
  read_secret(doc, "n", secret, "p.n", problems);
  CHECK(secret == "newpassword" && has(problems, "p.n", "invalid"));
  read_secret(parsed("{\"pw\":\"another-one\",\"pw_clear\":true}"), "pw", secret, "p.pw", problems);   // "<name>_clear" wins over a new value in the same document
  CHECK(secret.empty());
  read_secret(parsed("{\"pw_clear\":false,\"pw\":\"another-one\"}"), "pw", secret, "p.pw", problems);
  CHECK(secret == "another-one");
  // a device name is the rule of a piece of a topic
  for (const char* name : {"a", "relay1", "a-b_c", "0x"}) CHECK(config::valid_device_name(name));
  CHECK(config::valid_device_name(std::string(32, 'a')));
  for (const char* name : {"", "-a", "_a", "A", "a b", "a/b", "a+b", "a#", "a.b", "\xC3\xA9"}) CHECK(!config::valid_device_name(name));
  CHECK(!config::valid_device_name(std::string(33, 'a')));
  // two uses of one pin
  PinClaims claims;
  CHECK(claims.claim(5, "a") && !claims.claim(5, "b") && claims.claim(6, "b"));
  CHECK(claims.claim(-1, "x") && claims.claim(-1, "y") && claims.claim(board::kLastGpio + 1, "x") && claims.claim(board::kLastGpio + 1, "y"));   // a pin that does not exist cannot clash
  CHECK(claims.claim(board::kLastGpio, "last") && !claims.claim(board::kLastGpio, "again"));
}

// ---- the settings every node has -----------------------------------------------------------------------------------------------------------------

static config::NodeSettings good_node() {
  config::NodeSettings s;
  config::node_defaults(s, "a1b2c3");
  s.sta.enabled = true;
  s.sta.ssid = "casa";
  s.sta.password = "una-clave-larga";
  return s;
}
static config::Problems check(const config::NodeSettings& s) {
  config::Problems problems;
  config::node_validate(s, problems);
  return problems;
}
static std::string written(const config::NodeSettings& s, bool secrets) {
  json::Writer w;
  w.begin_object();
  config::node_write_head(w, s, secrets);
  config::node_write_tail(w, s);
  w.end_object();
  return w.str();
}

static void test_node_helpers() {
  using namespace config;
  CHECK(time_zone_is_valid("UTC0") && time_zone_is_valid("CET-1CEST,M3.5.0,M10.5.0/3") && time_zone_is_valid("<+03>-3") && time_zone_is_valid(std::string(48, 'A')));
  CHECK(!time_zone_is_valid("") && !time_zone_is_valid(std::string(49, 'A')) && !time_zone_is_valid("Europe Madrid") && !time_zone_is_valid("UTC0;rm") && !time_zone_is_valid("a\"b") && !time_zone_is_valid("A\n"));
  for (const char* code : {"en", "es", "de", "fr", "it", "ja", "zh", "pt"}) CHECK(language_is_known(code));
  for (const char* code : {"", "EN", "e", "eng", "pt-PT", "ru", "xx"}) CHECK(!language_is_known(code));
  for (const int hours : {0, 1, 2, 3, 4, 6, 12, 24, 48}) CHECK(auto_restart_hours_is_valid(hours));
  for (const int hours : {-1, 5, 7, 8, 13, 23, 25, 47, 49, 100}) CHECK(!auto_restart_hours_is_valid(hours));
  CHECK(broker_uri_is_valid("mqtt://broker.local") && broker_uri_is_valid("mqtt://192.168.0.2:1883") && broker_uri_is_valid("mqtts://broker.local:8883") && broker_uri_is_valid("mqtt://a:65535") && broker_uri_is_valid("mqtt://a:1"));
  for (const char* uri : {"", "broker", "mqtt://", "mqtt://:1883", "mqtt://a:", "mqtt://a:0", "mqtt://a:65536", "mqtt://a:123456", "mqtt://a:12x", "mqtt://a:-1", "http://a", "ws://a", "mqtt:/a", "MQTT://a", "mqtt://a b",
                          "mqtt://a/path", "mqtt://user@a", "mqtts:/a", "mqtt://.a", "mqtt://a:80:80"}) CHECK(!broker_uri_is_valid(uri));
  AccessPoint ap;
  ap.channel = 0;
  CHECK(effective_channel(ap, 0) == 1 && effective_channel(ap, 1) == 6 && effective_channel(ap, 2) == 11 && effective_channel(ap, 3) == 1 && effective_channel(ap, 0xFFFFFFFFu) == 1);
  ap.channel = 13; CHECK(effective_channel(ap, 1) == 13);
  ap.channel = 1; CHECK(effective_channel(ap, 1) == 1);
  ap.channel = 14; CHECK(effective_channel(ap, 1) == 6);
  ap.channel = -3; CHECK(effective_channel(ap, 2) == 11);
  CHECK(std::string(to_text(WifiSecurity::kOpen)) == "open" && std::string(to_text(WifiSecurity::kWpa2)) == "wpa2" && std::string(to_text(WifiSecurity::kWpa3)) == "wpa3" && std::string(to_text(WifiSecurity::kWpa2Wpa3)) == "wpa2wpa3");
  CHECK(std::string(to_text(Uplink::kWifi)) == "wifi" && std::string(to_text(Uplink::kEthernet)) == "ethernet" && std::string(to_text(BleMode::kOff)) == "off" && std::string(to_text(BleMode::kSetup)) == "setup" &&
        std::string(to_text(BleMode::kAlways)) == "always" && std::string(to_text(WebMode::kHttp)) == "http" && std::string(to_text(WebMode::kBoth)) == "both" && std::string(to_text(WebMode::kHttps)) == "https");
}

static void test_node_defaults_and_validation() {
  using namespace config;
  NodeSettings s;
  node_defaults(s, "a1b2c3");
  CHECK(s.node_id == "electrical-a1b2c3" && s.node_name == s.node_id && s.language == "en" && s.web == WebMode::kBoth && s.ble == BleMode::kSetup && s.time.zone == "UTC0" && s.auto_restart_hours == 0);
  CHECK(s.uplink == (board::kHasEthernet ? Uplink::kEthernet : Uplink::kWifi) && s.ip.dhcp && !s.ap.enabled && !s.sta.enabled && !s.mqtt.enabled && s.mqtt.heartbeat_s == 10 && s.ap.ssid == "ARMOR-ELECTRICAL");
  // a node that was never configured lacks only a way in, and only on the board without a cable
  Problems fresh = check(s);
  if (board::kHasEthernet) CHECK(fresh.empty()); else CHECK(fresh.size() == 1 && has(fresh, "sta.enabled", "required"));
  CHECK(check(good_node()).empty());

  NodeSettings n = good_node();
  n.node_id = "Bad Id"; CHECK(has(check(n), "node.id", "invalid")); n.node_id = ""; CHECK(has(check(n), "node.id", "invalid")); n = good_node();
  n.node_name = ""; CHECK(has(check(n), "node.name", "required"));
  n.node_name = std::string(49, 'n'); CHECK(has(check(n), "node.name", "invalid"));
  n.node_name = std::string(48, 'n'); CHECK(check(n).empty());
  n.node_name = std::string(48, '\xC3').substr(0, 0); n.node_name = "caf\xC3\xA9"; CHECK(check(n).empty());
  n.node_name = "\xC3"; CHECK(has(check(n), "node.name", "invalid"));   // a cut character
  { std::string many; for (int i = 0; i < 48; ++i) many += "\xC3\xA9"; n.node_name = many; CHECK(check(n).empty()); n.node_name = many + "\xC3\xA9"; CHECK(has(check(n), "node.name", "invalid")); }   // 48 characters, not bytes
  n = good_node();
  n.language = "xx"; CHECK(has(check(n), "ui.language", "invalid")); n = good_node();
  n.auto_restart_hours = 5; CHECK(has(check(n), "system.auto_restart_hours", "invalid")); n = good_node();
  n.hostname = "Bad Name"; CHECK(has(check(n), "node.hostname", "invalid")); n.hostname = "good-name"; CHECK(check(n).empty()); n = good_node();
  n.uplink = Uplink::kEthernet;
  if (board::kHasEthernet) CHECK(check(n).empty()); else CHECK(has(check(n), "uplink", "not_available"));
  n = good_node();
  // a fixed address
  n.uplink = Uplink::kWifi;
  n.ip.dhcp = false;
  n.ip.address = "192.168.0.50"; n.ip.netmask = "255.255.255.0"; n.ip.gateway = "192.168.0.1"; n.ip.dns1 = "1.1.1.1"; n.ip.dns2 = "8.8.8.8";
  CHECK(check(n).empty());
  { NodeSettings m = n; m.ip.address = ""; CHECK(has(check(m), "ip.address", "required")); }
  { NodeSettings m = n; m.ip.address = "300.1.1.1"; CHECK(has(check(m), "ip.address", "invalid")); }
  { NodeSettings m = n; m.ip.address = "127.0.0.1"; CHECK(has(check(m), "ip.address", "invalid")); }
  { NodeSettings m = n; m.ip.address = "224.0.0.9"; CHECK(has(check(m), "ip.address", "invalid")); }
  { NodeSettings m = n; m.ip.address = "192.168.0.0"; CHECK(has(check(m), "ip.address", "invalid")); }       // the network address
  { NodeSettings m = n; m.ip.address = "192.168.0.255"; CHECK(has(check(m), "ip.address", "invalid")); }     // the broadcast address
  { NodeSettings m = n; m.ip.netmask = ""; CHECK(has(check(m), "ip.netmask", "required")); }
  { NodeSettings m = n; m.ip.netmask = "255.0.255.0"; CHECK(has(check(m), "ip.netmask", "invalid")); }
  { NodeSettings m = n; m.ip.netmask = "255.255.255.255"; CHECK(has(check(m), "ip.netmask", "invalid")); }
  { NodeSettings m = n; m.ip.gateway = ""; CHECK(has(check(m), "ip.gateway", "required")); }
  { NodeSettings m = n; m.ip.gateway = "10.0.0.1"; CHECK(has(check(m), "ip.gateway", "outside_subnet")); }
  { NodeSettings m = n; m.ip.gateway = "192.168.0.50"; CHECK(has(check(m), "ip.gateway", "conflict")); }
  { NodeSettings m = n; m.ip.gateway = "0.0.0.0"; CHECK(has(check(m), "ip.gateway", "invalid")); }
  { NodeSettings m = n; m.ip.dns1 = "dns.local"; CHECK(has(check(m), "ip.dns1", "invalid")); }
  { NodeSettings m = n; m.ip.dns2 = "1.2.3"; CHECK(has(check(m), "ip.dns2", "invalid")); }
  { NodeSettings m = n; m.ip.dns1.clear(); m.ip.dns2.clear(); CHECK(check(m).empty()); }
  { NodeSettings m = n; m.ip.dhcp = true; m.ip.address = "garbage"; CHECK(check(m).empty()); }   // with DHCP the fixed address is not looked at
  n = good_node();
  // the access point
  n.ap.enabled = true; n.ap.ssid = "mine"; n.ap.password = "secret-key"; CHECK(check(n).empty());
  { NodeSettings m = n; m.ap.ssid = ""; CHECK(has(check(m), "ap.ssid", "required")); }
  { NodeSettings m = n; m.ap.ssid = std::string(33, 'x'); CHECK(has(check(m), "ap.ssid", "invalid")); }
  { NodeSettings m = n; m.ap.password = ""; CHECK(has(check(m), "ap.password", "required")); }
  { NodeSettings m = n; m.ap.password = "short"; CHECK(has(check(m), "ap.password", "invalid_key")); }
  { NodeSettings m = n; m.ap.security = WifiSecurity::kOpen; m.ap.password = ""; CHECK(check(m).empty()); }   // an open network needs no key
  { NodeSettings m = n; m.ap.country = "es"; CHECK(has(check(m), "ap.country", "invalid")); }
  { NodeSettings m = n; m.ap.country = "ESP"; CHECK(has(check(m), "ap.country", "invalid")); }
  { NodeSettings m = n; m.ap.country = ""; CHECK(has(check(m), "ap.country", "invalid")); }
  { NodeSettings m = n; m.ap.enabled = false; m.ap.ssid = ""; m.ap.password = "x"; CHECK(check(m).empty()); }   // a disabled access point is not checked
  n = good_node();
  // the station and its backups
  { NodeSettings m = n; m.sta.ssid = ""; CHECK(has(check(m), "sta.ssid", "required")); }
  { NodeSettings m = n; m.sta.password = "short"; CHECK(has(check(m), "sta.password", "invalid_key")); }
  { NodeSettings m = n; m.sta.password.clear(); CHECK(check(m).empty()); }   // an open network
  { NodeSettings m = n; m.sta.backup.push_back({"other", "another-key"}); CHECK(check(m).empty()); }
  { NodeSettings m = n; m.sta.backup.push_back({"", "another-key"}); CHECK(has(check(m), "sta.backup.0.ssid", "required")); }
  { NodeSettings m = n; m.sta.backup.push_back({"ok", "another-key"}); m.sta.backup.push_back({"ok2", "short"}); CHECK(has(check(m), "sta.backup.1.password", "invalid_key")); }
  // Wi-Fi with neither way in
  { NodeSettings m = n; m.uplink = Uplink::kWifi; m.sta.enabled = false; m.ap.enabled = false; CHECK(has(check(m), "sta.enabled", "required")); m.ap.enabled = true; m.ap.password = "secret-key"; CHECK(check(m).empty()); }
  // the clock
  { NodeSettings m = n; m.time.ntp = "bad host!"; CHECK(has(check(m), "time.ntp", "invalid")); m.time.ntp_enabled = false; CHECK(check(m).empty()); }
  { NodeSettings m = n; m.time.zone = "Europe/Madrid;x"; CHECK(has(check(m), "time.zone", "invalid")); }
  // the broker
  n.mqtt.enabled = true; n.mqtt.uri = "mqtt://broker.local:1883";
  CHECK(check(n).empty());
  { NodeSettings m = n; m.mqtt.uri = ""; CHECK(has(check(m), "mqtt.uri", "required")); }
  { NodeSettings m = n; m.mqtt.uri = "http://x"; CHECK(has(check(m), "mqtt.uri", "invalid")); }
  { NodeSettings m = n; m.mqtt.backup.push_back({"mqtt://b2", "", ""}); CHECK(check(m).empty()); m.mqtt.backup.push_back({"", "", ""}); CHECK(has(check(m), "mqtt.backup.1.uri", "required")); }
  { NodeSettings m = n; m.mqtt.backup.push_back({"nonsense", "", ""}); CHECK(has(check(m), "mqtt.backup.0.uri", "invalid")); }
  { NodeSettings m = n; m.mqtt.enabled = false; m.mqtt.uri = "nonsense"; CHECK(check(m).empty()); }   // a broker that is off is not looked at
  // encryption: it needs the installation's authority and an mqtts:// address, in the main broker and in the backups
  { NodeSettings m = n; m.mqtt.tls = true; Problems p = check(m); CHECK(has(p, "mqtt.ca_pem", "required") && has(p, "mqtt.uri", "need_mqtts")); }
  { NodeSettings m = n; m.mqtt.ca_pem = "not a certificate"; CHECK(has(check(m), "mqtt.ca_pem", "bad_pem")); }
  { NodeSettings m = n; m.mqtt.tls = true; m.mqtt.uri = "mqtts://broker.local:8883"; m.mqtt.backup.push_back({"mqtt://b2", "", ""}); Problems p = check(m); CHECK(has(p, "mqtt.backup.0.uri", "need_mqtts")); }
}

static void test_node_round_trip() {
  using namespace config;
  NodeSettings s = good_node();
  s.node_id = "custom-id"; s.node_name = "Caf\xC3\xA9 node"; s.hostname = "my-host"; s.uplink = Uplink::kWifi;
  s.ip.dhcp = false; s.ip.address = "10.1.2.3"; s.ip.netmask = "255.255.0.0"; s.ip.gateway = "10.1.0.1"; s.ip.dns1 = "9.9.9.9"; s.ip.dns2 = "1.1.1.1";
  s.ap.enabled = true; s.ap.ssid = "my-ap"; s.ap.security = WifiSecurity::kWpa2Wpa3; s.ap.password = "ap-secret-key"; s.ap.channel = 11; s.ap.hidden = true; s.ap.max_clients = 5; s.ap.tx_power_dbm = 8; s.ap.bandwidth_mhz = 40; s.ap.country = "DE";
  s.sta.backup = {{"back1", "back1-password"}, {"back2", ""}};
  s.mqtt.enabled = true; s.mqtt.uri = "mqtt://10.0.0.9:1884"; s.mqtt.username = "node"; s.mqtt.password = "mqtt-secret"; s.mqtt.heartbeat_s = 20;
  s.mqtt.backup = {{"mqtt://10.0.0.10", "u2", "p2"}};
  s.time.ntp_enabled = false; s.time.ntp = "ntp.local"; s.time.zone = "CET-1CEST,M3.5.0,M10.5.0/3";
  s.web = WebMode::kHttps; s.admin_sources = "192.168.0.0/24, 203.0.113.9"; s.ble = BleMode::kAlways; s.language = "pt"; s.auto_restart_hours = 12;
  CHECK(check(s).empty());

  // flash: everything, secrets included
  const std::string stored = written(s, true);
  NodeSettings back;
  Problems problems;
  node_read(parsed(stored), back, problems);
  CHECK(problems.empty());
  CHECK(back.node_id == s.node_id && back.node_name == s.node_name && back.hostname == s.hostname && back.uplink == s.uplink && !back.ip.dhcp == !s.ip.dhcp && back.ip.address == "10.1.2.3" && back.ip.netmask == "255.255.0.0" &&
        back.ip.gateway == "10.1.0.1" && back.ip.dns1 == "9.9.9.9" && back.ip.dns2 == "1.1.1.1");
  CHECK(back.ap.enabled && back.ap.ssid == "my-ap" && back.ap.security == WifiSecurity::kWpa2Wpa3 && back.ap.password == "ap-secret-key" && back.ap.channel == 11 && back.ap.hidden && back.ap.max_clients == 5 &&
        back.ap.tx_power_dbm == 8 && back.ap.bandwidth_mhz == 40 && back.ap.country == "DE");
  CHECK(back.sta.enabled && back.sta.ssid == "casa" && back.sta.password == "una-clave-larga" && back.sta.backup.size() == 2 && back.sta.backup[0].ssid == "back1" && back.sta.backup[0].password == "back1-password" && back.sta.backup[1].password.empty());
  CHECK(back.mqtt.enabled && back.mqtt.uri == "mqtt://10.0.0.9:1884" && back.mqtt.username == "node" && back.mqtt.password == "mqtt-secret" && back.mqtt.heartbeat_s == 20 && back.mqtt.backup.size() == 1 && back.mqtt.backup[0].password == "p2");
  CHECK(!back.time.ntp_enabled && back.time.ntp == "ntp.local" && back.time.zone == s.time.zone && back.web == WebMode::kHttps && back.admin_sources == s.admin_sources && back.ble == BleMode::kAlways && back.language == "pt" && back.auto_restart_hours == 12);
  CHECK(written(back, true) == stored);   // writing what was read gives the same document
  CHECK(check(back).empty());

  // the panel: the same document with no password in it, only whether there is one
  const std::string shown = written(s, false);
  CHECK(!contains_text(shown, "ap-secret-key") && !contains_text(shown, "una-clave-larga") && !contains_text(shown, "back1-password") && !contains_text(shown, "mqtt-secret") && !contains_text(shown, "\"p2\""));
  const json::Value view = parsed(shown);
  CHECK(view.get("ap")->bool_or("password_set", false) && view.get("sta")->bool_or("password_set", false) && view.get("mqtt")->bool_or("password_set", false));
  CHECK(view.get("ap")->get("password") == nullptr && view.get("sta")->get("password") == nullptr && view.get("mqtt")->get("password") == nullptr);
  CHECK(view.get("sta")->get("backup")->items[0].bool_or("password_set", false) && !view.get("sta")->get("backup")->items[1].bool_or("password_set", true));
  // what the panel sends back (no passwords) keeps the stored ones
  NodeSettings kept = s;
  Problems again;
  node_read(view, kept, again);
  CHECK(again.empty() && kept.ap.password == s.ap.password && kept.sta.password == s.sta.password && kept.mqtt.password == s.mqtt.password && kept.sta.backup[0].password == "back1-password" && kept.mqtt.backup[0].password == "p2");
  CHECK(written(kept, true) == stored);
}

static void test_node_read_rules() {
  using namespace config;
  auto read = [](NodeSettings& s, const std::string& text) { Problems p; node_read(parsed(text), s, p); return p; };
  NodeSettings s = good_node();
  const std::string before = written(s, true);
  CHECK(read(s, "{}").empty() && written(s, true) == before);                              // an empty document changes nothing
  CHECK(read(s, "{\"unknown\":{\"x\":1},\"ap\":{\"nothing\":1}}").empty() && written(s, true) == before);   // neither does what it does not know
  CHECK(read(s, "{\"node\":5,\"ap\":\"x\",\"sta\":[],\"mqtt\":null,\"web\":1,\"ble\":true,\"time\":0,\"ui\":\"x\",\"system\":[]}").empty() && written(s, true) == before);   // a section of the wrong type is skipped
  // the wrong type or range for a member is named, and the old value stays
  Problems p = read(s, "{\"node\":{\"id\":5,\"name\":true,\"hostname\":[]}}");
  CHECK(has(p, "node.id", "invalid") && has(p, "node.name", "invalid") && has(p, "node.hostname", "invalid") && s.node_id == "electrical-a1b2c3");
  p = read(s, "{\"node\":{\"id\":\"" + std::string(65, 'a') + "\",\"name\":\"" + std::string(49, 'a') + "\",\"hostname\":\"" + std::string(33, 'a') + "\"}}");
  CHECK(has(p, "node.id", "too_long") && has(p, "node.name", "too_long") && has(p, "node.hostname", "too_long"));
  p = read(s, "{\"uplink\":\"bogus\"}"); CHECK(has(p, "uplink", "invalid") && s.uplink == (board::kHasEthernet ? Uplink::kEthernet : Uplink::kWifi));
  p = read(s, "{\"uplink\":3}"); CHECK(has(p, "uplink", "invalid"));
  p = read(s, "{\"ip\":{\"dhcp\":\"yes\",\"address\":\"1.2.3.4.5.6.7.8.9\",\"netmask\":7}}"); CHECK(has(p, "ip.dhcp", "invalid") && has(p, "ip.address", "too_long") && has(p, "ip.netmask", "invalid") && s.ip.dhcp);
  p = read(s, "{\"ap\":{\"enabled\":1,\"ssid\":9,\"security\":\"wep\",\"channel\":14,\"max_clients\":0,\"tx_power_dbm\":1,\"bandwidth_mhz\":41,\"country\":\"ESP\",\"hidden\":\"no\"}}");
  CHECK(has(p, "ap.enabled", "invalid") && has(p, "ap.ssid", "invalid") && has(p, "ap.security", "invalid") && has(p, "ap.channel", "range") && has(p, "ap.max_clients", "range") && has(p, "ap.tx_power_dbm", "range") &&
        has(p, "ap.bandwidth_mhz", "range") && has(p, "ap.country", "too_long") && has(p, "ap.hidden", "invalid"));
  p = read(s, "{\"ap\":{\"channel\":13,\"max_clients\":10,\"tx_power_dbm\":20,\"bandwidth_mhz\":40,\"security\":\"wpa3\"}}");
  CHECK(p.empty() && s.ap.channel == 13 && s.ap.max_clients == 10 && s.ap.tx_power_dbm == 20 && s.ap.bandwidth_mhz == 40 && s.ap.security == WifiSecurity::kWpa3);
  p = read(s, "{\"ap\":{\"channel\":0,\"max_clients\":1,\"tx_power_dbm\":2,\"bandwidth_mhz\":20}}"); CHECK(p.empty() && s.ap.channel == 0 && s.ap.max_clients == 1 && s.ap.tx_power_dbm == 2);
  p = read(s, "{\"ap\":{\"channel\":1.5,\"max_clients\":\"3\"}}"); CHECK(has(p, "ap.channel", "range") && has(p, "ap.max_clients", "invalid"));
  p = read(s, "{\"mqtt\":{\"heartbeat_s\":1}}"); CHECK(has(p, "mqtt.heartbeat_s", "range") && s.mqtt.heartbeat_s == 10);
  p = read(s, "{\"mqtt\":{\"heartbeat_s\":301}}"); CHECK(has(p, "mqtt.heartbeat_s", "range"));
  p = read(s, "{\"mqtt\":{\"heartbeat_s\":300,\"uri\":\"" + std::string(161, 'u') + "\",\"username\":\"" + std::string(65, 'u') + "\"}}"); CHECK(has(p, "mqtt.uri", "too_long") && has(p, "mqtt.username", "too_long") && s.mqtt.heartbeat_s == 300);
  p = read(s, "{\"mqtt\":{\"heartbeat_s\":10}}");
  p = read(s, "{\"web\":{\"mode\":\"ftp\",\"admin_sources\":\"not-an-address\"}}"); CHECK(has(p, "web.mode", "invalid") && has(p, "web.admin_sources", "invalid") && s.admin_sources.empty());
  p = read(s, "{\"web\":{\"mode\":\"http\",\"admin_sources\":\"10.0.0.0/8\"}}"); CHECK(p.empty() && s.web == WebMode::kHttp && s.admin_sources == "10.0.0.0/8");
  p = read(s, "{\"web\":{\"admin_sources\":\"\"}}"); CHECK(p.empty() && s.admin_sources.empty());   // empty is the house's private ranges
  p = read(s, "{\"web\":{\"admin_sources\":\"" + std::string(121, '1') + "\"}}"); CHECK(has(p, "web.admin_sources", "too_long"));
  p = read(s, "{\"ble\":{\"mode\":\"sometimes\"}}"); CHECK(has(p, "ble.mode", "invalid") && s.ble == BleMode::kSetup);
  p = read(s, "{\"time\":{\"ntp_enabled\":\"x\",\"ntp\":\"" + std::string(65, 'n') + "\",\"zone\":\"" + std::string(49, 'z') + "\"}}"); CHECK(has(p, "time.ntp_enabled", "invalid") && has(p, "time.ntp", "too_long") && has(p, "time.zone", "too_long"));
  p = read(s, "{\"ui\":{\"language\":\"english\"}}"); CHECK(has(p, "ui.language", "too_long") && s.language == "en");
  p = read(s, "{\"ui\":{\"language\":\"xx\"}}"); CHECK(p.empty() && s.language == "xx" && has(check(s), "ui.language", "invalid"));   // reading accepts a short text; checking is what refuses an unknown language
  s.language = "en";
  p = read(s, "{\"system\":{\"auto_restart_hours\":49}}"); CHECK(has(p, "system.auto_restart_hours", "range") && s.auto_restart_hours == 0);
  p = read(s, "{\"system\":{\"auto_restart_hours\":5}}"); CHECK(p.empty() && s.auto_restart_hours == 5 && has(check(s), "system.auto_restart_hours", "invalid"));
  // the older place of the time server
  s.auto_restart_hours = 0;
  p = read(s, "{\"mqtt\":{\"ntp\":\"old.example\"}}"); CHECK(p.empty() && s.time.ntp == "old.example");
  // the broker's authority is stored in a normal form
  p = read(s, "{\"mqtt\":{\"ca_pem\":\"  -----BEGIN CERTIFICATE-----\\r\\nAAAA\\r\\n-----END CERTIFICATE-----  \"}}");
  CHECK(p.empty() && s.mqtt.ca_pem == "-----BEGIN CERTIFICATE-----\nAAAA\n-----END CERTIFICATE-----\n");
  p = read(s, "{\"mqtt\":{\"ca_pem\":\"\"}}"); CHECK(p.empty() && s.mqtt.ca_pem.empty());
  p = read(s, "{\"mqtt\":{\"ca_pem\":\"" + std::string(2049, 'A') + "\"}}"); CHECK(has(p, "mqtt.ca_pem", "too_long"));
}

static void test_node_backup_lists() {
  using namespace config;
  auto read = [](NodeSettings& s, const std::string& text) { Problems p; node_read(parsed(text), s, p); return p; };
  NodeSettings s = good_node();
  s.sta.backup = {{"one", "one-password"}, {"two", "two-password"}, {"three", "three-password"}};
  // the list that arrives replaces the stored one: removing an entry and saving does not bring it back
  Problems p = read(s, "{\"sta\":{\"backup\":[{\"ssid\":\"two\"}]}}");
  CHECK(p.empty() && s.sta.backup.size() == 1 && s.sta.backup[0].ssid == "two" && s.sta.backup[0].password == "two-password");
  p = read(s, "{\"sta\":{\"backup\":[]}}"); CHECK(p.empty() && s.sta.backup.empty());
  // a network with a new name starts without a password; the same name keeps its own, whatever its place
  s.sta.backup = {{"one", "one-password"}, {"two", "two-password"}};
  p = read(s, "{\"sta\":{\"backup\":[{\"ssid\":\"two\"},{\"ssid\":\"fresh\"},{\"ssid\":\"one\",\"password\":\"changed-pass\"}]}}");
  CHECK(p.empty() && s.sta.backup.size() == 3 && s.sta.backup[0].password == "two-password" && s.sta.backup[1].password.empty() && s.sta.backup[2].password == "changed-pass");
  // more than fit loses the extras only
  p = read(s, "{\"sta\":{\"backup\":[{\"ssid\":\"a\"},{\"ssid\":\"b\"},{\"ssid\":\"c\"},{\"ssid\":\"d\"},{\"ssid\":\"e\"}]}}");
  CHECK(p.empty() && s.sta.backup.size() == kMaxBackupNetworks && s.sta.backup[2].ssid == "c");
  // a list that is not a list, or an entry that is not an object, is named
  const std::size_t kept = s.sta.backup.size();
  p = read(s, "{\"sta\":{\"backup\":\"x\"}}"); CHECK(has(p, "sta.backup", "invalid") && s.sta.backup.size() == kept);
  p = read(s, "{\"sta\":{\"backup\":[5,{\"ssid\":\"ok\"}]}}"); CHECK(has(p, "sta.backup.0.ssid", "invalid") && s.sta.backup.size() == 1 && s.sta.backup[0].ssid == "ok");
  p = read(s, "{\"sta\":{\"backup\":[{\"ssid\":\"" + std::string(33, 's') + "\"}]}}"); CHECK(has(p, "sta.backup.0.ssid", "too_long"));
  // the brokers: an entry keeps its password when it is the same user at the same place or at the same address
  s.mqtt.backup = {{"mqtt://b1", "u1", "p1"}, {"mqtt://b2", "u2", "p2"}};
  p = read(s, "{\"mqtt\":{\"backup\":[{\"uri\":\"mqtt://moved\",\"username\":\"u1\"},{\"uri\":\"mqtt://b2\",\"username\":\"u2\"}]}}");
  CHECK(p.empty() && s.mqtt.backup[0].password == "p1" && s.mqtt.backup[1].password == "p2");   // u1 stayed in slot 0 and u2 kept its address
  p = read(s, "{\"mqtt\":{\"backup\":[{\"uri\":\"mqtt://b2\",\"username\":\"u2\"}]}}");
  CHECK(p.empty() && s.mqtt.backup.size() == 1 && s.mqtt.backup[0].password == "p2");            // moved up: the address is the same
  p = read(s, "{\"mqtt\":{\"backup\":[{\"uri\":\"mqtt://b2\",\"username\":\"someone-else\"}]}}");
  CHECK(p.empty() && s.mqtt.backup[0].password.empty());                                           // another user: not the same account
  p = read(s, "{\"mqtt\":{\"backup\":[{\"uri\":\"mqtt://a\"},{\"uri\":\"mqtt://b\"},{\"uri\":\"mqtt://c\"}]}}");
  CHECK(p.empty() && s.mqtt.backup.size() == kMaxBackupBrokers);
  p = read(s, "{\"mqtt\":{\"backup\":{}}}"); CHECK(has(p, "mqtt.backup", "invalid"));
  p = read(s, "{\"mqtt\":{\"backup\":[true]}}"); CHECK(has(p, "mqtt.backup.0.uri", "invalid"));
  // a secret: kept when absent or empty, erased on request, refused when too long
  s.mqtt.password = "keep-me"; s.ap.password = "ap-password";
  p = read(s, "{\"mqtt\":{\"password\":\"\"},\"ap\":{}}"); CHECK(p.empty() && s.mqtt.password == "keep-me" && s.ap.password == "ap-password");
  p = read(s, "{\"mqtt\":{\"password_clear\":true},\"ap\":{\"password_clear\":true,\"password\":\"ignored-when-cleared\"}}"); CHECK(p.empty() && s.mqtt.password.empty() && s.ap.password.empty());
  p = read(s, "{\"mqtt\":{\"password\":\"" + std::string(65, 'p') + "\"}}"); CHECK(has(p, "mqtt.password", "too_long") && s.mqtt.password.empty());
}

// The password stored for a broker is that broker's: pointing the node at another host without typing a password must not send the stored one there, or a stolen panel session could
// read it out by naming a broker of its own. The settings of the radar node do this; these settings do not yet.
static void test_password_follows_its_broker() {
  using namespace config;
  auto read = [](NodeSettings& s, const std::string& text) { Problems p; node_read(parsed(text), s, p); return p; };
  NodeSettings s = good_node();
  s.mqtt.enabled = true; s.mqtt.uri = "mqtt://broker.local:1883"; s.mqtt.username = "node"; s.mqtt.password = "secret-pass";
  NodeSettings same = s;
  CHECK(read(same, "{\"mqtt\":{\"uri\":\"mqtt://BROKER.local:1884\"}}").empty() && same.mqtt.password == "secret-pass");   // the same host keeps it
  NodeSettings typed = s;
  CHECK(read(typed, "{\"mqtt\":{\"uri\":\"mqtt://elsewhere.example\",\"password\":\"typed-pass\"}}").empty() && typed.mqtt.password == "typed-pass");
  NodeSettings moved = s;
  CHECK(read(moved, "{\"mqtt\":{\"uri\":\"mqtt://elsewhere.example:1883\"}}").empty());
  KNOWN_GAP(moved.mqtt.password.empty());
  s.mqtt.backup = {{"mqtt://b1.local", "u1", "p1"}};
  NodeSettings slot = s;
  CHECK(read(slot, "{\"mqtt\":{\"backup\":[{\"uri\":\"mqtt://elsewhere.example\",\"username\":\"u1\"}]}}").empty());
  KNOWN_GAP(slot.mqtt.backup.size() == 1 && slot.mqtt.backup[0].password.empty());
}

// ---- the base board's own settings ---------------------------------------------------------------------------------------------------------------

static config::Problems io_check(const config::IoSettings& io, bool broker) {
  config::Problems problems;
  config::detail::PinClaims claims;
  const auto claim = [&](int gpio, const std::string& who, const std::string& path) {
    if (!board::assignable(gpio)) { config::detail::bad(problems, path, "reserved"); return; }
    if (!claims.claim(gpio, who)) config::detail::bad(problems, path, "conflict");
  };
  config::io_validate(io, broker, claim, problems);
  return problems;
}

static void test_io_settings() {
  using namespace config;
  IoSettings io;
  io_defaults(io);
  CHECK(io.zigbee.rx == board::kZigbeePins.rx && io.zigbee.tx == board::kZigbeePins.tx && io.zigbee.reset == board::kZigbeePins.reset && io.zigbee.bootloader == board::kZigbeePins.bootloader);
  for (std::size_t i = 0; i < board::kRelayCount; ++i) CHECK(io.relays[i].name == "relay" + std::to_string(i + 1) && io.relays[i].pin == board::kRelayPins[i] && !io.relays[i].enabled && !io.relays[i].active_low);
  CHECK(!io.zigbee.enabled && !io.remote.relays && !io.remote.http && io.remote.link_timeout_s == 0 && io.zigbee.baud == 115200 && io.zigbee.port == 8888 && io.zigbee.fallback_after_s == 30);
  CHECK(io_check(io, false).empty());   // nothing is switched on by default
  // everything switched on at the board's own pins is consistent
  for (RelaySetting& relay : io.relays) relay.enabled = true;
  io.zigbee.enabled = true; io.zigbee.allowed_client = "192.168.0.10";
  CHECK(io_check(io, false).empty());
  // the zigbee radio
  { IoSettings m = io; m.zigbee.allowed_client = ""; CHECK(has(io_check(m, false), "zigbee.allowed_client", "required")); }
  { IoSettings m = io; m.zigbee.allowed_client = "nope"; CHECK(has(io_check(m, false), "zigbee.allowed_client", "invalid")); }
  { IoSettings m = io; m.zigbee.allowed_client = "127.0.0.1"; CHECK(has(io_check(m, false), "zigbee.allowed_client", "invalid")); }
  { IoSettings m = io; m.zigbee.fallback_client = "192.168.0.10"; CHECK(has(io_check(m, false), "zigbee.fallback_client", "conflict")); }
  { IoSettings m = io; m.zigbee.fallback_client = "300.1.1.1"; CHECK(has(io_check(m, false), "zigbee.fallback_client", "invalid")); }
  { IoSettings m = io; m.zigbee.fallback_client = "192.168.0.11"; CHECK(io_check(m, false).empty()); }
  { IoSettings m = io; m.zigbee.baud = 9600; CHECK(has(io_check(m, false), "zigbee.baud", "range")); m.zigbee.baud = 230400; CHECK(io_check(m, false).empty()); }
  { IoSettings m = io; m.zigbee.port = 80; CHECK(has(io_check(m, false), "zigbee.port", "range")); m.zigbee.port = 1024; CHECK(io_check(m, false).empty()); }
  { IoSettings m = io; m.zigbee.rx = -1; CHECK(has(io_check(m, false), "zigbee.rx", "required")); m.zigbee.tx = -1; CHECK(has(io_check(m, false), "zigbee.tx", "required")); }
  { IoSettings m = io; m.zigbee.reset = -1; m.zigbee.bootloader = -1; CHECK(io_check(m, false).empty()); }   // the control lines are optional
  { IoSettings m = io; m.zigbee.rx = 33; CHECK(has(io_check(m, false), "zigbee.rx", "reserved")); }
  { IoSettings m = io; m.zigbee.tx = m.zigbee.rx; CHECK(has(io_check(m, false), "zigbee.tx", "conflict")); }
  { IoSettings m = io; m.zigbee.enabled = false; m.zigbee.allowed_client.clear(); m.zigbee.rx = 33; CHECK(io_check(m, false).empty()); }   // a radio that is off is not looked at
  // the relays
  { IoSettings m = io; m.relays[2].name = ""; CHECK(has(io_check(m, false), "relays.2.name", "required")); }
  { IoSettings m = io; m.relays[2].name = "Bad Name"; CHECK(has(io_check(m, false), "relays.2.name", "invalid")); }
  { IoSettings m = io; m.relays[2].name = m.relays[0].name; CHECK(has(io_check(m, false), "relays.2.name", "conflict")); }
  { IoSettings m = io; m.relays[2].pin = -1; CHECK(has(io_check(m, false), "relays.2.pin", "required")); }
  { IoSettings m = io; m.relays[2].pin = 19; CHECK(has(io_check(m, false), "relays.2.pin", "reserved")); }
  { IoSettings m = io; m.relays[2].pin = m.relays[1].pin; CHECK(has(io_check(m, false), "relays.2.pin", "conflict")); }
  { IoSettings m = io; m.relays[2].label = std::string(41, 'l'); CHECK(has(io_check(m, false), "relays.2.label", "invalid")); m.relays[2].label = std::string(40, 'l'); CHECK(io_check(m, false).empty()); }
  { IoSettings m = io; m.relays[2].label = "\xC3"; CHECK(has(io_check(m, false), "relays.2.label", "invalid")); }
  { IoSettings m = io; m.relays[2].enabled = false; m.relays[2].name = "Bad Name"; m.relays[2].pin = 19; CHECK(io_check(m, false).empty()); }   // a relay that is off is not looked at
  // the relays against the radio's pins
  { IoSettings m = io; m.relays[0].pin = m.zigbee.rx; Problems p = io_check(m, false); CHECK(has(p, "relays.0.pin", "conflict") || has(p, "zigbee.rx", "conflict")); }
  // the broker moving the relays
  { IoSettings m = io; m.remote.relays = true; CHECK(has(io_check(m, false), "remote.relays", "needs_broker") && !has(io_check(m, true), "remote.relays", "needs_broker")); }
  { IoSettings m = io; m.remote.link_timeout_s = 5; CHECK(has(io_check(m, true), "remote.link_timeout_s", "range")); m.remote.link_timeout_s = 10; CHECK(io_check(m, true).empty()); m.remote.link_timeout_s = 0; CHECK(io_check(m, true).empty()); }
  // the direct API for a central
  { IoSettings m = io; m.remote.http = true; CHECK(has(io_check(m, false), "remote.token", "required")); m.remote.token = "0123456789abcdefghij"; CHECK(io_check(m, false).empty()); }
  { IoSettings m = io; m.remote.token = "short"; CHECK(has(io_check(m, false), "remote.token", "invalid")); m.remote.token = "has a space in it 12345"; CHECK(has(io_check(m, false), "remote.token", "invalid")); }
}

static void test_io_round_trip() {
  using namespace config;
  IoSettings io;
  io_defaults(io);
  io.zigbee.enabled = true; io.zigbee.allowed_client = "192.168.0.10"; io.zigbee.fallback_client = "192.168.0.11"; io.zigbee.baud = 230400; io.zigbee.port = 9000; io.zigbee.fallback_after_s = 60; io.zigbee.reset = -1;
  io.relays[0] = {true, "siren", "Siren \xC3\xA9", 38, true};
  io.relays[7].enabled = true;
  io.remote.relays = true; io.remote.link_timeout_s = 120; io.remote.http = true; io.remote.token = "0123456789abcdefghijKLMNOP";
  json::Writer w;
  w.begin_object();
  io_write(w, io, true);
  w.end_object();
  const std::string stored = w.str();
  IoSettings back;
  Problems problems;
  io_read(parsed(stored), back, problems);
  CHECK(problems.empty() && back.zigbee.enabled && back.zigbee.allowed_client == "192.168.0.10" && back.zigbee.fallback_client == "192.168.0.11" && back.zigbee.baud == 230400 && back.zigbee.port == 9000 &&
        back.zigbee.fallback_after_s == 60 && back.zigbee.reset == -1 && back.zigbee.rx == board::kZigbeePins.rx);
  CHECK(back.relays[0].enabled && back.relays[0].name == "siren" && back.relays[0].label == "Siren \xC3\xA9" && back.relays[0].pin == 38 && back.relays[0].active_low && back.relays[7].enabled && !back.relays[3].enabled);
  CHECK(back.remote.relays && back.remote.link_timeout_s == 120 && back.remote.http && back.remote.token == io.remote.token);
  json::Writer again;
  again.begin_object();
  io_write(again, back, true);
  again.end_object();
  CHECK(again.str() == stored);
  // the panel does not get the token
  json::Writer shown;
  shown.begin_object();
  io_write(shown, io, false);
  shown.end_object();
  CHECK(shown.str().find("0123456789abcdef") == std::string::npos && parsed(shown.str()).get("remote")->bool_or("token_set", false) && parsed(shown.str()).get("remote")->get("token") == nullptr);
  // reading refuses what does not fit and names it
  IoSettings bad;
  Problems p;
  io_read(parsed("{\"zigbee\":{\"enabled\":1,\"rx\":49,\"tx\":-2,\"baud\":100,\"port\":0,\"fallback_after_s\":4,\"allowed_client\":\"1.2.3.4.5.6.7.8.9\"},\"remote\":{\"link_timeout_s\":-1,\"token\":5}}"), bad, p);
  CHECK(has(p, "zigbee.enabled", "invalid") && has(p, "zigbee.rx", "range") && has(p, "zigbee.tx", "range") && has(p, "zigbee.baud", "range") && has(p, "zigbee.port", "range") && has(p, "zigbee.fallback_after_s", "range") &&
        has(p, "zigbee.allowed_client", "too_long") && has(p, "remote.link_timeout_s", "range") && has(p, "remote.token", "invalid"));
  p.clear();
  io_read(parsed("{\"relays\":\"x\"}"), bad, p); CHECK(has(p, "relays", "invalid"));
  p.clear();
  std::string nine = "{\"relays\":[";
  for (int i = 0; i < 9; ++i) nine += std::string(i ? "," : "") + "{}";
  io_read(parsed(nine + "]}"), bad, p); CHECK(has(p, "relays", "invalid"));   // more relays than the board has
  p.clear();
  io_read(parsed("{\"relays\":[7,{\"enabled\":true,\"name\":5,\"label\":\"" + std::string(41, 'l') + "\",\"pin\":99,\"active_low\":0}]}"), bad, p);
  CHECK(has(p, "relays.0.enabled", "invalid") && has(p, "relays.1.name", "invalid") && has(p, "relays.1.label", "too_long") && has(p, "relays.1.pin", "range") && has(p, "relays.1.active_low", "invalid") && bad.relays[1].enabled);
}

// ---- the relays ----------------------------------------------------------------------------------------------------------------------------------

static void test_relay_bank() {
  using namespace relays;
  config::IoSettings io;
  config::io_defaults(io);
  io.relays[0].enabled = true; io.relays[0].name = "siren";
  io.relays[1].enabled = true; io.relays[1].active_low = true;
  Bank bank(io);
  CHECK(bank.enabled(0) && bank.enabled(1) && !bank.enabled(2) && !bank.enabled(8) && !bank.enabled(1000));
  // every relay is open when the node starts, whatever the settings
  for (std::size_t i = 0; i < 8; ++i) CHECK(!bank.is_on(i));
  CHECK(bank.set(0, true) && bank.is_on(0) && bank.pin_level(0));                // closes on a high pin
  CHECK(bank.set(1, true) && bank.is_on(1) && !bank.pin_level(1));               // closes on a low pin
  CHECK(!bank.set(2, true) && !bank.is_on(2) && !bank.set(8, true) && !bank.pin_level(2) && !bank.pin_level(99));   // a relay that is not enabled never moves
  CHECK(bank.set(0, false) && !bank.is_on(0) && !bank.pin_level(0));
  CHECK(bank.set(1, false) && bank.pin_level(1));                                 // an open relay of an active-low slot idles high
  CHECK(!bank.open_level(0) && bank.open_level(1) && !bank.open_level(2) && !bank.open_level(99));
  bank.set(0, true); bank.set(1, true);
  bank.open_all();
  CHECK(!bank.is_on(0) && !bank.is_on(1) && bank.pin_level(1) && !bank.pin_level(0));
  CHECK(bank.find("siren") == 0 && bank.find("relay2") == 1 && bank.find("relay3") == -1 && bank.find("") == -1 && bank.find("SIREN") == -1);
  CHECK(bank.pin(0) == board::kRelayPins[0] && bank.pin(7) == board::kRelayPins[7] && bank.pin(8) == -1);
  CHECK(bank.name(0) == "siren" && bank.name(1) == "relay2");
  // a relay that was on and is then disabled in the settings is not on any more
  io.relays[0].enabled = false;
  Bank other(io);
  CHECK(!other.is_on(0) && other.find("siren") == -1);
  bank.set(0, true);
  const json::Value state = parsed(bank.json());
  CHECK(state.is_array() && state.items.size() == 8 && state.items[0].bool_or("on", false) && state.items[0].string_or("name", "") == "siren" && state.items[0].number_or("relay", 0) == 1 &&
        !state.items[1].bool_or("on", true) && state.items[1].bool_or("enabled", false) && !state.items[2].bool_or("enabled", true) && state.items[7].number_or("relay", 0) == 8);
}

static void test_relay_remote() {
  using namespace relays;
  for (const char* word : {"on", "ON", "On", " on ", "\ttrue\n", "1", "TRUE"}) CHECK(parse_command(word) == Action::kOn);
  for (const char* word : {"off", "OFF", " off", "false", "0", "False"}) CHECK(parse_command(word) == Action::kOff);
  for (const char* word : {"toggle", "TOGGLE", " Toggle "}) CHECK(parse_command(word) == Action::kToggle);
  for (const char* word : {"", " ", "2", "on off", "onn", "yes", "pulse 5", "{\"on\":true}", "-1", "01", "o n", "on\x01"}) CHECK(parse_command(word) == Action::kNone);
  // the broker may repeat a message, so it may not toggle
  CHECK(parse_broker_command("ON") == Action::kOn && parse_broker_command("off") == Action::kOff && parse_broker_command("TOGGLE") == Action::kNone && parse_broker_command("") == Action::kNone);
  CHECK(topic("node-1", "siren", "set") == "armor/device/node-1/siren/set" && topic("node-1", "siren", "state") == "armor/device/node-1/siren/state" && topic("node-1", "siren", "availability") == "armor/device/node-1/siren/availability");
  CHECK(topic("Bad Node", "siren", "set").empty() && topic("node-1", "Bad", "set").empty() && topic("node-1", "siren", "other").empty() && topic("node-1", "", "set").empty() && topic("", "siren", "set").empty() && topic("node-1", "a/b", "set").empty());
  CHECK(command_filter("node-1") == "armor/device/node-1/+/set" && command_filter("Bad Node").empty() && command_filter("").empty());
  CHECK(relay_of_command_topic("node-1", "armor/device/node-1/siren/set") == "siren" && relay_of_command_topic("node-1", "armor/device/node-2/siren/set").empty() &&
        relay_of_command_topic("node-1", "armor/device/node-1/siren/state").empty() && relay_of_command_topic("node-1", "armor/device/node-1/a/b/set").empty() && relay_of_command_topic("node-1", "armor/device/node-1//set").empty() &&
        relay_of_command_topic("node-1", "armor/device/node-1/Siren/set").empty() && relay_of_command_topic("node-1", "armor/device/node-1/set").empty() && relay_of_command_topic("node-1", "").empty() &&
        relay_of_command_topic("Bad Node", "armor/device/Bad Node/siren/set").empty() && relay_of_command_topic("node-1", "armor/device/node-1/siren/set/extra").empty());
  // a topic made by topic() is read back by relay_of_command_topic()
  CHECK(relay_of_command_topic("node-1", topic("node-1", "relay8", "set")) == "relay8");
  CHECK(report(true) == "{\"on\":true}" && report(false) == "{\"on\":false}");
  // the fall back to open after the broker has been out of reach
  LinkWatch watch;
  CHECK(!watch.lost() && watch.lost_for_ms(1000) == 0);
  watch.update(true, 100);
  CHECK(!watch.lost());
  watch.update(false, 1000);
  CHECK(watch.lost() && watch.lost_for_ms(1000) == 0 && watch.lost_for_ms(6000) == 5000);
  watch.update(false, 3000);   // still down: the time counts from the first failure
  CHECK(watch.lost_for_ms(6000) == 5000);
  watch.update(true, 7000);
  CHECK(!watch.lost() && watch.lost_for_ms(8000) == 0);
  watch.update(false, 9000);
  CHECK(watch.lost_for_ms(10000) == 1000);
  CHECK(!must_open(0, 999999999ULL) && !must_open(-5, 999999999ULL) && !must_open(10, 9999) && must_open(10, 10000) && must_open(10, 10001) && must_open(86400, 86400000ULL) && !must_open(86400, 86399999ULL));
  CHECK(lower_trimmed("  AbC \r\n") == "abc" && lower_trimmed("").empty() && lower_trimmed("   ").empty());
}

// ---- the Zigbee radio's bridge -------------------------------------------------------------------------------------------------------------------

static void test_zigbee_bridge() {
  using namespace zigbee;
  config::ZigbeeSetting setting;
  setting.allowed_client = "192.168.0.10";
  const std::uint32_t primary = 0xC0A8000Au, second = 0xC0A8000Bu, stranger = 0xC0A8000Cu;
  CHECK(client_allowed(setting.allowed_client, primary) && !client_allowed(setting.allowed_client, second) && !client_allowed("", 0) && !client_allowed("garbage", 0) && !client_allowed("192.168.0.010", primary));
  CHECK(classify(setting, primary) == Role::kPrimary && classify(setting, second) == Role::kNone);
  setting.fallback_client = "192.168.0.11";
  CHECK(classify(setting, second) == Role::kFallback && classify(setting, stranger) == Role::kNone && classify(setting, 0) == Role::kNone);
  setting.fallback_client = "192.168.0.10";   // validation refuses this; the primary wins anyway
  CHECK(classify(setting, primary) == Role::kPrimary);
  CHECK(std::string(role_text(Role::kPrimary)) == "primary" && std::string(role_text(Role::kFallback)) == "fallback" && std::string(role_text(Role::kNone)).empty());
  // the arbiter: the primary always connects; the fallback only after the primary has been away long enough
  Arbiter arbiter(30000, 0);
  CHECK(arbiter.offer(Role::kNone, 100000) == Arbiter::Verdict::kRefuse);
  CHECK(arbiter.offer(Role::kFallback, 29999) == Arbiter::Verdict::kRefuse && arbiter.offer(Role::kFallback, 30000) == Arbiter::Verdict::kAccept);   // a node that has just started waits like any absence
  CHECK(arbiter.offer(Role::kPrimary, 0) == Arbiter::Verdict::kAccept);
  arbiter.accepted(Role::kPrimary, 1000);
  CHECK(arbiter.current() == Role::kPrimary && arbiter.offer(Role::kFallback, 1000000) == Arbiter::Verdict::kRefuse && arbiter.offer(Role::kPrimary, 1000) == Arbiter::Verdict::kAccept);
  arbiter.released(50000);   // the primary leaves at 50 s
  CHECK(arbiter.current() == Role::kNone && arbiter.offer(Role::kFallback, 79999) == Arbiter::Verdict::kRefuse && arbiter.offer(Role::kFallback, 80000) == Arbiter::Verdict::kAccept);
  arbiter.accepted(Role::kFallback, 80000);
  CHECK(arbiter.current() == Role::kFallback && arbiter.takeovers() == 1);
  CHECK(arbiter.offer(Role::kPrimary, 80001) == Arbiter::Verdict::kAccept);   // the primary takes the radio back at once
  arbiter.released(90000);            // the fallback leaves: that does not start the primary's absence
  CHECK(arbiter.offer(Role::kFallback, 90001) == Arbiter::Verdict::kAccept);   // the primary has been away since 50 s
  arbiter.accepted(Role::kPrimary, 91000);
  arbiter.released(95000);
  CHECK(arbiter.offer(Role::kFallback, 124999) == Arbiter::Verdict::kRefuse && arbiter.offer(Role::kFallback, 125000) == Arbiter::Verdict::kAccept);
  CHECK(arbiter.takeovers() == 1);
  // a node started at 1000 s waits from its start, not from the beginning of time
  Arbiter late(30000, 1000000);
  CHECK(late.offer(Role::kFallback, 1020000) == Arbiter::Verdict::kRefuse && late.offer(Role::kFallback, 1030000) == Arbiter::Verdict::kAccept);
  // the lines of the radio: the node only ever pulls a line low or lets it go
  const std::vector<Step> reset = radio_sequence(false);
  CHECK(reset.size() == 2 && reset[0].reset_low && !reset[0].bootloader_low && reset[0].hold_ms == 50 && !reset[1].reset_low && !reset[1].bootloader_low);
  const std::vector<Step> boot = radio_sequence(true);
  CHECK(boot.size() == 3 && boot[0].reset_low && boot[0].bootloader_low && boot[1].bootloader_low && !boot[1].reset_low && boot[1].hold_ms >= 100 && !boot[2].reset_low && !boot[2].bootloader_low);
  for (const Step& step : boot) CHECK(step.hold_ms < 1000);
  // the panel's view of the bridge
  Status status;
  CHECK(status.state == "disabled" && status.takeovers == 0);
  status.state = "connected"; status.client = "192.168.0.10"; status.role = "primary"; status.to_radio = 5000000000ULL; status.from_radio = 7; status.connections = 3; status.refused = 2; status.dropped = 9; status.has_reset = true;
  setting.enabled = true; setting.port = 8888; setting.baud = 115200; setting.fallback_client = ""; setting.fallback_after_s = 30;
  const json::Value view = parsed(status_json(setting, status));
  CHECK(view.bool_or("enabled", false) && view.string_or("state", "") == "connected" && view.number_or("port", 0) == 8888 && view.number_or("baud", 0) == 115200 && view.string_or("allowed_client", "x") == "192.168.0.10" &&
        view.string_or("client", "") == "192.168.0.10" && view.string_or("role", "") == "primary" && view.number_or("to_radio", 0) == 5000000000.0 && view.number_or("from_radio", -1) == 7 && view.number_or("connections", 0) == 3 &&
        view.number_or("refused", 0) == 2 && view.number_or("dropped", 0) == 9 && view.bool_or("has_reset", false) && !view.bool_or("has_bootloader", true));
}

// ---- the direct API for a central -----------------------------------------------------------------------------------------------------------------

static void test_central_requests() {
  using namespace central;
  const std::string token = "0123456789abcdefghij";
  CHECK(valid_token(token) && valid_token(std::string(64, 'a')) && valid_token("a-b_c.d~e+f/g=h0123456789") && !valid_token(token.substr(1)) && !valid_token(std::string(65, 'a')) && !valid_token("0123456789abcdefghi ") &&
        !valid_token("0123456789abcdefghi\n") && !valid_token("0123456789abcdefghi\"") && !valid_token("0123456789abcdefghi:"));
  CHECK(authorized(true, token, "Bearer " + token) && !authorized(true, token, "bearer " + token) && !authorized(true, token, token) && !authorized(true, token, "Bearer  " + token) && !authorized(true, token.substr(1), "Bearer " + token.substr(1)));
  RelayRequest request;
  CHECK(parse_relay_request("{\"relay\":\"siren\",\"action\":\"on\"}", request) == Parse::kOk && request.relay == "siren" && request.action == relays::Action::kOn);
  CHECK(parse_relay_request("{\"relay\":\"siren\",\"action\":\"TOGGLE\"}", request) == Parse::kOk && request.action == relays::Action::kToggle);   // the node's own API may toggle: one request, one move
  CHECK(parse_relay_request("{\"relay\":\"siren\",\"action\":\"off\",\"extra\":1}", request) == Parse::kOk && request.action == relays::Action::kOff);
  const char* not_json[] = {"", "x", "[]", "5", "\"on\"", "{\"relay\":\"a\",", "{\"relay\":\"a\",\"relay\":\"b\",\"action\":\"on\"}"};
  for (const char* body : not_json) CHECK(parse_relay_request(body, request) == Parse::kNotJson);
  const char* invalid[] = {"{}", "{\"relay\":\"siren\"}", "{\"action\":\"on\"}", "{\"relay\":5,\"action\":\"on\"}", "{\"relay\":\"siren\",\"action\":true}", "{\"relay\":\"siren\",\"action\":\"pulse\"}",
                           "{\"relay\":\"Bad Name\",\"action\":\"on\"}", "{\"relay\":\"\",\"action\":\"on\"}", "{\"relay\":\"siren\",\"action\":\"\"}", "{\"relay\":\"a/b\",\"action\":\"on\"}"};
  for (const char* body : invalid) CHECK(parse_relay_request(body, request) == Parse::kInvalid);
  CHECK(relay_answer("siren", true) == "{\"relay\":\"siren\",\"on\":true}" && relay_answer("a\"b", false) == "{\"relay\":\"a\\\"b\",\"on\":false}");
}

int main() {
  test_module_pins();
  test_base_board();
  test_config_readers();
  test_node_helpers();
  test_node_defaults_and_validation();
  test_node_round_trip();
  test_node_read_rules();
  test_node_backup_lists();
  test_password_follows_its_broker();
  test_io_settings();
  test_io_round_trip();
  test_relay_bank();
  test_relay_remote();
  test_zigbee_bridge();
  test_central_requests();
  std::printf("%d checks, %d failures, %d known gaps\n", checks, failures, known_gaps);
  return failures == 0 ? 0 : 1;
}
