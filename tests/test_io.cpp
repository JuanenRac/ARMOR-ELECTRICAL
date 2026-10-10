// ARMOR-ELECTRICAL - host tests of what the base board adds: the Zigbee radio's bridge, the relay outputs and the analog inputs (settings, rules and the message).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
// No radio, relay or ADC was used: the samples and the pins are stand-ins.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../core/analog.hpp"
#include "../core/electrical_config.hpp"
#include "../core/electrical_json.hpp"
#include "../core/relay_bank.hpp"
#include "../core/zigbee_bridge.hpp"

static int failures = 0;
static int checks = 0;
#define CHECK(condition)                                                              \
  do {                                                                                \
    ++checks;                                                                         \
    if (!(condition)) {                                                               \
      ++failures;                                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);                \
    }                                                                                 \
  } while (0)

using namespace armor;

static config::Settings valid_settings() {
  config::Settings s = config::default_settings("a1b2c3");
  s.sta.enabled = true;
  s.sta.ssid = "casa";
  s.sta.password = "una-clave-larga";
  return s;
}
static bool has(const config::Problems& problems, const std::string& path, const std::string& code) {
  for (const config::Problem& p : problems) if (p.path == path && p.code == code) return true;
  return false;
}
static bool near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

// ---- the defaults and the pins -------------------------------------------------------------------------------------------------------

static void test_defaults() {
  const config::Settings s = config::default_settings("a1b2c3");
  CHECK(!s.zigbee.enabled && s.zigbee.baud == 115200 && s.zigbee.port == 8888 && s.zigbee.allowed_client.empty());
  CHECK(s.zigbee.rx == board::kZigbeePins.rx && s.zigbee.tx == board::kZigbeePins.tx && s.zigbee.reset == board::kZigbeePins.reset && s.zigbee.bootloader == board::kZigbeePins.bootloader);
  CHECK(!s.relays[0].enabled && s.relays[0].name == "relay1" && s.relays[7].name == "relay8" && s.relays[0].pin == board::kRelayPins[0]);
  CHECK(!s.analogs[0].enabled && s.analogs[0].channel == "analog1" && s.analogs[7].channel == "analog8" && s.analogs[0].quantity == "voltage" && s.analogs[0].mode == "dc");
  // nothing is switched on by the defaults: a node that was never set up has no bridge, no relay and no input
  for (const config::Problem& problem : config::validate(s)) CHECK(problem.path == "sta.enabled");   // only the Wi-Fi board needs a way in chosen
  CHECK(config::validate(valid_settings()).empty());
  // the default pins are real, free of each other and never a reserved pin
  std::vector<int> used{s.bus.rx, s.bus.tx, s.zigbee.rx, s.zigbee.tx, s.zigbee.reset, s.zigbee.bootloader};
  for (int pin : board::kRelayPins) used.push_back(pin);
  for (int pin : board::kAnalogPins) if (pin >= 0) used.push_back(pin);
  for (std::size_t i = 0; i < used.size(); ++i) {
    CHECK(board::assignable(used[i]));
    for (std::size_t k = i + 1; k < used.size(); ++k) CHECK(used[i] != used[k]);
  }
  for (int pin : board::kAnalogPins) if (pin >= 0) CHECK(board::analog_capable(pin));
  // the analog pins are the ADC1 ones: GPIO 1 to 10, and only those the board leaves free
  CHECK(!board::analog_capable(0) && !board::analog_capable(11) && !board::analog_capable(16) && !board::analog_capable(48) && !board::analog_capable(-1));
  CHECK(board::analog_capable(1) && board::adc1_channel(1) == 0 && board::adc1_channel(10) == 9);
#if defined(ARMOR_BOARD_S3_ETH)
  CHECK(!board::analog_capable(9) && !board::analog_capable(10) && !board::analog_capable(8));   // the W5500 and the camera connector
#else
  CHECK(board::analog_capable(9) && board::analog_capable(10) && board::analog_capable(8));
#endif
}

// ---- the Zigbee bridge settings ------------------------------------------------------------------------------------------------------

static void test_zigbee_settings() {
  config::Settings s = valid_settings();
  s.zigbee.enabled = true;
  // on, it needs the one client that may connect
  CHECK(has(config::validate(s), "zigbee.allowed_client", "required"));
  s.zigbee.allowed_client = "192.168.1.10";
  CHECK(config::validate(s).empty());
  for (const char* bad : {"192.168.1", "192.168.1.10/24", "0.0.0.0", "127.0.0.1", "224.0.0.1", "host", "192.168.1.010", "1.2.3.4.5", "999.1.1.1"}) {
    s.zigbee.allowed_client = bad;
    CHECK(has(config::validate(s), "zigbee.allowed_client", "invalid"));
  }
  s.zigbee.allowed_client = "192.168.1.10";
  // the radio's firmware speaks 115200 (a Z-Stack coordinator) or 230400 (the maker's own)
  for (int baud : {115200, 230400}) { s.zigbee.baud = baud; CHECK(config::validate(s).empty()); }
  for (int baud : {9600, 57600, 460800}) { s.zigbee.baud = baud; CHECK(has(config::validate(s), "zigbee.baud", "range")); }
  s.zigbee.baud = 115200;
  // the port is not one of the system's
  for (int port : {80, 443, 1023}) { s.zigbee.port = port; CHECK(has(config::validate(s), "zigbee.port", "range")); }
  for (int port : {1024, 8888, 65535}) { s.zigbee.port = port; CHECK(config::validate(s).empty()); }
  s.zigbee.port = 8888;
  // its pins: required, not reserved, not shared with the meters' line or each other; the two control lines are optional
  s.zigbee.rx = -1; CHECK(has(config::validate(s), "zigbee.rx", "required")); s.zigbee.rx = 18;
  s.zigbee.tx = 33; CHECK(has(config::validate(s), "zigbee.tx", "reserved")); s.zigbee.tx = 17;
  s.zigbee.rx = s.bus.rx; s.meters[0].enabled = true;
  CHECK(has(config::validate(s), "zigbee.rx", "conflict") || has(config::validate(s), "bus.rx", "conflict"));
  s.meters[0].enabled = false; s.zigbee.rx = 18;
  s.zigbee.reset = -1; s.zigbee.bootloader = -1; CHECK(config::validate(s).empty());
  s.zigbee.reset = 17; CHECK(has(config::validate(s), "zigbee.reset", "conflict")); s.zigbee.reset = board::kZigbeePins.reset;
  s.zigbee.bootloader = 22; CHECK(has(config::validate(s), "zigbee.bootloader", "reserved")); s.zigbee.bootloader = board::kZigbeePins.bootloader;
  CHECK(config::validate(s).empty());
  // the second client (an alarm panel that takes the radio over): optional, a usable address, not the first one
  CHECK(s.zigbee.fallback_client.empty() && s.zigbee.fallback_after_s == 30);
  s.zigbee.fallback_client = "192.168.1.20"; CHECK(config::validate(s).empty());
  for (const char* bad : {"192.168.1", "0.0.0.0", "host", "192.168.1.20/24"}) { s.zigbee.fallback_client = bad; CHECK(has(config::validate(s), "zigbee.fallback_client", "invalid")); }
  s.zigbee.fallback_client = s.zigbee.allowed_client; CHECK(has(config::validate(s), "zigbee.fallback_client", "conflict"));
  s.zigbee.fallback_client = "192.168.1.20";
  for (int seconds : {5, 30, 600}) { s.zigbee.fallback_after_s = seconds; CHECK(config::validate(s).empty()); }
  json::Value one;
  config::Problems found;
  config::Settings read = s;
  CHECK(json::parse("{\"zigbee\":{\"fallback_after_s\":4}}", one)); config::read_settings(one, read, found); CHECK(has(found, "zigbee.fallback_after_s", "range"));
  s.zigbee.fallback_after_s = 30; s.zigbee.fallback_client = "";
  // off, none of it is checked: a node that does not use the bridge is not held back by what is left in it
  s.zigbee.enabled = false; s.zigbee.allowed_client = ""; s.zigbee.rx = -1; s.zigbee.baud = 1;
  CHECK(config::validate(s).empty());
}

// ---- the relays ----------------------------------------------------------------------------------------------------------------------

static void test_relay_settings() {
  config::Settings s = valid_settings();
  s.relays[0] = {true, "lights", "Hall lights", board::kRelayPins[0], false};
  s.relays[1] = {true, "pump", "", board::kRelayPins[1], true};
  CHECK(config::validate(s).empty());
  s.relays[1].name = "lights"; CHECK(has(config::validate(s), "relays.1.name", "conflict"));
  s.relays[1].name = "Bad Name"; CHECK(has(config::validate(s), "relays.1.name", "invalid"));
  s.relays[1].name = ""; CHECK(has(config::validate(s), "relays.1.name", "required"));
  s.relays[1].name = "pump";
  s.relays[1].pin = -1; CHECK(has(config::validate(s), "relays.1.pin", "required"));
  s.relays[1].pin = 30; CHECK(has(config::validate(s), "relays.1.pin", "reserved"));
  s.relays[1].pin = board::kRelayPins[0]; CHECK(has(config::validate(s), "relays.1.pin", "conflict"));
  s.relays[1].pin = board::kRelayPins[1];
  CHECK(config::validate(s).empty());
  // a relay that is not enabled is not checked
  s.relays[1].enabled = false; s.relays[1].name = ""; s.relays[1].pin = 30;
  CHECK(config::validate(s).empty());
  // a relay's pin may not be the radio's either
  s.relays[1] = {true, "pump", "", board::kZigbeePins.reset, false}; s.zigbee.enabled = true; s.zigbee.allowed_client = "192.168.1.10";
  CHECK(has(config::validate(s), "relays.1.pin", "conflict") || has(config::validate(s), "zigbee.reset", "conflict"));
}

static void test_relay_bank() {
  config::Settings s = valid_settings();
  s.relays[0] = {true, "lights", "", board::kRelayPins[0], false};
  s.relays[1] = {true, "pump", "", board::kRelayPins[1], true};
  relays::Bank bank(s);
  // every relay starts open; an active-low relay's open level is a high pin
  CHECK(!bank.is_on(0) && !bank.is_on(1));
  CHECK(!bank.pin_level(0) && bank.pin_level(1) && !bank.open_level(0) && bank.open_level(1));
  CHECK(bank.set(0, true) && bank.is_on(0) && bank.pin_level(0));
  CHECK(bank.set(1, true) && bank.is_on(1) && !bank.pin_level(1));   // closed on a low pin
  CHECK(bank.set(1, false) && bank.pin_level(1));
  // a relay that is not enabled, or does not exist, cannot be told anything and never reads as closed
  CHECK(!bank.set(2, true) && !bank.is_on(2) && !bank.pin_level(2));
  CHECK(!bank.set(8, true) && !bank.set(99, true) && !bank.is_on(99) && bank.pin(99) == -1);
  bank.open_all();
  CHECK(!bank.is_on(0) && !bank.pin_level(0));
  // a new bank (the node restarted) starts open again whatever the old one held
  bank.set(0, true);
  relays::Bank restarted(s);
  CHECK(!restarted.is_on(0));
  CHECK(bank.json().find("\"name\":\"lights\"") != std::string::npos && bank.json().find("\"on\":true") != std::string::npos);
  json::Value document;
  CHECK(json::parse(bank.json(), document) && document.is_array() && document.items.size() == 8);
}

// ---- the analog inputs ---------------------------------------------------------------------------------------------------------------

static config::AnalogSetting input(const char* quantity, const char* mode, double scale) {
  config::AnalogSetting a;
  a.enabled = true; a.channel = "x"; a.pin = 1; a.quantity = quantity; a.mode = mode; a.scale = scale;
  return a;
}

static void test_analog_settings() {
  config::Settings s = valid_settings();
  s.analogs[0] = input("voltage", "dc", 11.0); s.analogs[0].channel = "battery"; s.analogs[0].pin = 1;
  s.analogs[1] = input("current", "ac", 30.0); s.analogs[1].channel = "oven"; s.analogs[1].pin = 2; s.analogs[1].nominal_voltage_v = 230.0;
  CHECK(config::validate(s).empty());
  s.analogs[1].channel = "battery"; CHECK(has(config::validate(s), "analogs.1.channel", "conflict")); s.analogs[1].channel = "oven";
  s.analogs[1].channel = "Bad"; CHECK(has(config::validate(s), "analogs.1.channel", "invalid")); s.analogs[1].channel = "oven";
  s.analogs[1].pin = 1; CHECK(has(config::validate(s), "analogs.1.pin", "conflict")); s.analogs[1].pin = 2;
  s.analogs[1].pin = 16; CHECK(has(config::validate(s), "analogs.1.pin", "not_analog")); s.analogs[1].pin = 2;   // ADC2 is never offered
  s.analogs[1].pin = 33; CHECK(has(config::validate(s), "analogs.1.pin", "reserved")); s.analogs[1].pin = 2;
  s.analogs[1].pin = -1; CHECK(has(config::validate(s), "analogs.1.pin", "required")); s.analogs[1].pin = 2;
  s.analogs[1].quantity = "power"; CHECK(has(config::validate(s), "analogs.1.quantity", "invalid")); s.analogs[1].quantity = "current";
  s.analogs[1].mode = "rms"; CHECK(has(config::validate(s), "analogs.1.mode", "invalid")); s.analogs[1].mode = "ac";
  s.analogs[1].scale = 0.0; CHECK(has(config::validate(s), "analogs.1.scale", "range")); s.analogs[1].scale = 30.0;
  s.analogs[1].offset_v = 1.65; CHECK(has(config::validate(s), "analogs.1.offset_v", "invalid")); s.analogs[1].offset_v = 0.0;   // an AC input centres itself
  s.analogs[0].nominal_voltage_v = 12.0; CHECK(has(config::validate(s), "analogs.0.nominal_voltage_v", "invalid")); s.analogs[0].nominal_voltage_v = 0.0;   // only a current has one
  CHECK(config::validate(s).empty());
  // the pin may not be one the meters' line or the radio uses
  s.meters[0].enabled = true; s.analogs[0].pin = s.bus.rx;
  CHECK(has(config::validate(s), "analogs.0.pin", "not_analog") || has(config::validate(s), "analogs.0.pin", "conflict"));
  s.meters[0].enabled = false; s.analogs[0].pin = 1;
  // the message holds sixteen channels: the meters and the inputs together may not pass them
  for (std::size_t i = 0; i < 16; ++i) s.meters[i].enabled = true;
  s.meters[0] = {true, "grid", "", "ac", 1};
  for (std::size_t i = 0; i < 16; ++i) { s.meters[i].channel = "m" + std::to_string(i); s.meters[i].address = static_cast<int>(i) + 1; }
  CHECK(has(config::validate(s), "analogs", "too_many"));
  s.meters[15].enabled = false; s.meters[14].enabled = false;
  CHECK(!has(config::validate(s), "analogs", "too_many"));
}

static void test_analog_values() {
  // a DC voltage through a 1:11 divider: 1.000 V at the pin is 11 V
  config::AnalogSetting v = input("voltage", "dc", 11.0);
  std::vector<double> flat(64, 1000.0);
  CHECK(near(analog::quantity_of(v, flat), 11.0, 1e-9));
  // a sensor with a mid-supply bias: 2.5 V at zero, 100 mV per amp (10 A per volt) -> 2.6 V is 1 A
  config::AnalogSetting hall = input("current", "dc", 10.0); hall.offset_v = 2.5;
  CHECK(near(analog::quantity_of(hall, std::vector<double>(64, 2600.0)), 1.0, 1e-9));
  CHECK(near(analog::quantity_of(hall, std::vector<double>(64, 2400.0)), -1.0, 1e-9));   // the other way round is negative
  // an AC clamp: a sine of 1.0 V peak around 1.65 V is 0.7071 V RMS, 30 A per volt -> 21.2 A
  config::AnalogSetting clamp = input("current", "ac", 30.0);
  std::vector<double> sine;
  for (int i = 0; i < 400; ++i) sine.push_back(1650.0 + 1000.0 * std::sin(2.0 * 3.14159265358979 * i / 100.0));   // four whole periods
  CHECK(near(analog::quantity_of(clamp, sine), 30.0 * 0.70710678, 0.01));
  // the bias does not matter to an AC reading, and the sign of the scale does not either
  std::vector<double> shifted;
  for (double x : sine) shifted.push_back(x + 300.0);
  CHECK(near(analog::quantity_of(clamp, shifted), analog::quantity_of(clamp, sine), 1e-6));
  config::AnalogSetting negative = clamp; negative.scale = -30.0;
  CHECK(near(analog::quantity_of(negative, sine), analog::quantity_of(clamp, sine), 1e-6));
  // a flat line is no current; the noise floor is shown as zero
  CHECK(near(analog::quantity_of(clamp, std::vector<double>(100, 1650.0)), 0.0, 1e-9));
  config::AnalogSetting quiet = clamp; quiet.zero_below = 0.5;
  std::vector<double> noise;
  for (int i = 0; i < 200; ++i) noise.push_back(1650.0 + (i % 2 == 0 ? 3.0 : -3.0));   // 3 mV -> 0.09 A
  CHECK(analog::quantity_of(clamp, noise) > 0.05 && analog::quantity_of(quiet, noise) == 0.0);
  CHECK(analog::quantity_of(clamp, {}) == 0.0);
}

static void test_analog_channels() {
  analog::Energy energy;
  electrical::AnalogChannel channel;
  config::AnalogSetting v = input("voltage", "dc", 11.0); v.channel = "battery"; v.label = "Battery";
  CHECK(!analog::make_channel(v, {}, 1000, energy, channel));   // no samples, no channel
  CHECK(analog::make_channel(v, std::vector<double>(8, 1100.0), 1000, energy, channel));
  CHECK(channel.id == "battery" && channel.has_voltage && !channel.has_current && !channel.has_power && !channel.has_energy && near(channel.voltage_v, 12.1, 1e-9) && !channel.ac);
  // a reversed or absurd voltage never leaves the contract's range (0 to 1000)
  v.scale = -11.0; CHECK(analog::make_channel(v, std::vector<double>(8, 1100.0), 1000, energy, channel) && channel.voltage_v == 0.0);
  v.scale = 1.0e5; CHECK(analog::make_channel(v, std::vector<double>(8, 3000.0), 1000, energy, channel) && channel.voltage_v == 1000.0);
  // a current with a nominal voltage makes a power and an energy; without one only the current
  config::AnalogSetting c = input("current", "ac", 30.0); c.channel = "oven"; c.nominal_voltage_v = 230.0;
  std::vector<double> sine;
  for (int i = 0; i < 400; ++i) sine.push_back(1650.0 + 1000.0 * std::sin(2.0 * 3.14159265358979 * i / 100.0));
  analog::Energy oven;
  CHECK(analog::make_channel(c, sine, 3600000, oven, channel));   // an hour at this current
  CHECK(channel.ac && channel.has_current && channel.has_power && channel.has_energy && !channel.has_voltage);
  CHECK(near(channel.current_a, 21.213, 0.01) && near(channel.power_w, 21.213 * 230.0, 5.0) && near(channel.energy_kwh, 21.213 * 230.0 / 1000.0, 0.01));
  const double after_one = channel.energy_kwh;
  CHECK(analog::make_channel(c, std::vector<double>(100, 1650.0), 3600000, oven, channel));   // nothing flows for the next hour: the energy stays
  CHECK(channel.current_a == 0.0 && channel.power_w == 0.0 && near(channel.energy_kwh, after_one, 1e-9));
  c.nominal_voltage_v = 0.0;
  analog::Energy none;
  CHECK(analog::make_channel(c, sine, 1000, none, channel) && channel.has_current && !channel.has_power && !channel.has_energy);
  // a DC current feeding the network (negative power) never lowers the energy
  config::AnalogSetting back = input("current", "dc", 10.0); back.channel = "back"; back.offset_v = 2.5; back.nominal_voltage_v = 12.0;
  analog::Energy dc;
  CHECK(analog::make_channel(back, std::vector<double>(8, 2400.0), 3600000, dc, channel) && channel.current_a < 0.0 && channel.power_w < 0.0 && channel.energy_kwh == 0.0);
}

static void test_message() {
  // the analog channels follow the meters' in the message, and the message is the contract's JSON
  electrical::AnalogChannel a;
  a.id = "battery"; a.label = "Battery"; a.has_voltage = true; a.voltage_v = 12.34;
  electrical::AnalogChannel b;
  b.id = "oven"; b.ac = true; b.has_current = true; b.current_a = 21.2; b.has_power = true; b.power_w = 4876.0; b.has_energy = true; b.energy_kwh = 1.25;
  const std::string text = electrical::message_json("electrical-1", 1000, false, {}, {}, "", {a, b});
  json::Value message;
  CHECK(json::parse(text, message));
  const json::Value* channels = message.get("channels");
  CHECK(channels != nullptr && channels->is_array() && channels->items.size() == 2);
  if (channels != nullptr && channels->items.size() == 2) {
    const json::Value& first = channels->items[0];
    CHECK(first.string_or("id", "") == "battery" && first.string_or("domain", "") == "dc" && first.get("voltage_v") != nullptr && first.get("current_a") == nullptr && first.get("power_w") == nullptr && first.get("energy_kwh") == nullptr);
    const json::Value& second = channels->items[1];
    CHECK(second.string_or("domain", "") == "ac" && second.get("voltage_v") == nullptr && second.number_or("current_a", 0) > 21.0 && second.number_or("energy_kwh", 0) > 1.2 && second.get("label") == nullptr);
  }
  // no more than sixteen channels in all
  std::vector<electrical::AnalogChannel> many;
  for (int i = 0; i < 20; ++i) { electrical::AnalogChannel c; c.id = "a" + std::to_string(i); c.has_voltage = true; many.push_back(c); }
  json::Value capped;
  CHECK(json::parse(electrical::message_json("electrical-1", 1, false, {}, {}, "", many), capped) && capped.get("channels")->items.size() == 16);
}

// ---- the bridge ----------------------------------------------------------------------------------------------------------------------

static void test_bridge() {
  // only the named address connects
  CHECK(zigbee::client_allowed("192.168.1.10", (192u << 24) | (168u << 16) | (1u << 8) | 10u));
  CHECK(!zigbee::client_allowed("192.168.1.10", (192u << 24) | (168u << 16) | (1u << 8) | 11u));
  CHECK(!zigbee::client_allowed("", 0) && !zigbee::client_allowed("", (192u << 24) | (168u << 16) | (1u << 8) | 10u));   // nothing named, nobody allowed
  CHECK(!zigbee::client_allowed("not an address", 0) && !zigbee::client_allowed("192.168.1.10/24", (192u << 24) | (168u << 16) | (1u << 8) | 10u));

  // a plain reset never touches the boot line; the way into the boot loader holds it low while RESET_N rises and lets both go at the end
  const std::vector<zigbee::Step> plain = zigbee::radio_sequence(false);
  CHECK(plain.size() == 2 && plain[0].reset_low && !plain[1].reset_low);
  for (const zigbee::Step& step : plain) CHECK(!step.bootloader_low);
  CHECK(plain[0].hold_ms >= 10 && plain.back().hold_ms == 0);
  const std::vector<zigbee::Step> boot = zigbee::radio_sequence(true);
  CHECK(boot.size() == 3 && boot[0].reset_low && boot[0].bootloader_low);                 // both low: the radio is held in reset with the boot line already low
  CHECK(!boot[1].reset_low && boot[1].bootloader_low && boot[1].hold_ms >= 100);          // RESET_N released with the boot line still low, long enough to be seen
  CHECK(!boot[2].reset_low && !boot[2].bootloader_low && boot[2].hold_ms == 0);           // both let go
  for (const std::vector<zigbee::Step>* sequence : {&plain, &boot}) CHECK(!sequence->back().reset_low && !sequence->back().bootloader_low);   // a sequence never leaves a line held

  // two clients: the primary (the server) and the fallback (a panel); everybody else is none
  config::ZigbeeSetting two; two.enabled = true; two.allowed_client = "192.168.1.10"; two.fallback_client = "192.168.1.20";
  const std::uint32_t server = (192u << 24) | (168u << 16) | (1u << 8) | 10u, panel = (192u << 24) | (168u << 16) | (1u << 8) | 20u, other = (192u << 24) | (168u << 16) | (1u << 8) | 30u;
  CHECK(zigbee::classify(two, server) == zigbee::Role::kPrimary && zigbee::classify(two, panel) == zigbee::Role::kFallback && zigbee::classify(two, other) == zigbee::Role::kNone);
  config::ZigbeeSetting one_only = two; one_only.fallback_client.clear();
  CHECK(zigbee::classify(one_only, panel) == zigbee::Role::kNone && zigbee::classify(one_only, server) == zigbee::Role::kPrimary);
  CHECK(std::string(zigbee::role_text(zigbee::Role::kPrimary)) == "primary" && std::string(zigbee::role_text(zigbee::Role::kFallback)) == "fallback" && std::string(zigbee::role_text(zigbee::Role::kNone)).empty());
  using zigbee::Role; using Verdict = zigbee::Arbiter::Verdict;
  {
    zigbee::Arbiter arbiter(30000, 1000);   // the bridge started at 1 s; the panel waits 30 s
    CHECK(arbiter.offer(Role::kNone, 100000) == Verdict::kRefuse);
    CHECK(arbiter.offer(Role::kFallback, 20000) == Verdict::kRefuse);              // the primary has been away only 19 s since the start
    CHECK(arbiter.offer(Role::kFallback, 31000) == Verdict::kAccept);              // 30 s of absence
    CHECK(arbiter.offer(Role::kPrimary, 2000) == Verdict::kAccept);                // the primary may always connect
    arbiter.accepted(Role::kPrimary, 2000);
    CHECK(arbiter.current() == Role::kPrimary && arbiter.takeovers() == 0);
    CHECK(arbiter.offer(Role::kFallback, 500000) == Verdict::kRefuse);             // never while the primary is connected, however long
    arbiter.released(100000);                                                      // the primary leaves
    CHECK(arbiter.current() == Role::kNone && arbiter.offer(Role::kFallback, 120000) == Verdict::kRefuse && arbiter.offer(Role::kFallback, 130000) == Verdict::kAccept);
    arbiter.accepted(Role::kFallback, 130000);
    CHECK(arbiter.current() == Role::kFallback && arbiter.takeovers() == 1);
    CHECK(arbiter.offer(Role::kFallback, 140000) == Verdict::kAccept);             // the panel itself reconnecting replaces its old connection
    CHECK(arbiter.offer(Role::kPrimary, 140000) == Verdict::kAccept);              // and the primary takes the line back
    arbiter.accepted(Role::kPrimary, 140000);
    CHECK(arbiter.current() == Role::kPrimary && arbiter.takeovers() == 1);
    arbiter.released(200000);                                                      // the clock of the absence restarts at each departure of the primary
    CHECK(arbiter.offer(Role::kFallback, 210000) == Verdict::kRefuse && arbiter.offer(Role::kFallback, 230000) == Verdict::kAccept);
    // the fallback leaving does not restart that clock
    arbiter.accepted(Role::kFallback, 230000);
    arbiter.released(240000);
    CHECK(arbiter.offer(Role::kFallback, 241000) == Verdict::kAccept);
  }

  // what the panel shows
  config::ZigbeeSetting setting; setting.enabled = true; setting.allowed_client = "192.168.1.10";
  zigbee::Status status; status.state = "connected"; status.client = "192.168.1.10"; status.to_radio = 120; status.from_radio = 4096; status.connections = 2; status.refused = 1; status.has_reset = true;
  json::Value shown;
  CHECK(json::parse(zigbee::status_json(setting, status), shown));
  CHECK(shown.string_or("state", "") == "connected" && shown.integer_or("port", 0, 0, 65535) == 8888 && shown.integer_or("from_radio", 0, 0, 1000000) == 4096 && shown.integer_or("refused", 0, 0, 100) == 1);
  CHECK(shown.bool_or("has_reset", false) && !shown.bool_or("has_bootloader", true));
  CHECK(shown.string_or("role", "x").empty() && shown.integer_or("takeovers", -1, 0, 100) == 0 && shown.integer_or("fallback_after_s", 0, 0, 1000) == 30 && shown.get("fallback_client") != nullptr);
}

// ---- the stored document -------------------------------------------------------------------------------------------------------------

static void test_document() {
  config::Settings s = valid_settings();
  s.zigbee.enabled = true; s.zigbee.allowed_client = "192.168.1.10"; s.zigbee.baud = 230400; s.zigbee.port = 6638;
  s.relays[0] = {true, "lights", "Hall", board::kRelayPins[0], true};
  s.analogs[0] = input("current", "ac", 30.0); s.analogs[0].channel = "oven"; s.analogs[0].pin = 1; s.analogs[0].zero_below = 0.25; s.analogs[0].nominal_voltage_v = 230.0;
  s.analogs[1] = input("voltage", "dc", 11.0); s.analogs[1].channel = "battery"; s.analogs[1].pin = 2; s.analogs[1].offset_v = 0.05;
  CHECK(config::validate(s).empty());
  const std::string text = config::to_json(s, true);
  config::Settings back;
  config::Problems problems;
  CHECK(config::load(text, config::default_settings("a1b2c3"), back, problems) && problems.empty());
  CHECK(back.zigbee.enabled && back.zigbee.baud == 230400 && back.zigbee.port == 6638 && back.zigbee.allowed_client == "192.168.1.10" && back.zigbee.reset == board::kZigbeePins.reset && back.zigbee.bootloader == board::kZigbeePins.bootloader);
  CHECK(back.relays[0].enabled && back.relays[0].name == "lights" && back.relays[0].label == "Hall" && back.relays[0].active_low && back.relays[0].pin == board::kRelayPins[0] && !back.relays[1].enabled);
  CHECK(back.analogs[0].enabled && back.analogs[0].quantity == "current" && back.analogs[0].mode == "ac" && near(back.analogs[0].scale, 30.0, 1e-9) && near(back.analogs[0].zero_below, 0.25, 1e-9) && near(back.analogs[0].nominal_voltage_v, 230.0, 1e-9));
  CHECK(back.analogs[1].quantity == "voltage" && near(back.analogs[1].offset_v, 0.05, 1e-9) && back.analogs[1].pin == 2);
  CHECK(config::to_json(back, true) == text);

  // the panel's copy is the same document (it never held a password to lose)
  CHECK(config::to_json(s, false).find("\"allowed_client\":\"192.168.1.10\"") != std::string::npos);

  // a document that only changes one thing keeps the rest; a wrong type or range is refused by name
  config::Settings partial;
  problems.clear();
  CHECK(config::load("{\"zigbee\":{\"port\":7000}}", s, partial, problems) && partial.zigbee.port == 7000 && partial.zigbee.allowed_client == "192.168.1.10" && partial.relays[0].name == "lights");
  problems.clear();
  CHECK(!config::load("{\"zigbee\":{\"port\":\"x\"}}", s, partial, problems) && has(problems, "zigbee.port", "invalid"));
  problems.clear();
  CHECK(!config::load("{\"zigbee\":{\"port\":70000}}", s, partial, problems) && has(problems, "zigbee.port", "range"));
  problems.clear();
  CHECK(!config::load("{\"analogs\":[{\"scale\":\"big\"}]}", s, partial, problems) && has(problems, "analogs.0.scale", "invalid"));
  problems.clear();
  CHECK(!config::load("{\"analogs\":[{\"scale\":1e9}]}", s, partial, problems) && has(problems, "analogs.0.scale", "range"));
  problems.clear();
  CHECK(!config::load("{\"relays\":[{\"pin\":99}]}", s, partial, problems) && has(problems, "relays.0.pin", "range"));
  problems.clear();
  CHECK(!config::load("{\"relays\":\"all\"}", s, partial, problems) && has(problems, "relays", "invalid"));
  std::string nine = "{\"relays\":[";
  for (int i = 0; i < 9; ++i) nine += std::string(i ? "," : "") + "{}";
  problems.clear();
  CHECK(!config::load(nine + "]}", s, partial, problems) && has(problems, "relays", "invalid"));
  // an older document, from before the base board, still loads and has none of it on
  problems.clear();
  config::Settings old;
  CHECK(config::load("{\"v\":1,\"node\":{\"id\":\"electrical-a1b2c3\",\"name\":\"x\"},\"sta\":{\"enabled\":true,\"ssid\":\"casa\",\"password\":\"una-clave-larga\"}}", config::default_settings("a1b2c3"), old, problems) && !old.zigbee.enabled && !old.relays[0].enabled && !old.analogs[0].enabled);
}

int main() {
  test_defaults();
  test_zigbee_settings();
  test_relay_settings();
  test_relay_bank();
  test_analog_settings();
  test_analog_values();
  test_analog_channels();
  test_message();
  test_bridge();
  test_document();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
