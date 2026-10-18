// ARMOR-ELECTRICAL - host tests of the edges the other tests pass by: what a damaged meter reply is called, what the analog inputs make of odd samples, and what the switch commands do with
// text that is nearly right, with a token that is spent, or with a clock that has moved on.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
// No meter, ADC or contactor is used: the bytes and the samples are written out in the tests, and the contactors are numbers.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../core/analog.hpp"
#include "../core/pzem.hpp"
#include "../core/pzem_bus.hpp"
#include "../core/switch_set.hpp"

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
using namespace armor::electrical;

// ---- the meter's reply ------------------------------------------------------------------------------------------------------------------------

static std::vector<std::uint8_t> sealed(std::vector<std::uint8_t> body) {
  const std::uint16_t crc = pzem::crc16(body.data(), body.size());
  body.push_back(static_cast<std::uint8_t>(crc & 0xFF));
  body.push_back(static_cast<std::uint8_t>(crc >> 8));
  return body;
}
// A reply of the AC meter: address, 0x04, 20, ten registers.
static std::vector<std::uint8_t> ac_reply(std::uint8_t address, const std::vector<std::uint16_t>& registers) {
  std::vector<std::uint8_t> body{address, 0x04, static_cast<std::uint8_t>(registers.size() * 2)};
  for (const std::uint16_t r : registers) { body.push_back(static_cast<std::uint8_t>(r >> 8)); body.push_back(static_cast<std::uint8_t>(r & 0xFF)); }
  return sealed(body);
}
static const std::vector<std::uint16_t> kAcRegisters = {2301, 1500, 0, 3450, 0, 12345, 0, 500, 98, 0};   // 230.1 V, 1.5 A, 345 W, 12.345 kWh, 50.0 Hz, 0.98, no alarm
static const std::vector<std::uint16_t> kDcRegisters = {1250, 1000, 5000, 0, 2500, 0, 0, 0};              // 12.50 V, 10.00 A, 500.0 W, 2.5 kWh

static void test_crc_and_requests() {
  const std::uint8_t nine[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  CHECK(pzem::crc16(nine, 9) == 0x4B37);                                       // the check value of CRC-16/MODBUS
  CHECK(pzem::crc16(nine, 0) == 0xFFFF);
  std::array<std::uint8_t, pzem::kRequestLength> request{};
  CHECK(pzem::build_read_request(pzem::Model::kAc, 1, request) && request[0] == 1 && request[1] == 0x04 && request[2] == 0 && request[3] == 0 && request[4] == 0 && request[5] == 10);
  CHECK(pzem::crc16(request.data(), 8) == 0);                                  // a message with its own CRC added checks to zero
  CHECK(pzem::build_read_request(pzem::Model::kDc, 247, request) && request[5] == 8 && pzem::build_read_request(pzem::Model::kAc, 0xF8, request) && request[0] == 0xF8);
  CHECK(!pzem::build_read_request(pzem::Model::kAc, 0, request) && !pzem::build_read_request(pzem::Model::kAc, 249, request) && !pzem::build_read_request(pzem::Model::kAc, 0xF9, request) && !pzem::build_read_request(pzem::Model::kAc, 0xFF, request));
  CHECK(pzem::register_count(pzem::Model::kAc) == 10 && pzem::register_count(pzem::Model::kDc) == 8 && pzem::reply_length(pzem::Model::kAc) == 25 && pzem::reply_length(pzem::Model::kDc) == 21);
}

static void test_decoding_and_its_refusals() {
  pzem::Reading reading;
  const std::vector<std::uint8_t> good = ac_reply(1, kAcRegisters);
  CHECK(good.size() == pzem::reply_length(pzem::Model::kAc));
  CHECK(pzem::decode(pzem::Model::kAc, 1, good.data(), good.size(), reading) == pzem::Result::kOk);
  CHECK(std::fabs(reading.voltage_v - 230.1) < 1e-9 && std::fabs(reading.current_a - 1.5) < 1e-9 && std::fabs(reading.power_w - 345.0) < 1e-9 && std::fabs(reading.energy_kwh - 12.345) < 1e-9 &&
        reading.has_frequency && reading.frequency_hz == 50.0 && reading.has_power_factor && std::fabs(reading.power_factor - 0.98) < 1e-9 && !reading.alarm);
  // the address 0xF8 takes the answer of whichever meter is there; any other must match
  CHECK(pzem::decode(pzem::Model::kAc, 0xF8, good.data(), good.size(), reading) == pzem::Result::kOk);
  CHECK(pzem::decode(pzem::Model::kAc, 2, good.data(), good.size(), reading) == pzem::Result::kAddress);
  // every piece shorter than the answer is too short, and one byte more is the wrong length
  for (std::size_t length = 0; length < good.size(); ++length) CHECK(pzem::decode(pzem::Model::kAc, 1, good.data(), length, reading) == pzem::Result::kTooShort);
  std::vector<std::uint8_t> longer = good;
  longer.push_back(0);
  CHECK(pzem::decode(pzem::Model::kAc, 1, longer.data(), longer.size(), reading) == pzem::Result::kLength);
  // every single bit turned over is caught by the CRC (or by the address or function checks that come after it)
  std::size_t accepted = 0;
  for (std::size_t byte = 0; byte < good.size(); ++byte) {
    for (int bit = 0; bit < 8; ++bit) {
      std::vector<std::uint8_t> damaged = good;
      damaged[byte] = static_cast<std::uint8_t>(damaged[byte] ^ (1u << bit));
      if (pzem::decode(pzem::Model::kAc, 1, damaged.data(), damaged.size(), reading) == pzem::Result::kOk) ++accepted;
    }
  }
  CHECK(accepted == 0);
  // a reply that is whole and signed but is not an answer to a read
  std::vector<std::uint8_t> other_function = good;
  other_function[1] = 0x03;
  other_function.resize(other_function.size() - 2);
  other_function = sealed(other_function);
  CHECK(pzem::decode(pzem::Model::kAc, 1, other_function.data(), other_function.size(), reading) == pzem::Result::kFunction);
  std::vector<std::uint8_t> wrong_count = good;
  wrong_count[2] = 18;
  wrong_count.resize(wrong_count.size() - 2);
  wrong_count = sealed(wrong_count);
  CHECK(pzem::decode(pzem::Model::kAc, 1, wrong_count.data(), wrong_count.size(), reading) == pzem::Result::kLength);
  // an exception: the meter refused; of another meter it is not ours
  const std::vector<std::uint8_t> exception = sealed({1, 0x84, 2});
  CHECK(pzem::decode(pzem::Model::kAc, 1, exception.data(), exception.size(), reading) == pzem::Result::kException);
  CHECK(pzem::decode(pzem::Model::kAc, 2, exception.data(), exception.size(), reading) == pzem::Result::kAddress);
  CHECK(pzem::decode(pzem::Model::kAc, 0xF8, exception.data(), exception.size(), reading) == pzem::Result::kException);
  std::vector<std::uint8_t> bad_exception = exception;
  bad_exception[3] ^= 1;
  CHECK(pzem::decode(pzem::Model::kAc, 1, bad_exception.data(), bad_exception.size(), reading) == pzem::Result::kCrc);
  const std::vector<std::uint8_t> long_exception = sealed({1, 0x84, 2, 0});
  CHECK(pzem::decode(pzem::Model::kAc, 1, long_exception.data(), long_exception.size(), reading) == pzem::Result::kLength);
  // a reading no real meter gives is not a value, even when the frame is whole and signed
  std::vector<std::uint16_t> registers = kAcRegisters;
  registers[0] = 10001;                                       // 1000.1 V
  std::vector<std::uint8_t> frame = ac_reply(1, registers);
  CHECK(pzem::decode(pzem::Model::kAc, 1, frame.data(), frame.size(), reading) == pzem::Result::kRange);
  registers = kAcRegisters; registers[8] = 101;               // power factor 1.01
  frame = ac_reply(1, registers);
  CHECK(pzem::decode(pzem::Model::kAc, 1, frame.data(), frame.size(), reading) == pzem::Result::kRange);
  registers = kAcRegisters; registers[7] = 1001;              // 100.1 Hz
  frame = ac_reply(1, registers);
  CHECK(pzem::decode(pzem::Model::kAc, 1, frame.data(), frame.size(), reading) == pzem::Result::kRange);
  registers = kAcRegisters; registers[2] = 0xFFFF;            // a current of 4 294 967.295 A
  frame = ac_reply(1, registers);
  CHECK(pzem::decode(pzem::Model::kAc, 1, frame.data(), frame.size(), reading) == pzem::Result::kRange);
  registers = kAcRegisters; registers[9] = 0xFFFF;            // the alarm
  frame = ac_reply(1, registers);
  CHECK(pzem::decode(pzem::Model::kAc, 1, frame.data(), frame.size(), reading) == pzem::Result::kOk && reading.alarm);
  registers = kAcRegisters; registers[9] = 1;                 // anything but all ones is no alarm
  frame = ac_reply(1, registers);
  CHECK(pzem::decode(pzem::Model::kAc, 1, frame.data(), frame.size(), reading) == pzem::Result::kOk && !reading.alarm);
  // the direct-current meter
  const std::vector<std::uint8_t> dc = ac_reply(7, kDcRegisters);
  CHECK(dc.size() == pzem::reply_length(pzem::Model::kDc));
  CHECK(pzem::decode(pzem::Model::kDc, 7, dc.data(), dc.size(), reading) == pzem::Result::kOk);
  CHECK(std::fabs(reading.voltage_v - 12.5) < 1e-9 && std::fabs(reading.current_a - 10.0) < 1e-9 && std::fabs(reading.power_w - 500.0) < 1e-9 && std::fabs(reading.energy_kwh - 2.5) < 1e-9 &&
        !reading.has_frequency && !reading.has_power_factor && !reading.alarm);
  std::vector<std::uint16_t> alarms = kDcRegisters; alarms[7] = 0xFFFF;   // the low-voltage alarm
  const std::vector<std::uint8_t> low = ac_reply(7, alarms);
  CHECK(pzem::decode(pzem::Model::kDc, 7, low.data(), low.size(), reading) == pzem::Result::kOk && reading.alarm);
  // the answer of one model is not the answer of the other
  CHECK(pzem::decode(pzem::Model::kAc, 7, dc.data(), dc.size(), reading) == pzem::Result::kTooShort);
  CHECK(pzem::decode(pzem::Model::kDc, 1, good.data(), good.size(), reading) == pzem::Result::kLength);
  // a failed decode leaves the caller's reading alone
  pzem::Reading kept;
  kept.voltage_v = 12.5;
  CHECK(pzem::decode(pzem::Model::kAc, 1, longer.data(), longer.size(), kept) == pzem::Result::kLength && kept.voltage_v == 12.5);
}

static void test_bus_edges() {
  // one meter, cycle of two seconds: a good reply, then a reply with a damaged CRC, then silence
  std::vector<MeterConfig> meters(1);
  meters[0].channel = "grid"; meters[0].address = 1;
  PzemBus bus(meters, 2000);
  CHECK(bus.freshness_ms() == 10000 && bus.fresh(0).empty());
  PzemBus slow(meters, 60000);
  CHECK(slow.freshness_ms() == 60000 + 400 + 2000);                            // a whole cycle, the round and a margin
  std::vector<MeterConfig> sixteen(16);
  for (std::size_t i = 0; i < 16; ++i) { sixteen[i].channel = "m" + std::to_string(i); sixteen[i].address = static_cast<std::uint8_t>(i + 1); }
  CHECK(PzemBus(sixteen, 5000).freshness_ms() == 5000 + 16 * 400 + 2000 && PzemBus({}, 5000).next_tx(0).empty());
  std::uint64_t now = 0;
  std::vector<std::uint8_t> sent = bus.next_tx(now);
  CHECK(sent.size() == pzem::kRequestLength && sent[0] == 1);
  CHECK(bus.next_tx(now + 10).empty());                                       // it waits for the answer
  const std::vector<std::uint8_t> good = ac_reply(1, kAcRegisters);
  bus.on_rx(good.data(), good.size(), now + 50);
  CHECK(bus.meters()[0].have_reading && bus.meters()[0].replies_ok == 1 && bus.fresh(now + 50).size() == 1 && bus.fresh(now + 50 + 10000).size() == 1 && bus.fresh(now + 50 + 10001).empty());
  // a reply nobody asked for is noise
  bus.on_rx(good.data(), good.size(), now + 60);
  CHECK(bus.meters()[0].replies_ok == 1);
  // the next request waits for the cycle; a damaged reply counts as bad and the last good reading stays
  now = 2000;
  sent = bus.next_tx(now);
  CHECK(!sent.empty());
  std::vector<std::uint8_t> damaged = good;
  damaged[5] ^= 0x40;
  bus.on_rx(damaged.data(), damaged.size(), now + 20);
  CHECK(bus.meters()[0].replies_bad == 1 && bus.meters()[0].misses_in_a_row == 1 && bus.meters()[0].last_result == pzem::Result::kCrc && bus.meters()[0].have_reading);
  // silence: the request times out after 400 ms
  now = 4000;
  CHECK(!bus.next_tx(now).empty());
  CHECK(bus.next_tx(now + 399).empty() && bus.meters()[0].timeouts == 0);
  bus.next_tx(now + 400);
  CHECK(bus.meters()[0].timeouts == 1 && bus.meters()[0].misses_in_a_row == 2);
  // a good answer after bad ones clears the run of misses
  now = 6000;
  CHECK(!bus.next_tx(now).empty());
  bus.on_rx(good.data(), good.size(), now + 30);
  CHECK(bus.meters()[0].misses_in_a_row == 0 && bus.meters()[0].replies_ok == 2);
  // the echo of the request, the stray byte and the answer in one piece
  now = 8000;
  sent = bus.next_tx(now);
  std::vector<std::uint8_t> noisy = sent;
  noisy.push_back(0x00);
  noisy.insert(noisy.end(), good.begin(), good.end());
  bus.on_rx(noisy.data(), noisy.size(), now + 40);
  CHECK(bus.meters()[0].replies_ok == 3);
  // a meter with an address no meter can have is skipped without waiting for it
  std::vector<MeterConfig> broken(2);
  broken[0].channel = "bad"; broken[0].address = 0;
  broken[1].channel = "good"; broken[1].address = 2;
  PzemBus skipping(broken, 1000);
  const std::vector<std::uint8_t> first = skipping.next_tx(0);
  CHECK(first.empty());                                                       // the impossible address was passed over
  const std::vector<std::uint8_t> second = skipping.next_tx(1);
  CHECK(second.size() == pzem::kRequestLength && second[0] == 2);
}

// ---- the analog inputs --------------------------------------------------------------------------------------------------------------------------

static config::AnalogSetting input(const std::string& quantity, const std::string& mode, double scale) {
  config::AnalogSetting a;
  a.enabled = true; a.channel = "line"; a.label = "Line"; a.pin = 1; a.quantity = quantity; a.mode = mode; a.scale = scale;
  return a;
}

static void test_analog_values() {
  CHECK(analog::mean({}) == 0.0 && analog::rms_about_mean({}) == 0.0 && analog::mean({1000, 2000, 3000}) == 2000.0);
  CHECK(std::fabs(analog::rms_about_mean({1000, 1000, 1000})) < 1e-12);                       // a steady level has no alternating part
  CHECK(std::fabs(analog::rms_about_mean({1000, 3000}) - 1000.0) < 1e-9);                     // two points a volt apart around 2 V: 1 V of RMS
  // a sine around its own average: the RMS is the amplitude over the square root of two
  std::vector<double> sine;
  for (int i = 0; i < 1000; ++i) sine.push_back(1650.0 + 1000.0 * std::sin(2.0 * 3.14159265358979323846 * i / 100.0));
  CHECK(std::fabs(analog::rms_about_mean(sine) - 1000.0 / std::sqrt(2.0)) < 1.0);
  // dc: the average, minus the volts at zero, times the scale
  config::AnalogSetting dc = input("voltage", "dc", 11.0);
  CHECK(std::fabs(analog::quantity_of(dc, {1000, 1000}) - 11.0) < 1e-9);
  dc.offset_v = 0.5;
  CHECK(std::fabs(analog::quantity_of(dc, {1500}) - 11.0) < 1e-9 && std::fabs(analog::quantity_of(dc, {0}) - (-5.5)) < 1e-9);
  dc.zero_below = 0.2;
  dc.scale = 1.0;
  CHECK(analog::quantity_of(dc, {600}) == 0.0 && analog::quantity_of(dc, {400}) == 0.0 && std::fabs(analog::quantity_of(dc, {800}) - 0.3) < 1e-9);   // under the dead band: zero, both ways
  // ac: the size of the alternating signal, times the size of the scale (its sign is of no use to a size)
  config::AnalogSetting ac = input("current", "ac", -30.0);
  CHECK(std::fabs(analog::quantity_of(ac, sine) - 30.0 * 1.0 / std::sqrt(2.0)) < 0.05);
  ac.zero_below = 100.0;
  CHECK(analog::quantity_of(ac, sine) == 0.0);

  // the channel of the message
  analog::Energy energy;
  AnalogChannel channel;
  CHECK(!analog::make_channel(dc, {}, 1000, energy, channel));
  config::AnalogSetting volts = input("voltage", "dc", 1.0);
  CHECK(analog::make_channel(volts, {2500}, 1000, energy, channel) && channel.has_voltage && !channel.has_current && !channel.has_power && !channel.has_energy && channel.id == "line" && !channel.ac &&
        std::fabs(channel.voltage_v - 2.5) < 1e-9);
  volts.offset_v = 3.0;
  CHECK(analog::make_channel(volts, {2500}, 1000, energy, channel) && channel.voltage_v == 0.0);   // a voltage is never negative in the message
  volts.offset_v = 0; volts.scale = 1e6;
  CHECK(analog::make_channel(volts, {3000}, 1000, energy, channel) && channel.voltage_v == 1000.0);   // nor beyond the contract
  config::AnalogSetting amps = input("current", "dc", 10.0);
  CHECK(analog::make_channel(amps, {2000}, 1000, energy, channel) && channel.has_current && std::fabs(channel.current_a - 20.0) < 1e-9 && !channel.has_power && !channel.has_voltage);
  amps.scale = 1e6;
  CHECK(analog::make_channel(amps, {2000}, 1000, energy, channel) && channel.current_a == 1000.0);
  amps.scale = -1e6;
  CHECK(analog::make_channel(amps, {2000}, 1000, energy, channel) && channel.current_a == -1000.0);
  // with a nominal voltage: the power is apparent power and the energy only grows
  config::AnalogSetting house = input("current", "dc", 10.0);
  house.nominal_voltage_v = 230.0;
  analog::Energy kwh;
  CHECK(analog::make_channel(house, {1000}, 3600000, kwh, channel) && channel.has_power && channel.power_estimated && channel.has_energy && std::fabs(channel.power_w - 2300.0) < 1e-9);
  CHECK(std::fabs(channel.energy_kwh - 2.3) < 1e-9 && std::fabs(kwh.kwh() - 2.3) < 1e-9);   // 2300 W for an hour
  CHECK(analog::make_channel(house, {-1000}, 3600000, kwh, channel) && channel.power_w == -2300.0 && std::fabs(channel.energy_kwh - 2.3) < 1e-9);   // a negative power takes nothing off
  CHECK(analog::make_channel(house, {1000}, 0, kwh, channel) && std::fabs(channel.energy_kwh - 2.3) < 1e-9);
  analog::Energy huge;
  huge.add(1e12, 3600000ULL * 1000ULL);
  config::AnalogSetting capped = house;
  CHECK(analog::make_channel(capped, {1000}, 0, huge, channel) && channel.energy_kwh == 10000000.0);   // the message holds no more than the contract allows
  CHECK(analog::clamp(5, 0, 10) == 5 && analog::clamp(-5, 0, 10) == 0 && analog::clamp(50, 0, 10) == 10);
  config::AnalogSetting alternating = input("current", "ac", 30.0);
  CHECK(analog::make_channel(alternating, sine, 1000, energy, channel) && channel.ac && channel.has_current);
}

// ---- the commands to a switch ---------------------------------------------------------------------------------------------------------------------

static std::string command(const std::string& action, const std::string& token = "", const std::string& id = "0123456789abcdef", const std::string& node = "electrical-1", const std::string& target = "transfer") {
  std::string text = "{\"kind\":\"electrical_command\",\"node_id\":\"" + node + "\",\"timestamp_ms\":1000,\"command_id\":\"" + id + "\",\"switch\":\"" + target + "\",\"action\":\"" + action + "\"";
  if (!token.empty()) text += ",\"token\":\"" + token + "\"";
  return text + "}";
}
static json::Value parsed(const std::string& text) {
  json::Value document;
  const bool ok = json::parse(text, document);
  CHECK(ok);
  return document;
}
static std::string token_of(const std::string& result) { return parsed(result).string_or("token", ""); }
static std::string refusal_of(const std::string& result) { return parsed(result).string_or("refusal", "?"); }

static SwitchSet make_set(bool allowed = true, bool random_works = true) {
  static std::uint8_t seed = 1;
  SwitchSet set("electrical-1", [random_works](std::uint8_t* out, std::size_t n) { if (!random_works) return false; for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<std::uint8_t>(seed++ * 37 + 11); return true; });
  SwitchConfig config;
  config.id = "transfer"; config.label = "Grid or inverter"; config.source_a = "grid"; config.source_b = "inverter";
  config.transfer = TransferConfig{allowed, 2000, 1000, 10'000};
  set.add(config);
  return set;
}

static void test_names_and_topics() {
  CHECK(std::string(action_name(Action::kArm)) == "arm" && std::string(action_name(Action::kCloseA)) == "close_a" && std::string(action_name(Action::kCloseB)) == "close_b" && std::string(action_name(Action::kOpen)) == "open" &&
        std::string(action_name(Action::kAcknowledge)) == "acknowledge");
  const Answer answers[] = {Answer::kNone, Answer::kDisabled, Answer::kFault, Answer::kNotArmed, Answer::kNotConfirmedOpen, Answer::kUnknownSwitch, Answer::kBadToken, Answer::kNotSupported};
  const char* names[] = {"none", "disabled", "fault", "not_armed", "not_confirmed_open", "unknown_switch", "bad_token", "not_supported"};
  for (std::size_t i = 0; i < 8; ++i) CHECK(std::string(answer_name(answers[i])) == names[i]);
  CHECK(std::string(fault_name(Fault::kNone)) == "none" && std::string(fault_name(Fault::kDidNotClose)) == "did_not_close" && std::string(fault_name(Fault::kDidNotOpen)) == "did_not_open" &&
        std::string(fault_name(Fault::kBothClosed)) == "both_closed" && std::string(fault_name(Fault::kDisabled)) == "disabled");
  CHECK(std::string(side_name(Source::kA)) == "a" && std::string(side_name(Source::kB)) == "b" && std::string(side_name(Source::kNone)) == "none");
  CHECK(command_topic("electrical-1") == "armor/electrical/electrical-1/command" && result_topic("electrical-1") == "armor/electrical/electrical-1/result" && command_topic("Bad Node").empty() && result_topic("").empty() &&
        command_topic(std::string(65, 'a')).empty() && command_topic(std::string(64, 'a')) == "armor/electrical/" + std::string(64, 'a') + "/command");
  for (const char* ok : {"0123abcd", "abcdefgh", "a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6"}) CHECK(valid_code(ok));
  for (const char* bad : {"", "abc", "0123abc", "0123ABCD", "0123abc-", "0123 abcd", "0123abcd\n"}) CHECK(!valid_code(bad));
  CHECK(!valid_code(std::string(33, 'a')));
}

static void test_the_reader_is_strict() {
  SwitchCommand parsed_command;
  std::string why;
  CHECK(parse_command(command("arm"), parsed_command, &why) && parsed_command.action == Action::kArm && parsed_command.switch_id == "transfer" && parsed_command.node_id == "electrical-1" && parsed_command.token.empty());
  CHECK(parse_command(command("close_a", "89abcdef01234567"), parsed_command) && parsed_command.action == Action::kCloseA && parsed_command.token == "89abcdef01234567");
  CHECK(parse_command(command("close_b", "89abcdef01234567"), parsed_command) && parsed_command.action == Action::kCloseB);
  CHECK(parse_command(command("open"), parsed_command) && parsed_command.action == Action::kOpen && parse_command(command("acknowledge"), parsed_command) && parsed_command.action == Action::kAcknowledge);
  // a token goes on a close and on nothing else
  CHECK(!parse_command(command("close_a"), parsed_command, &why) && !why.empty());
  CHECK(!parse_command(command("arm", "89abcdef01234567"), parsed_command) && !parse_command(command("open", "89abcdef01234567"), parsed_command) && !parse_command(command("acknowledge", "89abcdef01234567"), parsed_command));
  CHECK(!parse_command(command("close_a", "SHORT"), parsed_command) && !parse_command(command("close_a", "89ABCDEF01234567"), parsed_command));
  // the fields of the contract, and no others
  const std::string good = command("arm");
  CHECK(!parse_command(good.substr(0, good.size() - 1) + ",\"extra\":1}", parsed_command, &why) && why == "a field that is not in the contract");
  for (const char* field : {"kind", "node_id", "timestamp_ms", "command_id", "switch", "action"}) {
    json::Value document = parsed(good);
    std::string without = "{";
    for (std::size_t i = 0; i < document.names.size(); ++i) {
      if (document.names[i] == field) continue;
      const json::Value& v = document.items[i];
      without += (without.size() > 1 ? "," : "") + json::quote(document.names[i]) + ":" + (v.is_number() ? std::to_string(static_cast<long long>(v.number)) : json::quote(v.text));
    }
    CHECK(!parse_command(without + "}", parsed_command, &why) && why == field);
  }
  CHECK(!parse_command("", parsed_command) && !parse_command("[]", parsed_command) && !parse_command("\"arm\"", parsed_command) && !parse_command("{", parsed_command) && !parse_command("null", parsed_command));
  CHECK(!parse_command(command("ARM"), parsed_command) && !parse_command(command("close"), parsed_command) && !parse_command(command(""), parsed_command) && !parse_command(command("close_c", "89abcdef01234567"), parsed_command));
  CHECK(!parse_command(command("arm", "", "short"), parsed_command) && !parse_command(command("arm", "", "0123456789ABCDEF"), parsed_command));
  CHECK(!parse_command(command("arm", "", "0123456789abcdef", "Bad Node"), parsed_command) && !parse_command(command("arm", "", "0123456789abcdef", "electrical-1", "Bad Switch"), parsed_command) &&
        !parse_command(command("arm", "", "0123456789abcdef", "electrical-1", std::string(33, 'a')), parsed_command));
  // the kind and the stamp
  std::string wrong_kind = good;
  wrong_kind.replace(wrong_kind.find("electrical_command"), 18, "electrical_state_");
  CHECK(!parse_command(wrong_kind, parsed_command));
  for (const char* stamp : {"-1", "1.5", "\"1000\"", "true", "null", "1e300", "-1e300", "9223372036854775808"}) {
    std::string text = good;
    text.replace(text.find("1000"), 4, stamp);
    CHECK(!parse_command(text, parsed_command));
  }
  std::string zero = good;
  zero.replace(zero.find("1000"), 4, "0");
  CHECK(parse_command(zero, parsed_command));
  CHECK(!parse_command(good.substr(0, good.size() - 1) + ",\"token\":5}", parsed_command));
  // a name repeated in the text is not one command
  CHECK(!parse_command(good.substr(0, good.size() - 1) + ",\"action\":\"open\"}", parsed_command));
}

static void test_the_token_is_spent() {
  SwitchSet set = make_set();
  const std::string armed = set.handle(command("arm"), 1000);
  CHECK(parsed(armed).bool_or("accepted", false) && armed.find("\"kind\":\"electrical_result\"") != std::string::npos);
  const std::string token = token_of(armed);
  CHECK(token.size() == 16 && valid_code(token));
  // the answer repeats what was asked
  const json::Value answer = parsed(armed);
  CHECK(answer.string_or("command_id", "") == "0123456789abcdef" && answer.string_or("switch", "") == "transfer" && answer.string_or("action", "") == "arm" && answer.string_or("refusal", "") == "none" &&
        answer.string_or("node_id", "") == "electrical-1" && answer.number_or("timestamp_ms", 0) == 1000);
  // a refused request carries no token
  CHECK(token_of(set.handle(command("close_a", "ffffffffffffffff"), 1100)).empty());
  // the wrong token takes the arm away: the right one no longer closes
  set = make_set();
  const std::string t1 = token_of(set.handle(command("arm"), 1000));
  CHECK(refusal_of(set.handle(command("close_a", "ffffffffffffffff"), 1100)) == "bad_token");
  CHECK(refusal_of(set.handle(command("close_a", t1), 1200)) == "not_armed");
  // the right token closes once; the same message again finds nothing to match
  set = make_set();
  const std::string t2 = token_of(set.handle(command("arm"), 1000));
  CHECK(refusal_of(set.handle(command("close_b", t2), 1100)) == "none");
  CHECK(refusal_of(set.handle(command("close_b", t2), 1200)) == "not_armed");
  CHECK(refusal_of(set.handle(command("close_a", t2), 1300)) == "not_armed");
  // a token is not the token of another arm
  set = make_set();
  const std::string first = token_of(set.handle(command("arm"), 1000));
  const std::string second = token_of(set.handle(command("arm", "", "fedcba9876543210"), 1500));
  CHECK(first != second);
  CHECK(refusal_of(set.handle(command("close_a", first), 1600)) == "bad_token");
  // an arm that ran out: the token goes with it
  set = make_set();
  const std::string late = token_of(set.handle(command("arm"), 1000));
  set.tick(1000 + 10'000);
  CHECK(refusal_of(set.handle(command("close_a", late), 1000 + 10'001)) == "not_armed");
  // and without a tick in between, the controller still refuses a close after the window
  set = make_set();
  const std::string stale = token_of(set.handle(command("arm"), 1000));
  CHECK(refusal_of(set.handle(command("close_a", stale), 1000 + 10'000)) == "not_armed");
  // open is always accepted, needs no token, and withdraws an arm
  set = make_set();
  const std::string held = token_of(set.handle(command("arm"), 1000));
  CHECK(refusal_of(set.handle(command("open"), 1100)) == "none");
  CHECK(refusal_of(set.handle(command("close_a", held), 1200)) == "not_armed");
  // a switch that is not there, and a command for another node, which gets no answer at all
  CHECK(refusal_of(set.handle(command("arm", "", "0123456789abcdef", "electrical-1", "other"), 1300)) == "unknown_switch");
  CHECK(set.handle(command("arm", "", "0123456789abcdef", "electrical-2"), 1400).empty() && set.handle("not a command", 1400).empty() && set.handle("", 1400).empty());
  // a random source that fails grants no arm
  SwitchSet no_random = make_set(true, false);
  const std::string refused = no_random.handle(command("arm"), 1000);
  CHECK(refusal_of(refused) == "not_supported" && token_of(refused).empty() && !parsed(refused).bool_or("accepted", true));
  CHECK(refusal_of(no_random.handle(command("close_a", "0123456789abcdef"), 1100)) == "not_armed");
  // switching that is not allowed: everything but open is refused
  SwitchSet off = make_set(false);
  CHECK(!off.switching_enabled() && refusal_of(off.handle(command("arm"), 1000)) == "disabled" && refusal_of(off.handle(command("close_a", "0123456789abcdef"), 1000)) == "disabled" &&
        refusal_of(off.handle(command("acknowledge"), 1000)) == "disabled" && refusal_of(off.handle(command("open"), 1000)) == "none");
  CHECK(make_set().switching_enabled());
}

static void test_the_set_of_switches() {
  SwitchSet set("electrical-1", nullptr);
  SwitchConfig config;
  config.id = "one";
  CHECK(set.size() == 0 && set.switches_json(0).empty() && !set.switching_enabled());
  CHECK(set.add(config) && !set.add(config));                                  // the same id twice
  config.id = "Bad Id"; CHECK(!set.add(config));
  config.id = ""; CHECK(!set.add(config));
  config.id = "two"; config.label = std::string(41, 'x'); CHECK(!set.add(config));
  config.label = std::string(40, 'x'); CHECK(set.add(config));
  config.id = "three"; config.label = ""; config.source_a = "Bad Source"; CHECK(!set.add(config));
  config.source_a = "grid"; config.source_b = std::string(33, 'a'); CHECK(!set.add(config));
  config.source_b = "inverter"; CHECK(set.add(config));
  config.id = "four"; CHECK(set.add(config));
  config.id = "five"; CHECK(!set.add(config) && set.size() == 4);              // no fifth switch
  // an unknown switch has nothing energised and no fault
  CHECK(!set.coils("nobody").a && !set.coils("nobody").b && set.fault("nobody") == Fault::kNone && set.selected("nobody") == Source::kNone);
  set.set_feedback("nobody", Feedback{true, true});                            // and feedback for it goes nowhere
  const json::Value view = parsed(set.switches_json(0));
  CHECK(view.is_array() && view.items.size() == 4 && view.items[0].string_or("id", "") == "one" && view.items[0].string_or("kind", "") == "transfer" && view.items[0].get("label") == nullptr &&
        view.items[0].get("source_a") == nullptr && view.items[2].string_or("source_a", "") == "grid" && view.items[2].string_or("source_b", "") == "inverter");
  for (const json::Value& entry : view.items) CHECK(!entry.bool_or("armed", true) && entry.string_or("fault", "") == "none" && entry.string_or("selected", "") == "none" && entry.string_or("wanted", "") == "none");
}

static void test_the_state_shows_the_contacts() {
  SwitchSet set = make_set();
  auto state = [&](std::uint64_t now) { return parsed(set.switches_json(now)).items[0]; };
  CHECK(!state(0).bool_or("armed", true));
  set.handle(command("arm"), 1000);
  CHECK(state(1000).bool_or("armed", false) && state(10'999).bool_or("armed", false) && !state(11'000).bool_or("armed", true));
  set.set_feedback("transfer", Feedback{true, false});
  CHECK(state(0).string_or("selected", "") == "a" && state(0).bool_or("a_closed", false) && !state(0).bool_or("b_closed", true));
  set.set_feedback("transfer", Feedback{false, true});
  CHECK(state(0).string_or("selected", "") == "b");
  set.set_feedback("transfer", Feedback{true, true});                          // both contacts closed: no side is selected, whatever the controller thinks
  CHECK(state(0).string_or("selected", "") == "none" && state(0).bool_or("a_closed", false) && state(0).bool_or("b_closed", false));
  set.tick(2000);                                                              // and the controller latches the fault, which the state then shows
  CHECK(set.fault("transfer") == Fault::kBothClosed);
  CHECK(state(2000).string_or("fault", "") == "both_closed" && !state(2000).bool_or("armed", true) && !state(2000).bool_or("closing", true) && state(2000).string_or("wanted", "") == "none");
  CHECK(refusal_of(set.handle(command("arm"), 2100)) == "fault" && refusal_of(set.handle(command("close_a", "0123456789abcdef"), 2100)) == "fault");
  CHECK(refusal_of(set.handle(command("acknowledge"), 2200)) == "not_confirmed_open");
  set.set_feedback("transfer", Feedback{false, false});
  CHECK(refusal_of(set.handle(command("acknowledge"), 2300)) == "none" && set.fault("transfer") == Fault::kNone);
  CHECK(refusal_of(set.handle(command("acknowledge"), 2400)) == "none");       // nothing to acknowledge is not a refusal
  CHECK(refusal_of(set.handle(command("open"), 2500)) == "none");
}

int main() {
  test_crc_and_requests();
  test_decoding_and_its_refusals();
  test_bus_edges();
  test_analog_values();
  test_names_and_topics();
  test_the_reader_is_strict();
  test_the_token_is_spent();
  test_the_set_of_switches();
  test_the_state_shows_the_contacts();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
