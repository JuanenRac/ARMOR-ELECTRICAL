// ARMOR-ELECTRICAL - host tests of the meters: the Modbus CRC, the PZEM frames, the bus that asks them one at a time, and the message the node makes.
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

int main() {
  test_crc_and_requests();
  test_decoding();
  test_bus();
  test_message();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
