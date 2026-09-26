// ARMOR-ELECTRICAL - the meters at work.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// One task: it opens the serial line, asks the meters one at a time whenever the bus says so, feeds the bus what arrives and, every round, hands the node's message to the
// broker link. A line that cannot be opened says why (pins, speed, driver). Nothing here has run on a board, and nothing here writes to a meter.
#include "electrical_manager.hpp"

#include <array>
#include <memory>
#include <mutex>
extern "C" {
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}
#include "core/electrical_json.hpp"
#include "mqtt_link.hpp"
#include "uart_bus.hpp"

namespace armor::manager {
namespace {
constexpr char kTag[] = "armor-meters";

std::mutex g_lock;
BusStatus g_bus;
std::array<MeterStatus, config::kMeterCount> g_meters;
std::string g_last_message;
config::Settings g_settings;
Publish g_publish;

std::uint64_t uptime_ms() { return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL; }

const char* result_text(electrical::pzem::Result result) {
  using electrical::pzem::Result;
  switch (result) {
    case Result::kOk: return "";
    case Result::kTooShort: return "silent";
    case Result::kCrc: return "crc";
    case Result::kAddress: return "address";
    case Result::kException: return "exception";
    case Result::kFunction: return "function";
    case Result::kLength: return "length";
    case Result::kRange: return "range";
  }
  return "?";
}

void bus_task(void*) {
  const config::Settings settings = g_settings;
  std::string error;
  std::unique_ptr<uartbus::Line> line = uartbus::open(settings.bus.rx, settings.bus.tx, settings.bus.baud, error);
  if (!line) {
    ESP_LOGE(kTag, "the serial line could not be opened (%s): the meters are not read", error.c_str());
    std::lock_guard<std::mutex> guard(g_lock);
    g_bus.state = "error"; g_bus.error = error;
    vTaskDelete(nullptr);
    return;
  }
  { std::lock_guard<std::mutex> guard(g_lock); g_bus.state = "running"; }
  // on the heap: the bus keeps its meters and its buffer
  std::vector<electrical::MeterConfig> meters;
  std::vector<std::size_t> slots;          // the panel's number of each meter of the bus
  for (std::size_t i = 0; i < config::kMeterCount; ++i) {
    const config::MeterSetting& setting = settings.meters[i];
    if (!setting.enabled) continue;
    electrical::MeterConfig meter;
    meter.channel = setting.channel; meter.label = setting.label;
    meter.model = setting.model == "dc" ? electrical::pzem::Model::kDc : electrical::pzem::Model::kAc;
    meter.address = static_cast<std::uint8_t>(setting.address);
    meters.push_back(meter); slots.push_back(i);
  }
  const auto bus = std::make_unique<electrical::PzemBus>(meters, static_cast<std::uint64_t>(settings.bus.poll_s) * 1000ULL);
  std::uint8_t buffer[64];
  std::uint64_t last_publish_ms = 0, last_status_ms = 0;
  for (;;) {
    const std::uint64_t now = uptime_ms();
    const std::vector<std::uint8_t> request = bus->next_tx(now);
    if (!request.empty()) {
      if (line->write(request.data(), request.size())) { std::lock_guard<std::mutex> guard(g_lock); g_bus.bytes_tx += static_cast<std::uint32_t>(request.size()); }
      else ESP_LOGW(kTag, "a request could not be sent");
    }
    const std::size_t n = line->read(buffer, sizeof buffer, 20);
    if (n > 0) { bus->on_rx(buffer, n, uptime_ms()); std::lock_guard<std::mutex> guard(g_lock); g_bus.bytes_rx += static_cast<std::uint32_t>(n); }
    const std::uint64_t after = uptime_ms();
    if (after - last_status_ms >= 1000) {
      last_status_ms = after;
      std::lock_guard<std::mutex> guard(g_lock);
      for (std::size_t k = 0; k < slots.size(); ++k) {
        const electrical::MeterState& state = bus->meters()[k];
        MeterStatus& s = g_meters[slots[k]];
        s.replies_ok = state.replies_ok; s.replies_bad = state.replies_bad; s.timeouts = state.timeouts;
        const bool fresh = state.have_reading && after - state.reading_at_ms <= electrical::PzemBus::kFreshMs;
        s.have_reading = fresh;
        if (fresh) { s.voltage_v = state.reading.voltage_v; s.current_a = state.reading.current_a; s.power_w = state.reading.power_w; s.energy_kwh = state.reading.energy_kwh; s.alarm = state.reading.alarm; }
        s.error = fresh ? "" : result_text(state.last_result);
        s.state = fresh ? "reading" : state.replies_bad > 0 && state.replies_ok == 0 ? "garbled" : state.timeouts > 0 && state.replies_ok == 0 ? "silent" : "waiting";
      }
    }
    if (after - last_publish_ms >= static_cast<std::uint64_t>(settings.bus.poll_s) * 1000ULL) {
      last_publish_ms = after;
      // The message carries the node's wall clock; while the clock is not set it is only for the panel (the broker link drops it).
      const std::string payload = electrical::message_json(settings.node_id, mqtt_link::clock_is_set() ? mqtt_link::wall_clock_ms() : after, false, bus->fresh(after));
      { std::lock_guard<std::mutex> guard(g_lock); g_last_message = payload; ++g_bus.messages; }
      if (g_publish) g_publish(electrical::topic(settings.node_id), payload);
    }
  }
}
}  // namespace

void start(const config::Settings& settings, Publish publish) {
  g_publish = std::move(publish);
  g_settings = settings;
  bool any = false;
  {
    std::lock_guard<std::mutex> guard(g_lock);
    g_bus.rx = settings.bus.rx; g_bus.tx = settings.bus.tx; g_bus.baud = settings.bus.baud; g_bus.poll_s = settings.bus.poll_s;
    for (std::size_t i = 0; i < config::kMeterCount; ++i) {
      const config::MeterSetting& m = settings.meters[i];
      MeterStatus& s = g_meters[i];
      s.number = static_cast<int>(i + 1); s.enabled = m.enabled; s.channel = m.channel; s.label = m.label; s.model = m.model; s.address = m.address;
      s.state = m.enabled ? "waiting" : "disabled";
      any = any || m.enabled;
    }
    g_bus.state = any ? "starting" : "disabled";
  }
  if (any) xTaskCreate(bus_task, "meters", 6144, nullptr, 5, nullptr);
}

BusStatus bus() { std::lock_guard<std::mutex> guard(g_lock); return g_bus; }
MeterStatus meter(std::size_t index) { if (index >= config::kMeterCount) return {}; std::lock_guard<std::mutex> guard(g_lock); return g_meters[index]; }
std::string last_message() { std::lock_guard<std::mutex> guard(g_lock); return g_last_message; }

}  // namespace armor::manager
