// ARMOR-NODE-ELECTRICAL - host tests of what a node tells an administrator about itself when it misbehaves: the report of a dirty start (core/boot_report.hpp), the copy of the settings made before an
// update and the check of the start (core/config_snapshot.hpp), and the diagnostic package, which must never carry a secret (core/diag_package.hpp).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
// Nothing here touches hardware or a network.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#if __has_include("../core/json.hpp")
#include "../core/boot_report.hpp"
#include "../core/config_snapshot.hpp"
#include "../core/control_gate.hpp"
#include "../core/diag_package.hpp"
#include "../core/json.hpp"
#include "../core/node_health.hpp"
#else
#include "../main/core/boot_report.hpp"
#include "../main/core/config_snapshot.hpp"
#include "../main/core/control_gate.hpp"
#include "../main/core/diag_package.hpp"
#include "../main/core/json.hpp"
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

static bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

static json::Value parsed(const std::string& text) {
  json::Value doc;
  CHECK(json::parse(text, doc));
  return doc;
}

// ---- the report of a dirty start -----------------------------------------------------------------------------------------------------------

static bootreport::Facts facts(const char* reason) {
  bootreport::Facts f;
  f.reset_reason = reason;
  return f;
}

static void test_a_clean_start_says_nothing() {
  for (const char* reason : {"power_on", "external", "software", "deep_sleep", "usb", "unknown"}) {
    const bootreport::Report r = bootreport::classify(facts(reason));
    CHECK(!r.dirty && r.cause == bootreport::Cause::kNone);
    CHECK(!bootreport::should_report(r, 5));
  }
  // a restart someone asked for is a software reset: clean
  bootreport::Facts f = facts("software");
  f.deliberate = true;
  f.kept_memory = true;
  CHECK(!bootreport::classify(f).dirty);
}

static void test_a_crash_and_the_watchdogs_are_dirty() {
  const struct { const char* reason; bootreport::Cause cause; } table[] = {
      {"panic", bootreport::Cause::kPanic}, {"task_wdt", bootreport::Cause::kTaskWatchdog}, {"int_wdt", bootreport::Cause::kInterruptWatchdog}, {"other_wdt", bootreport::Cause::kOtherWatchdog},
      {"brownout", bootreport::Cause::kBrownout}, {"power_glitch", bootreport::Cause::kPowerGlitch}, {"cpu_lockup", bootreport::Cause::kCpuLockup}};
  for (const auto& row : table) {
    const bootreport::Report r = bootreport::classify(facts(row.reason));
    CHECK(r.dirty && r.cause == row.cause && r.reset_reason == row.reason);
    CHECK(bootreport::should_report(r, 5) && bootreport::should_report(r, bootreport::kReportForS - 1) && !bootreport::should_report(r, bootreport::kReportForS));
    CHECK(!r.has_uptime_before && !r.has_heap);   // nothing was kept: no figures are invented
  }
}

static void test_the_last_look_is_used_only_when_it_checks() {
  bootreport::Facts f = facts("panic");
  f.kept_memory = true;
  f.crash = bootreport::make_crash(4200, 30000, 20000);
  bootreport::Report r = bootreport::classify(f);
  CHECK(r.has_uptime_before && r.uptime_before_s == 4200 && r.has_heap && r.heap_internal_free == 30000 && r.heap_internal_largest == 20000);
  // the largest block can never be bigger than the free memory
  f.crash = bootreport::make_crash(10, 1000, 5000);
  r = bootreport::classify(f);
  CHECK(r.heap_internal_largest == 1000);
  // garbage in the kept memory (a power-up) is refused by its check number
  f.crash = bootreport::make_crash(4200, 30000, 20000);
  f.crash.uptime_s ^= 1;
  CHECK(!bootreport::classify(f).has_uptime_before);
  f.crash = bootreport::make_crash(4200, 30000, 20000);
  f.kept_memory = false;   // a power-up: the memory is not ours
  CHECK(!bootreport::classify(f).has_uptime_before);
}

static void test_an_out_of_memory_comes_first() {
  bootreport::Facts f = facts("software");
  f.kept_memory = true;
  f.by_guard = true;
  f.why = bootreport::make_why(bootreport::Why::kLowMemory);
  f.oom = nodeguard::make_oom(9000, 4000, 777, 1000);
  bootreport::Report r = bootreport::classify(f);
  CHECK(r.dirty && r.cause == bootreport::Cause::kOutOfMemory && r.uptime_before_s == 777 && r.heap_internal_free == 9000 && r.heap_internal_largest == 4000);
  f.oom.free_bytes ^= 4;   // a damaged record is not trusted
  r = bootreport::classify(f);
  CHECK(r.cause == bootreport::Cause::kGuardLowMemory);
}

static void test_the_guards_own_restarts_say_why() {
  const struct { bootreport::Why why; bootreport::Cause cause; } table[] = {{bootreport::Why::kMissingTask, bootreport::Cause::kGuardMissingTask},
                                                                          {bootreport::Why::kLowMemory, bootreport::Cause::kGuardLowMemory},
                                                                          {bootreport::Why::kDeadMan, bootreport::Cause::kGuardDeadMan}};
  for (const auto& row : table) {
    bootreport::Facts f = facts("software");
    f.kept_memory = true;
    f.by_guard = true;
    f.why = bootreport::make_why(row.why);
    const bootreport::Report r = bootreport::classify(f);
    CHECK(r.dirty && r.guard && r.cause == row.cause);
  }
  bootreport::Facts f = facts("software");
  f.by_guard = true;   // the ledger says the guard, but the reason did not survive
  CHECK(bootreport::classify(f).cause == bootreport::Cause::kGuard);
  CHECK(!bootreport::why_valid(bootreport::WhyRecord{}));
  bootreport::WhyRecord bad = bootreport::make_why(bootreport::Why::kDeadMan);
  bad.why = 9;
  CHECK(!bootreport::why_valid(bad));
}

static void test_the_report_is_json_with_the_contract_members() {
  bootreport::Facts f = facts("panic");
  f.kept_memory = true;
  f.crash = bootreport::make_crash(60, 50000, 30000);
  f.coredump_present = true;
  const bootreport::Report r = bootreport::classify(f);
  json::Writer w;
  bootreport::write_json(w, r);
  const json::Value doc = parsed(w.str());
  CHECK(doc.string_or("cause", "") == "panic" && doc.string_or("reset_reason", "") == "panic");
  CHECK(doc.number_or("uptime_before_s", -1) == 60 && doc.number_or("heap_internal_free", -1) == 50000 && doc.number_or("heap_internal_largest", -1) == 30000);
  CHECK(doc.get("coredump") != nullptr && doc.get("coredump")->boolean && doc.get("deliberate") != nullptr && !doc.get("deliberate")->boolean && doc.get("guard") != nullptr);
  // the figures are left out when there are none
  json::Writer bare;
  bootreport::write_json(bare, bootreport::classify(facts("brownout")));
  CHECK(!contains(bare.str(), "uptime_before_s") && !contains(bare.str(), "heap_internal_free"));
}

static void test_the_health_report_carries_the_new_members_only_when_asked() {
  nodehealth::Report report;
  report.node_id = "radar-1";
  report.family = "radar";
  report.firmware = "0.7.9";
  report.uptime_s = 30;
  report.reset_reason = "panic";
  std::string plain = nodehealth::to_json(report);
  CHECK(!contains(plain, "\"boot\"") && !contains(plain, "memory_gate") && !contains(plain, "coredump") && !contains(plain, "config_check"));
  bootreport::Facts f = facts("panic");
  f.coredump_present = true;
  report.has_boot = true;
  report.boot = bootreport::classify(f);
  report.has_gate = true;
  report.gate.closed = true;
  report.gate.transitions = 3;
  report.has_coredump = true;
  report.coredump_present = true;
  report.coredump_size = 40000;
  report.has_config_check = true;
  report.config_check.snapshot = "restored";
  const json::Value doc = parsed(nodehealth::to_json(report));
  CHECK(doc.get("boot") != nullptr && doc.get("boot")->string_or("cause", "") == "panic");
  const json::Value* gate = doc.get("memory_gate");
  CHECK(gate != nullptr && gate->get("closed") != nullptr && gate->get("closed")->boolean && gate->number_or("transitions", -1) == 3 && gate->number_or("close_free", 0) > 0 && gate->number_or("open_block", 0) > 0);
  const json::Value* core = doc.get("coredump");
  CHECK(core != nullptr && core->get("present") != nullptr && core->get("present")->boolean && core->number_or("size", -1) == 40000);
  CHECK(doc.get("config_check") != nullptr && doc.get("config_check")->string_or("snapshot", "") == "restored" && doc.get("config_check")->get("reverted") == nullptr);
  // after the first minutes of the run the dirty start and the checks are old news; the gate and the core dump stay
  report.uptime_s = bootreport::kReportForS + 1;
  const json::Value later = parsed(nodehealth::to_json(report));
  CHECK(later.get("boot") == nullptr && later.get("config_check") == nullptr && later.get("memory_gate") != nullptr && later.get("coredump") != nullptr);
  // a core dump of size zero is not announced with a size
  report.coredump_present = false;
  report.coredump_size = 0;
  CHECK(!contains(nodehealth::to_json(report), "\"size\""));
}

// ---- the copy of the settings and the check of the start --------------------------------------------------------------------------------------

static void test_the_copy_of_the_settings() {
  const std::string text = "{\"v\":3,\"node_id\":\"radar-1\"}";
  const std::string blob = snapshot::make("0.7.9", text);
  CHECK(!blob.empty());
  snapshot::Parsed back;
  CHECK(snapshot::parse(blob, back) && back.firmware == "0.7.9" && back.settings == text);
  // nothing to copy, a version that is not one, a text that is too big
  CHECK(snapshot::make("0.7.9", "").empty());
  CHECK(snapshot::make("v1; rm", text).empty() && snapshot::make("", text).empty());
  CHECK(snapshot::make("0.7.9", std::string(snapshot::kMaxBytes + 1, 'a')).empty());
  CHECK(!snapshot::make("0.7.9", std::string(snapshot::kMaxBytes, 'a')).empty());
  // a damaged copy is not a copy
  std::string damaged = blob;
  damaged[damaged.size() - 2] ^= 1;
  CHECK(!snapshot::parse(damaged, back));
  CHECK(!snapshot::parse("", back) && !snapshot::parse("ARMORSNAP1", back) && !snapshot::parse("ARMORSNAP2 0.7.9 00000000\n{}", back));
  CHECK(!snapshot::parse(blob.substr(0, blob.size() / 2), back));
}

static void test_which_text_the_node_starts_with() {
  using snapshot::Pick;
  CHECK(snapshot::pick_source(true, 0, false, 0) == Pick::kCurrent);   // no usable copy
  CHECK(snapshot::pick_source(false, 0, true, 5) == Pick::kSnapshot);  // the stored text cannot be read at all
  CHECK(snapshot::pick_source(true, 3, true, 1) == Pick::kSnapshot);   // the copy loses fewer fields
  CHECK(snapshot::pick_source(true, 1, true, 1) == Pick::kCurrent);    // a tie keeps the newest
  CHECK(snapshot::pick_source(true, 0, true, 2) == Pick::kCurrent);
}

static void test_the_check_after_the_start() {
  using snapshot::Recheck;
  CHECK(snapshot::recheck(false, 0, true, 123) == Recheck::kFine);       // nothing stored yet: nothing to compare
  CHECK(snapshot::recheck(true, 123, true, 123) == Recheck::kFine);
  CHECK(snapshot::recheck(true, 123, true, 124) == Recheck::kWriteAgain);
  CHECK(snapshot::recheck(true, 123, false, 0) == Recheck::kWriteAgain); // unreadable counts as different
  CHECK(std::string(snapshot::reverted_outcome(Recheck::kFine, true)).empty());
  CHECK(std::string(snapshot::reverted_outcome(Recheck::kWriteAgain, true)) == "reapplied");
  CHECK(std::string(snapshot::reverted_outcome(Recheck::kWriteAgain, false)) == "failed");
  CHECK(std::string(snapshot::snapshot_outcome(true, false)) == "restored" && std::string(snapshot::snapshot_outcome(false, true)) == "failed" && std::string(snapshot::snapshot_outcome(false, false)).empty());
  CHECK(snapshot::fnv1a("a") != snapshot::fnv1a("b") && snapshot::fnv1a("abc") == snapshot::fnv1a("abc"));
  CHECK(nodeguard::kSettingsCheckDelayS == 15);
}

// ---- the diagnostic package never carries a secret ------------------------------------------------------------------------------------------------

static const char* const kSecrets[] = {"hunter2hunter2", "Sup3rSecretWifiPass", "mqttpass-9917", "s3cr3t-token-abc123", "ana.administrator", "PEMKEYDATA-QWERTY", "cookie-value-77",
                                       "fleet-secret-0123456789abcdef", "0123456789abcdef0123456789abcdef0123", "Calle-Mayor-12-casa"};

static const char* kSettings =
    "{\"v\":3,\"node_id\":\"radar-1\",\"node_name\":\"Calle-Mayor-12-casa\",\"language\":\"es\","
    "\"wifi\":{\"mode\":\"station\",\"ssid\":\"Calle-Mayor-12-casa\",\"password\":\"Sup3rSecretWifiPass\",\"dhcp\":true,\"channel\":6,\"hidden\":false},"
    "\"ap\":{\"password\":\"hunter2hunter2\",\"channel\":1},"
    "\"mqtt\":{\"enabled\":true,\"uri\":\"mqtts://ana.administrator:mqttpass-9917@broker.casa.local:8883/path\",\"username\":\"ana.administrator\",\"password\":\"mqttpass-9917\",\"heartbeat_s\":30,\"telemetry_ms\":500},"
    "\"api\":{\"token\":\"s3cr3t-token-abc123\",\"session_cookie\":\"cookie-value-77\",\"pin\":\"4821\"},"
    "\"tls\":{\"enabled\":true,\"private_key\":\"PEMKEYDATA-QWERTY\",\"cert_pem\":\"PEMKEYDATA-QWERTY\"},"
    "\"fleet\":{\"secret\":\"fleet-secret-0123456789abcdef\",\"hash\":\"0123456789abcdef0123456789abcdef0123\"},"
    "\"time\":{\"ntp\":\"pool.ntp.casa.local\",\"zone\":\"Europe/Madrid\"},"
    "\"users\":[{\"name\":\"ana.administrator\",\"salt\":\"aabbccdd\"}],"
    "\"radars\":[{\"enabled\":true,\"model\":\"ld2450\",\"baud\":256000,\"yaw_deg\":12.5,\"name\":\"Calle-Mayor-12-casa\"}]}";

static diag::Inputs sample_inputs() {
  diag::Inputs in;
  in.firmware = "0.7.9";
  in.idf = "v5.5.1";
  in.board = "s3-eth";
  in.uptime_s = 4242;
  in.reset_reason = "panic";
  in.guard_restarts_in_hour = 1;
  in.memory.internal_free = 90000;
  in.memory.internal_min = 70000;
  in.memory.internal_largest = 60000;
  in.heap_total_free = 5000000;
  in.heap_total_min = 4000000;
  in.tasks.push_back({"node-health", 3000, 3, 'B'});
  in.tasks.push_back({"radar", 900, 5, 'R'});
  in.has_gate = true;
  in.sensors.push_back({"ld2450-1", "V1.02.22062416"});
  in.full_settings_json = kSettings;
  in.coredump_present = true;
  in.coredump_size = 41000;
  in.core.has_summary = true;
  in.core.task = "radar";
  in.core.pc = "0x4200abcd";
  in.core.backtrace = {"0x4200abcd", "0x40081234", "not-an-address"};
  in.core.app_sha256 = "0123456789abcdef";
  in.log = "I (100) mqtt: connecting\nW (200) wifi: password=Sup3rSecretWifiPass retry\nE (300) api: token: s3cr3t-token-abc123\n"
           "I (400) mqtt: uri mqtts://ana.administrator:mqttpass-9917@broker.casa.local:8883\nI (500) tls: key 0123456789abcdef0123456789abcdef0123 loaded\n"
           "I (600) cfg: hunter2hunter2 was set\nI (700) net: Authorization: Bearer abc.def.ghi\n";
  return in;
}

static void test_the_package_carries_no_secret() {
  for (const diag::Detail detail : {diag::Detail::kPackage, diag::Detail::kLog}) {
    const std::string text = diag::package_json(sample_inputs(), detail);
    CHECK(!text.empty());
    for (const char* secret : kSecrets) {
      if (contains(text, secret)) std::printf("the package leaks \"%s\"\n", secret);
      CHECK(!contains(text, secret));
    }
    CHECK(!contains(text, "abc.def.ghi") && !contains(text, "4821"));
    const json::Value doc = parsed(text);
    CHECK(doc.is_object() && doc.string_or("detail", "") == diag::detail_name(detail));
    CHECK(doc.get("log") != nullptr && doc.get("log")->is_string());
  }
}

static void test_the_package_follows_the_allow_list() {
  const json::Value doc = parsed(diag::package_json(sample_inputs(), diag::Detail::kPackage));
  const json::Value* config = doc.get("config");
  CHECK(config != nullptr && config->is_object());
  if (config == nullptr) return;
  // what is carried: the allowed leaves, as numbers, booleans and short text
  CHECK(config->string_or("wifi.mode", "") == "station" && config->string_or("wifi.dhcp", "") == "true");
  CHECK(config->string_or("wifi.channel", "") == "6" && config->string_or("mqtt.heartbeat_s", "") == "30" && config->string_or("language", "") == "es");
  CHECK(config->string_or("mqtt.uri_host", "") == "mqtts://broker.casa.local:8883");
  CHECK(config->string_or("radars.0.baud", "") == "256000" && config->string_or("radars.0.model", "") == "ld2450");
  // what is not: names of the house, users, credentials, keys, addresses
  for (const char* path : {"node_name", "node_id", "wifi.ssid", "wifi.password", "ap.password", "mqtt.uri", "mqtt.username", "mqtt.password", "api.token", "api.pin", "tls.private_key", "tls.cert_pem",
                           "fleet.secret", "fleet.hash", "time.ntp", "users.0.name", "users.0.salt", "radars.0.name"}) CHECK(config->get(path) == nullptr);
  // every path that is carried ends in an allowed leaf and holds no word of the second list (the host name is the one exception: it is built by safe_uri)
  for (std::size_t i = 0; i < config->names.size(); ++i) {
    const std::string& path = config->names[i];
    const std::size_t dot = path.rfind('.');
    const std::string leaf = dot == std::string::npos ? path : path.substr(dot + 1);
    CHECK(diag::allowed_leaf(leaf));
    CHECK(!diag::secret_path(path));
  }
  // the tasks, the memory and the versions are there; the core dump summary keeps only well-formed addresses
  CHECK(doc.get("heap") != nullptr && doc.get("tasks") != nullptr && doc.get("resets") != nullptr && doc.get("versions") != nullptr && doc.get("memory_gate") != nullptr);
  const json::Value* tasks = doc.get("tasks");
  CHECK(tasks != nullptr && tasks->items.size() == 2 && tasks->items[0].string_or("name", "") == "radar");   // the least stack first
  const json::Value* core = doc.get("coredump");
  CHECK(core != nullptr && core->get("summary") != nullptr && core->get("summary")->get("backtrace") != nullptr && core->get("summary")->get("backtrace")->items.size() == 2);
}

static void test_the_log_alone_has_nothing_else() {
  const std::string text = diag::package_json(sample_inputs(), diag::Detail::kLog);
  const json::Value doc = parsed(text);
  CHECK(doc.get("config") == nullptr && doc.get("tasks") == nullptr && doc.get("heap") == nullptr && doc.get("coredump") == nullptr);
  CHECK(contains(text, "mqtt: connecting") && contains(text, "***"));
  CHECK(diag::package_json(sample_inputs(), diag::Detail::kSummary).empty());
}

static void test_the_package_is_bounded() {
  diag::Inputs in = sample_inputs();
  std::string big;
  for (int i = 0; i < 4000; ++i) big += "I (" + std::to_string(i) + ") tick: line number " + std::to_string(i) + " of a very chatty log\n";
  in.log = big;
  for (int i = 0; i < 200; ++i) in.tasks.push_back({"task" + std::to_string(i), static_cast<std::uint32_t>(500 + i), 1, 'B'});
  const std::string text = diag::package_json(in, diag::Detail::kPackage);
  CHECK(text.size() <= diag::kMaxPackageBytes + 2000);   // the log gives way first; the fixed parts are small
  const json::Value doc = parsed(text);
  CHECK(doc.get("truncated") != nullptr && doc.get("truncated")->boolean);
  CHECK(doc.get("tasks")->items.size() <= diag::kMaxTasks);
  // a log with control characters and colour codes is printable text
  in = sample_inputs();
  in.log = std::string("\x1b[0;32mI (1) ok\x1b[0m\r\n\x01\x02 raw \xff bytes\n");
  const std::string log = parsed(diag::package_json(in, diag::Detail::kLog)).string_or("log", "");
  for (const char c : log) CHECK(c == '\n' || (c >= 32 && c < 127));
}

static void test_the_washing_functions() {
  CHECK(diag::safe_uri("mqtts://user:pass@broker.local:8883/path?x=1") == "mqtts://broker.local:8883");
  CHECK(diag::safe_uri("mqtt://10.0.0.5") == "mqtt://10.0.0.5");
  CHECK(diag::safe_uri("not a uri").empty() && diag::safe_uri("://x").empty() && diag::safe_uri("mqtt://").empty() && diag::safe_uri("mqtt://bad host").empty());
  CHECK(diag::secret_path("wifi.password") && diag::secret_path("mqtt.Token") && diag::secret_path("a.b.PSK") && !diag::secret_path("wifi.channel"));
  CHECK(!diag::allowed_leaf("ssid") && !diag::allowed_leaf("password") && !diag::allowed_leaf("") && diag::allowed_leaf("channel"));
  CHECK(diag::wash("the pass is hunter2 ok", {"hunter2"}) == "the pass is *** ok");
  CHECK(diag::wash("ab", {"ab"}) == "ab");   // a value shorter than four characters would eat ordinary words
  CHECK(diag::wash("ab\\\"cd", {"ab\"cd"}) == "***");   // the escaped form inside JSON text is washed too
  const std::string log = diag::sanitize_log("password=abc123 token: zzz9 ok bearer qqq1 mqtt://u:p@h:1/x deadbeefdeadbeefdeadbeefdeadbeefdeadbeef end", 1000, {});
  CHECK(!contains(log, "abc123") && !contains(log, "zzz9") && !contains(log, "qqq1") && !contains(log, "u:p@") && !contains(log, "deadbeefdead") && contains(log, "ok") && contains(log, "end"));
}

// ---- the order that asks for it -------------------------------------------------------------------------------------------------------------

static std::string order(const char* action, const char* extra = "") {
  return std::string("{\"kind\":\"node_control_command\",\"node_id\":\"radar-1\",\"timestamp_ms\":9000,\"command_id\":\"d0d0d0d012345678\",\"action\":\"") + action +
         "\",\"operation\":\"diagnostics\",\"mode\":\"now\"" + extra + ",\"requested_by\":\"ana\"}";
}

static control::Env env_at(std::uint64_t now_ms) {
  control::Env env;
  env.now_ms = now_ms;
  env.wall_ms = 1760000000000ULL + now_ms;
  env.stamp_ms = env.wall_ms;
  env.fresh_token = "0123456789abcdef0123456789abcdef";
  env.diagnostics.firmware = "0.7.9";
  env.diagnostics.reset_reason = "power_on";
  env.diagnostics.heap_free = 142000;
  env.diagnostics.heap_min = 98000;
  env.diagnostics.mqtt_connected = true;
  env.package = [](control::Detail detail) { return diag::package_json(sample_inputs(), detail); };
  return env;
}

static std::string give(control::Controller& node, std::uint64_t now_ms, const char* detail) {
  const control::Reply armed = node.handle("radar-1", false, order("arm"), env_at(now_ms));
  const json::Value answer = parsed(armed.json);
  if (answer.string_or("stage", "") != "armed") return armed.json;
  const std::string extra = std::string(",\"token\":\"") + answer.string_or("token", "") + "\"" + (*detail != '\0' ? std::string(",\"detail\":\"") + detail + "\"" : std::string());
  return node.handle("radar-1", false, order("execute", extra.c_str()), env_at(now_ms + 500)).json;
}

static void test_the_order_asks_for_a_package_or_a_log() {
  control::Command command;
  std::string why;
  CHECK(control::parse_command(order("execute", ",\"token\":\"0123456789abcdef0123456789abcdef\",\"detail\":\"package\""), command, &why) && command.detail == control::Detail::kPackage);
  CHECK(control::parse_command(order("execute", ",\"token\":\"0123456789abcdef0123456789abcdef\",\"detail\":\"log\""), command, &why) && command.detail == control::Detail::kLog);
  CHECK(control::parse_command(order("execute", ",\"token\":\"0123456789abcdef0123456789abcdef\""), command, &why) && command.detail == control::Detail::kSummary);
  CHECK(!control::parse_command(order("execute", ",\"token\":\"0123456789abcdef0123456789abcdef\",\"detail\":\"everything\""), command, &why));
  CHECK(!control::parse_command(order("execute", ",\"token\":\"0123456789abcdef0123456789abcdef\",\"detail\":1"), command, &why));
  CHECK(!control::parse_command(order("arm", ",\"detail\":\"package\""), command, &why));   // a detail goes on the execute only
  // and only on diagnostics
  const std::string restart = "{\"kind\":\"node_control_command\",\"node_id\":\"radar-1\",\"timestamp_ms\":9000,\"command_id\":\"d0d0d0d012345678\",\"action\":\"execute\",\"operation\":\"restart\","
                              "\"mode\":\"now\",\"token\":\"0123456789abcdef0123456789abcdef\",\"detail\":\"package\",\"requested_by\":\"ana\"}";
  CHECK(!control::parse_command(restart, command, &why));
}

static void test_the_answer_carries_the_package_and_packages_are_spaced() {
  control::Controller node;
  const std::string plain = give(node, 120000, "");
  CHECK(!contains(plain, "\"package\""));
  const std::string first = give(node, 130000, "package");
  const json::Value doc = parsed(first);
  CHECK(doc.string_or("stage", "") == "diagnostics");
  const json::Value* diagnostics = doc.get("diagnostics");
  CHECK(diagnostics != nullptr && diagnostics->get("package") != nullptr && diagnostics->get("package")->is_object() && diagnostics->get("package")->string_or("detail", "") == "package");
  for (const char* secret : kSecrets) CHECK(!contains(first, secret));
  // a second one right after is refused, the plain snapshot is not; later another goes
  const std::string second = give(node, 135000, "log");
  CHECK(contains(second, "rate_limited") && !contains(second, "\"package\""));
  CHECK(!contains(give(node, 136000, ""), "rate_limited"));
  const std::string third = give(node, 130000 + control::kPackageGapMs + 1000, "log");
  CHECK(parsed(third).get("diagnostics") != nullptr && parsed(third).get("diagnostics")->get("package") != nullptr);
}

int main() {
  test_a_clean_start_says_nothing();
  test_a_crash_and_the_watchdogs_are_dirty();
  test_the_last_look_is_used_only_when_it_checks();
  test_an_out_of_memory_comes_first();
  test_the_guards_own_restarts_say_why();
  test_the_report_is_json_with_the_contract_members();
  test_the_health_report_carries_the_new_members_only_when_asked();
  test_the_copy_of_the_settings();
  test_which_text_the_node_starts_with();
  test_the_check_after_the_start();
  test_the_package_carries_no_secret();
  test_the_package_follows_the_allow_list();
  test_the_log_alone_has_nothing_else();
  test_the_package_is_bounded();
  test_the_washing_functions();
  test_the_order_asks_for_a_package_or_a_log();
  test_the_answer_carries_the_package_and_packages_are_spaced();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
