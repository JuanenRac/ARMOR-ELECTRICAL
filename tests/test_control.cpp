// ARMOR-NODE-ELECTRICAL - host tests of the orders the server can give a node (core/control_gate.hpp): the strict reading of the order against the shared vectors, the one-time token bound to its operation
// (once, in time, the right one, never again), the refusals (too soon, busy, already in that state), the state machine (running, paused, standby, maintenance, identify), the standby that survives
// a restart, the wait of a rolling restart, and the answers the node publishes against the shared vectors.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
// Nothing here touches hardware or a network: the clock is a number the test moves. Run it with the folder of the shared vectors (ARMOR-COMMON/conformance) as the argument.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if __has_include("../core/control_gate.hpp")
#include "../core/control_gate.hpp"
#include "../core/node_health.hpp"
#else
#include "../main/core/control_gate.hpp"
#include "../main/core/node_health.hpp"
#endif

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
using control::Busy;
using control::Operation;
using control::Refusal;
using control::State;

static const std::string kToken = "0123456789abcdef0123456789abcdef";
static const std::string kMine = "radar-1";
static const std::string kOther = "bbbbbbbbbbbbbbbbbbbbbbbb";
static const std::uint64_t kUp = 3600;   // the node has been up for an hour
static const control::Situation kRunning{State::kRunning, false};

static const Operation kAll[] = {Operation::kRestart, Operation::kPause, Operation::kResume, Operation::kStandby,
                                 Operation::kIdentify, Operation::kDiagnostics, Operation::kMaintenanceOn, Operation::kMaintenanceOff};

static control::Command command(Operation operation = Operation::kRestart, control::Action action = control::Action::kArm, control::Mode mode = control::Mode::kNow, int slot = -1,
                                std::uint32_t delay = 0) {
  control::Command c;
  c.node_id = kMine;
  c.command_id = "d0d0d0d012345678";
  c.requested_by = "ana";
  c.operation = operation;
  c.action = action;
  c.mode = mode;
  c.slot = slot;
  c.delay_s = delay;
  if (action == control::Action::kExecute) c.token = kToken;
  return c;
}

// An order as the server writes it.
static std::string order(const char* action, const char* operation, const std::string& extra = "", const char* mode = "now", const char* node = "radar-1") {
  return std::string("{\"kind\":\"node_control_command\",\"node_id\":\"") + node + "\",\"timestamp_ms\":9000,\"command_id\":\"d0d0d0d012345678\",\"action\":\"" + action +
         "\",\"operation\":\"" + operation + "\",\"mode\":\"" + mode + "\"" + extra + ",\"requested_by\":\"ana\"}";
}
static const std::string kTokenField = ",\"token\":\"0123456789abcdef0123456789abcdef\"";

static bool read_file(const std::string& path, std::string& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  std::ostringstream text;
  text << file.rdbuf();
  out = text.str();
  return true;
}

// The same document, whatever the order of the members.
static bool same_json(const json::Value& a, const json::Value& b) {
  if (a.type != b.type) return false;
  switch (a.type) {
    case json::Type::kNull: return true;
    case json::Type::kBool: return a.boolean == b.boolean;
    case json::Type::kNumber: return a.number == b.number;
    case json::Type::kString: return a.text == b.text;
    case json::Type::kArray:
      if (a.items.size() != b.items.size()) return false;
      for (std::size_t i = 0; i < a.items.size(); ++i) if (!same_json(a.items[i], b.items[i])) return false;
      return true;
    case json::Type::kObject:
      if (a.names.size() != b.names.size()) return false;
      for (std::size_t i = 0; i < a.names.size(); ++i) {
        const json::Value* other = b.get(a.names[i]);
        if (other == nullptr || !same_json(a.items[i], *other)) return false;
      }
      return true;
  }
  return false;
}

// The text of a JSON value (the members the order is made of), to hand back to the parser of the node.
static std::string text_of(const json::Value& v) {
  switch (v.type) {
    case json::Type::kNull: return "null";
    case json::Type::kBool: return v.boolean ? "true" : "false";
    case json::Type::kNumber: { char buffer[64]; std::snprintf(buffer, sizeof buffer, "%.17g", v.number); return buffer; }
    case json::Type::kString: return json::quote(v.text);
    case json::Type::kArray: { std::string out = "["; for (std::size_t i = 0; i < v.items.size(); ++i) out += (i ? "," : "") + text_of(v.items[i]); return out + "]"; }
    case json::Type::kObject: { std::string out = "{"; for (std::size_t i = 0; i < v.names.size(); ++i) out += (i ? "," : "") + json::quote(v.names[i]) + ":" + text_of(v.items[i]); return out + "}"; }
  }
  return "null";
}

static bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// ---- the order, against the vectors every implementation shares -----------------------------------------------------------------------------

static void test_the_order_against_the_shared_vectors(const std::string& dir) {
  std::string text;
  json::Value doc;
  CHECK(read_file(dir + "/node_control_command.json", text) && json::parse(text, doc));
  const json::Value* vectors = doc.get("vectors");
  CHECK(vectors != nullptr && vectors->is_array() && vectors->items.size() >= 60);
  if (vectors == nullptr) return;
  int accepted = 0, refused = 0;
  for (const json::Value& vector : vectors->items) {
    const json::Value* payload = vector.get("payload");
    const std::string name = vector.string_or("name", "?");
    if (payload == nullptr) continue;
    control::Command parsed;
    const bool ok = control::parse_command(text_of(*payload), parsed);
    if (vector.bool_or("valid", false)) {
      ++accepted;
      if (!ok) std::printf("the node refuses a valid order: %s\n", name.c_str());
      CHECK(ok);
    } else {
      ++refused;
      if (ok) std::printf("the node accepts an order the contract refuses: %s\n", name.c_str());
      CHECK(!ok);
    }
  }
  CHECK(accepted >= 25 && refused >= 40);
}

static void test_the_answers_are_the_ones_of_the_vectors(const std::string& dir) {
  std::string text;
  json::Value doc;
  CHECK(read_file(dir + "/node_control_result.json", text) && json::parse(text, doc));
  const json::Value* vectors = doc.get("vectors");
  CHECK(vectors != nullptr && vectors->items.size() >= 30);
  if (vectors == nullptr) return;
  auto payload_of = [&](const std::string& name) {
    json::Value out;
    bool found = false;
    for (const json::Value& vector : vectors->items) {
      if (vector.string_or("name", "") != name) continue;
      const json::Value* p = vector.get("payload");
      if (p != nullptr) { out = *p; found = true; }
    }
    if (!found) std::printf("no vector named: %s\n", name.c_str());
    CHECK(found);
    return out;
  };
  auto parsed = [&](const std::string& produced) { json::Value out; CHECK(json::parse(produced, out)); return out; };
  control::Command arm = command();
  CHECK(same_json(parsed(control::armed_json(kMine, 9100, arm, kToken)), payload_of("a restart arm accepted, with its token")));
  control::Command pause_arm = command(Operation::kPause);
  CHECK(same_json(parsed(control::armed_json(kMine, 9100, pause_arm, kToken)), payload_of("a pause arm accepted")));
  control::Command execute = command(Operation::kRestart, control::Action::kExecute);
  execute.command_id = "d0d0d0d012345679";
  CHECK(same_json(parsed(control::restarting_json(kMine, 9200, execute, 3, 86400, "0.7.7")), payload_of("the node is restarting now")));
  control::Command rolling = execute;
  rolling.mode = control::Mode::kRolling;
  CHECK(same_json(parsed(control::restarting_json(kMine, 9200, rolling, 90, 86400, "0.7.7")), payload_of("the node is restarting in a rolling slot")));
  CHECK(same_json(parsed(control::rejected_json(kMine, 9100, arm, Refusal::kBusyUpdate)), payload_of("refused: busy installing an update")));
  control::Command pause = command(Operation::kPause, control::Action::kExecute);
  pause.command_id = "d0d0d0d01234567a";
  control::Machine paused;
  paused.pause(1000, 3600, 1760000000000ULL, "ana");
  CHECK(same_json(parsed(control::applied_json(kMine, 9300, pause, paused, 1000)), payload_of("a pause applied")));
  control::Machine standby;
  standby.standby(1760000000000ULL, "ana");
  control::Command stand = command(Operation::kStandby, control::Action::kExecute);
  stand.command_id = "d0d0d0d01234567a";
  CHECK(same_json(parsed(control::applied_json(kMine, 9300, stand, standby, 1000)), payload_of("a standby applied")));
  control::Machine resumed = standby;
  resumed.resume();
  control::Command res = command(Operation::kResume, control::Action::kExecute);
  res.command_id = "d0d0d0d01234567a";
  CHECK(same_json(parsed(control::applied_json(kMine, 9300, res, resumed, 1000)), payload_of("a resume applied")));
  control::Machine maintained;
  maintained.maintenance_on(1000, 3600);
  control::Command maintenance = command(Operation::kMaintenanceOn, control::Action::kExecute);
  maintenance.command_id = "d0d0d0d01234567a";
  {
    control::Machine zero_clock;   // the node's clock is not set: since is 0
    zero_clock.maintenance_on(1000, 3600);
    CHECK(same_json(parsed(control::applied_json(kMine, 9300, maintenance, zero_clock, 1000)), payload_of("maintenance on applied")));
  }
  control::Machine off;
  control::Command maintenance_off = command(Operation::kMaintenanceOff, control::Action::kExecute);
  maintenance_off.command_id = "d0d0d0d01234567a";
  CHECK(same_json(parsed(control::applied_json(kMine, 9300, maintenance_off, off, 1000)), payload_of("maintenance off applied")));
  control::Diagnostics d;
  d.firmware = "0.7.7"; d.reset_reason = "software"; d.uptime_s = 86400; d.heap_free = 142000; d.heap_min = 98000; d.psram_free = 7000000; d.stack_min_free = 1800;
  d.has_rssi = true; d.rssi = -61; d.mqtt_connected = true;
  control::Command diag = command(Operation::kDiagnostics, control::Action::kExecute);
  diag.command_id = "d0d0d0d01234567b";
  CHECK(same_json(parsed(control::diagnostics_json(kMine, 9400, diag, d, control::Machine())), payload_of("the diagnostics snapshot")));
  control::Diagnostics bare = d;
  bare.psram_free = -1; bare.stack_min_free = -1; bare.has_rssi = false;
  CHECK(same_json(parsed(control::diagnostics_json(kMine, 9400, diag, bare, control::Machine())), payload_of("a diagnostics snapshot of the minimum")));
  // every refusal the node can give is on the list of the schema
  json::Value schema;
  std::string schema_text;
  const bool have_schema = read_file(dir + "/../src/armor_common/schemas/node_control_result.schema.json", schema_text) && json::parse(schema_text, schema);
  if (have_schema) {
    const json::Value* list = schema.get("properties") ? schema.get("properties")->get("refusal") : nullptr;
    const json::Value* names = list != nullptr ? list->get("enum") : nullptr;
    CHECK(names != nullptr);
    for (const Refusal r : {Refusal::kNone, Refusal::kNotArmed, Refusal::kInvalidToken, Refusal::kTokenExpired, Refusal::kRateLimited, Refusal::kAlreadyRestarting, Refusal::kAlreadyInState,
                            Refusal::kBusyUpdate, Refusal::kBusyUpload, Refusal::kBusySlotCopy, Refusal::kBusyBattery, Refusal::kUnavailable}) {
      bool found = false;
      if (names != nullptr) for (const json::Value& item : names->items) found = found || item.text == control::refusal_name(r);
      CHECK(found);
    }
    CHECK(names != nullptr && names->items.size() == 12);
  }
  // and the operations of the command schema are exactly the ones of the node
  json::Value command_schema;
  std::string command_text;
  if (read_file(dir + "/../src/armor_common/schemas/node_control_command.schema.json", command_text) && json::parse(command_text, command_schema)) {
    const json::Value* props = command_schema.get("properties");
    const json::Value* operation = props != nullptr ? props->get("operation") : nullptr;
    const json::Value* names = operation != nullptr ? operation->get("enum") : nullptr;
    CHECK(names != nullptr && names->items.size() == 8);
    for (const Operation o : kAll) {
      bool found = false;
      if (names != nullptr) for (const json::Value& item : names->items) found = found || item.text == control::operation_name(o);
      CHECK(found);
    }
  }
}

// ---- the order, by hand ---------------------------------------------------------------------------------------------------------------------

static void test_the_order_is_read_strictly() {
  control::Command out;
  std::string why;
  CHECK(control::parse_command(order("arm", "restart"), out, &why) && out.node_id == "radar-1" && out.action == control::Action::kArm && out.operation == Operation::kRestart &&
        out.mode == control::Mode::kNow && out.slot == -1 && out.duration_s == 0);
  CHECK(control::parse_command(order("execute", "restart", ",\"slot\":3,\"delay_s\":10" + kTokenField, "rolling"), out) && out.action == control::Action::kExecute &&
        out.mode == control::Mode::kRolling && out.slot == 3 && out.delay_s == 10 && out.token == kToken);
  CHECK(!control::parse_command("", out) && !control::parse_command("[]", out) && !control::parse_command("{", out) && !control::parse_command(order("arm", "restart") + " x", out));
  // every operation can be armed and executed
  for (const Operation o : kAll) {
    const char* name = control::operation_name(o);
    CHECK(control::parse_command(order("arm", name), out) && out.operation == o);
    CHECK(control::parse_command(order("execute", name, kTokenField), out) && out.operation == o);
    CHECK(control::parse_command(order("arm", name, "", "now", "all"), out) && out.node_id == control::kBroadcast);   // a broadcast arms
    CHECK(!control::parse_command(order("execute", name, kTokenField, "now", "all"), out));                           // and never executes
  }
  // an order that tries for more than the contract has
  CHECK(!control::parse_command(order("factory_reset", "restart"), out));
  CHECK(!control::parse_command(order("arm", "factory_reset"), out, &why) && why == "operation");
  CHECK(!control::parse_command(order("arm", "erase"), out) && !control::parse_command(order("arm", "wipe_wifi"), out) && !control::parse_command(order("arm", "deep_sleep"), out));
  CHECK(!control::parse_command(order("arm", "radio_sleep"), out) && !control::parse_command(order("arm", "reset"), out) && !control::parse_command(order("arm", "RESTART"), out));
  CHECK(!control::parse_command(order("arm", "restart", ",\"erase\":true"), out, &why) && contains(why, "contract"));
  for (const char* field : {"wake_after_s", "wake_timer_s", "sleep_s", "factory_reset", "force"})
    CHECK(!control::parse_command(order("execute", "standby", kTokenField + ",\"" + field + "\":600"), out));
  // a repeated name
  CHECK(!control::parse_command(R"({"kind":"node_control_command","node_id":"radar-1","node_id":"radar-2","timestamp_ms":9000,"command_id":"d0d0d0d012345678","action":"arm","operation":"restart","mode":"now","requested_by":"ana"})", out));
  // a token on an arm, none on an execute
  CHECK(!control::parse_command(order("arm", "restart", kTokenField), out));
  CHECK(!control::parse_command(order("execute", "restart"), out));
  CHECK(!control::parse_command(order("execute", "restart", ",\"token\":\"abc\""), out));
  // the kind of the first draft, another kind
  CHECK(!control::parse_command(R"({"kind":"node_restart_command","node_id":"radar-1","timestamp_ms":9000,"command_id":"d0d0d0d012345678","action":"arm","operation":"restart","mode":"now","requested_by":"ana"})", out));
  CHECK(!control::parse_command(R"({"kind":"node_control_command","node_id":"radar-1","timestamp_ms":-1,"command_id":"d0d0d0d012345678","action":"arm","operation":"restart","mode":"now","requested_by":"ana"})", out));
  CHECK(!control::parse_command(R"({"kind":"node_control_command","node_id":"radar-1","timestamp_ms":9000,"command_id":"abc","action":"arm","operation":"restart","mode":"now","requested_by":"ana"})", out));
  CHECK(!control::parse_command(R"({"kind":"node_control_command","node_id":"radar-1","timestamp_ms":9000,"command_id":"d0d0d0d012345678","action":"arm","operation":"restart","mode":"now","requested_by":"ana\n"})", out));
}

static void test_rolling_delay_slot_and_duration_rules() {
  control::Command out;
  // the rolling mode is for a restart
  for (const Operation o : kAll) {
    if (o == Operation::kRestart) continue;
    CHECK(!control::parse_command(order("arm", control::operation_name(o), "", "rolling"), out));
    CHECK(!control::parse_command(order("execute", control::operation_name(o), kTokenField, "rolling"), out));
    // a delay and a place belong to a restart
    CHECK(!control::parse_command(order("execute", control::operation_name(o), kTokenField + ",\"delay_s\":5"), out));
    CHECK(!control::parse_command(order("execute", control::operation_name(o), kTokenField + ",\"slot\":1"), out));
  }
  CHECK(!control::parse_command(order("arm", "restart", ",\"delay_s\":5"), out));                                             // a delay on an arm
  CHECK(!control::parse_command(order("arm", "restart", ",\"slot\":1", "rolling"), out));                                     // a place on an arm
  CHECK(!control::parse_command(order("execute", "restart", kTokenField + ",\"slot\":1"), out));                              // a place in the mode now
  CHECK(!control::parse_command(order("execute", "restart", kTokenField + ",\"slot\":64", "rolling"), out));
  CHECK(control::parse_command(order("execute", "restart", kTokenField + ",\"slot\":63", "rolling"), out) && out.slot == 63);
  CHECK(!control::parse_command(order("execute", "restart", kTokenField + ",\"delay_s\":601"), out));
  CHECK(control::parse_command(order("execute", "restart", kTokenField + ",\"delay_s\":600"), out) && out.delay_s == 600);
  CHECK(control::parse_command(order("execute", "restart", kTokenField + ",\"delay_s\":0"), out) && out.delay_s == 0);
  CHECK(!control::parse_command(order("execute", "restart", kTokenField + ",\"delay_s\":1.5"), out));
  CHECK(!control::parse_command(order("execute", "restart", kTokenField + ",\"delay_s\":-1"), out));
  // the durations: the range of each operation, on an execute only
  struct Range { Operation operation; std::uint32_t low, high; };
  for (const Range& r : {Range{Operation::kPause, 60, 3600}, Range{Operation::kIdentify, 5, 300}, Range{Operation::kMaintenanceOn, 60, 28800}}) {
    const char* name = control::operation_name(r.operation);
    auto with = [&](std::uint32_t seconds) { return order("execute", name, kTokenField + ",\"duration_s\":" + std::to_string(seconds)); };
    CHECK(control::parse_command(with(r.low), out) && out.duration_s == r.low);
    CHECK(control::parse_command(with(r.high), out) && out.duration_s == r.high);
    CHECK(control::parse_command(with((r.low + r.high) / 2), out));
    CHECK(!control::parse_command(with(r.low - 1), out));
    CHECK(!control::parse_command(with(r.high + 1), out));
    CHECK(!control::parse_command(with(0), out));
    CHECK(!control::parse_command(order("arm", name, ",\"duration_s\":" + std::to_string(r.low)), out));   // a duration on an arm
    CHECK(control::parse_command(order("execute", name, kTokenField), out) && out.duration_s == 0);        // none: the default
  }
  for (const Operation o : {Operation::kRestart, Operation::kResume, Operation::kStandby, Operation::kDiagnostics, Operation::kMaintenanceOff})
    CHECK(!control::parse_command(order("execute", control::operation_name(o), kTokenField + ",\"duration_s\":60"), out));   // standby has no end; the rest take none
  // what applies when the order names none
  control::Command pause = command(Operation::kPause, control::Action::kExecute);
  CHECK(control::duration_of(pause) == 3600);
  pause.duration_s = 120;
  CHECK(control::duration_of(pause) == 120);
  CHECK(control::duration_of(command(Operation::kIdentify, control::Action::kExecute)) == 30);
  CHECK(control::duration_of(command(Operation::kMaintenanceOn, control::Action::kExecute)) == 3600);
}

static void test_there_is_no_way_to_say_factory_reset() {
  // the node knows eight operations, and none of their names is an erase, a reset of the settings or a sleep
  CHECK(sizeof(kAll) / sizeof(kAll[0]) == 8);
  for (const Operation o : kAll) {
    const std::string name = control::operation_name(o);
    CHECK(name.find("reset") == std::string::npos && name.find("erase") == std::string::npos && name.find("wipe") == std::string::npos);
    CHECK(name.find("sleep") == std::string::npos && name.find("wake") == std::string::npos && name.find("format") == std::string::npos);
    Operation back;
    CHECK(control::operation_from_name(name, back) && back == o);
  }
  Operation any;
  for (const char* name : {"factory_reset", "reset", "erase", "wipe", "format", "deep_sleep", "light_sleep", "radio_sleep", "sleep", "wake", "wake_timer", "", "Restart", "restart "})
    CHECK(!control::operation_from_name(name, any));
  // and none of the refusals or the states is a sleep
  for (const Refusal r : {Refusal::kNone, Refusal::kNotArmed, Refusal::kInvalidToken, Refusal::kTokenExpired, Refusal::kRateLimited, Refusal::kAlreadyRestarting, Refusal::kAlreadyInState,
                          Refusal::kBusyUpdate, Refusal::kBusyUpload, Refusal::kBusySlotCopy, Refusal::kBusyBattery, Refusal::kUnavailable})
    CHECK(std::string(control::refusal_name(r)).find("sleep") == std::string::npos);
  CHECK(std::string(control::state_name(State::kRunning)) == "running" && std::string(control::state_name(State::kPaused)) == "paused" && std::string(control::state_name(State::kStandby)) == "standby");
}

// ---- the token ------------------------------------------------------------------------------------------------------------------------------

static void test_a_token_is_used_once() {
  control::Gate gate;
  const control::Gate::Armed armed = gate.arm(10000, kUp, Operation::kRestart, Busy::kNone, kRunning, kToken);
  CHECK(armed.refusal == Refusal::kNone && armed.token == kToken && gate.armed());
  CHECK(gate.execute(12000, kUp, Operation::kRestart, Busy::kNone, kRunning, kToken) == Refusal::kNone);
  CHECK(gate.scheduled() && !gate.armed());
  // the same order again (a broker that held it, a copy): the node is already going
  CHECK(gate.execute(12500, kUp, Operation::kRestart, Busy::kNone, kRunning, kToken) == Refusal::kAlreadyRestarting);
  CHECK(gate.arm(20000, kUp, Operation::kRestart, Busy::kNone, kRunning, "zzzzzzzzzzzzzzzzzzzzzzzz").refusal == Refusal::kAlreadyRestarting);
  // the other operations find a node that is going down: unavailable
  CHECK(gate.arm(20000, kUp, Operation::kPause, Busy::kNone, kRunning, kToken).refusal == Refusal::kUnavailable);
  CHECK(gate.execute(20000, kUp, Operation::kResume, Busy::kNone, kRunning, kToken) == Refusal::kUnavailable);
  control::Gate fresh;   // what a node that did not take the order into account sees: no arm, no action
  for (const Operation o : kAll) CHECK(fresh.execute(12000, kUp, o, Busy::kNone, kRunning, kToken) == Refusal::kNotArmed);
}

static void test_a_token_is_used_once_for_every_operation() {
  for (const Operation o : {Operation::kPause, Operation::kStandby, Operation::kIdentify, Operation::kDiagnostics, Operation::kMaintenanceOn}) {
    control::Gate gate;
    CHECK(gate.arm(1000, 5, o, Busy::kNone, kRunning, kToken).refusal == Refusal::kNone);   // only a restart cares how long the node has been up
    CHECK(gate.execute(2000, 5, o, Busy::kNone, kRunning, kToken) == Refusal::kNone);
    CHECK(!gate.armed() && !gate.scheduled());
    CHECK(gate.execute(2001, 5, o, Busy::kNone, kRunning, kToken) == Refusal::kNotArmed);   // a replay finds nothing
  }
  for (const Operation o : {Operation::kResume, Operation::kMaintenanceOff}) {
    control::Gate gate;
    const control::Situation away{State::kStandby, true};
    CHECK(gate.arm(1000, kUp, o, Busy::kNone, away, kToken).refusal == Refusal::kNone);
    CHECK(gate.execute(2000, kUp, o, Busy::kNone, away, kToken) == Refusal::kNone);
    CHECK(gate.execute(2001, kUp, o, Busy::kNone, away, kToken) == Refusal::kNotArmed);
  }
}

static void test_a_token_lives_thirty_seconds() {
  control::Gate gate;
  gate.arm(1000, kUp, Operation::kPause, Busy::kNone, kRunning, kToken);
  CHECK(gate.execute(1000 + 29999, kUp, Operation::kPause, Busy::kNone, kRunning, kToken) == Refusal::kNone);
  control::Gate late;
  late.arm(1000, kUp, Operation::kPause, Busy::kNone, kRunning, kToken);
  CHECK(late.execute(1000 + 30000, kUp, Operation::kPause, Busy::kNone, kRunning, kToken) == Refusal::kTokenExpired);
  CHECK(late.execute(1000 + 30001, kUp, Operation::kPause, Busy::kNone, kRunning, kToken) == Refusal::kNotArmed);   // the old token is gone
}

static void test_a_wrong_token_disarms() {
  control::Gate gate;
  gate.arm(1000, kUp, Operation::kStandby, Busy::kNone, kRunning, kToken);
  CHECK(gate.execute(2000, kUp, Operation::kStandby, Busy::kNone, kRunning, "ffffffffffffffffffffffffffffffff") == Refusal::kInvalidToken);
  CHECK(!gate.armed());
  CHECK(gate.execute(2500, kUp, Operation::kStandby, Busy::kNone, kRunning, kToken) == Refusal::kNotArmed);   // the right one comes too late: it was spent by the wrong one
  CHECK(!control::same_text("abc", "abcd") && !control::same_text("abcd", "abce") && control::same_text("abcd", "abcd") && control::same_text("", ""));
}

static void test_a_token_belongs_to_its_operation() {
  // armed for a pause, presented for a restart (or for a standby, or for anything else): refused, and the token is spent
  for (const Operation armed_for : kAll) {
    for (const Operation presented_for : kAll) {
      if (armed_for == presented_for) continue;
      control::Gate gate;
      const control::Situation any{State::kRunning, true};   // maintenance on, so that both maintenance operations can be armed or presented
      const control::Situation fits = (armed_for == Operation::kResume) ? control::Situation{State::kPaused, false} : any;
      if (gate.arm(1000, kUp, armed_for, Busy::kNone, fits, kToken).refusal != Refusal::kNone) continue;   // an arm the node would not give is not the case
      CHECK(gate.execute(2000, kUp, presented_for, Busy::kNone, fits, kToken) == Refusal::kInvalidToken);
      CHECK(!gate.armed() && !gate.scheduled());
      CHECK(gate.execute(2100, kUp, armed_for, Busy::kNone, fits, kToken) == Refusal::kNotArmed);   // the right operation with the right token, a moment later: the token is gone
    }
  }
  // and the case that matters most: a pause token cannot restart the node
  control::Gate gate;
  gate.arm(1000, kUp, Operation::kPause, Busy::kNone, kRunning, kToken);
  CHECK(gate.execute(1500, kUp, Operation::kRestart, Busy::kNone, kRunning, kToken) == Refusal::kInvalidToken && !gate.scheduled());
}

static void test_a_new_arm_replaces_the_old_token() {
  control::Gate gate;
  gate.arm(1000, kUp, Operation::kPause, Busy::kNone, kRunning, "aaaaaaaaaaaaaaaaaaaaaaaa");
  gate.arm(5000, kUp, Operation::kPause, Busy::kNone, kRunning, kOther);
  CHECK(gate.execute(6000, kUp, Operation::kPause, Busy::kNone, kRunning, "aaaaaaaaaaaaaaaaaaaaaaaa") == Refusal::kInvalidToken);
  control::Gate again;
  again.arm(1000, kUp, Operation::kPause, Busy::kNone, kRunning, "aaaaaaaaaaaaaaaaaaaaaaaa");
  again.arm(5000, kUp, Operation::kPause, Busy::kNone, kRunning, kOther);
  CHECK(again.execute(6000, kUp, Operation::kPause, Busy::kNone, kRunning, kOther) == Refusal::kNone);
  // a new arm for another operation replaces it too: one token at a time per node
  control::Gate other;
  other.arm(1000, kUp, Operation::kPause, Busy::kNone, kRunning, "aaaaaaaaaaaaaaaaaaaaaaaa");
  other.arm(2000, kUp, Operation::kStandby, Busy::kNone, kRunning, kOther);
  CHECK(other.execute(3000, kUp, Operation::kPause, Busy::kNone, kRunning, "aaaaaaaaaaaaaaaaaaaaaaaa") == Refusal::kInvalidToken);
}

static void test_a_token_from_random_bytes() {
  std::uint8_t bytes[24];
  for (int i = 0; i < 24; ++i) bytes[i] = static_cast<std::uint8_t>(i * 11 + 7);
  const std::string token = control::token_from_bytes(bytes, sizeof bytes);
  CHECK(token.size() == 24);
  bool clean = true;
  for (const char c : token) clean = clean && ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'));
  CHECK(clean);
  CHECK(token == control::token_from_bytes(bytes, sizeof bytes));
  bytes[5] ^= 0x55;
  CHECK(token != control::token_from_bytes(bytes, sizeof bytes));
  CHECK(control::token_from_bytes(bytes, 100).size() == 24);   // never longer than the schema allows
}

// ---- when the node says no ------------------------------------------------------------------------------------------------------------------

static void test_the_rate_limit_is_for_a_restart() {
  control::Gate gate;
  CHECK(gate.arm(1000, 59, Operation::kRestart, Busy::kNone, kRunning, kToken).refusal == Refusal::kRateLimited);   // a node younger than a minute
  CHECK(!gate.armed());
  CHECK(gate.arm(1000, 60, Operation::kRestart, Busy::kNone, kRunning, kToken).refusal == Refusal::kNone);
  // an arm made while the node was old enough, an execute when it is not (the clock of the test goes back): still refused
  CHECK(gate.execute(2000, 10, Operation::kRestart, Busy::kNone, kRunning, kToken) == Refusal::kRateLimited);
  CHECK(!gate.scheduled());
  // arms of a restart in quick succession are ignored, so a flood of them cannot be used to wear a token out
  control::Gate flood;
  CHECK(flood.arm(1000, kUp, Operation::kRestart, Busy::kNone, kRunning, kToken).refusal == Refusal::kNone);
  CHECK(flood.arm(1500, kUp, Operation::kRestart, Busy::kNone, kRunning, kOther).refusal == Refusal::kRateLimited);
  CHECK(flood.arm(1999, kUp, Operation::kRestart, Busy::kNone, kRunning, kOther).refusal == Refusal::kRateLimited);
  CHECK(flood.execute(1600, kUp, Operation::kRestart, Busy::kNone, kRunning, kToken) == Refusal::kNone);   // the first token is still the good one
  // a node that has just started still pauses, resumes, goes to standby, identifies and answers diagnostics
  for (const Operation o : {Operation::kPause, Operation::kStandby, Operation::kIdentify, Operation::kDiagnostics, Operation::kMaintenanceOn}) {
    control::Gate young;
    CHECK(young.arm(1000, 5, o, Busy::kNone, kRunning, kToken).refusal == Refusal::kNone);
  }
  // arms of the other operations may follow each other at once
  control::Gate quick;
  CHECK(quick.arm(1000, kUp, Operation::kIdentify, Busy::kNone, kRunning, kToken).refusal == Refusal::kNone);
  CHECK(quick.arm(1100, kUp, Operation::kIdentify, Busy::kNone, kRunning, kOther).refusal == Refusal::kNone);
}

static void test_a_busy_node_says_what_it_is_doing() {
  const struct { Busy busy; Refusal refusal; const char* name; } cases[] = {
      {Busy::kUpdate, Refusal::kBusyUpdate, "busy_update"}, {Busy::kUpload, Refusal::kBusyUpload, "busy_upload"},
      {Busy::kSlotCopy, Refusal::kBusySlotCopy, "busy_slot_copy"}, {Busy::kBattery, Refusal::kBusyBattery, "busy_battery"}};
  for (const auto& c : cases) {
    CHECK(std::string(control::refusal_name(c.refusal)) == c.name);
    for (const Operation o : kAll) {
      const bool refused = o == Operation::kRestart || o == Operation::kPause || o == Operation::kStandby;
      CHECK(control::refused_while_busy(o) == refused);
      const control::Situation fits = (o == Operation::kResume) ? control::Situation{State::kPaused, false} : control::Situation{State::kRunning, o == Operation::kMaintenanceOff};
      control::Gate gate;
      const control::Gate::Armed armed = gate.arm(1000, kUp, o, c.busy, fits, kToken);
      if (refused) CHECK(armed.refusal == c.refusal && armed.token.empty() && !gate.armed());
      else CHECK(armed.refusal == Refusal::kNone && gate.armed());   // resume, identify, diagnostics and maintenance are always allowed
      // armed while idle, busy by the time of the execute: refused, and the token is spent
      control::Gate later;
      later.arm(1000, kUp, o, Busy::kNone, fits, kToken);
      const Refusal at_execute = later.execute(3000, kUp, o, c.busy, fits, kToken);
      if (refused) CHECK(at_execute == c.refusal && !later.scheduled() && later.execute(3500, kUp, o, Busy::kNone, fits, kToken) == Refusal::kNotArmed);
      else CHECK(at_execute == Refusal::kNone);
    }
  }
  // a refusal of a busy node takes an earlier token away: nothing armed before the node began to work can be used in the middle of it
  control::Gate gate;
  gate.arm(1000, kUp, Operation::kPause, Busy::kNone, kRunning, kToken);
  gate.arm(3000, kUp, Operation::kStandby, Busy::kUpdate, kRunning, kOther);
  CHECK(!gate.armed());
  // while busy, an identify still arms (and does not disturb)
  control::Gate id;
  CHECK(id.arm(1000, kUp, Operation::kIdentify, Busy::kUpdate, kRunning, kToken).refusal == Refusal::kNone);
}

static void test_already_in_state() {
  const control::Situation running{State::kRunning, false}, paused{State::kPaused, false}, standby{State::kStandby, false};
  const control::Situation maintained{State::kRunning, true};
  CHECK(!control::already_in_state(Operation::kPause, running) && control::already_in_state(Operation::kPause, paused) && control::already_in_state(Operation::kPause, standby));
  CHECK(!control::already_in_state(Operation::kStandby, running) && !control::already_in_state(Operation::kStandby, paused) && control::already_in_state(Operation::kStandby, standby));
  CHECK(control::already_in_state(Operation::kResume, running) && !control::already_in_state(Operation::kResume, paused) && !control::already_in_state(Operation::kResume, standby));
  CHECK(!control::already_in_state(Operation::kMaintenanceOn, running) && control::already_in_state(Operation::kMaintenanceOn, maintained));
  CHECK(control::already_in_state(Operation::kMaintenanceOff, running) && !control::already_in_state(Operation::kMaintenanceOff, maintained));
  for (const Operation o : {Operation::kRestart, Operation::kIdentify, Operation::kDiagnostics})
    CHECK(!control::already_in_state(o, running) && !control::already_in_state(o, paused) && !control::already_in_state(o, standby) && !control::already_in_state(o, maintained));
  // the gate says so at the arm, and at the execute when the state changed in between
  control::Gate gate;
  CHECK(gate.arm(1000, kUp, Operation::kPause, Busy::kNone, paused, kToken).refusal == Refusal::kAlreadyInState && !gate.armed());
  CHECK(gate.arm(1000, kUp, Operation::kResume, Busy::kNone, running, kToken).refusal == Refusal::kAlreadyInState);
  CHECK(gate.arm(1000, kUp, Operation::kStandby, Busy::kNone, standby, kToken).refusal == Refusal::kAlreadyInState);
  CHECK(gate.arm(1000, kUp, Operation::kMaintenanceOn, Busy::kNone, maintained, kToken).refusal == Refusal::kAlreadyInState);
  CHECK(gate.arm(1000, kUp, Operation::kMaintenanceOff, Busy::kNone, running, kToken).refusal == Refusal::kAlreadyInState);
  control::Gate changed;
  changed.arm(1000, kUp, Operation::kPause, Busy::kNone, running, kToken);
  CHECK(changed.execute(2000, kUp, Operation::kPause, Busy::kNone, standby, kToken) == Refusal::kAlreadyInState);   // went to standby while the token was waiting
  CHECK(!changed.armed());
  // a standby can be asked from a pause (the pause is replaced), a resume from either
  CHECK(gate.arm(1000, kUp, Operation::kStandby, Busy::kNone, paused, kToken).refusal == Refusal::kNone);
  CHECK(gate.arm(1000, kUp, Operation::kResume, Busy::kNone, paused, kToken).refusal == Refusal::kNone);
  CHECK(gate.arm(1000, kUp, Operation::kResume, Busy::kNone, standby, kToken).refusal == Refusal::kNone);
}

// ---- the state machine ----------------------------------------------------------------------------------------------------------------------

static void test_a_pause_ends_by_itself() {
  control::Machine machine;
  CHECK(machine.state() == State::kRunning && machine.working() && !machine.maintenance() && !machine.identifying());
  control::Change change = machine.pause(10000, 3600, 1760000000000ULL, "ana");   // the default length: an hour
  CHECK(change.state_changed && !change.save_standby && machine.state() == State::kPaused && !machine.working());
  CHECK(machine.since_ms() == 1760000000000ULL && machine.set_by() == "ana");
  CHECK(machine.pause_left_s(10000) == 3600 && machine.pause_left_s(10000 + 1) == 3600 && machine.pause_left_s(10000 + 1000) == 3599 && machine.pause_left_s(10000 + 3599999) == 1);
  CHECK(!machine.tick(10000 + 3599999).state_changed && machine.state() == State::kPaused);
  change = machine.tick(10000 + 3600000);
  CHECK(change.state_changed && machine.state() == State::kRunning && machine.working() && machine.since_ms() == 0 && machine.set_by().empty() && machine.pause_left_s(10000 + 3600000) == -1);
  CHECK(!machine.tick(10000 + 3600001).state_changed);   // nothing more to end
  // an explicit length
  control::Machine minute;
  minute.pause(0, 60, 0, "ana");
  CHECK(minute.pause_left_s(0) == 60 && !minute.tick(59999).state_changed && minute.tick(60000).state_changed && minute.working());
  // a pause that is asked again while paused starts the window again and is not a change of state
  control::Machine again;
  again.pause(0, 60, 5, "ana");
  CHECK(!again.pause(30000, 120, 6, "ben").state_changed && again.pause_left_s(30000) == 120 && again.set_by() == "ben");
  // a standby is never turned into a pause
  control::Machine standby;
  standby.standby(7, "ana");
  CHECK(!standby.pause(0, 60, 8, "ben").state_changed && standby.state() == State::kStandby && standby.set_by() == "ana");
  // a resume ends a pause at once
  control::Machine resumed;
  resumed.pause(0, 3600, 5, "ana");
  change = resumed.resume();
  CHECK(change.state_changed && !change.clear_standby && resumed.working() && resumed.pause_left_s(1) == -1);
  CHECK(!resumed.resume().state_changed);   // already running
}

static void test_a_pause_is_cleared_by_a_restart() {
  // a restart is a new machine: the node boots running, whatever it was doing, and the maintenance flag is gone with it
  control::Controller before;
  before.machine().pause(0, 3600, 5, "ana");
  before.machine().maintenance_on(0, 3600);
  CHECK(before.machine().state() == State::kPaused && before.machine().maintenance());
  control::Controller after;
  CHECK(after.machine().state() == State::kRunning && after.machine().working() && !after.machine().maintenance());
}

static void test_a_standby_has_no_end_and_survives_a_restart() {
  control::Machine machine;
  const control::Change change = machine.standby(1760000000000ULL, "ana");
  CHECK(change.state_changed && change.save_standby && !change.clear_standby && machine.state() == State::kStandby && !machine.working());
  CHECK(machine.pause_left_s(0) == -1);   // no end
  CHECK(!machine.tick(1ULL << 40).state_changed && machine.state() == State::kStandby);   // a very long time later, still in standby
  // what is kept in flash, read back by the node that starts again
  const std::string kept = control::standby_record(machine.since_ms(), machine.set_by());
  std::uint64_t since = 0;
  std::string by;
  CHECK(control::parse_standby_record(kept, since, by) && since == 1760000000000ULL && by == "ana");
  control::Machine rebooted;   // the new run of the node
  CHECK(rebooted.state() == State::kRunning);
  const control::Change restored = rebooted.restore_standby(since, by);
  CHECK(restored.state_changed && rebooted.state() == State::kStandby && !rebooted.working() && rebooted.since_ms() == 1760000000000ULL && rebooted.set_by() == "ana");
  CHECK(!restored.save_standby);   // it is already kept
  // a restart again: still in standby (restored again from the same record)
  control::Machine third;
  CHECK(third.restore_standby(since, by).state_changed && third.state() == State::kStandby);
  // cleared by a resume: the record says there is no standby, and the next boot is running
  const control::Change resumed = rebooted.resume();
  CHECK(resumed.state_changed && resumed.clear_standby && rebooted.working() && rebooted.since_ms() == 0 && rebooted.set_by().empty());
  CHECK(!control::parse_standby_record(control::standby_cleared_record(), since, by));
  // a record that does not read whole is not a standby
  for (const char* bad : {"", "{", "[]", "{}", "{\"standby\":false,\"since_ms\":5,\"set_by\":\"ana\"}", "{\"standby\":true}", "{\"standby\":true,\"since_ms\":-1,\"set_by\":\"ana\"}",
                          "{\"standby\":true,\"since_ms\":5}", "{\"standby\":true,\"since_ms\":5,\"set_by\":\"a\\nb\"}", "{\"standby\":true,\"since_ms\":\"5\",\"set_by\":\"ana\"}"})
    CHECK(!control::parse_standby_record(bad, since, by));
  CHECK(control::parse_standby_record("{\"standby\":true,\"since_ms\":0,\"set_by\":\"\"}", since, by) && since == 0 && by.empty());   // the clock was not set and nobody is named
  // a standby asked while paused replaces the pause, and keeps no end
  control::Machine paused;
  paused.pause(0, 60, 5, "ana");
  CHECK(paused.standby(9, "ben").state_changed && paused.state() == State::kStandby && paused.pause_left_s(0) == -1 && !paused.tick(1000000).state_changed && paused.set_by() == "ben");
  // a standby whose person is not valid is restored without a name
  control::Machine odd;
  odd.restore_standby(5, "bad\nname");
  CHECK(odd.state() == State::kStandby && odd.set_by().empty());
}

static void test_maintenance_and_identify_windows() {
  control::Machine machine;
  machine.maintenance_on(1000, 3600);
  CHECK(machine.maintenance() && machine.maintenance_left_s(1000) == 3600 && machine.maintenance_left_s(1000 + 1000) == 3599 && machine.working());   // the flag does not stop the work
  CHECK(!machine.tick(1000 + 3599999).state_changed && machine.maintenance());
  machine.tick(1000 + 3600000);
  CHECK(!machine.maintenance() && machine.maintenance_left_s(1000 + 3600000) == -1);
  machine.maintenance_on(0, 60);
  machine.maintenance_off();
  CHECK(!machine.maintenance());
  // the maintenance flag goes with a pause or a standby, and is independent of them
  machine.maintenance_on(0, 600);
  machine.pause(0, 60, 5, "ana");
  machine.tick(60000);
  CHECK(machine.working() && machine.maintenance());
  machine.standby(5, "ana");
  CHECK(machine.maintenance() && machine.resume().clear_standby && machine.maintenance());
  // identify: a window of its own
  control::Machine id;
  const control::Change started = id.identify(5000, 30);
  CHECK(started.identify_changed && !started.state_changed && id.identifying() && id.identify_left_s(5000) == 30 && id.working());
  CHECK(!id.tick(5000 + 29999).identify_changed && id.identifying());
  const control::Change ended = id.tick(5000 + 30000);
  CHECK(ended.identify_changed && !ended.state_changed && !id.identifying() && id.identify_left_s(5000 + 30000) == -1);
  // identify in a standby does not wake the node
  control::Machine sleepy;
  sleepy.standby(5, "ana");
  sleepy.identify(0, 10);
  CHECK(sleepy.identifying() && sleepy.state() == State::kStandby);
}

// ---- the wait of a rolling restart ----------------------------------------------------------------------------------------------------------

static void test_the_wait_of_a_restart() {
  CHECK(control::restart_in_s(command(Operation::kRestart, control::Action::kExecute, control::Mode::kNow)) == 3);
  CHECK(control::restart_in_s(command(Operation::kRestart, control::Action::kExecute, control::Mode::kNow, -1, 10)) == 13);
  CHECK(control::restart_in_s(command(Operation::kRestart, control::Action::kExecute, control::Mode::kRolling, 0)) == 3);
  CHECK(control::restart_in_s(command(Operation::kRestart, control::Action::kExecute, control::Mode::kRolling, 1)) == 33);
  CHECK(control::restart_in_s(command(Operation::kRestart, control::Action::kExecute, control::Mode::kRolling, 2, 5)) == 68);
  CHECK(control::restart_in_s(command(Operation::kRestart, control::Action::kExecute, control::Mode::kRolling, 63, 600)) == 3 + 63 * 30 + 600);
  CHECK(control::restart_in_s(command(Operation::kRestart, control::Action::kExecute, control::Mode::kRolling, 63, 600)) <= 3600);
  // the places the server gives: nodes of one kind are never closer than the half minute, in any order the places are taken
  for (int a = 0; a <= control::kMaxSlot; a += 7) {
    for (int b = 0; b <= control::kMaxSlot; ++b) {
      if (a == b) continue;
      const std::int64_t gap = static_cast<std::int64_t>(control::restart_in_s(command(Operation::kRestart, control::Action::kExecute, control::Mode::kRolling, a))) -
                               static_cast<std::int64_t>(control::restart_in_s(command(Operation::kRestart, control::Action::kExecute, control::Mode::kRolling, b)));
      CHECK(std::llabs(gap) >= 30);
    }
  }
  // a place the node chooses from its name: always the same, inside the sixteen places, and not the same for every name
  control::Command unplaced = command(Operation::kRestart, control::Action::kExecute, control::Mode::kRolling);
  const std::uint32_t first = control::restart_in_s(unplaced);
  CHECK(first == control::restart_in_s(unplaced));
  CHECK(first >= 3 && first <= 3 + 30 * (control::kJitterSlots - 1));
  std::vector<unsigned> seen;
  for (const char* name : {"radar-1", "radar-2", "radar-3", "solar-1", "solar-2", "alarm-1", "hmi-1", "electrical-1", "electrical-2", "north-1", "north-2", "garden", "gate", "garage"}) {
    const unsigned slot = control::jitter_slot(name);
    CHECK(slot < control::kJitterSlots);
    bool known = false;
    for (const unsigned s : seen) known = known || s == slot;
    if (!known) seen.push_back(slot);
  }
  CHECK(seen.size() >= 6);
  // the mode now never waits for a place, even if the order carries none
  CHECK(control::restart_in_s(command(Operation::kRestart, control::Action::kExecute, control::Mode::kNow)) == control::kLeaveSeconds);
}

// ---- the topics -----------------------------------------------------------------------------------------------------------------------------

static void test_the_topics_and_the_nodes_that_can_be_controlled() {
  CHECK(control::command_topic("radar-1") == "armor/control/radar-1/command");
  CHECK(control::result_topic("radar-1") == "armor/control/radar-1/result");
  CHECK(control::broadcast_topic() == "armor/control/all/command");
  CHECK(control::command_topic("Radar").empty() && control::command_topic("").empty() && control::result_topic("a/b").empty());
  CHECK(control::node_can_be_controlled("radar-1") && !control::node_can_be_controlled("all") && !control::node_can_be_controlled("-x"));
}

// ---- the whole of it: an order in, an answer out --------------------------------------------------------------------------------------------

static control::Env env_at(std::uint64_t now_ms, const std::string& token = kToken) {
  control::Env env;
  env.now_ms = now_ms;
  env.wall_ms = 1760000000000ULL + now_ms;
  env.stamp_ms = env.wall_ms;
  env.fresh_token = token;
  env.diagnostics.firmware = "0.7.7";
  env.diagnostics.reset_reason = "power_on";
  env.diagnostics.uptime_s = now_ms / 1000;
  env.diagnostics.heap_free = 142000;
  env.diagnostics.heap_min = 98000;
  env.diagnostics.mqtt_connected = true;
  return env;
}

static json::Value parsed(const std::string& text) {
  json::Value doc;
  CHECK(json::parse(text, doc));
  return doc;
}

// Arms and executes one order on a controller; returns the answer to the execute.
static control::Reply give(control::Controller& node, const char* operation, std::uint64_t now_ms, const std::string& extra = "", bool broadcast = false, Busy busy = Busy::kNone) {
  control::Env env = env_at(now_ms);
  env.busy = busy;
  const control::Reply armed = node.handle(kMine, broadcast, order("arm", operation, "", "now", broadcast ? "all" : "radar-1"), env);
  json::Value answer = parsed(armed.json);
  if (answer.string_or("stage", "") != "armed") return armed;
  env = env_at(now_ms + 500);
  env.busy = busy;
  return node.handle(kMine, false, order("execute", operation, ",\"token\":\"" + answer.string_or("token", "") + "\"" + extra), env);
}

static void test_a_node_follows_the_orders() {
  control::Controller node;
  std::uint64_t t = 120000;   // two minutes after the start
  // an order for another node, a broadcast that executes, a message that is not an order: nothing is said
  CHECK(node.handle(kMine, false, order("arm", "pause", "", "now", "radar-2"), env_at(t)).json.empty());
  CHECK(node.handle(kMine, true, order("arm", "pause", "", "now", "radar-1"), env_at(t)).json.empty());    // on the topic of everyone, but for one node
  CHECK(node.handle(kMine, false, order("arm", "pause", "", "now", "all"), env_at(t)).json.empty());        // for everyone, but on the node's own topic
  CHECK(node.handle(kMine, false, "{\"hello\":1}", env_at(t)).json.empty());
  // a broadcast arms, and the node answers with a token that works on the node's own topic
  const control::Reply broadcast_arm = node.handle(kMine, true, order("arm", "pause", "", "now", "all"), env_at(t));
  json::Value token_answer = parsed(broadcast_arm.json);
  CHECK(token_answer.string_or("stage", "") == "armed" && token_answer.string_or("operation", "") == "pause" && token_answer.string_or("node_id", "") == kMine);
  // pause: the work stops and the answer says so
  control::Reply reply = node.handle(kMine, false, order("execute", "pause", ",\"duration_s\":600" + kTokenField), env_at(t + 1000));
  json::Value answer = parsed(reply.json);
  CHECK(reply.carried_out && reply.change.state_changed && !reply.restart);
  CHECK(answer.string_or("stage", "") == "applied" && answer.string_or("state", "") == "paused" && answer.number_or("until_s", -1) == 600 && !answer.bool_or("maintenance", true));
  CHECK(!node.machine().working());
  // armed with the token the node gave, not the fixed one
  CHECK(node.handle(kMine, false, order("execute", "resume", kTokenField), env_at(t + 2000)).json.find("not_armed") != std::string::npos);
  // a pause while paused: refused at the arm
  reply = node.handle(kMine, false, order("arm", "pause"), env_at(t + 3000));
  CHECK(contains(reply.json, "already_in_state") && contains(reply.why_not, "already_in_state"));
  // the pause ends by itself
  CHECK(!node.tick(t + 1000 + 599999).state_changed && node.tick(t + 1000 + 600000).state_changed && node.machine().working());
  // the default pause is an hour
  t += 1000000;
  reply = give(node, "pause", t);
  CHECK(parsed(reply.json).number_or("until_s", -1) == 3600);
  // resume at once
  reply = give(node, "resume", t + 5000);
  answer = parsed(reply.json);
  CHECK(answer.string_or("state", "") == "running" && reply.change.state_changed && node.machine().working() && answer.get("until_s") == nullptr && answer.get("since_ms") == nullptr);
  // resume when running
  reply = node.handle(kMine, false, order("arm", "resume"), env_at(t + 7000));
  CHECK(contains(reply.json, "already_in_state"));
  // standby: indefinite, with who and since when
  t += 10000;
  reply = give(node, "standby", t);
  answer = parsed(reply.json);
  CHECK(answer.string_or("state", "") == "standby" && answer.get("until_s") == nullptr && answer.number_or("since_ms", 0) > 0 && answer.string_or("requested_by", "") == "ana");
  CHECK(reply.change.save_standby && reply.change.state_changed && !node.machine().working());
  // a pause while in standby is refused, standby again is refused, a resume ends it and says to forget it
  CHECK(contains(node.handle(kMine, false, order("arm", "pause"), env_at(t + 5000)).json, "already_in_state"));
  CHECK(contains(node.handle(kMine, false, order("arm", "standby"), env_at(t + 6000)).json, "already_in_state"));
  reply = give(node, "resume", t + 10000);
  CHECK(reply.change.clear_standby && node.machine().working());
  // maintenance: the work goes on, the flag is reported and ends by itself
  t += 100000;
  reply = give(node, "maintenance_on", t, ",\"duration_s\":120");
  answer = parsed(reply.json);
  CHECK(answer.string_or("state", "") == "running" && answer.bool_or("maintenance", false) && answer.number_or("until_s", -1) == 120 && node.machine().working());
  CHECK(contains(node.handle(kMine, false, order("arm", "maintenance_on"), env_at(t + 5000)).json, "already_in_state"));
  node.tick(t + 500 + 120000);
  CHECK(!node.machine().maintenance());
  reply = give(node, "maintenance_on", t + 200000);
  CHECK(give(node, "maintenance_off", t + 300000).carried_out && !node.machine().maintenance());
  CHECK(contains(node.handle(kMine, false, order("arm", "maintenance_off"), env_at(t + 400000)).json, "already_in_state"));
  // identify: a window, nothing else changes
  reply = give(node, "identify", t + 500000, ",\"duration_s\":10");
  answer = parsed(reply.json);
  CHECK(reply.change.identify_changed && !reply.change.state_changed && answer.number_or("until_s", -1) == 10 && node.machine().identifying() && node.machine().working());
  CHECK(node.tick(t + 500000 + 500 + 10000).identify_changed && !node.machine().identifying());
  // diagnostics: a snapshot, read only
  reply = give(node, "diagnostics", t + 600000);
  answer = parsed(reply.json);
  const json::Value* snapshot = answer.get("diagnostics");
  CHECK(answer.string_or("stage", "") == "diagnostics" && snapshot != nullptr && snapshot->string_or("firmware", "") == "0.7.7" && snapshot->string_or("state", "") == "running" && !reply.change.state_changed);
  // restart: the acknowledgement says it, and the reply asks the node to restart in the given seconds
  reply = give(node, "restart", t + 700000, ",\"delay_s\":10");
  answer = parsed(reply.json);
  CHECK(reply.restart && reply.restart_in_s == 13 && answer.string_or("stage", "") == "restarting" && answer.number_or("restart_in_s", 0) == 13 && answer.string_or("firmware", "") == "0.7.7");
  CHECK(node.gate().scheduled());
  // once it has agreed to go, it ignores the rest
  CHECK(contains(node.handle(kMine, false, order("arm", "restart"), env_at(t + 701000)).json, "already_restarting"));
  CHECK(contains(node.handle(kMine, false, order("arm", "pause"), env_at(t + 702000)).json, "unavailable"));
}

static void test_busy_refusals_reach_the_answer() {
  for (const Operation o : {Operation::kRestart, Operation::kPause, Operation::kStandby}) {
    control::Controller node;
    const control::Reply reply = give(node, control::operation_name(o), 120000, "", false, Busy::kBattery);
    CHECK(contains(reply.json, "busy_battery") && contains(reply.json, "\"stage\":\"rejected\"") && !reply.carried_out && node.machine().working());
  }
  for (const char* name : {"identify", "diagnostics", "maintenance_on"}) {
    control::Controller node;
    CHECK(give(node, name, 120000, "", false, Busy::kUpload).carried_out);
  }
  // resume while busy, from a standby
  control::Controller node;
  CHECK(give(node, "standby", 120000).carried_out);
  CHECK(give(node, "resume", 130000, "", false, Busy::kUpdate).carried_out && node.machine().working());
}

static void test_a_token_cannot_be_used_across_operations_in_the_controller() {
  control::Controller node;
  const json::Value armed = parsed(node.handle(kMine, false, order("arm", "pause"), env_at(120000)).json);
  const control::Reply wrong = node.handle(kMine, false, order("execute", "restart", ",\"token\":\"" + armed.string_or("token", "") + "\""), env_at(121000));
  CHECK(contains(wrong.json, "invalid_token") && !wrong.restart && !node.gate().scheduled() && node.machine().working());
  CHECK(contains(node.handle(kMine, false, order("execute", "pause", ",\"token\":\"" + armed.string_or("token", "") + "\""), env_at(121500)).json, "not_armed"));   // and the token is spent
}

static void test_the_rate_limit_in_the_controller() {
  control::Controller node;
  CHECK(contains(node.handle(kMine, false, order("arm", "restart"), env_at(30000)).json, "rate_limited"));     // started half a minute ago
  CHECK(contains(node.handle(kMine, false, order("arm", "restart"), env_at(61000)).json, "\"stage\":\"armed\""));
  CHECK(contains(node.handle(kMine, false, order("arm", "restart"), env_at(61500)).json, "rate_limited"));    // again within a second
}

// ---- the health report says what the node is doing -------------------------------------------------------------------------------------------

static nodehealth::Report sample_report() {
  nodehealth::Report r;
  r.node_id = "radar-1"; r.family = "radar"; r.firmware = "0.7.7"; r.board = "s3-eth"; r.timestamp_ms = 9000; r.uptime_s = 86400; r.reset_reason = "power_on";
  r.heap_free = 142000; r.heap_min = 98000; r.link = {nodehealth::Link::kEthernet, true, 0}; r.broker_reconnects = 1; r.low_stack_tasks = 0;
  return r;
}

static void test_the_health_report_carries_the_control_state() {
  nodehealth::Report r = sample_report();
  CHECK(!contains(nodehealth::to_json(r), "control_state") && !contains(nodehealth::to_json(r), "maintenance"));   // a firmware that cannot be controlled says nothing
  r.has_control = true;
  std::string text = nodehealth::to_json(r);
  json::Value doc = parsed(text);
  CHECK(doc.string_or("control_state", "") == "running" && doc.get("control_since_ms") == nullptr && doc.get("control_by") == nullptr && doc.get("control_until_s") == nullptr && !doc.bool_or("maintenance", true));
  r.control_state = "paused"; r.control_since_ms = 1760000000000ULL; r.control_by = "ana"; r.control_until_s = 1800;
  doc = parsed(nodehealth::to_json(r));
  CHECK(doc.string_or("control_state", "") == "paused" && doc.number_or("control_since_ms", 0) == 1760000000000.0 && doc.string_or("control_by", "") == "ana" && doc.number_or("control_until_s", -1) == 1800);
  r.control_state = "standby"; r.control_until_s = -1;
  doc = parsed(nodehealth::to_json(r));
  CHECK(doc.string_or("control_state", "") == "standby" && doc.get("control_until_s") == nullptr && doc.string_or("control_by", "") == "ana");
  r.control_until_s = 60;   // a standby has no end: never reported
  CHECK(parsed(nodehealth::to_json(r)).get("control_until_s") == nullptr);
  r.control_state = "running"; r.control_by = "ana"; r.control_since_ms = 5; r.control_until_s = 60;   // since, by and time left go only with the state they describe
  doc = parsed(nodehealth::to_json(r));
  CHECK(doc.get("control_since_ms") == nullptr && doc.get("control_by") == nullptr && doc.get("control_until_s") == nullptr);
  r.maintenance = true; r.maintenance_until_s = 3000;
  doc = parsed(nodehealth::to_json(r));
  CHECK(doc.bool_or("maintenance", false) && doc.number_or("maintenance_until_s", -1) == 3000);
  r.maintenance = false;
  CHECK(parsed(nodehealth::to_json(r)).get("maintenance_until_s") == nullptr);
  r.control_state = "standby"; r.control_by = "";
  CHECK(parsed(nodehealth::to_json(r)).get("control_by") == nullptr);
  // the machine feeds the report
  control::Machine machine;
  machine.standby(1760000000000ULL, "ana");
  nodehealth::Report fed = sample_report();
  fed.has_control = true;
  fed.control_state = control::state_name(machine.state());
  fed.control_since_ms = machine.since_ms();
  fed.control_by = machine.set_by();
  CHECK(parsed(nodehealth::to_json(fed)).string_or("control_state", "") == "standby");
}

// ---- the planned-maintenance signal ---------------------------------------------------------------------------------------------------------

using control::Reason;

static void test_the_maintenance_message_is_the_one_of_the_vectors(const std::string& dir) {
  std::string text;
  json::Value doc;
  CHECK(read_file(dir + "/node_maintenance.json", text) && json::parse(text, doc));
  const json::Value* vectors = doc.get("vectors");
  CHECK(vectors != nullptr && vectors->items.size() >= 15);
  if (vectors == nullptr) return;
  auto payload_of = [&](const std::string& name) {
    json::Value out;
    bool found = false;
    for (const json::Value& vector : vectors->items) {
      if (vector.string_or("name", "") != name) continue;
      const json::Value* p = vector.get("payload");
      if (p != nullptr) { out = *p; found = true; }
    }
    if (!found) std::printf("no vector named: %s\n", name.c_str());
    CHECK(found);
    return out;
  };
  control::Signal restart;
  restart.publish = restart.active = true;
  restart.reason = Reason::kRestart;
  restart.expected_s = 90;
  restart.by = "ana";
  CHECK(same_json(parsed(control::maintenance_json(kMine, 9500, restart, "0.7.7")), payload_of("a restart is about to happen")));
  control::Signal alone = restart;
  alone.reason = Reason::kStandby;
  alone.expected_s = 0;
  CHECK(same_json(parsed(control::maintenance_json(kMine, 9500, alone, "0.7.7")), payload_of("a standby with no expected time")));
  control::Signal over;
  over.publish = true;
  over.active = false;
  over.reason = Reason::kRestart;
  over.by = "ana";
  CHECK(same_json(parsed(control::maintenance_json(kMine, 9500, over, "0.7.7")), payload_of("the window is over")));
  // the message of a window that ends never carries a time, and the time never leaves 1..3600
  over.expected_s = 90;
  CHECK(parsed(control::maintenance_json(kMine, 9500, over, "0.7.7")).get("expected_s") == nullptr);
  restart.expected_s = 99999;
  CHECK(parsed(control::maintenance_json(kMine, 9500, restart, "0.7.7")).number_or("expected_s", 0) == 3600);
  // every reason the node can say is a word of the vectors' list, and the topic is the node's own
  for (int i = 0; i < control::kReasonCount; ++i) {
    Reason back;
    CHECK(control::reason_from_name(control::reason_name(static_cast<Reason>(i)), back) && back == static_cast<Reason>(i));
  }
  Reason none;
  CHECK(!control::reason_from_name("deep_sleep", none) && !control::reason_from_name("", none) && !control::reason_from_name("factory_reset", none));
  CHECK(control::maintenance_topic("radar-1") == "armor/control/radar-1/maintenance" && control::maintenance_topic("Radar").empty());
}

static void test_a_boot_clears_what_the_last_run_left() {
  control::MaintenanceSignal signal;
  CHECK(!signal.active());
  // a normal boot: the first message once the broker is there says "not active" (the broker may still hold the 'active' of the restart)
  control::Signal said = signal.on_connected(false, "", 5000);
  CHECK(said.publish && !said.active && said.expected_s == 0);
  // and again after a reconnection: still the truth
  said = signal.on_connected(false, "", 9000);
  CHECK(said.publish && !said.active);
  // a boot into standby keeps saying standby, with no time
  control::MaintenanceSignal kept;
  said = kept.on_connected(true, "ana", 5000);
  CHECK(said.publish && said.active && said.reason == Reason::kStandby && said.expected_s == 0 && said.by == "ana" && kept.active());
  said = kept.on_connected(false, "", 7000);   // a reconnection: the flag of the boot is not asked again
  CHECK(said.active && said.reason == Reason::kStandby);
  // a tick never ends a standby, however long
  CHECK(!kept.tick(5000ULL * 24 * 3600 * 1000, Busy::kNone).publish && kept.active());
  // the resume clears it
  said = kept.lower(Reason::kStandby, 100000);
  CHECK(said.publish && !said.active && !kept.active());
}

static void test_a_restart_pause_and_standby_announce_themselves() {
  control::Controller node;
  control::MaintenanceSignal signal;
  std::uint64_t t = 120000;
  // a restart: said with the acknowledgement, with the wait of the restart and the time to come back
  control::Reply reply = give(node, "restart", t);
  control::Signal said = control::follow_order(signal, reply, node.machine(), t + 500);
  CHECK(reply.restart && said.publish && said.active && said.reason == Reason::kRestart && said.expected_s == reply.restart_in_s + control::kRestartBootAllowanceS && said.by == "ana");
  // a rolling restart far down the line still fits the 3600 s of the contract
  control::MaintenanceSignal far_signal;
  control::Reply slow;
  slow.carried_out = true;
  slow.operation = Operation::kRestart;
  slow.restart_in_s = 63 * 30 + 600;
  said = control::follow_order(far_signal, slow, node.machine(), t);
  CHECK(said.expected_s == 63 * 30 + 600 + control::kRestartBootAllowanceS && said.expected_s <= 3600);
  // the node comes back: the boot clears (a new signal object is a new boot)
  control::MaintenanceSignal booted;
  CHECK(!booted.on_connected(false, "", 3000).active);

  // a pause: said with its length, ended by the resume, ended by itself
  control::Controller paused;
  control::MaintenanceSignal pause_signal;
  reply = give(paused, "pause", t, ",\"duration_s\":600");
  said = control::follow_order(pause_signal, reply, paused.machine(), t + 500);
  CHECK(said.publish && said.active && said.reason == Reason::kPause && said.expected_s == 600 && said.by == "ana");
  reply = give(paused, "resume", t + 5000);
  said = control::follow_order(pause_signal, reply, paused.machine(), t + 5500);
  CHECK(said.publish && !said.active && !pause_signal.active());
  // a pause that ends by itself ends its window the same second
  reply = give(paused, "pause", t + 10000, ",\"duration_s\":60");
  control::follow_order(pause_signal, reply, paused.machine(), t + 10500);
  CHECK(pause_signal.active());
  const std::uint64_t over = t + 10500 + 60000;
  const control::Change change = paused.tick(over);
  said = control::follow_tick(pause_signal, paused.machine(), change, over, Busy::kNone);
  CHECK(change.state_changed && said.publish && !said.active && !pause_signal.active());

  // a standby: no time, kept until the resume; a pause under it never replaces it
  reply = give(paused, "standby", t + 100000);
  said = control::follow_order(pause_signal, reply, paused.machine(), t + 100500);
  CHECK(said.publish && said.active && said.reason == Reason::kStandby && said.expected_s == 0);
  control::Signal kept = control::follow_tick(pause_signal, paused.machine(), paused.tick(t + 9000000), t + 9000000, Busy::kNone);
  CHECK(!kept.publish && pause_signal.active());
  reply = give(paused, "resume", t + 9100000);
  said = control::follow_order(pause_signal, reply, paused.machine(), t + 9100500);
  CHECK(said.publish && !said.active && !pause_signal.active());

  // an order that was refused or did not change anything says nothing
  control::Reply refused = give(paused, "resume", t + 9200000);
  CHECK(!control::follow_order(pause_signal, refused, paused.machine(), t + 9200500).publish);
  reply = give(paused, "identify", t + 9300000);
  CHECK(!control::follow_order(pause_signal, reply, paused.machine(), t + 9300500).publish);
  reply = give(paused, "diagnostics", t + 9400000);
  CHECK(!control::follow_order(pause_signal, reply, paused.machine(), t + 9400500).publish);
}

static void test_the_operator_window_follows_the_flag() {
  control::Controller node;
  control::MaintenanceSignal signal;
  std::uint64_t t = 120000;
  control::Reply reply = give(node, "maintenance_on", t, ",\"duration_s\":1800");
  control::Signal said = control::follow_order(signal, reply, node.machine(), t + 500);
  CHECK(said.publish && said.active && said.reason == Reason::kOperator && said.expected_s == 1800);
  // a window longer than the contract allows is said as an hour (the server keeps the operator's own length)
  reply = give(node, "maintenance_off", t + 5000);
  said = control::follow_order(signal, reply, node.machine(), t + 5500);
  CHECK(said.publish && !said.active);
  reply = give(node, "maintenance_on", t + 10000, ",\"duration_s\":28800");
  said = control::follow_order(signal, reply, node.machine(), t + 10500);
  CHECK(said.active && said.expected_s == 3600);
  // the flag ends by itself (28800 s): the window ended long before, by its own time
  said = control::follow_tick(signal, node.machine(), node.tick(t + 10500 + 3600000), t + 10500 + 3600000, Busy::kNone);
  CHECK(node.machine().maintenance() && said.publish && !said.active);
  reply = give(node, "maintenance_off", t + 3700000);
  control::follow_order(signal, reply, node.machine(), t + 3700500);
  // and when the flag goes the window goes with it
  reply = give(node, "maintenance_on", t + 4000000, ",\"duration_s\":60");
  control::follow_order(signal, reply, node.machine(), t + 4000500);
  const std::uint64_t ended = t + 4000500 + 60000;
  said = control::follow_tick(signal, node.machine(), node.tick(ended), ended, Busy::kNone);
  CHECK(said.publish && !said.active && !signal.active());
  // an operator window under a pause: the pause is what is said, and when it ends the operator window is what is left
  node = control::Controller();
  signal = control::MaintenanceSignal();
  reply = give(node, "maintenance_on", t, ",\"duration_s\":1800");
  control::follow_order(signal, reply, node.machine(), t + 500);
  reply = give(node, "pause", t + 5000, ",\"duration_s\":600");
  said = control::follow_order(signal, reply, node.machine(), t + 5500);
  CHECK(said.active && said.reason == Reason::kPause);
  reply = give(node, "resume", t + 20000);
  said = control::follow_order(signal, reply, node.machine(), t + 20500);
  CHECK(said.publish && said.active && said.reason == Reason::kOperator && said.expected_s > 0 && said.expected_s <= 1800);
}

static void test_an_update_or_an_upload_announces_and_clears() {
  control::MaintenanceSignal signal;
  std::uint64_t t = 1000000;
  // an update announces before it starts, with the time it expects
  control::Signal said = signal.raise(Reason::kUpdate, 900, t, "");
  CHECK(said.publish && said.active && said.reason == Reason::kUpdate && said.expected_s == 900 && said.by.empty());
  // it ends without a restart (it failed): cleared at once
  said = signal.lower(Reason::kUpdate, t + 30000);
  CHECK(said.publish && !said.active && said.reason == Reason::kUpdate);
  // clearing what is not on says nothing (no stray message)
  CHECK(!signal.lower(Reason::kUpdate, t + 31000).publish && !signal.lower(Reason::kUpload, t + 31000).publish);
  // an upload, and the time asked for is held to the contract's range
  said = signal.raise(Reason::kUpload, 99999, t, "");
  CHECK(said.expected_s == 3600);
  said = signal.raise(Reason::kUpload, 0, t, "");
  CHECK(said.expected_s == 1);
  said = signal.raise(Reason::kUpload, 300, t, "");
  // the time counts down in what the node says
  CHECK(signal.expected_now(t + 100000) == 200 && signal.expected_now(t + 299001) == 1);
  // expiry: nothing happens before the time, a stale window ends after it
  CHECK(!signal.tick(t + 299999, Busy::kNone).publish && signal.active());
  said = signal.tick(t + 300000, Busy::kNone);
  CHECK(said.publish && !said.active && !signal.active());
  // an update that is still going is renewed, not dropped, and ends one span after the work does
  signal.raise(Reason::kUpdate, 600, t, "");
  CHECK(!signal.tick(t + 600000, Busy::kUpdate).publish && signal.active());
  CHECK(!signal.tick(t + 1199999, Busy::kSlotCopy).publish && signal.active());
  said = signal.tick(t + 1200000, Busy::kNone);
  CHECK(said.publish && !said.active);
  // an upload is renewed by an upload only, an update by an update or a slot copy only
  signal.raise(Reason::kUpload, 300, t, "");
  CHECK(signal.tick(t + 300000, Busy::kUpdate).publish);
  signal.raise(Reason::kUpdate, 300, t, "");
  CHECK(signal.tick(t + 300000, Busy::kUpload).publish);
  // a pause that ends does not clear the update that began meanwhile
  signal.raise(Reason::kPause, 600, t, "ana");
  signal.raise(Reason::kUpdate, 900, t + 1000, "");
  said = signal.lower(Reason::kPause, t + 2000);
  CHECK(said.publish && said.active && said.reason == Reason::kUpdate && signal.active());
  // a restart is what is said while it is the highest, and the update comes back after
  signal.raise(Reason::kRestart, 90, t + 3000, "ana");
  said = signal.lower(Reason::kRestart, t + 4000);
  CHECK(said.active && said.reason == Reason::kUpdate);
  said = signal.lower(Reason::kUpdate, t + 5000);
  CHECK(said.publish && !said.active && !signal.active());
  // a name that is not a good person is not repeated in the message
  said = signal.raise(Reason::kRestart, 90, t, "bad name\"");
  CHECK(said.by.empty());
}

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : "";
  test_the_order_is_read_strictly();
  test_rolling_delay_slot_and_duration_rules();
  test_there_is_no_way_to_say_factory_reset();
  test_a_token_is_used_once();
  test_a_token_is_used_once_for_every_operation();
  test_a_token_lives_thirty_seconds();
  test_a_wrong_token_disarms();
  test_a_token_belongs_to_its_operation();
  test_a_new_arm_replaces_the_old_token();
  test_a_token_from_random_bytes();
  test_the_rate_limit_is_for_a_restart();
  test_a_busy_node_says_what_it_is_doing();
  test_already_in_state();
  test_a_pause_ends_by_itself();
  test_a_pause_is_cleared_by_a_restart();
  test_a_standby_has_no_end_and_survives_a_restart();
  test_maintenance_and_identify_windows();
  test_the_wait_of_a_restart();
  test_the_topics_and_the_nodes_that_can_be_controlled();
  test_a_node_follows_the_orders();
  test_busy_refusals_reach_the_answer();
  test_a_token_cannot_be_used_across_operations_in_the_controller();
  test_the_rate_limit_in_the_controller();
  test_the_health_report_carries_the_control_state();
  test_a_boot_clears_what_the_last_run_left();
  test_a_restart_pause_and_standby_announce_themselves();
  test_the_operator_window_follows_the_flag();
  test_an_update_or_an_upload_announces_and_clears();
  if (!dir.empty()) {
    test_the_order_against_the_shared_vectors(dir);
    test_the_answers_are_the_ones_of_the_vectors(dir);
    test_the_maintenance_message_is_the_one_of_the_vectors(dir);
  } else {
    std::printf("the shared vectors were not given: only the rules were tried\n");
  }
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
