// ARMOR-NODE-ELECTRICAL - the node's own watchdog over its health: reads the real values, tells, and restarts once when the rules in core/node_guard.hpp say so. Nothing here has run on a board.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include "node_guard.hpp"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <new>
#include <string>
#include <vector>
extern "C" {
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}
#include "core/boot_report.hpp"
#include "core/node_health.hpp"
#include "node_control.hpp"
#include "node_diag.hpp"
#include "node_health.hpp"
#include "psram_mem.hpp"
#include "web_server.hpp"

namespace armor::nodeguard {
namespace {
constexpr char kTag[] = "armor-guard";
constexpr unsigned kLookEveryMs = 2000;
constexpr unsigned kRestartDelayMs = 5000;

std::mutex g_lock;
std::vector<std::string> g_expected;

// Memory that a software reset (esp_restart, a crash) leaves as it was; a power-on leaves garbage there, which the check numbers tell apart.
RTC_NOINIT_ATTR Ledger g_rtc_ledger;
RTC_NOINIT_ATTR std::uint32_t g_rtc_check;
RTC_NOINIT_ATTR OomRecord g_rtc_oom;
RTC_NOINIT_ATTR std::uint32_t g_rtc_deliberate;   // kDeliberateMark when the last restart was asked for on purpose
RTC_NOINIT_ATTR bootreport::WhyRecord g_rtc_why;      // why the guard asked for its last restart
RTC_NOINIT_ATTR bootreport::CrashRecord g_rtc_crash;  // the guard's last look: how long the node had run and the internal memory it had
bootreport::Facts g_facts;                            // what this boot learned, kept for the report of a dirty start
bootreport::Report g_boot_report;
bool g_boot_report_ready = false;
std::atomic<std::uint32_t> g_gate_changes{0};
Ledger g_ledger;
BootInfo g_boot;
OomRecord g_previous_oom;   // what the last boot left (invalid when there was none)
std::once_flag g_boot_once;

std::atomic<bool> g_gate_closed{false};
std::atomic<bool> g_broker_in_use{false};
std::atomic<bool> g_broker_connected{false};
std::atomic<std::uint64_t> g_last_publish_ms{0};
std::atomic<bool> g_writing_firmware{false};

std::uint64_t now_ms() { return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL; }
std::uint32_t uptime_s() { return static_cast<std::uint32_t>(static_cast<std::uint64_t>(esp_timer_get_time()) / 1000000ULL); }

void save_ledger() {
  g_rtc_ledger = g_ledger;
  g_rtc_check = ledger_check(g_ledger);
}

void load_ledger() {
  const esp_reset_reason_t reason = esp_reset_reason();
  const bool kept = reason == ESP_RST_SW || reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT;
  g_ledger = ledger_at_boot(g_rtc_ledger, g_rtc_check, kept, g_boot);
  save_ledger();   // the "pending" mark is read once
  if (kept && oom_valid(g_rtc_oom)) g_previous_oom = g_rtc_oom;
  g_rtc_oom = OomRecord{};   // read once
  g_boot.deliberate = deliberate_boot(g_rtc_deliberate, kept, g_boot.by_guard);
  g_rtc_deliberate = 0;   // read once
  // What the report of a dirty start is made of (core/boot_report.hpp). A brown-out leaves the memory as it was most of the time; the check numbers refuse the garbage when it does not.
  g_facts.reset_reason = nodehealth::reset_reason_text(static_cast<int>(reason));
  g_facts.kept_memory = kept || reason == ESP_RST_BROWNOUT;
  g_facts.by_guard = g_boot.by_guard;
  g_facts.deliberate = g_boot.deliberate;
  if (g_facts.kept_memory) {
    g_facts.why = g_rtc_why;
    g_facts.crash = g_rtc_crash;
    g_facts.oom = g_previous_oom;
  }
  g_rtc_why = bootreport::WhyRecord{};   // read once
  if (g_boot.deliberate) ESP_LOGW(kTag, "this start follows a restart that was asked for on purpose (the server or the panel), not one of the guard's: it does not count against the guard's limit");
  if (g_boot.by_guard) ESP_LOGW(kTag, "this start follows a restart the node asked for itself (%u in the last hour of running time): Bluetooth stays off", g_boot.restarts_in_hour);
  // The line below is also what the health report carries as its last error, so the server and the panel show why the last run ended.
  if (oom_valid(g_previous_oom)) ESP_LOGE(kTag, "%s", oom_text(g_previous_oom).c_str());
}

// A failed `new`: nothing here allocates (no string, no log with a buffer from the heap, no lock). The incident is written where the next boot reads it, the ledger counts the restart, and the
// node restarts at once. When it must not (an image is being written, a second one within a minute, the limit of restarts is used up) it aborts, as it would without this handler: a loop of them
// stays visible.
void out_of_memory() {
  static std::atomic<bool> inside{false};
  if (inside.exchange(true)) std::abort();
  const std::uint32_t up = uptime_s();
  const std::uint32_t free_bytes = static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  const std::uint32_t largest = static_cast<std::uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  const std::uint32_t clock = g_ledger.clock(up);
  if (oom_decide(g_previous_oom, clock, g_writing_firmware.load()) == OomAction::kAbort || !g_ledger.may_restart(up)) std::abort();
  g_rtc_oom = make_oom(free_bytes, largest, up, clock);
  g_ledger.note_restart(up);
  save_ledger();
  esp_restart();
}

std::vector<std::string> missing_tasks() {
  std::vector<std::string> expected;
  { std::lock_guard<std::mutex> guard(g_lock); expected = g_expected; }
  std::vector<std::string> missing;
  for (const std::string& name : expected) if (xTaskGetHandle(name.substr(0, configMAX_TASK_NAME_LEN - 1).c_str()) == nullptr) missing.push_back(name);
  return missing;
}

void restart_cleanly(bootreport::Why why) {
  g_rtc_why = bootreport::make_why(why);
  g_ledger.note_restart(uptime_s());
  save_ledger();
  ESP_LOGW(kTag, "restarting once, cleanly, in %u s (%u restarts of this kind in the last hour; the limit is %u)", kRestartDelayMs / 1000, g_ledger.recent(uptime_s()), kMaxRestartsPerHour);
  web::restart_after(kRestartDelayMs);
}

void guard_task(void*) {
  State state;
  Gate gate;
  bool degraded_said = false;
  std::uint64_t degraded_at_ms = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(kLookEveryMs));
    const membudget::Snapshot memory = memory::snapshot();
    const std::uint64_t now = now_ms();

    // The gate at the door of the panel.
    if (gate.step(memory.internal_free, memory.internal_largest)) {
      g_gate_closed = gate.closed;
      g_gate_changes = gate.transitions;
      const std::string text = gate_text(gate.closed, memory.internal_free, memory.internal_largest);
      if (gate.closed) ESP_LOGE(kTag, "%s", text.c_str());
      else ESP_LOGW(kTag, "%s", text.c_str());
      node_health::publish_now();
    }

    // The dead-man switch: a broker is set up and connected, and nothing has been published for ten minutes.
    if (g_broker_in_use.load() && g_broker_connected.load()) {
      const DeadMan verdict = dead_man(true, now, g_last_publish_ms.load(), g_ledger.may_restart(uptime_s()));
      if (verdict == DeadMan::kRestart) {
        ESP_LOGE(kTag, "guard: nothing was published to the broker for ten minutes while it is connected");
        node_health::publish_now();
        restart_cleanly(bootreport::Why::kDeadMan);
        g_last_publish_ms = now;   // not asked again while the restart is on its way
      } else if (verdict == DeadMan::kDegraded && (!degraded_said || now - degraded_at_ms >= kRepeatAlarmMs)) {
        degraded_said = true;
        degraded_at_ms = now;
        ESP_LOGE(kTag, "guard: nothing is published to the broker and the limit of restarts is used up: the node stays up, degraded (panel and local work only)");
      }
    } else {
      degraded_said = false;
    }

    // The last look before a crash: how long the node had run and the internal memory it had (read at the next start, see core/boot_report.hpp).
    g_rtc_crash = bootreport::make_crash(uptime_s(), memory.internal_free, memory.internal_largest);

    // Tasks that are missing and memory that stays low.
    // A node the server paused or put in standby, or one with a restart on its way that someone asked for, is not restarted for a task it stopped on purpose; the memory rules stay in force.
    std::vector<std::string> missing = missing_tasks();
    if (counted_missing_tasks(static_cast<unsigned>(missing.size()), node_control::working(), node_control::restart_pending()) == 0) missing.clear();
    Inputs in;
    in.now_ms = now;
    in.internal_free = memory.internal_free;
    in.internal_largest = memory.internal_largest;
    in.missing_tasks = static_cast<unsigned>(missing.size());
    const bool may_restart = g_ledger.may_restart(uptime_s());
    const Action action = step(state, in, may_restart);
    if (action == Action::kNone) continue;
    const std::string reason = reason_text(missing, in, state.memory_low);
    ESP_LOGE(kTag, "%s", reason.c_str());
    node_health::publish_now();
    if (action == Action::kAlertAndRestart) restart_cleanly(missing.empty() ? bootreport::Why::kLowMemory : bootreport::Why::kMissingTask);
    else ESP_LOGE(kTag, "no restart: the limit of %u in an hour is used up, the node stays up and says so again in ten minutes", kMaxRestartsPerHour);
  }
}
}  // namespace

void expect_task(const char* name) {
  if (name == nullptr || *name == '\0') return;
  std::lock_guard<std::mutex> guard(g_lock);
  for (const std::string& known : g_expected) if (known == name) return;
  g_expected.push_back(name);
}

BootInfo boot_info() {
  std::call_once(g_boot_once, load_ledger);
  return g_boot;
}

void start() {
  node_diag::init();   // the one look at the core dump partition (flash is read here, in the main task, and nowhere else)
  boot_info();
  boot_report();
  std::set_new_handler(out_of_memory);
  // No flash is touched here (a log line, a message to the queue, a timer), so the stack can be in the external RAM.
  memory::start_task(guard_task, "node-guard", 4096, nullptr, 3);
}

void note_deliberate_restart() { g_rtc_deliberate = kDeliberateMark; }

bootreport::Report boot_report() {
  boot_info();
  if (!g_boot_report_ready) {
    bootreport::Facts facts = g_facts;
    facts.coredump_present = node_diag::core_info().present;
    g_boot_report = bootreport::classify(facts);
    g_boot_report_ready = true;
    if (g_boot_report.dirty) ESP_LOGW(kTag, "the last run ended badly: %s (reset %s)%s", bootreport::cause_name(g_boot_report.cause), g_boot_report.reset_reason.c_str(), g_boot_report.coredump ? ", a core dump is waiting in flash" : "");
  }
  return g_boot_report;
}

bootreport::GateInfo gate_info() {
  bootreport::GateInfo info;
  info.closed = g_gate_closed.load();
  info.transitions = g_gate_changes.load();
  return info;
}

unsigned restarts_in_hour() { return g_ledger.recent(uptime_s()); }

void note_broker_in_use() { g_broker_in_use = true; g_last_publish_ms = now_ms(); }
void note_broker_connected(bool connected) {
  g_broker_connected = connected;
  if (connected) g_last_publish_ms = now_ms();   // the ten minutes count from the connection
}
void note_publish() { g_last_publish_ms = now_ms(); }
void note_firmware_write(bool writing) { g_writing_firmware = writing; }

bool accept_gate_open() { return !g_gate_closed.load(); }
esp_err_t http_open_gate(httpd_handle_t, int) { return g_gate_closed.load() ? ESP_FAIL : ESP_OK; }

}  // namespace armor::nodeguard
