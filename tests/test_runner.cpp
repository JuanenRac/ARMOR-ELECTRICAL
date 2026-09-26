// ARMOR-ELECTRICAL - host tests of the meters at work: what the node does on every turn of its loop, with a stand-in serial line that plays the meters and a clock the test moves.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
// The meters are stand-ins written from the makers' public description; no real PZEM has been read, and no UART was used.
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "../core/meter_runner.hpp"

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
using namespace armor::manager;
namespace pzem = armor::electrical::pzem;

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
static const std::vector<std::uint16_t> kAc = {2305, 1234, 0, 2844, 0, 1500, 0, 500, 98, 0};     // 230.5 V, 1.234 A, 284.4 W, 1.5 kWh, 50 Hz, 0.98
static const std::vector<std::uint16_t> kDc = {5214, 1420, 7404, 0, 25000, 0, 0, 0};              // 52.14 V, 14.20 A, 740.4 W, 25 kWh

// The line: it answers the requests it hears with what each address is set to say, after a latency, in pieces of at most `chunk` bytes; and it is the clock (waiting moves time).
struct World : uartbus::Line {
  std::uint64_t now = 5000;
  std::map<int, std::vector<std::uint8_t>> answers;
  std::vector<std::pair<std::uint64_t, std::uint8_t>> pending;   // (time it arrives, byte)
  std::vector<int> asked;
  bool fail_writes = false;
  unsigned latency_ms = 40;
  std::size_t chunk = 64;
  bool wall_set = false;
  std::uint64_t wall = 1'700'000'000'000ULL;

  std::size_t read(std::uint8_t* out, std::size_t capacity, unsigned wait_ms) override {
    now += wait_ms;
    std::size_t n = 0;
    while (n < capacity && n < chunk && !pending.empty() && pending.front().first <= now) { out[n++] = pending.front().second; pending.erase(pending.begin()); }
    return n;
  }
  bool write(const std::uint8_t* data, std::size_t length) override {
    if (fail_writes) return false;
    if (length >= 2) asked.push_back(data[0]);
    const auto answer = answers.find(data[0]);
    if (answer != answers.end()) for (std::uint8_t b : answer->second) pending.push_back({now + latency_ms, b});
    return true;
  }
  Clock clock() {
    Clock c;
    c.uptime_ms = [this] { return now; };
    c.wall_clock_is_set = [this] { return wall_set; };
    c.wall_clock_ms = [this] { return wall; };
    return c;
  }
};

struct Tally { int statuses = 0, messages = 0; };
static Tally run(Runner& runner, World& world, std::uint64_t seconds) {
  Tally tally;
  const Clock clock = world.clock();
  const std::uint64_t until = world.now + seconds * 1000ULL;
  while (world.now < until) {
    const Runner::Outcome outcome = runner.step(world, clock);
    tally.statuses += outcome.statuses ? 1 : 0;
    tally.messages += outcome.message ? 1 : 0;
  }
  return tally;
}

static config::Settings settings_with(std::initializer_list<config::MeterSetting> meters, int poll_s = 2) {
  config::Settings s = config::default_settings("a1b2c3");
  s.bus.poll_s = poll_s;
  std::size_t i = 0;
  for (const config::MeterSetting& m : meters) s.meters[i++] = m;
  return s;
}
static bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

static void test_no_meter() {
  Runner runner(settings_with({}));
  CHECK(!runner.any_meter() && runner.bus().state == "disabled");
  for (const MeterStatus& m : runner.meters()) CHECK(!m.enabled && m.state == "disabled");
  CHECK(runner.meters()[0].number == 1 && runner.meters()[15].number == 16);
}

static void test_one_meter() {
  World world;
  world.answers[1] = reply(1, kAc);
  Runner runner(settings_with({{true, "grid", "Red", "ac", 1}}));
  CHECK(runner.any_meter() && runner.bus().state == "starting" && runner.meters()[0].state == "waiting" && runner.meters()[1].state == "disabled");
  runner.set_line_open();
  const Tally tally = run(runner, world, 10);
  const MeterStatus& m = runner.meters()[0];
  CHECK(runner.bus().state == "running" && m.state == "reading" && m.have_reading && m.error.empty() && m.replies_ok >= 3 && m.replies_bad == 0 && m.timeouts == 0);
  CHECK(m.voltage_v == 230.5 && m.current_a == 1.234 && m.power_w == 284.4 && m.energy_kwh == 1.5 && !m.alarm);
  CHECK(m.channel == "grid" && m.label == "Red" && m.model == "ac" && m.address == 1);
  // every request is a whole read request (8 bytes) and every reply a whole AC reply (25 bytes)
  CHECK(runner.bus().bytes_tx == 8 * world.asked.size() && runner.bus().bytes_rx == 25 * runner.meters()[0].replies_ok);
  // a status every second and a message every poll_s (2 s): about 10 and 5 in ten seconds
  CHECK(tally.statuses >= 9 && tally.statuses <= 11 && tally.messages >= 4 && tally.messages <= 6 && runner.bus().messages == static_cast<std::uint32_t>(tally.messages));
  const std::string& message = runner.message();
  CHECK(contains(message, "\"kind\":\"electrical\"") && contains(message, "\"node_id\":\"electrical-a1b2c3\"") && contains(message, "\"id\":\"grid\"") && contains(message, "\"voltage_v\":230.5"));
  CHECK(contains(message, "\"switching_enabled\":false") && runner.write_failures() == 0);
}

static void test_two_meters_in_order() {
  World world;
  world.answers[1] = reply(1, kAc);
  world.answers[7] = reply(7, kDc);
  Runner runner(settings_with({{true, "grid", "", "ac", 1}, {false, "unused", "", "ac", 2}, {true, "dc-bus", "Bus", "dc", 7}}));
  runner.set_line_open();
  run(runner, world, 8);
  // the disabled one is never asked, and the two are asked alternately
  CHECK(world.asked.size() >= 6 && world.asked[0] == 1 && world.asked[1] == 7 && world.asked[2] == 1);
  for (int a : world.asked) CHECK(a == 1 || a == 7);
  CHECK(runner.meters()[0].state == "reading" && runner.meters()[1].state == "disabled" && runner.meters()[2].state == "reading");
  CHECK(runner.meters()[2].voltage_v == 52.14 && runner.meters()[2].power_w == 740.4 && runner.meters()[2].model == "dc");
  CHECK(contains(runner.message(), "\"id\":\"grid\"") && contains(runner.message(), "\"id\":\"dc-bus\"") && !contains(runner.message(), "unused"));
}

static void test_silent_and_garbled() {
  World world;
  world.answers[1] = reply(1, kAc);       // good
  // 2 does not answer at all
  std::vector<std::uint8_t> broken = reply(3, kAc); broken[5] ^= 1;
  world.answers[3] = broken;                                  // a bad CRC
  world.answers[4] = reply(9, kAc);                           // the reply of another address
  world.answers[5] = exception_reply(5, 0x02);                // a refusal
  Runner runner(settings_with({{true, "m1", "", "ac", 1}, {true, "m2", "", "ac", 2}, {true, "m3", "", "ac", 3}, {true, "m4", "", "ac", 4}, {true, "m5", "", "ac", 5}}));
  runner.set_line_open();
  run(runner, world, 12);
  const auto& m = runner.meters();
  CHECK(m[0].state == "reading");
  CHECK(m[1].state == "silent" && m[1].error == "silent" && m[1].timeouts >= 3 && m[1].replies_ok == 0 && !m[1].have_reading);
  CHECK(m[2].state == "garbled" && m[2].error == "crc" && m[2].replies_bad >= 3 && !m[2].have_reading);
  CHECK(m[3].state == "garbled" && m[3].error == "address");
  CHECK(m[4].state == "garbled" && m[4].error == "exception");
  // only the meter that reads is in the message
  CHECK(contains(runner.message(), "\"id\":\"m1\"") && !contains(runner.message(), "\"id\":\"m2\"") && !contains(runner.message(), "\"id\":\"m3\"") && !contains(runner.message(), "\"id\":\"m5\""));
}

static void test_a_meter_that_stops() {
  World world;
  world.answers[1] = reply(1, kAc);
  world.answers[2] = reply(2, kAc);
  Runner runner(settings_with({{true, "a", "", "ac", 1}, {true, "b", "", "ac", 2}}));
  runner.set_line_open();
  run(runner, world, 6);
  CHECK(runner.meters()[0].state == "reading" && runner.meters()[1].state == "reading" && contains(runner.message(), "\"id\":\"b\""));
  world.answers.erase(2);   // the second meter is unplugged
  run(runner, world, 5);
  // for a few seconds its last figures are still fresh; then they are no longer shown: silent, with the reason, and out of the message
  CHECK(runner.meters()[1].state == "reading" || runner.meters()[1].state == "silent");
  run(runner, world, 12);
  CHECK(runner.meters()[1].state == "silent" && runner.meters()[1].error == "silent" && !runner.meters()[1].have_reading && runner.meters()[1].replies_ok > 0);
  CHECK(runner.meters()[0].state == "reading" && contains(runner.message(), "\"id\":\"a\"") && !contains(runner.message(), "\"id\":\"b\""));
  world.answers[2] = reply(2, kAc);   // it is plugged back
  run(runner, world, 6);
  CHECK(runner.meters()[1].state == "reading" && contains(runner.message(), "\"id\":\"b\""));
}

static void test_pieces_and_slow_replies() {
  // a reply that arrives seven bytes at a time is put together
  World world;
  world.chunk = 7;
  world.answers[1] = reply(1, kAc);
  Runner runner(settings_with({{true, "grid", "", "ac", 1}}));
  runner.set_line_open();
  run(runner, world, 6);
  CHECK(runner.meters()[0].state == "reading" && runner.meters()[0].replies_bad == 0 && runner.bus().bytes_rx == 25 * runner.meters()[0].replies_ok);
  // a reply that comes after the timeout is a timeout, and the late bytes do no harm
  World slow;
  slow.latency_ms = 600;
  slow.answers[1] = reply(1, kAc);
  Runner late(settings_with({{true, "grid", "", "ac", 1}}));
  late.set_line_open();
  run(late, slow, 10);
  // (the very first request of a run has no cycle behind it, so the next one goes out at once and takes that late reply for its own: at most one reading gets through)
  run(late, slow, 30);
  CHECK(late.meters()[0].state == "silent" && late.meters()[0].timeouts >= 10 && late.meters()[0].replies_ok <= 1 && late.meters()[0].replies_bad == 0);
}

static void test_a_line_that_will_not_send() {
  World world;
  world.fail_writes = true;
  world.answers[1] = reply(1, kAc);
  Runner runner(settings_with({{true, "grid", "", "ac", 1}}));
  runner.set_line_open();
  run(runner, world, 4);
  CHECK(runner.write_failures() >= 3 && runner.bus().bytes_tx == 0 && runner.bus().bytes_rx == 0 && runner.meters()[0].state == "silent");
  // and a line that did not open says why
  Runner broken(settings_with({{true, "grid", "", "ac", 1}}));
  broken.set_line_failed("pins");
  CHECK(broken.bus().state == "error" && broken.bus().error == "pins");
}

static void test_the_clock_of_the_message() {
  World world;
  world.answers[1] = reply(1, kAc);
  Runner runner(settings_with({{true, "grid", "", "ac", 1}}));
  runner.set_line_open();
  run(runner, world, 4);
  // no wall clock yet: the message carries the time since the start
  CHECK(!contains(runner.message(), "1700000000000"));
  world.wall_set = true;
  run(runner, world, 4);
  CHECK(contains(runner.message(), "\"timestamp_ms\":17000000") || contains(runner.message(), "\"timestamp_ms\":1700000000"));
}

static void test_the_polling_period() {
  for (int poll : {1, 5, 30}) {
    World world;
    world.answers[1] = reply(1, kAc);
    Runner runner(settings_with({{true, "grid", "", "ac", 1}}, poll));
    runner.set_line_open();
    const Tally tally = run(runner, world, 60);
    const int expected = 60 / poll;
    CHECK(tally.messages >= expected - 1 && tally.messages <= expected + 1);
  }
}

static void test_sixteen_meters() {
  World world;
  config::Settings s = config::default_settings("a1b2c3");
  for (int i = 0; i < 16; ++i) {
    s.meters[i] = {true, "m" + std::to_string(i + 1), "", i % 2 ? "dc" : "ac", i + 1};
    world.answers[i + 1] = i % 2 ? reply(i + 1, kDc) : reply(i + 1, kAc);
  }
  Runner runner(s);
  runner.set_line_open();
  run(runner, world, 30);
  int reading = 0;
  for (const MeterStatus& m : runner.meters()) reading += m.state == "reading" ? 1 : 0;
  CHECK(reading == 16 && contains(runner.message(), "\"id\":\"m16\"") && contains(runner.message(), "\"id\":\"m1\""));
  CHECK(world.asked.size() >= 32 && world.asked[0] == 1 && world.asked[15] == 16 && world.asked[16] == 1);
}

int main() {
  test_no_meter();
  test_one_meter();
  test_two_meters_in_order();
  test_silent_and_garbled();
  test_a_meter_that_stops();
  test_pieces_and_slow_replies();
  test_a_line_that_will_not_send();
  test_the_clock_of_the_message();
  test_the_polling_period();
  test_sixteen_meters();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
