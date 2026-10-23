// ARMOR-NODE-ELECTRICAL - host tests of the meters: the Modbus CRC, the PZEM frames, the bus that asks them one at a time, and the message the node makes.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
// The meters are stand-ins written from the makers' public description; no real PZEM has been read.
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "../core/electrical_json.hpp"

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

using namespace armor::electrical;
using pzem::Model;
using pzem::Result;

// A reply with its CRC, from the registers.
static std::vector<std::uint8_t> reply(std::uint8_t address, const std::vector<std::uint16_t>& registers, std::uint8_t function = 0x04) {
  std::vector<std::uint8_t> frame{address, function, static_cast<std::uint8_t>(registers.size() * 2)};
  for (std::uint16_t r : registers) { frame.push_back(static_cast<std::uint8_t>(r >> 8)); frame.push_back(static_cast<std::uint8_t>(r & 0xFF)); }
  const std::uint16_t crc = pzem::crc16(frame.data(), frame.size());
  frame.push_back(static_cast<std::uint8_t>(crc & 0xFF)); frame.push_back(static_cast<std::uint8_t>(crc >> 8));
  return frame;
}
static std::vector<std::uint8_t> exception_reply(std::uint8_t address, std::uint8_t code) {
  std::vector<std::uint8_t> frame{address, 0x84, code};
  const std::uint16_t crc = pzem::crc16(frame.data(), frame.size());
  frame.push_back(static_cast<std::uint8_t>(crc & 0xFF)); frame.push_back(static_cast<std::uint8_t>(crc >> 8));
  return frame;
}
// 230.5 V, 1.234 A, 284.4 W, 1500 Wh, 50.0 Hz, power factor 0.98, no alarm
static const std::vector<std::uint16_t> kAcRegisters = {2305, 1234, 0, 2844, 0, 1500, 0, 500, 98, 0};
// 52.14 V, 14.20 A, 740.4 W, 25000 Wh, no alarm
static const std::vector<std::uint16_t> kDcRegisters = {5214, 1420, 7404, 0, 25000, 0, 0, 0};

static void test_crc_and_requests() {
  // the well-known Modbus example: 01 03 00 00 00 0A has the CRC C5 CD on the wire
  const std::uint8_t sample[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
  CHECK(pzem::crc16(sample, 6) == 0xCDC5);
  CHECK(pzem::crc16(nullptr, 0) == 0xFFFF);
  std::array<std::uint8_t, 8> request{};
  CHECK(pzem::build_read_request(Model::kAc, 1, request) && request[0] == 1 && request[1] == 0x04 && request[3] == 0 && request[5] == 10);
  CHECK(request[6] == 0x70 && request[7] == 0x0D);                          // the request the PZEM-004T's own description shows: 01 04 00 00 00 0A 70 0D
  CHECK(pzem::build_read_request(Model::kDc, 3, request) && request[0] == 3 && request[5] == 8 && pzem::crc16(request.data(), 8) == 0);   // a frame with its own CRC checks to zero
  CHECK(pzem::build_read_request(Model::kAc, 0xF8, request) && request[0] == 0xF8);
  CHECK(!pzem::build_read_request(Model::kAc, 0, request) && !pzem::build_read_request(Model::kAc, 249, request) && !pzem::build_read_request(Model::kAc, 255, request));
  // the only function that is ever built is 0x04, the read: nothing here can write to a meter
  for (int address = 1; address <= 247; ++address) { pzem::build_read_request(Model::kAc, static_cast<std::uint8_t>(address), request); if (request[1] != 0x04) CHECK(false); }
}

static void test_decoding() {
  pzem::Reading r;
  const std::vector<std::uint8_t> ac = reply(1, kAcRegisters);
  CHECK(ac.size() == pzem::reply_length(Model::kAc) && pzem::decode(Model::kAc, 1, ac.data(), ac.size(), r) == Result::kOk);
  CHECK(r.voltage_v == 230.5 && r.current_a == 1.234 && r.power_w == 284.4 && r.energy_kwh == 1.5 && r.has_frequency && r.frequency_hz == 50.0 && r.has_power_factor && r.power_factor == 0.98 && !r.alarm);
  // current and power are 32 bits: the low word first
  const std::vector<std::uint8_t> big = reply(1, {2300, 0x86A0, 0x0001, 0x0000, 0x0001, 0x0000, 0x0000, 500, 100, 0xFFFF});   // 100 A (100000 mA), 6553.6 W, alarm
  CHECK(pzem::decode(Model::kAc, 1, big.data(), big.size(), r) == Result::kOk && r.current_a == 100.0 && r.power_w == 6553.6 && r.alarm);
  const std::vector<std::uint8_t> dc = reply(7, kDcRegisters);
  CHECK(dc.size() == pzem::reply_length(Model::kDc) && pzem::decode(Model::kDc, 7, dc.data(), dc.size(), r) == Result::kOk);
  CHECK(r.voltage_v == 52.14 && r.current_a == 14.2 && r.power_w == 740.4 && r.energy_kwh == 25.0 && !r.has_frequency && !r.has_power_factor && !r.alarm);
  std::vector<std::uint8_t> alarm = reply(7, {5214, 1420, 7404, 0, 25000, 0, 0xFFFF, 0});
  CHECK(pzem::decode(Model::kDc, 7, alarm.data(), alarm.size(), r) == Result::kOk && r.alarm);
  // the broadcast address takes any meter's answer
  CHECK(pzem::decode(Model::kAc, 0xF8, ac.data(), ac.size(), r) == Result::kOk);
  // what is refused
  std::vector<std::uint8_t> bad = ac; bad[10] ^= 1;
  CHECK(pzem::decode(Model::kAc, 1, bad.data(), bad.size(), r) == Result::kCrc);
  CHECK(pzem::decode(Model::kAc, 2, ac.data(), ac.size(), r) == Result::kAddress);
  CHECK(pzem::decode(Model::kAc, 1, ac.data(), ac.size() - 1, r) == Result::kTooShort);
  CHECK(pzem::decode(Model::kAc, 1, ac.data(), 3, r) == Result::kTooShort);
  std::vector<std::uint8_t> longer = ac; longer.push_back(0);
  CHECK(pzem::decode(Model::kAc, 1, longer.data(), longer.size(), r) == Result::kLength);
  const std::vector<std::uint8_t> wrong_function = reply(1, kAcRegisters, 0x03);
  CHECK(pzem::decode(Model::kAc, 1, wrong_function.data(), wrong_function.size(), r) == Result::kFunction);
  const std::vector<std::uint8_t> refused = exception_reply(1, 0x02);
  CHECK(pzem::decode(Model::kAc, 1, refused.data(), refused.size(), r) == Result::kException);
  CHECK(pzem::decode(Model::kAc, 2, refused.data(), refused.size(), r) == Result::kAddress);   // the refusal of another meter on the line is not this meter's
  CHECK(pzem::decode(Model::kAc, 0xF8, refused.data(), refused.size(), r) == Result::kException);   // 0xF8 answers whichever meter is asked
  std::vector<std::uint8_t> refused_bad = refused; refused_bad[3] ^= 1;
  CHECK(pzem::decode(Model::kAc, 1, refused_bad.data(), refused_bad.size(), r) == Result::kCrc);
  // an AC reply read as DC (or the other way round) does not fit
  CHECK(pzem::decode(Model::kDc, 1, ac.data(), ac.size(), r) != Result::kOk && pzem::decode(Model::kAc, 7, dc.data(), dc.size(), r) != Result::kOk);
  // a reading no real meter could give, with a CRC that fits, is not a value
  const std::vector<std::uint8_t> absurd = reply(1, {0xFFFE, 1234, 0, 2844, 0, 1500, 0, 500, 98, 0});   // 6553.4 V
  CHECK(pzem::decode(Model::kAc, 1, absurd.data(), absurd.size(), r) == Result::kRange);
  const std::vector<std::uint8_t> bad_factor = reply(1, {2305, 1234, 0, 2844, 0, 1500, 0, 500, 250, 0});   // power factor 2.5
  CHECK(pzem::decode(Model::kAc, 1, bad_factor.data(), bad_factor.size(), r) == Result::kRange);
}

// A stand-in line: it answers the request it hears, after a latency, from a table of replies by address (absent: silence).
struct Line {
  PzemBus bus;
  std::map<int, std::vector<std::uint8_t>> answers;
  std::vector<int> asked;
  std::uint64_t now = 0;
  Line(std::vector<MeterConfig> meters, std::uint64_t cycle_ms) : bus(std::move(meters), cycle_ms) {}
  void run(std::uint64_t until_ms, std::uint64_t latency_ms = 40) {
    std::vector<std::pair<std::uint64_t, std::vector<std::uint8_t>>> in_flight;
    for (; now < until_ms; now += 10) {
      for (auto it = in_flight.begin(); it != in_flight.end();) {
        if (it->first <= now) { bus.on_rx(it->second.data(), it->second.size(), now); it = in_flight.erase(it); } else ++it;
      }
      const std::vector<std::uint8_t> out = bus.next_tx(now);
      if (!out.empty()) {
        asked.push_back(out[0]);
        const std::uint16_t crc = pzem::crc16(out.data(), 6);
        if (out[1] != 0x04 || out[6] != (crc & 0xFF) || out[7] != (crc >> 8)) asked.push_back(-1);   // never happens: what is sent is a valid read request
        const auto answer = answers.find(out[0]);
        if (answer != answers.end()) in_flight.push_back({now + latency_ms, answer->second});
      }
    }
  }
};

static void test_bus() {
  std::vector<MeterConfig> meters = {{"grid", "Grid input", Model::kAc, 1}, {"heater", "", Model::kAc, 2}, {"dc-bus", "Battery bus", Model::kDc, 7}};
  Line line(meters, 1000);
  line.answers[1] = reply(1, kAcRegisters); line.answers[2] = reply(2, {2310, 8000, 0, 18500, 0, 30, 0, 499, 99, 0}); line.answers[7] = reply(7, kDcRegisters);
  line.run(3500);
  // one at a time, in order, again after the cycle
  CHECK(line.asked.size() >= 9 && line.asked[0] == 1 && line.asked[1] == 2 && line.asked[2] == 7 && line.asked[3] == 1 && line.asked[6] == 1);
  CHECK(std::find(line.asked.begin(), line.asked.end(), -1) == line.asked.end());
  const auto fresh = line.bus.fresh(line.now);
  CHECK(fresh.size() == 3 && fresh[1]->reading.current_a == 8.0 && fresh[1]->reading.power_w == 1850.0 && fresh[2]->reading.voltage_v == 52.14);
  CHECK(line.bus.meters()[0].replies_ok >= 3 && line.bus.meters()[0].timeouts == 0 && line.bus.meters()[0].replies_bad == 0);

  // a meter that says nothing is asked again each cycle, costs a timeout, and its old reading is not published for ever
  Line silent(meters, 1000);
  silent.answers[1] = reply(1, kAcRegisters); silent.answers[7] = reply(7, kDcRegisters);
  silent.run(4000);
  CHECK(silent.bus.meters()[1].timeouts >= 3 && silent.bus.meters()[1].misses_in_a_row >= 3 && !silent.bus.meters()[1].have_reading);
  CHECK(silent.bus.fresh(silent.now).size() == 2);
  silent.answers.clear();
  silent.run(silent.now + 12'000);
  CHECK(silent.bus.fresh(silent.now).empty());      // everything went quiet: a reading older than ten seconds is not published

  // a garbled reply, one for another address and a refusal are counted and never become a reading
  Line garbled(meters, 1000);
  std::vector<std::uint8_t> broken = reply(1, kAcRegisters); broken[5] ^= 1;
  garbled.answers[1] = broken; garbled.answers[2] = reply(1, kAcRegisters); garbled.answers[7] = exception_reply(7, 0x01);
  garbled.run(2500);
  CHECK(garbled.bus.meters()[0].replies_bad >= 1 && garbled.bus.meters()[0].last_result == Result::kCrc);
  CHECK(garbled.bus.meters()[1].replies_bad >= 1 && garbled.bus.meters()[1].last_result == Result::kAddress);
  CHECK(garbled.bus.meters()[2].replies_bad >= 1 && garbled.bus.meters()[2].last_result == Result::kException);
  CHECK(garbled.bus.fresh(garbled.now).empty());

  // bytes nobody asked for are ignored
  PzemBus idle(meters, 1000);
  const std::vector<std::uint8_t> late = reply(1, kAcRegisters);
  idle.on_rx(late.data(), late.size(), 5);
  CHECK(idle.meters()[0].replies_ok == 0 && idle.meters()[0].replies_bad == 0);
  // a reply that comes in pieces is put together
  PzemBus pieces({meters[0]}, 1000);
  CHECK(!pieces.next_tx(0).empty());
  pieces.on_rx(late.data(), 7, 10); pieces.on_rx(late.data() + 7, late.size() - 7, 20);
  CHECK(pieces.meters()[0].replies_ok == 1 && pieces.fresh(30).size() == 1);
  // no meters: nothing to send
  PzemBus none({}, 1000);
  CHECK(none.next_tx(0).empty());
}

// An RS485 adapter that hears its own request, and a stray byte on the line: the answer is still found.
static void test_echo_and_stray_bytes() {
  const std::vector<MeterConfig> meters = {{"grid", "", Model::kAc, 1}};
  const std::vector<std::uint8_t> good = reply(1, kAcRegisters);
  for (int variant = 0; variant < 3; ++variant) {
    PzemBus bus(meters, 1000);
    const std::vector<std::uint8_t> request = bus.next_tx(0);
    CHECK(request.size() == pzem::kRequestLength);
    std::vector<std::uint8_t> heard;
    if (variant == 0) heard = request;                      // the echo of the request, then the answer
    if (variant == 1) heard = {0x00, 0xFF};                 // two stray bytes, then the answer
    if (variant == 2) heard = request;                      // the echo arrives in a piece of its own, the answer in another
    heard.insert(heard.end(), good.begin(), good.end());
    if (variant == 2) { bus.on_rx(heard.data(), request.size(), 5); bus.on_rx(heard.data() + request.size(), good.size(), 20); }
    else bus.on_rx(heard.data(), heard.size(), 20);
    CHECK(bus.meters()[0].replies_ok == 1 && bus.meters()[0].replies_bad == 0);
    CHECK(bus.meters()[0].reading.voltage_v == 231.0 || bus.meters()[0].have_reading);
  }
}

static void test_message() {
  std::vector<MeterConfig> meters = {{"grid", "Grid input", Model::kAc, 1}, {"dc-bus", "", Model::kDc, 7}};
  Line line(meters, 1000);
  line.answers[1] = reply(1, kAcRegisters); line.answers[7] = reply(7, kDcRegisters);
  line.run(2500);
  const std::string text = message_json("electrical-1", 123456, false, line.bus.fresh(line.now));
  armor::json::Value doc;
  CHECK(armor::json::parse(text, doc) && doc.get("kind")->text == "electrical" && doc.get("node_id")->text == "electrical-1" && doc.get("timestamp_ms")->number == 123456);
  CHECK(doc.get("switching_enabled")->is_bool() && !doc.get("switching_enabled")->boolean);
  const armor::json::Value* channels = doc.get("channels");
  CHECK(channels != nullptr && channels->items.size() == 2);
  const armor::json::Value& grid = channels->items[0];
  CHECK(grid.get("id")->text == "grid" && grid.get("domain")->text == "ac" && grid.get("label")->text == "Grid input" && grid.get("voltage_v")->number == 230.5);
  CHECK(grid.get("current_a")->number == 1.234 && grid.get("power_w")->number == 284.4 && grid.get("energy_kwh")->number == 1.5 && grid.get("frequency_hz")->number == 50.0 && grid.get("power_factor")->number == 0.98);
  CHECK(grid.get("state") == nullptr && grid.get("alarm")->is_bool() && !grid.get("alarm")->boolean);
  const armor::json::Value& dc = channels->items[1];
  CHECK(dc.get("domain")->text == "dc" && dc.get("label") == nullptr && dc.get("frequency_hz") == nullptr && dc.get("power_factor") == nullptr && dc.get("voltage_v")->number == 52.14);
  // the state of a switch and an alarm code ride along by channel id
  const std::string with_extra = message_json("electrical-1", 1, true, line.bus.fresh(line.now), {{"grid", {SwitchState::kOpen, "over_current"}}, {"nothing", {SwitchState::kClosed, ""}}});
  armor::json::Value doc2;
  CHECK(armor::json::parse(with_extra, doc2) && doc2.get("switching_enabled")->boolean && doc2.get("channels")->items[0].get("state")->text == "open" && doc2.get("channels")->items[0].get("alarm_code")->text == "over_current");
  CHECK(doc2.get("channels")->items[1].get("state") == nullptr);
  // given the node clock, every meter channel says how old its reading is and how many requests failed; without it the message is as before
  CHECK(grid.get("reading_age_ms") == nullptr && grid.get("errors") == nullptr);
  armor::json::Value quality;
  CHECK(armor::json::parse(message_json("electrical-1", 1, false, line.bus.fresh(line.now), {}, "", {}, line.now), quality));
  const armor::json::Value& graded = quality.get("channels")->items[0];
  CHECK(graded.get("reading_age_ms") != nullptr && graded.get("reading_age_ms")->number >= 0 && graded.get("reading_age_ms")->number <= 10000);
  CHECK(graded.get("errors") != nullptr && graded.get("errors")->number == 0);
  // a clock behind the reading cannot give a negative age
  armor::json::Value early;
  CHECK(armor::json::parse(message_json("electrical-1", 1, false, line.bus.fresh(line.now), {}, "", {}, 1), early) && early.get("channels")->items[0].get("reading_age_ms")->number == 0);
  // no meters with a reading: still a message, with no channels
  armor::json::Value doc3;
  CHECK(armor::json::parse(message_json("electrical-1", 1, false, {}), doc3) && doc3.get("channels")->is_array() && doc3.get("channels")->items.empty());
  // at most sixteen channels
  std::vector<MeterConfig> many;
  for (int i = 0; i < 20; ++i) many.push_back({"c" + std::to_string(i), "", Model::kAc, static_cast<std::uint8_t>(i + 1)});
  Line crowd(many, 1000);
  for (int i = 0; i < 20; ++i) crowd.answers[i + 1] = reply(static_cast<std::uint8_t>(i + 1), kAcRegisters);
  crowd.run(9000);
  armor::json::Value doc4;
  CHECK(crowd.bus.fresh(crowd.now).size() == 20 && armor::json::parse(message_json("electrical-1", 1, false, crowd.bus.fresh(crowd.now)), doc4) && doc4.get("channels")->items.size() == 16);
  // the names
  CHECK(topic("electrical-1") == "armor/electrical/electrical-1/state" && topic("Bad Node").empty() && topic("").empty() && topic("-x").empty() && topic(std::string(65, 'a')).empty());
  CHECK(valid_name("grid", 32) && !valid_name("Grid", 32) && !valid_name("g", 0) && !valid_name(std::string(33, 'a'), 32));
}

// ---- the meter's own settings (holding registers, function 0x03) ---------------------------------------------------------------------------------------------------------

static std::vector<std::uint8_t> holding_reply(std::uint8_t address, const std::vector<std::uint16_t>& registers) { return reply(address, registers, 0x03); }

static void test_holding_requests_and_decoding() {
  std::array<std::uint8_t, 8> request{};
  CHECK(pzem::build_holding_request(Model::kAc, 1, request) && request[0] == 1 && request[1] == 0x03 && request[2] == 0 && request[3] == 1 && request[4] == 0 && request[5] == 2);
  CHECK(pzem::crc16(request.data(), 8) == 0);   // a frame with its own CRC checks to zero
  CHECK(pzem::build_holding_request(Model::kDc, 7, request) && request[0] == 7 && request[1] == 0x03 && request[2] == 0 && request[3] == 0 && request[5] == 4 && pzem::crc16(request.data(), 8) == 0);
  CHECK(pzem::build_holding_request(Model::kAc, 0xF8, request) && request[0] == 0xF8);
  CHECK(!pzem::build_holding_request(Model::kAc, 0, request) && !pzem::build_holding_request(Model::kDc, 249, request));
  // only reads are ever built: function 0x03 here and 0x04 above, whatever the address and the model
  for (int address = 1; address <= 247; ++address)
    for (const Model model : {Model::kAc, Model::kDc}) { pzem::build_holding_request(model, static_cast<std::uint8_t>(address), request); if (request[1] != 0x03) CHECK(false); }

  pzem::Holding h;
  const std::vector<std::uint8_t> ac = holding_reply(1, {2300, 1});   // alarm at 2300 W, address 1
  CHECK(ac.size() == pzem::holding_reply_length(Model::kAc) && pzem::decode_holding(Model::kAc, 1, ac.data(), ac.size(), h) == Result::kOk);
  CHECK(h.has_power_alarm && h.power_alarm_w == 2300.0 && h.address == 1 && !h.has_voltage_alarms && !h.has_shunt);
  const std::vector<std::uint8_t> dc = holding_reply(5, {30000, 700, 5, 0});   // 300.00 V high, 7.00 V low, address 5, 100 A shunt
  CHECK(dc.size() == pzem::holding_reply_length(Model::kDc) && pzem::decode_holding(Model::kDc, 5, dc.data(), dc.size(), h) == Result::kOk);
  CHECK(h.has_voltage_alarms && h.high_voltage_alarm_v == 300.0 && h.low_voltage_alarm_v == 7.0 && h.address == 5 && h.has_shunt && h.shunt_a == 100 && !h.has_power_alarm);
  const int shunts[4] = {100, 50, 200, 300};
  for (std::uint16_t code = 0; code < 4; ++code) {
    const std::vector<std::uint8_t> r = holding_reply(5, {30000, 700, 5, code});
    CHECK(pzem::decode_holding(Model::kDc, 5, r.data(), r.size(), h) == Result::kOk && h.shunt_a == shunts[code]);
  }
  // the broadcast address takes any meter's answer and tells which one it was
  CHECK(pzem::decode_holding(Model::kDc, 0xF8, dc.data(), dc.size(), h) == Result::kOk && h.address == 5);

  // what is refused
  std::vector<std::uint8_t> bad = ac; bad[4] ^= 1;
  CHECK(pzem::decode_holding(Model::kAc, 1, bad.data(), bad.size(), h) == Result::kCrc);
  CHECK(pzem::decode_holding(Model::kAc, 2, ac.data(), ac.size(), h) == Result::kAddress);
  CHECK(pzem::decode_holding(Model::kAc, 1, ac.data(), ac.size() - 1, h) == Result::kTooShort);
  std::vector<std::uint8_t> longer = ac; longer.push_back(0);
  CHECK(pzem::decode_holding(Model::kAc, 1, longer.data(), longer.size(), h) == Result::kLength);
  const std::vector<std::uint8_t> measurement = reply(1, {2300, 1});   // function 0x04 where 0x03 was asked
  CHECK(pzem::decode_holding(Model::kAc, 1, measurement.data(), measurement.size(), h) == Result::kFunction);
  std::vector<std::uint8_t> refused{1, 0x83, 0x02};
  const std::uint16_t crc = pzem::crc16(refused.data(), refused.size());
  refused.push_back(static_cast<std::uint8_t>(crc & 0xFF)); refused.push_back(static_cast<std::uint8_t>(crc >> 8));
  CHECK(pzem::decode_holding(Model::kAc, 1, refused.data(), refused.size(), h) == Result::kException);
  CHECK(pzem::decode_holding(Model::kAc, 2, refused.data(), refused.size(), h) == Result::kAddress);
  // a setting no meter has, with a CRC that fits, is not a value
  const std::vector<std::uint8_t> no_address = holding_reply(1, {2300, 0}), far_address = holding_reply(1, {2300, 248}), no_shunt = holding_reply(5, {30000, 700, 5, 4});
  CHECK(pzem::decode_holding(Model::kAc, 0xF8, no_address.data(), no_address.size(), h) == Result::kRange);
  CHECK(pzem::decode_holding(Model::kAc, 0xF8, far_address.data(), far_address.size(), h) == Result::kRange);
  CHECK(pzem::decode_holding(Model::kDc, 5, no_shunt.data(), no_shunt.size(), h) == Result::kRange);
  // the measurement decoder does not take a settings reply for a reading
  pzem::Reading r;
  CHECK(pzem::decode(Model::kAc, 1, ac.data(), ac.size(), r) != Result::kOk);
}

// A stand-in line that answers the measurements and the settings from their own tables (by address); an address that is absent from a table is silent for that function.
struct SettingsLine {
  PzemBus bus;
  std::map<int, std::vector<std::uint8_t>> measurements, settings;
  int measurement_requests = 0, settings_requests = 0;
  std::uint64_t now = 0;
  SettingsLine(std::vector<MeterConfig> meters, std::uint64_t cycle_ms, std::uint64_t check_every_ms) : bus(std::move(meters), cycle_ms, check_every_ms) {}
  void run(std::uint64_t until_ms, std::uint64_t latency_ms = 40) {
    std::vector<std::pair<std::uint64_t, std::vector<std::uint8_t>>> in_flight;
    for (; now < until_ms; now += 10) {
      for (auto it = in_flight.begin(); it != in_flight.end();) {
        if (it->first <= now) { bus.on_rx(it->second.data(), it->second.size(), now); it = in_flight.erase(it); } else ++it;
      }
      const std::vector<std::uint8_t> out = bus.next_tx(now);
      if (out.empty()) continue;
      const bool holding = out[1] == 0x03;
      (holding ? settings_requests : measurement_requests)++;
      const auto& table = holding ? settings : measurements;
      const auto answer = table.find(out[0]);
      if (answer != table.end()) in_flight.push_back({now + latency_ms, answer->second});
    }
  }
};

static void test_the_settings_are_read_now_and_then() {
  std::vector<MeterConfig> meters = {{"grid", "", Model::kAc, 1}, {"dc-bus", "", Model::kDc, 2}, {"old", "", Model::kAc, 3}};
  const auto fill = [](SettingsLine& line) {
    line.measurements[1] = reply(1, kAcRegisters); line.measurements[2] = reply(2, kDcRegisters); line.measurements[3] = reply(3, kAcRegisters);
    line.settings[1] = holding_reply(1, {2300, 1});
    line.settings[2] = holding_reply(2, {30000, 700, 9, 1});   // the meter says its address is 9, yet it answers to the 2 this node uses
    // meter 3 never answers the settings request (an older firmware, a clone): its measurements go on
  };
  SettingsLine line(meters, 1000, 60'000);
  fill(line);
  line.run(8000);
  const auto& states = line.bus.meters();
  CHECK(states[0].have_holding && states[0].holding.power_alarm_w == 2300.0 && states[0].holding.address == 1 && !states[0].address_mismatch());
  CHECK(states[1].have_holding && states[1].holding.address == 9 && states[1].address_mismatch() && states[1].holding.shunt_a == 50);
  CHECK(!states[2].have_holding && states[2].holding_bad >= 1 && states[2].holding_result == Result::kTooShort);
  // asking the settings never counts against the readings: meter 3 has no timeouts and no bad replies, and all three stay fresh
  CHECK(states[2].timeouts == 0 && states[2].replies_bad == 0 && states[2].misses_in_a_row == 0 && states[0].replies_bad == 0 && states[1].replies_bad == 0);
  CHECK(line.bus.fresh(line.now).size() == 3);
  // once each in this time (the first time is right after the first good reading), not at every round
  CHECK(states[0].holding_ok == 1 && states[1].holding_ok == 1 && states[2].holding_bad == 1);
  CHECK(line.settings_requests == 3);
  // and again after the interval
  line.run(75'000);
  CHECK(states[0].holding_ok == 2 && states[2].holding_bad == 2 && line.settings_requests == 6);
  CHECK(line.measurement_requests > 3 * 60);   // the readings went on every second meanwhile

  // never asked when the interval is 0 (the default of the bus itself)
  SettingsLine quiet(meters, 1000, 0);
  fill(quiet);
  quiet.run(8000);
  CHECK(quiet.settings_requests == 0 && !quiet.bus.meters()[0].have_holding && quiet.bus.fresh(quiet.now).size() == 3);

  // a meter that has stopped answering the measurements is not asked for its settings
  SettingsLine silent(meters, 1000, 5000);
  fill(silent);
  silent.measurements.erase(1);
  silent.run(8000);
  CHECK(!silent.bus.meters()[0].holding_tried && silent.bus.meters()[0].timeouts > 0 && silent.bus.meters()[1].have_holding);

  // the address 0xF8 has nothing to compare: the stored address is shown and it is no mismatch
  SettingsLine broadcast({{"only", "", Model::kAc, 0xF8}}, 1000, 60'000);
  broadcast.measurements[0xF8] = reply(4, kAcRegisters);
  broadcast.settings[0xF8] = holding_reply(4, {2300, 4});
  broadcast.run(4000);
  CHECK(broadcast.bus.meters()[0].have_holding && broadcast.bus.meters()[0].holding.address == 4 && !broadcast.bus.meters()[0].address_mismatch());
}

int main() {
  test_crc_and_requests();
  test_holding_requests_and_decoding();
  test_the_settings_are_read_now_and_then();
  test_decoding();
  test_bus();
  test_echo_and_stray_bytes();
  test_message();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
