// ARMOR-ELECTRICAL - lets the server control the node. Nothing here has run on a board; what it decides is in core/control_gate.hpp and is tested on a computer.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// Who may ask: only the server's identity can write armor/control/... on the broker (its access list), and even then the node acts only on an execute that carries the token it gave in the
// answer to an arm a moment ago, for that very operation. The node never acts on anything it is told on its panel's unauthenticated paths, never erases anything and never sleeps: a restart is
// the same clean one the panel asks for (the connections are closed first, then esp_restart), and pause and standby only stop the node's work while it stays on the network.
#include "node_control.hpp"

#include <atomic>
#include <ctime>
#include <mutex>
extern "C" {
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}
#include "core/node_health.hpp"
#include "github_update.hpp"
#include "mqtt_link.hpp"
#include "network.hpp"
#include "node_diag.hpp"
#include "node_guard.hpp"
#include "node_store.hpp"
#include "task_health.hpp"
#include "web_server.hpp"

namespace armor::node_control {
namespace {
constexpr char kTag[] = "armor-control";
constexpr char kStandbyKey[] = "ctl_standby";   // an NVS key is at most 15 characters
Options g_options;
control::Controller g_controller;
std::mutex g_lock;
std::atomic<int> g_uploads{0};
std::atomic<bool> g_started{false};
std::atomic<bool> g_restart_pending{false};
control::MaintenanceSignal g_signal;   // under g_lock
bool g_was_connected = false;          // the tick task's own
std::atomic<int> g_state{static_cast<int>(control::State::kRunning)};

std::uint64_t now_ms() { return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL; }
std::uint64_t wall_ms() { return mqtt_link::clock_is_set() ? mqtt_link::wall_clock_ms() : 0; }

control::Busy busy_now() {
  if (g_uploads.load() > 0) return control::Busy::kUpload;
  const std::string install = github_update::progress().state;
  if (install == "downloading" || install == "verifying") return control::Busy::kUpdate;
  if (github_update::copy_progress().state == "copying") return control::Busy::kSlotCopy;
  if (g_options.busy) {
    const control::Busy other = g_options.busy();
    if (other != control::Busy::kNone) return other;
  }
  return control::Busy::kNone;
}

void fill_diagnostics(control::Diagnostics& d) {
  d.firmware = esp_app_get_description()->version;
  d.reset_reason = nodehealth::reset_reason_text(static_cast<int>(esp_reset_reason()));
  d.uptime_s = now_ms() / 1000ULL;
  d.heap_free = esp_get_free_heap_size();
  d.heap_min = esp_get_minimum_free_heap_size();
  if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0) d.psram_free = static_cast<long long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  const network::Status net = network::status();
  if (net.sta_connected) { d.has_rssi = true; d.rssi = net.sta_rssi; }
  d.mqtt_connected = mqtt_link::connected();
  std::uint32_t lowest = 0;
  bool any = false;
  for (const auto& task : taskhealth::tasks()) {
    if (!any || task.stack_free < lowest) lowest = task.stack_free;
    any = true;
  }
  if (any) d.stack_min_free = lowest;
  if (g_options.diagnostics_extra) g_options.diagnostics_extra(d);
}

// Says what the planned-maintenance signal now is, retained (the clearing message too, so the broker never keeps a stale "active"). Called with no lock held.
void say(const control::Signal& signal) {
  if (!signal.publish) return;
  const std::string topic = control::maintenance_topic(g_options.node_id);
  const std::string payload = control::maintenance_json(g_options.node_id, wall_ms() != 0 ? wall_ms() : now_ms(), signal, esp_app_get_description()->version);
  if (topic.empty()) return;
  if (g_options.publish_retained) g_options.publish_retained(topic, payload);
  else if (g_options.publish) g_options.publish(topic, payload);
}

void answer(const std::string& payload) {
  const std::string topic = control::result_topic(g_options.node_id);
  if (!topic.empty() && !payload.empty() && g_options.publish) g_options.publish(topic, payload);
}

// What the change of an order or of the tick means for the rest of the node. Called with no lock held.
void act_on(const control::Change& change) {
  if (change.save_standby || change.clear_standby) {
    std::uint64_t since = 0;
    std::string by;
    {
      const std::lock_guard<std::mutex> guard(g_lock);
      since = g_controller.machine().since_ms();
      by = g_controller.machine().set_by();
    }
    const bool saved = store::blob_write(kStandbyKey, change.save_standby ? control::standby_record(since, by) : control::standby_cleared_record());
    if (!saved) ESP_LOGE(kTag, "the standby could not be kept in flash: a restart would end it");
  }
  if (change.state_changed) {
    control::State state;
    {
      const std::lock_guard<std::mutex> guard(g_lock);
      state = g_controller.machine().state();
    }
    g_state = static_cast<int>(state);
    ESP_LOGW(kTag, "the node is now %s", control::state_name(state));
    if (g_options.on_state) g_options.on_state(state);
  }
  if (change.identify_changed) {
    long long left = 0;
    {
      const std::lock_guard<std::mutex> guard(g_lock);
      left = g_controller.machine().identify_left_s(now_ms());
    }
    ESP_LOGW(kTag, "identify: %lld s", left > 0 ? left : 0LL);
    if (g_options.identify) g_options.identify(left > 0 ? static_cast<unsigned>(left) : 0u);
  }
}

void on_command(const std::string& topic, const std::string& payload) {
  const bool broadcast = topic == control::broadcast_topic();
  if (!broadcast && topic != control::command_topic(g_options.node_id)) return;
  control::Env env;
  env.now_ms = now_ms();
  env.wall_ms = wall_ms();
  env.stamp_ms = env.wall_ms != 0 ? env.wall_ms : env.now_ms;
  env.busy = busy_now();
  std::uint8_t random[24];
  esp_fill_random(random, sizeof random);
  env.fresh_token = control::token_from_bytes(random, sizeof random);
  fill_diagnostics(env.diagnostics);
  // The package (or the log) of a diagnostics order that asks for one is built only after the order was accepted (see control::Controller::handle).
  env.package = [](control::Detail detail) { return node_diag::build_package(detail); };
  control::Reply reply;
  {
    const std::lock_guard<std::mutex> guard(g_lock);
    reply = g_controller.handle(g_options.node_id, broadcast, payload, env);
    // The acknowledgement leaves first (it is queued for the broker now and the restart is a few seconds away).
    answer(reply.json);
  }
  if (!reply.why_not.empty()) ESP_LOGW(kTag, "%s", reply.why_not.c_str());
  act_on(reply.change);
  control::Signal signal;
  {
    const std::lock_guard<std::mutex> guard(g_lock);
    signal = control::follow_order(g_signal, reply, g_controller.machine(), env.now_ms);
  }
  say(signal);   // after the acknowledgement and before the restart delay ends
  if (reply.restart) {
    ESP_LOGW(kTag, "restarting in %u s, as the server asked", static_cast<unsigned>(reply.restart_in_s));
    g_restart_pending = true;
    nodeguard::note_deliberate_restart();   // the next boot tells this restart from one of the guard's
    web::restart_after(reply.restart_in_s * 1000u);   // the same clean restart the panel uses
  }
}

void tick_task(void*) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    control::Change change;
    control::Signal signal;
    bool connected_now = mqtt_link::connected();
    const control::Busy busy = busy_now();
    {
      const std::lock_guard<std::mutex> guard(g_lock);
      const std::uint64_t now = now_ms();
      change = g_controller.tick(now);
      signal = control::follow_tick(g_signal, g_controller.machine(), change, now, busy);
      if (connected_now && !g_was_connected) {
        // The broker is there (after the boot, and after every reconnection): say what is true now. A node booted into standby keeps saying so; any other boot clears what the last run left.
        const control::Machine& machine = g_controller.machine();
        signal = g_signal.on_connected(machine.state() == control::State::kStandby, machine.set_by(), now);
      }
    }
    g_was_connected = connected_now;
    act_on(change);
    say(signal);
  }
}
}  // namespace

void start(const Options& options) {
  if (!control::node_can_be_controlled(options.node_id)) { ESP_LOGW(kTag, "no control by the server: the node id is not valid for it"); return; }
  g_options = options;
  if (options.sensor_versions) node_diag::set_sensor_versions(options.sensor_versions);
  std::string kept;
  std::uint64_t since = 0;
  std::string by;
  if (store::blob_read(kStandbyKey, kept) && control::parse_standby_record(kept, since, by)) {
    {
      const std::lock_guard<std::mutex> guard(g_lock);
      g_controller.machine().restore_standby(since, by);
    }
    g_state = static_cast<int>(control::State::kStandby);
    ESP_LOGW(kTag, "the node boots into standby: its work stays off until the server resumes it");
    if (g_options.on_state) g_options.on_state(control::State::kStandby);
  } else if (g_options.on_state) {
    g_options.on_state(control::State::kRunning);
  }
  mqtt_link::subscribe(control::command_topic(options.node_id), on_command);
  mqtt_link::subscribe(control::broadcast_topic(), on_command);
  g_started = true;
  nodeguard::expect_task("node-control");   // right before the task is made: the guard misses it if it could not be made (it keeps its stack in the internal memory: it writes the standby to flash)
  xTaskCreate(tick_task, "node-control", 4096, nullptr, 2, nullptr);
}

bool working() { return g_state.load() == static_cast<int>(control::State::kRunning); }
control::State state() { return static_cast<control::State>(g_state.load()); }
bool restart_pending() { return g_restart_pending.load(); }

Status status() {
  Status out;
  const std::uint64_t now = now_ms();
  const std::lock_guard<std::mutex> guard(g_lock);
  const control::Machine& machine = g_controller.machine();
  out.state = machine.state();
  out.since_ms = machine.since_ms();
  out.by = machine.set_by();
  out.paused_left_s = machine.pause_left_s(now);
  out.maintenance = machine.maintenance();
  out.maintenance_left_s = machine.maintenance_left_s(now);
  out.identifying = machine.identifying();
  return out;
}

void announce_maintenance(const char* reason, unsigned expected_s) {
  if (!g_started.load() || reason == nullptr) return;
  control::Reason which;
  if (!control::reason_from_name(reason, which) || (which != control::Reason::kUpdate && which != control::Reason::kUpload)) return;
  control::Signal signal;
  {
    const std::lock_guard<std::mutex> guard(g_lock);
    signal = g_signal.raise(which, expected_s, now_ms(), std::string());
  }
  say(signal);
}

void announce_maintenance_done(const char* reason) {
  if (!g_started.load() || reason == nullptr) return;
  control::Reason which;
  if (!control::reason_from_name(reason, which) || (which != control::Reason::kUpdate && which != control::Reason::kUpload)) return;
  control::Signal signal;
  {
    const std::lock_guard<std::mutex> guard(g_lock);
    signal = g_signal.lower(which, now_ms());
  }
  say(signal);
}

UploadGuard::UploadGuard() { ++g_uploads; announce_maintenance("upload", 300); }
UploadGuard::~UploadGuard() { --g_uploads; announce_maintenance_done("upload"); }   // an upload does not restart the node by itself: a restart that follows announces itself

}  // namespace armor::node_control
