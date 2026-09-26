// ARMOR-ELECTRICAL - host tests of the commands to a switch: the strict reader (against the shared conformance vectors), the two-step close and its token, every refusal,
// the state message with switches, and a hundred thousand random messages against stand-in contactors. No real contactor has ever been driven.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

using namespace armor::electrical;
namespace json = armor::json;

// A contactor as in test_interlock.cpp: it follows its coil after a delay, and can be stuck.
struct Contactor {
  bool actual = false, stuck_closed = false, stuck_open = false, last_coil = false;
  std::uint64_t close_ms = 60, open_ms = 40, changed_at = 0;
  void step(bool coil, std::uint64_t now) {
    if (coil != last_coil) { last_coil = coil; changed_at = now; }
    if (coil && !actual && !stuck_open && now - changed_at >= close_ms) actual = true;
    if (!coil && actual && !stuck_closed && now - changed_at >= open_ms) actual = false;
  }
};

// A bench: one switch called "transfer", two contactors, and random bytes that count up (so a token is predictable in the test and different each time).
struct Bench {
  std::uint8_t seed = 1;
  SwitchSet set;
  Contactor a, b;
  std::uint64_t now = 0;
  std::uint64_t both_closed_ticks = 0;
  explicit Bench(bool allowed = true, bool random_works = true)
      : set("electrical-1", [this, random_works](std::uint8_t* out, std::size_t n) { if (!random_works) return false; for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<std::uint8_t>(seed++ * 37 + 11); return true; }) {
    SwitchConfig config;
    config.id = "transfer"; config.label = "Grid or inverter"; config.source_a = "grid"; config.source_b = "inverter";
    config.transfer = TransferConfig{allowed, 2000, 1000, 10'000};
    set.add(config);
  }
  void run(std::uint64_t ms) {
    for (std::uint64_t end = now + ms; now < end; now += 10) {
      set.set_feedback("transfer", Feedback{a.actual, b.actual});
      set.tick(now);
      const Coils c = set.coils("transfer");
      a.step(c.a, now); b.step(c.b, now);
      if (a.actual && b.actual) ++both_closed_ticks;
    }
  }
  static std::string command(const std::string& action, const std::string& token = "", const std::string& id = "c0ffee0123456789", const std::string& target = "transfer", const std::string& node = "electrical-1") {
    return "{\"kind\":\"electrical_command\",\"node_id\":\"" + node + "\",\"timestamp_ms\":1,\"command_id\":\"" + id + "\",\"switch\":\"" + target + "\",\"action\":\"" + action + "\"" +
           (token.empty() ? "" : ",\"token\":\"" + token + "\"") + "}";
  }
  json::Value ask(const std::string& text) {
    json::Value out;
    const std::string answer = set.handle(text, now);
    if (!answer.empty()) json::parse(answer, out);
    return out;
  }
  static bool accepted(const json::Value& v) { return v.bool_or("accepted", false); }
  static std::string refusal(const json::Value& v) { return v.string_or("refusal", "?"); }
  std::string arm() { const json::Value v = ask(command("arm")); return accepted(v) ? v.string_or("token", "") : ""; }
};

static void test_reader() {
  SwitchCommand c;
  std::string why;
  CHECK(parse_command(Bench::command("arm"), c, &why) && c.action == Action::kArm && c.command_id == "c0ffee0123456789" && c.switch_id == "transfer" && c.token.empty());
  CHECK(parse_command(Bench::command("close_b", "ab12cd34ef56ab12"), c) && c.action == Action::kCloseB && c.token == "ab12cd34ef56ab12");
  CHECK(!parse_command(Bench::command("close_a"), c, &why));                                  // a close needs its token
  CHECK(!parse_command(Bench::command("open", "ab12cd34ef56ab12"), c));                       // and nothing else carries one
  CHECK(!parse_command(Bench::command("toggle"), c));
  CHECK(!parse_command(Bench::command("ARM"), c));
  CHECK(!parse_command(Bench::command("arm", "", "short"), c));
  CHECK(!parse_command(Bench::command("arm", "", "c0ffee0123456789", "my switch"), c));
  CHECK(!parse_command(Bench::command("arm", "", "c0ffee0123456789", "transfer", "my node"), c));
  CHECK(!parse_command("{\"kind\":\"electrical_command\",\"node_id\":\"electrical-1\",\"timestamp_ms\":1,\"command_id\":\"c0ffee0123456789\",\"switch\":\"transfer\",\"action\":\"arm\",\"force\":true}", c));
  CHECK(!parse_command("{\"kind\":\"electrical\",\"node_id\":\"electrical-1\",\"timestamp_ms\":1,\"command_id\":\"c0ffee0123456789\",\"switch\":\"transfer\",\"action\":\"arm\"}", c));
  CHECK(!parse_command("{\"kind\":\"electrical_command\",\"node_id\":\"electrical-1\",\"timestamp_ms\":-1,\"command_id\":\"c0ffee0123456789\",\"switch\":\"transfer\",\"action\":\"arm\"}", c));
  CHECK(!parse_command("{\"kind\":\"electrical_command\",\"node_id\":\"electrical-1\",\"command_id\":\"c0ffee0123456789\",\"switch\":\"transfer\",\"action\":\"arm\"}", c));
  CHECK(!parse_command("", c) && !parse_command("[]", c) && !parse_command("{\"kind\":", c) && !parse_command("null", c));
}

// The shared vectors of ARMOR-COMMON decide, as they decide for the server and for the Python validator.
static void test_conformance(const std::string& directory) {
  std::ifstream file(directory + "/electrical_command.json");
  if (!file) { std::printf("SKIP conformance: ARMOR-COMMON is not next to this project\n"); return; }
  std::stringstream text;
  text << file.rdbuf();
  json::Value doc;
  CHECK(json::parse(text.str(), doc) && doc.get("vectors") != nullptr);
  if (doc.get("vectors") == nullptr) return;
  std::size_t seen = 0;
  for (const json::Value& vector : doc.get("vectors")->items) {
    // Re-serialise the payload: the reader takes text.
    const json::Value* payload = vector.get("payload");
    if (payload == nullptr) continue;
    std::string out = "{";
    for (std::size_t i = 0; i < payload->names.size(); ++i) {
      const json::Value& v = payload->items[i];
      out += (i ? "," : "") + std::string("\"") + payload->names[i] + "\":";
      if (v.is_string()) out += "\"" + v.text + "\""; else if (v.is_bool()) out += v.boolean ? "true" : "false"; else if (v.is_number()) { char n[40]; std::snprintf(n, sizeof n, "%.0f", v.number); out += n; } else out += "null";
    }
    out += "}";
    SwitchCommand c;
    const bool ok = parse_command(out, c);
    CHECK(ok == vector.bool_or("valid", false));
    if (ok != vector.bool_or("valid", false)) std::printf("  vector: %s\n", vector.string_or("name", "?").c_str());
    ++seen;
  }
  CHECK(seen >= 20);
}

static void test_two_step_close() {
  Bench bench;
  bench.run(3000);
  // Closing without an arm is refused.
  CHECK(Bench::refusal(bench.ask(Bench::command("close_a", "ab12cd34ef56ab12"))) == "not_armed");
  bench.run(500);
  CHECK(!bench.a.actual && !bench.set.coils("transfer").a);
  // Arm, then close with the token: accepted, and after the dead time the contactor closes.
  const std::string token = bench.arm();
  CHECK(token.size() == 16);
  CHECK(bench.set.switches_json(bench.now).find("\"armed\":true") != std::string::npos);
  const json::Value closed = bench.ask(Bench::command("close_a", token, "aaaaaaaa11111111"));
  CHECK(Bench::accepted(closed) && Bench::refusal(closed) == "none" && closed.string_or("command_id", "") == "aaaaaaaa11111111");
  bench.run(3000);
  CHECK(bench.a.actual && !bench.b.actual && bench.set.selected("transfer") == Source::kA);
  // The same close again moves nothing: the token is spent.
  CHECK(Bench::refusal(bench.ask(Bench::command("close_a", token))) == "not_armed");
  // Going to B: arm, then close_b: A opens, the dead time passes, B closes; never both.
  const std::string second = bench.arm();
  CHECK(second != token && Bench::accepted(bench.ask(Bench::command("close_b", second))));
  bench.run(6000);
  CHECK(!bench.a.actual && bench.b.actual && bench.both_closed_ticks == 0);
  // Open is accepted with no token and no arm, and everything opens.
  CHECK(Bench::accepted(bench.ask(Bench::command("open"))));
  bench.run(500);
  CHECK(!bench.a.actual && !bench.b.actual);
}

static void test_the_token_is_the_key() {
  Bench bench;
  bench.run(100);
  const std::string token = bench.arm();
  // A wrong token is refused and withdraws the arm: the right one no longer works.
  CHECK(Bench::refusal(bench.ask(Bench::command("close_a", "ffffffffffffffff"))) == "bad_token");
  CHECK(Bench::refusal(bench.ask(Bench::command("close_a", token))) == "not_armed");
  bench.run(3000);
  CHECK(!bench.a.actual);
  // An arm that waits too long expires with its token.
  const std::string late = bench.arm();
  bench.run(11'000);
  CHECK(Bench::refusal(bench.ask(Bench::command("close_a", late))) == "not_armed");
  // A token from an arm of one moment cannot be used for the close of another: the newer arm replaces it.
  const std::string first = bench.arm(), newer = bench.arm();
  CHECK(first != newer && Bench::refusal(bench.ask(Bench::command("close_b", first))) == "bad_token");
  // An open withdraws an arm.
  const std::string again = bench.arm();
  CHECK(Bench::accepted(bench.ask(Bench::command("open"))));
  CHECK(Bench::refusal(bench.ask(Bench::command("close_a", again))) == "not_armed");
  // Nothing moved in all of this.
  bench.run(3000);
  CHECK(!bench.a.actual && !bench.b.actual && bench.set.coils("transfer").a == false && bench.set.coils("transfer").b == false);
}

static void test_refusals() {
  {   // a node that may not switch: every request but open is refused, and no coil is ever energised
    Bench bench(false);
    bench.run(100);
    CHECK(Bench::refusal(bench.ask(Bench::command("arm"))) == "disabled");
    CHECK(Bench::refusal(bench.ask(Bench::command("close_a", "ab12cd34ef56ab12"))) == "disabled");
    CHECK(Bench::refusal(bench.ask(Bench::command("close_b", "ab12cd34ef56ab12"))) == "disabled");
    CHECK(Bench::refusal(bench.ask(Bench::command("acknowledge"))) == "disabled");
    CHECK(Bench::accepted(bench.ask(Bench::command("open"))));
    bench.run(5000);
    CHECK(!bench.a.actual && !bench.b.actual);
    CHECK(!bench.set.switching_enabled() && bench.set.switches_json(bench.now).find("\"armed\":false") != std::string::npos);
  }
  {   // no source of randomness: no arm is granted
    Bench bench(true, false);
    CHECK(Bench::refusal(bench.ask(Bench::command("arm"))) == "not_supported");
  }
  {   // a switch it does not have, and a command for another node
    Bench bench;
    CHECK(Bench::refusal(bench.ask(Bench::command("open", "", "c0ffee0123456789", "nothing"))) == "unknown_switch");
    CHECK(Bench::refusal(bench.ask(Bench::command("arm", "", "c0ffee0123456789", "nothing"))) == "unknown_switch");
    CHECK(bench.set.handle(Bench::command("open", "", "c0ffee0123456789", "transfer", "electrical-2"), bench.now).empty());
    CHECK(bench.set.handle("not json", bench.now).empty() && bench.set.handle(Bench::command("toggle"), bench.now).empty());
  }
  {   // a contactor that welds: fault, everything open, no new arm or close until it is acknowledged with both open
    Bench bench;
    bench.run(100);
    const std::string token = bench.arm();
    bench.ask(Bench::command("close_a", token));
    bench.run(3000);
    CHECK(bench.a.actual);
    bench.a.stuck_closed = true;
    bench.ask(Bench::command("open"));
    bench.run(2000);
    CHECK(bench.set.fault("transfer") == Fault::kDidNotOpen && bench.set.switches_json(bench.now).find("\"fault\":\"did_not_open\"") != std::string::npos);
    CHECK(Bench::refusal(bench.ask(Bench::command("arm"))) == "fault");
    CHECK(Bench::refusal(bench.ask(Bench::command("close_b", "ab12cd34ef56ab12"))) == "fault");
    CHECK(Bench::refusal(bench.ask(Bench::command("acknowledge"))) == "not_confirmed_open");   // the welded contactor still shows closed
    bench.a.stuck_closed = false; bench.a.actual = false;
    bench.run(200);
    CHECK(Bench::accepted(bench.ask(Bench::command("acknowledge"))) && bench.set.fault("transfer") == Fault::kNone);
    CHECK(!bench.set.coils("transfer").a && !bench.set.coils("transfer").b && bench.both_closed_ticks == 0);
  }
}

static void test_state_message() {
  Bench bench;
  bench.run(3000);
  CHECK(bench.set.switching_enabled());
  const std::string switches = bench.set.switches_json(bench.now);
  json::Value doc;
  CHECK(json::parse(switches, doc) && doc.is_array() && doc.items.size() == 1);
  const json::Value& s = doc.items[0];
  CHECK(s.string_or("id", "") == "transfer" && s.string_or("kind", "") == "transfer" && s.string_or("source_a", "") == "grid" && s.string_or("source_b", "") == "inverter");
  CHECK(s.string_or("selected", "?") == "none" && s.string_or("wanted", "?") == "none" && !s.bool_or("closing", true) && !s.bool_or("armed", true) && s.string_or("fault", "?") == "none");
  const std::string token = bench.arm();
  bench.ask(Bench::command("close_a", token));
  bench.run(30);
  json::Value closing;
  json::parse(bench.set.switches_json(bench.now), closing);
  CHECK(closing.items[0].string_or("wanted", "?") == "none" || closing.items[0].string_or("wanted", "?") == "a");
  bench.run(3000);
  json::Value done;
  json::parse(bench.set.switches_json(bench.now), done);
  CHECK(done.items[0].bool_or("a_closed", false) && !done.items[0].bool_or("b_closed", true) && done.items[0].string_or("selected", "?") == "a" && done.items[0].string_or("wanted", "?") == "a");
  // The whole state message carries the array after the channels.
  const std::string message = message_json("electrical-1", 5, bench.set.switching_enabled(), {}, {}, bench.set.switches_json(bench.now));
  json::Value whole;
  CHECK(json::parse(message, whole) && whole.get("switches") != nullptr && whole.get("switches")->items.size() == 1 && whole.bool_or("switching_enabled", false));
  // A node with no switch has no `switches` in its message.
  CHECK(json::parse(message_json("electrical-1", 5, false, {}), whole) && whole.get("switches") == nullptr);
  // The set refuses a bad id, a repeated one and a fifth switch.
  SwitchSet set("electrical-1", nullptr);
  SwitchConfig config;
  config.id = "Bad"; CHECK(!set.add(config));
  for (const char* id : {"t1", "t2", "t3", "t4"}) { config.id = id; CHECK(set.add(config)); }
  config.id = "t1"; CHECK(!set.add(config));
  config.id = "t5"; CHECK(!set.add(config) && set.size() == 4);
}

// A hundred thousand messages, half of them noise, half of them real commands in a random order, with contactors that sometimes weld, stick or fall out: whatever is said,
// the node never energises two coils, never energises one with switching off, and a close that was not armed with its own token moves nothing.
static void test_random() {
  std::uint32_t state = 12345;
  auto next = [&state](std::uint32_t n) { state = state * 1664525u + 1013904223u; return (state >> 8) % n; };
  for (int round = 0; round < 40; ++round) {
    const bool allowed = next(4) != 0;
    Bench bench(allowed);
    std::string token;
    int steps = 0;
    for (; steps < 2500; ++steps) {
      const std::uint32_t pick = next(12);
      std::string text;
      switch (pick) {
        case 0: text = Bench::command("arm"); break;
        case 1: text = Bench::command("close_a", token.empty() ? "0123456789abcdef" : token); break;
        case 2: text = Bench::command("close_b", token.empty() ? "0123456789abcdef" : token); break;
        case 3: text = Bench::command("open"); break;
        case 4: text = Bench::command("acknowledge"); break;
        case 5: text = Bench::command("close_a", "ffffffffffffffff"); break;
        case 6: text = "{\"kind\":\"electrical_command\"}"; break;
        case 7: text = Bench::command("toggle"); break;
        default: break;
      }
      if (!text.empty()) {
        const json::Value v = bench.ask(text);
        if (pick == 0 && Bench::accepted(v)) token = v.string_or("token", "");
        if (!Bench::accepted(v) && !v.string_or("refusal", "").empty() && pick != 0) token = pick == 1 || pick == 2 ? std::string() : token;
      }
      if (next(200) == 0) { bench.a.stuck_closed = next(2) == 0; bench.a.stuck_open = next(2) == 0; }
      if (next(200) == 0) { bench.b.stuck_closed = next(2) == 0; bench.b.stuck_open = next(2) == 0; }
      if (next(300) == 0) bench.a.actual = !bench.a.actual;
      bench.run(10 + next(400));
      const Coils c = bench.set.coils("transfer");
      CHECK(!(c.a && c.b));
      CHECK(allowed || (!c.a && !c.b));
      if (bench.set.fault("transfer") != Fault::kNone) CHECK(!c.a && !c.b);
    }
  }
}

int main(int argc, char** argv) {
  test_reader();
  test_conformance(argc > 1 ? argv[1] : "../ARMOR-COMMON/conformance");
  test_two_step_close();
  test_the_token_is_the_key();
  test_refusals();
  test_state_message();
  test_random();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
