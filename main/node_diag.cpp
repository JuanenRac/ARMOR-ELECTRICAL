// ARMOR-ELECTRICAL - the core dump in flash and the diagnostic package. Nothing here has run on a board; what goes into the package is decided in core/diag_package.hpp and tested on a computer.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// Everything that reads flash (the check of the core dump, its summary) is done ONCE, from init(), which the guard's start calls in the main task: the tasks that report (node-health) keep their
// stack in the external RAM and must never touch the flash. The routes that hand the dump out are the panel's, behind its login and its admin role; this file never decides who may ask.
#include "node_diag.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
extern "C" {
#include "esp_app_desc.h"
#include "esp_core_dump.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"
}
#include "core/node_health.hpp"
#include "log_buffer.hpp"
#include "network.hpp"
#include "node_guard.hpp"
#include "node_store.hpp"
#include "psram_mem.hpp"
#include "task_health.hpp"

namespace armor::node_diag {
namespace {
constexpr char kTag[] = "armor-diag";
// A core dump of this node is never bigger than its partition (64 KB); anything above this is not served.
constexpr std::size_t kMaxDumpBytes = 128 * 1024;

std::mutex g_lock;
CoreInfo g_core;
diag::CoreSummary g_summary;
bool g_ready = false;
std::function<std::vector<diag::Sensor>()> g_sensors;

const esp_partition_t* core_partition() { return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr); }

std::string hex_word(std::uint32_t value) {
  char text[16];
  std::snprintf(text, sizeof text, "0x%08x", static_cast<unsigned>(value));
  return text;
}

void read_summary(diag::CoreSummary& out) {
  out = diag::CoreSummary{};
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
  std::unique_ptr<esp_core_dump_summary_t> summary(new (std::nothrow) esp_core_dump_summary_t);
  if (!summary || esp_core_dump_get_summary(summary.get()) != ESP_OK) return;
  out.has_summary = true;
  out.task.assign(summary->exc_task, strnlen(summary->exc_task, sizeof summary->exc_task));
  out.pc = hex_word(summary->exc_pc);
  const std::uint32_t depth = summary->exc_bt_info.depth > 16 ? 16 : summary->exc_bt_info.depth;
  if (!summary->exc_bt_info.corrupted) for (std::uint32_t i = 0; i < depth; ++i) out.backtrace.push_back(hex_word(summary->exc_bt_info.bt[i]));
  out.app_sha256.assign(reinterpret_cast<const char*>(summary->app_elf_sha256), strnlen(reinterpret_cast<const char*>(summary->app_elf_sha256), sizeof summary->app_elf_sha256));
  if (out.app_sha256.size() > 16) out.app_sha256.resize(16);
  char reason[96];
  if (esp_core_dump_get_panic_reason(reason, sizeof reason) == ESP_OK) out.reason.assign(reason, strnlen(reason, sizeof reason));
#endif
}
}  // namespace

void init() {
  std::lock_guard<std::mutex> guard(g_lock);
  if (g_ready) return;
  g_ready = true;
  g_core = CoreInfo{};
  g_summary = diag::CoreSummary{};
  if (core_partition() == nullptr) return;   // a partition table without the partition: nothing to report
  if (esp_core_dump_image_check() != ESP_OK) return;   // none, or a damaged one (the checksum is read through)
  std::size_t address = 0, size = 0;
  if (esp_core_dump_image_get(&address, &size) != ESP_OK || size == 0 || size > kMaxDumpBytes) return;
  g_core.present = true;
  g_core.size = static_cast<std::uint32_t>(size);
  read_summary(g_summary);
  ESP_LOGW(kTag, "a core dump of %u bytes is waiting in flash (task %s)", static_cast<unsigned>(size), g_summary.has_summary ? g_summary.task.c_str() : "unknown");
}

CoreInfo core_info() {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_core;
}

diag::CoreSummary core_summary() {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_summary;
}

std::string core_info_json() {
  const CoreInfo info = core_info();
  json::Writer w;
  w.begin_object().field("present", info.present);
  if (info.present) {
    w.key("size").integer(info.size);
    const diag::CoreSummary summary = core_summary();
    if (summary.has_summary) {
      w.key("summary").begin_object().field("task", summary.task).field("pc", summary.pc);
      if (!summary.reason.empty()) w.field("reason", summary.reason);
      w.key("backtrace").begin_array();
      for (const std::string& address : summary.backtrace) w.string(address);
      w.end_array();
      if (!summary.app_sha256.empty()) w.field("app_sha256", summary.app_sha256);
      w.end_object();
    }
  }
  w.end_object();
  return w.str();
}

esp_err_t send_core_dump(httpd_req_t* request) {
  const esp_partition_t* partition = core_partition();
  std::size_t address = 0, size = 0;
  if (!core_info().present || partition == nullptr || esp_core_dump_image_get(&address, &size) != ESP_OK || size == 0 || size > kMaxDumpBytes) {
    httpd_resp_set_status(request, "404 Not Found");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"error\":\"no_coredump\"}");
  }
  std::size_t offset = address >= partition->address ? address - partition->address : 0;
  if (offset + size > partition->size) { offset = 0; if (size > partition->size) size = partition->size; }
  // The strings given to httpd_resp_set_hdr must live until the response is sent: these are literals.
  httpd_resp_set_type(request, "application/octet-stream");
  httpd_resp_set_hdr(request, "Content-Disposition", "attachment; filename=\"coredump.bin\"");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  std::unique_ptr<std::uint8_t[]> chunk(new (std::nothrow) std::uint8_t[1024]);
  if (!chunk) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
  for (std::size_t done = 0; done < size;) {
    const std::size_t n = size - done > 1024 ? 1024 : size - done;
    if (esp_partition_read(partition, offset + done, chunk.get(), n) != ESP_OK) { httpd_resp_send_chunk(request, nullptr, 0); return ESP_FAIL; }
    if (httpd_resp_send_chunk(request, reinterpret_cast<const char*>(chunk.get()), static_cast<ssize_t>(n)) != ESP_OK) return ESP_FAIL;
    done += n;
  }
  ESP_LOGW(kTag, "the core dump was downloaded by an administrator of the panel");
  return httpd_resp_send_chunk(request, nullptr, 0);
}

bool erase_core_dump() {
  if (!core_info().present) return false;
  const bool ok = esp_core_dump_image_erase() == ESP_OK;
  if (ok) {
    std::lock_guard<std::mutex> guard(g_lock);
    g_core = CoreInfo{};
    g_summary = diag::CoreSummary{};
    ESP_LOGW(kTag, "the core dump was erased by an administrator of the panel");
  }
  return ok;
}

void set_sensor_versions(std::function<std::vector<diag::Sensor>()> provider) {
  std::lock_guard<std::mutex> guard(g_lock);
  g_sensors = std::move(provider);
}

std::string build_package(diag::Detail detail) {
  diag::Inputs in;
  in.firmware = esp_app_get_description()->version;
  in.idf = esp_get_idf_version();
  const network::Status net = network::status();
  in.board = net.board;
  in.uptime_s = static_cast<std::uint64_t>(esp_timer_get_time()) / 1000000ULL;
  in.reset_reason = nodehealth::reset_reason_text(static_cast<int>(esp_reset_reason()));
  in.guard_restarts_in_hour = nodeguard::restarts_in_hour();
  in.memory = memory::snapshot();
  in.heap_total_free = esp_get_free_heap_size();
  in.heap_total_min = esp_get_minimum_free_heap_size();
  in.tasks = taskhealth::tasks();
  const bootreport::Report boot = nodeguard::boot_report();
  in.has_boot = boot.dirty;
  in.boot = boot;
  in.has_gate = true;
  in.gate = nodeguard::gate_info();
  std::function<std::vector<diag::Sensor>()> sensors;
  {
    std::lock_guard<std::mutex> guard(g_lock);
    sensors = g_sensors;
    in.coredump_present = g_core.present;
    in.coredump_size = g_core.size;
    in.core = g_summary;
  }
  if (sensors) in.sensors = sensors();
  in.full_settings_json = store::settings_json_full();
  std::uint32_t next = 0;
  in.log = logbuf::read_from(0, next, 8192);
  return diag::package_json(in, detail);
}

}  // namespace armor::node_diag
