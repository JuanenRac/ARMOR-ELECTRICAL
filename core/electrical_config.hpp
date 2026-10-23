// ARMOR-NODE-ELECTRICAL - the settings of an electrical node: what the web panel edits, what is stored in flash and what the firmware obeys.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// One document (JSON) holds everything that differs from node to node, so the same firmware image serves every node and nothing secret has to be
// compiled in. This file only reads, checks and writes that document; it touches no hardware, so all of it is tested on a computer. Passwords are never
// written back to the panel: a section sent without a password (or with an empty one) keeps the stored one, and "password_set" tells the panel that
// there is one.
//
// A node has one serial line (a hardware UART) with up to sixteen energy meters on it, each with its own address and the channel it makes in the message; a second, independent
// line (UART0, the console being on the USB) lets the PZEM-004T meters (TTL) and the PZEM-017 (RS485) work together. Each line can name a driver-enable pin for an RS485 adapter.
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

#include "base_board.hpp"
#include "board_s3.hpp"
#include "config_common.hpp"
#include "io_config.hpp"
#include "node_settings.hpp"
#include "json.hpp"
#include "net_text.hpp"
#include "node_id.hpp"
#include "stored_load.hpp"

namespace armor::config {

constexpr std::size_t kMeterCount = 16;
constexpr std::size_t kMaxChannels = 16;   // what one message carries: the meters and the analog inputs together

// The serial line the meters share (a hardware UART; the PZEM meters talk 9600 baud 8N1).
struct BusSetting {
  int rx = -1;        // the GPIO that receives the meters' TX line
  int tx = -1;        // the GPIO that sends to the meters' RX line
  int de = -1;        // the GPIO of an RS485 adapter's driver-enable (and receiver-enable) pins: the UART drives it high while it sends (half duplex); -1: none (TTL or an adapter that turns itself)
  int baud = 9600;
  int poll_s = 2;     // seconds between two rounds of readings (the second line takes the first one's)
};

// One meter on the line: which channel of the message it makes, and how to talk to it.
struct MeterSetting {
  bool enabled = false;
  std::string channel;     // the channel id in the message (armor/electrical/<node>/state): lowercase letters, digits, - and _
  std::string label;       // its name, or empty
  std::string model = "ac";   // "ac" (PZEM-004T v3) or "dc" (PZEM-017)
  int address = 1;         // its Modbus address on its line (1 to 247)
  int line = 1;            // which serial line it is on: 1 or 2
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

struct Settings : NodeSettings, IoSettings {   // what every node has (core/node_settings.hpp) and the base board's own (core/io_config.hpp), plus the line of the meters and the analog inputs
  BusSetting bus;
  BusSetting bus2;   // the second line; unused while no meter says line 2
  std::array<MeterSetting, kMeterCount> meters;
  std::array<AnalogSetting, board::kAnalogCount> analogs;
};

// The speeds the line may use.
inline const std::vector<int>& allowed_bauds() {
  static const std::vector<int> speeds{1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
  return speeds;
}

// ---- the defaults of a node that has never been configured ---------------------------------------------------------------------

// `mac_tail` is the last three bytes of the node's MAC as six lowercase hexadecimal digits: it makes the first identity unique.
inline Settings default_settings(std::string_view mac_tail) {
  Settings s;
  node_defaults(s, mac_tail);
  s.bus.rx = board::kDefaultPins[0].rx;
  s.bus.tx = board::kDefaultPins[0].tx;
  for (std::size_t i = 0; i < kMeterCount; ++i) s.meters[i].channel = "meter" + std::to_string(i + 1);
  io_defaults(s);
  for (std::size_t i = 0; i < board::kAnalogCount; ++i) { s.analogs[i].channel = "analog" + std::to_string(i + 1); s.analogs[i].pin = board::kAnalogPins[i]; }
  return s;
}

// ---- reading -------------------------------------------------------------------------------------------------------------------

// Applies the members present in `document` on top of `settings`; whatever the document omits stays as it was. The result is checked
// with validate(): read_settings() only reports a member of the wrong type or size.
inline void read_settings(const json::Value& document, Settings& s, Problems& problems) {
  using namespace detail;
  if (!document.is_object()) { bad(problems, "", "invalid"); return; }
  node_read(document, s, problems);
  if (const json::Value* bus = document.get("bus"); bus != nullptr && bus->is_object()) {
    read_int(*bus, "rx", s.bus.rx, -1, board::kLastGpio, "bus.rx", problems);
    read_int(*bus, "tx", s.bus.tx, -1, board::kLastGpio, "bus.tx", problems);
    read_int(*bus, "de", s.bus.de, -1, board::kLastGpio, "bus.de", problems);
    read_int(*bus, "baud", s.bus.baud, 1200, 115200, "bus.baud", problems);
    read_int(*bus, "poll_s", s.bus.poll_s, 1, 3600, "bus.poll_s", problems);
  }
  if (const json::Value* bus2 = document.get("bus2"); bus2 != nullptr && bus2->is_object()) {
    read_int(*bus2, "rx", s.bus2.rx, -1, board::kLastGpio, "bus2.rx", problems);
    read_int(*bus2, "tx", s.bus2.tx, -1, board::kLastGpio, "bus2.tx", problems);
    read_int(*bus2, "de", s.bus2.de, -1, board::kLastGpio, "bus2.de", problems);
    read_int(*bus2, "baud", s.bus2.baud, 1200, 115200, "bus2.baud", problems);
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
      read_int(item, "line", meter.line, 1, 2, base + "line", problems);
    }
  }
  io_read(document, s, problems);
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
}

// ---- checking ------------------------------------------------------------------------------------------------------------------

inline Problems validate(const Settings& s) {
  using detail::bad;
  Problems problems;
  node_validate(s, problems);

  // the line and its meters: the pins may not be reserved nor shared, each meter has its own channel and its own address
  detail::PinClaims claims;
  const auto claim = [&](int gpio, const std::string& who, const std::string& path) {
    if (!board::assignable(gpio)) { bad(problems, path, "reserved"); return; }
    if (!claims.claim(gpio, who)) bad(problems, path, "conflict");
  };
  std::vector<std::string> channels;
  std::array<std::vector<int>, 2> addresses;   // an address only has to be its own on its line
  std::array<bool, 2> in_use{false, false};
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
    const int line = meter.line == 2 ? 1 : 0;
    if (meter.line != 1 && meter.line != 2) bad(problems, base + "line", "range");
    else in_use[line] = true;
    if (meter.address < 1 || meter.address > 247) bad(problems, base + "address", "range");
    else if (std::find(addresses[line].begin(), addresses[line].end(), meter.address) != addresses[line].end()) bad(problems, base + "address", "conflict");
    else addresses[line].push_back(meter.address);
    std::size_t label_characters = 0;
    if (!meter.label.empty() && (!net::valid_utf8(meter.label, &label_characters) || label_characters > 40)) bad(problems, base + "label", "invalid");
  }
  if (any) {
    if (s.bus.poll_s < 1 || s.bus.poll_s > 3600) bad(problems, "bus.poll_s", "range");
    const std::array<const BusSetting*, 2> lines{&s.bus, &s.bus2};
    const std::array<const char*, 2> names{"bus", "bus2"};
    for (int k = 0; k < 2; ++k) {
      if (!in_use[k]) continue;
      const BusSetting& line = *lines[k];
      const std::string name = names[k];
      if (line.rx < 0) bad(problems, name + ".rx", "required"); else claim(line.rx, name, name + ".rx");
      if (line.tx < 0) bad(problems, name + ".tx", "required"); else claim(line.tx, name, name + ".tx");
      if (line.de >= 0) claim(line.de, name, name + ".de");
      if (std::find(allowed_bauds().begin(), allowed_bauds().end(), line.baud) == allowed_bauds().end()) bad(problems, name + ".baud", "range");
    }
  }

  // the Zigbee radio, the relays and the broker moving them: the base board's own checks
  io_validate(s, s.mqtt.enabled, claim, problems);

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
  node_write_head(w, s, secrets);
  w.key("bus").begin_object().field("rx", s.bus.rx).field("tx", s.bus.tx).field("de", s.bus.de).field("baud", s.bus.baud).field("poll_s", s.bus.poll_s).end_object();
  w.key("bus2").begin_object().field("rx", s.bus2.rx).field("tx", s.bus2.tx).field("de", s.bus2.de).field("baud", s.bus2.baud).end_object();
  w.key("meters").begin_array();
  for (const MeterSetting& meter : s.meters) {
    w.begin_object().field("enabled", meter.enabled).field("channel", meter.channel).field("label", meter.label).field("model", meter.model).field("address", meter.address).field("line", meter.line).end_object();
  }
  w.end_array();
  io_write(w, s, secrets);
  w.key("analogs").begin_array();
  for (const AnalogSetting& input : s.analogs) {
    w.begin_object().field("enabled", input.enabled).field("channel", input.channel).field("label", input.label).field("pin", input.pin).field("quantity", input.quantity).field("mode", input.mode)
        .key("scale").number(input.scale).key("offset_v").number(input.offset_v).key("zero_below").number(input.zero_below).key("nominal_voltage_v").number(input.nominal_voltage_v).end_object();
  }
  w.end_array();
  node_write_tail(w, s);
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
