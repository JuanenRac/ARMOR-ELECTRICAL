// ARMOR-ELECTRICAL - the meters at work: the task that runs core/meter_runner.hpp on the real serial line (and, with no meter, only on the analog inputs).
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
#include "analog_inputs.hpp"
#include "mqtt_link.hpp"
#include "uart_bus.hpp"

namespace armor::manager {
namespace {
constexpr char kTag[] = "armor-meters";

std::mutex g_lock;
std::array<BusStatus, 2> g_bus;
std::array<MeterStatus, config::kMeterCount> g_meters;
std::string g_last_message;
config::Settings g_settings;
Publish g_publish;
std::unique_ptr<Runner> g_runner;   // only the task touches it once it runs; the panel reads the copies above

std::uint64_t uptime_ms() { return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL; }

// A node whose only inputs are its analog pins has no meters and no serial line: the loop still turns (and makes the message) with a line that never has anything to say.
class NoLine final : public uartbus::Line {
 public:
  std::size_t read(std::uint8_t*, std::size_t, unsigned wait_ms) override { vTaskDelay(pdMS_TO_TICKS(wait_ms)); return 0; }
  bool write(const std::uint8_t*, std::size_t) override { return true; }
};

void copy_buses() { g_bus[0] = g_runner->bus(0); g_bus[1] = g_runner->bus(1); }

void bus_task(void*) {
  std::string error;
  std::unique_ptr<uartbus::Line> line, second;
  if (g_runner->line_in_use(0)) line = uartbus::open(0, g_settings.bus.rx, g_settings.bus.tx, g_settings.bus.de, g_settings.bus.baud, error);
  else line = std::make_unique<NoLine>();
  if (!line) {
    ESP_LOGE(kTag, "the first serial line could not be opened (%s): its meters are not read", error.c_str());
    g_runner->set_line_failed(error, 0);
    line = std::make_unique<NoLine>();   // the other line and the analog inputs go on
  } else if (g_runner->line_in_use(0)) g_runner->set_line_open(0);
  if (g_runner->line_in_use(1)) {
    second = uartbus::open(1, g_settings.bus2.rx, g_settings.bus2.tx, g_settings.bus2.de, g_settings.bus2.baud, error);
    if (!second) { ESP_LOGE(kTag, "the second serial line could not be opened (%s): its meters are not read", error.c_str()); g_runner->set_line_failed(error, 1); }
    else g_runner->set_line_open(1);
  }
  { std::lock_guard<std::mutex> guard(g_lock); copy_buses(); }
  Clock clock;
  clock.uptime_ms = uptime_ms;
  clock.wall_clock_is_set = [] { return mqtt_link::clock_is_set(); };
  clock.wall_clock_ms = [] { return mqtt_link::wall_clock_ms(); };
  std::uint32_t reported_failures = 0;
  bool reported_no_publisher = false;
  for (;;) {
    const Runner::Outcome outcome = g_runner->step(*line, second.get(), clock);
    if (g_runner->write_failures() != reported_failures) { reported_failures = g_runner->write_failures(); ESP_LOGW(kTag, "a request could not be sent"); }
    if (outcome.statuses || outcome.message) {
      std::lock_guard<std::mutex> guard(g_lock);
      copy_buses();
      g_meters = g_runner->meters();
      if (outcome.message) g_last_message = g_runner->message();
    }
    if (outcome.message && g_publish) {
      g_publish(electrical::topic(g_settings.node_id), g_runner->message());
    } else if (outcome.message && !reported_no_publisher) {
      reported_no_publisher = true;   // said once: the readings are being taken but nothing can carry them to the broker
      ESP_LOGW(kTag, "the readings are not being published: the broker link has not been given to the meters");
    }
  }
}
}  // namespace

void start(const config::Settings& settings, Publish publish) {
  g_publish = std::move(publish);
  g_settings = settings;
  g_runner = std::make_unique<Runner>(settings);
  {
    std::lock_guard<std::mutex> guard(g_lock);
    copy_buses();
    g_meters = g_runner->meters();
  }
  g_runner->set_analog_provider([] { return analoginputs::channels(); });
  bool any_analog = false;
  for (const config::AnalogSetting& input : settings.analogs) any_analog = any_analog || input.enabled;
  if (g_runner->any_meter() || any_analog) xTaskCreate(bus_task, "meters", 6144, nullptr, 5, nullptr);
}

BusStatus bus(int line) { std::lock_guard<std::mutex> guard(g_lock); return g_bus[line == 1 ? 1 : 0]; }
MeterStatus meter(std::size_t index) { if (index >= config::kMeterCount) return {}; std::lock_guard<std::mutex> guard(g_lock); return g_meters[index]; }
std::string last_message() { std::lock_guard<std::mutex> guard(g_lock); return g_last_message; }

}  // namespace armor::manager
