// ARMOR-NODE-ELECTRICAL - tells the server how the node is doing. Nothing here has run on a board; what it decides is in core/node_health.hpp and is tested on a computer.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include "node_health.hpp"

#include <atomic>
#include <ctime>
extern "C" {
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}
#include "core/node_health.hpp"
#include "log_buffer.hpp"
#include "network.hpp"
#include "node_control.hpp"
#include "node_diag.hpp"
#include "node_guard.hpp"
#include "node_store.hpp"
#include "psram_mem.hpp"
#include "task_health.hpp"

namespace armor::node_health {
namespace {
constexpr char kTag[] = "armor-health";
std::atomic<std::uint32_t> g_connects{0};
Options g_options;
std::atomic<bool> g_started{false};

// The wall clock in milliseconds when the node has one; otherwise the time since it started (the server stamps such a message with the time it receives it).
std::uint64_t stamp_ms() {
  const std::time_t now = std::time(nullptr);
  if (now > 1700000000) return static_cast<std::uint64_t>(now) * 1000ULL;
  return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL;
}

nodehealth::Report build(const std::string& node_id, const std::string& family) {
  nodehealth::Report report;
  report.node_id = node_id;
  report.family = family;
  report.firmware = esp_app_get_description()->version;
  const network::Status net = network::status();
  report.board = net.board;
  report.timestamp_ms = stamp_ms();
  report.uptime_s = static_cast<std::uint64_t>(esp_timer_get_time()) / 1000000ULL;
  report.reset_reason = nodehealth::reset_reason_text(static_cast<int>(esp_reset_reason()));
  report.heap_free = esp_get_free_heap_size();
  report.heap_min = esp_get_minimum_free_heap_size();
  report.memory = memory::snapshot();   // the internal memory on its own: the figures above add the external RAM, which hides a short internal memory
  report.has_memory = true;
  report.link = nodehealth::choose_link(net.ethernet_available, net.link_up, net.has_ip, net.sta_connected, net.sta_rssi);
  report.broker_reconnects = g_connects;
  report.low_stack_tasks = health::low_count(taskhealth::tasks());
  std::uint32_t next = 0;
  report.last_error = nodehealth::last_error_of_log(logbuf::read_from(0, next, 8192));
  const node_control::Status ctl = node_control::status();   // a node that is not controlled by the server is always running
  report.has_control = true;
  report.control_state = control::state_name(ctl.state);
  report.control_since_ms = ctl.since_ms;
  report.control_by = ctl.by;
  report.control_until_s = ctl.paused_left_s;
  report.maintenance = ctl.maintenance;
  report.maintenance_until_s = ctl.maintenance_left_s;
  // How the last run ended (said only after a dirty start, for the first minutes), the gate at the door of the panel, the core dump in flash and what the checks of the start found. All of it is
  // kept in memory already: nothing here reads the flash (this task keeps its stack in the external RAM).
  report.has_boot = true;
  report.boot = nodeguard::boot_report();
  report.has_gate = true;
  report.gate = nodeguard::gate_info();
  const node_diag::CoreInfo core = node_diag::core_info();
  report.has_coredump = true;
  report.coredump_present = core.present;
  report.coredump_size = core.size;
  report.has_config_check = true;
  report.config_check = store::config_check();
  return report;
}

void publish_report() {
  if (g_options.publish && (!g_options.connected || g_options.connected())) {
    const std::string topic = nodehealth::topic(g_options.node_id);
    if (!topic.empty()) g_options.publish(topic, nodehealth::to_json(build(g_options.node_id, g_options.family)));
  }
}

void health_task(void*) {
  vTaskDelay(pdMS_TO_TICKS(20000));
  for (;;) {
    publish_report();
    vTaskDelay(pdMS_TO_TICKS(static_cast<std::uint32_t>(g_options.every_s < 10 ? 10 : g_options.every_s) * 1000));
  }
}
}  // namespace

void start(const Options& options) {
  if (!nodehealth::valid_family(options.family) || !armor::node_id_is_valid(options.node_id)) { ESP_LOGW(kTag, "no health report: the node id or the family is not valid"); return; }
  g_options = options;
  g_started = true;
  nodeguard::expect_task("node-health");
  memory::start_task(health_task, "node-health", 4096, nullptr, 2);   // it only reads counters and queues a message: no flash, so its stack can be in the external RAM
}

void note_broker_connected() { ++g_connects; }

void publish_now() {
  if (g_started) publish_report();
}

std::string report_json(const std::string& node_id, const std::string& family) { return nodehealth::to_json(build(node_id, family)); }

}  // namespace armor::node_health
