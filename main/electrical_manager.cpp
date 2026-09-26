// ARMOR-ELECTRICAL - the meters at work: the task that runs core/meter_runner.hpp on the real serial line.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// What the node does on every turn (asking the meters, the states, the message) is in core/meter_runner.hpp and is tested on a computer; this file only opens the UART, gives the
// runner the node's clock and keeps a copy of its states for the panel. A line that cannot be opened says why (pins, speed, driver). Nothing here has run on a board, and nothing
// here writes to a meter.
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
std::unique_ptr<Runner> g_runner;   // only the task touches it once it runs; the panel reads the copies above

std::uint64_t uptime_ms() { return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL; }

void bus_task(void*) {
  std::string error;
  std::unique_ptr<uartbus::Line> line = uartbus::open(g_settings.bus.rx, g_settings.bus.tx, g_settings.bus.baud, error);
  if (!line) {
    ESP_LOGE(kTag, "the serial line could not be opened (%s): the meters are not read", error.c_str());
    g_runner->set_line_failed(error);
    { std::lock_guard<std::mutex> guard(g_lock); g_bus = g_runner->bus(); }
    vTaskDelete(nullptr);
    return;
  }
  g_runner->set_line_open();
  { std::lock_guard<std::mutex> guard(g_lock); g_bus = g_runner->bus(); }
  Clock clock;
  clock.uptime_ms = uptime_ms;
  clock.wall_clock_is_set = [] { return mqtt_link::clock_is_set(); };
  clock.wall_clock_ms = [] { return mqtt_link::wall_clock_ms(); };
  std::uint32_t reported_failures = 0;
  for (;;) {
    const Runner::Outcome outcome = g_runner->step(*line, clock);
    if (g_runner->write_failures() != reported_failures) { reported_failures = g_runner->write_failures(); ESP_LOGW(kTag, "a request could not be sent"); }
    if (outcome.statuses || outcome.message) {
      std::lock_guard<std::mutex> guard(g_lock);
      g_bus = g_runner->bus();
      g_meters = g_runner->meters();
      if (outcome.message) g_last_message = g_runner->message();
    }
    if (outcome.message && g_publish) g_publish(electrical::topic(g_settings.node_id), g_runner->message());
  }
}
}  // namespace

void start(const config::Settings& settings, Publish publish) {
  g_publish = std::move(publish);
  g_settings = settings;
  g_runner = std::make_unique<Runner>(settings);
  {
    std::lock_guard<std::mutex> guard(g_lock);
    g_bus = g_runner->bus();
    g_meters = g_runner->meters();
  }
  if (g_runner->any_meter()) xTaskCreate(bus_task, "meters", 6144, nullptr, 5, nullptr);
}

BusStatus bus() { std::lock_guard<std::mutex> guard(g_lock); return g_bus; }
MeterStatus meter(std::size_t index) { if (index >= config::kMeterCount) return {}; std::lock_guard<std::mutex> guard(g_lock); return g_meters[index]; }
std::string last_message() { std::lock_guard<std::mutex> guard(g_lock); return g_last_message; }

}  // namespace armor::manager
