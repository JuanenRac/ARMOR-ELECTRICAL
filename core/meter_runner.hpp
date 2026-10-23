// ARMOR-NODE-ELECTRICAL - the meters at work, without the hardware: what the node does on every turn of its loop. It asks the meters one at a time (core/pzem_bus.hpp), feeds the bus
// what arrives, keeps the state of the line and of each meter as the panel shows it, and every round makes the message of the node. The serial line and the clock are handed
// in, so the firmware gives it a UART and the tests give it a stand-in that plays the meters. READ ONLY: nothing here writes to a meter or switches anything.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "electrical_config.hpp"
#include "electrical_json.hpp"
#include "pzem_bus.hpp"
#include "serial_line.hpp"

namespace armor::manager {

struct BusStatus {
  std::string state = "disabled";   // disabled, starting, error, running
  std::string error;                // why the line did not open
  int rx = -1, tx = -1, de = -1, baud = 0, poll_s = 0;
  std::uint32_t bytes_rx = 0, bytes_tx = 0, messages = 0;
};

struct MeterStatus {
  int number = 0;             // 1 to 16
  bool enabled = false;
  std::string channel, label, model;
  int address = 0;
  int line = 1;
  std::string state = "disabled";   // disabled, waiting, reading, silent, garbled
  std::string error;                // the last result of an exchange, when it was not good
  std::uint32_t replies_ok = 0, replies_bad = 0, timeouts = 0;
  bool have_reading = false;
  double voltage_v = 0, current_a = 0, power_w = 0, energy_kwh = 0;
  bool alarm = false;
};

// The time the node lives by: milliseconds since it started, and its wall clock when it has been set (the message carries the wall clock; until then it only serves the panel).
struct Clock {
  std::function<std::uint64_t()> uptime_ms;
  std::function<bool()> wall_clock_is_set;
  std::function<std::uint64_t()> wall_clock_ms;
};

inline const char* result_text(electrical::pzem::Result result) {
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

class Runner {
 public:
  static constexpr std::uint64_t kStatusEveryMs = 1000;
  static constexpr int kLines = 2;

  explicit Runner(const config::Settings& settings) : settings_(settings) {
    std::array<std::vector<electrical::MeterConfig>, kLines> meters;
    for (std::size_t i = 0; i < config::kMeterCount; ++i) {
      const config::MeterSetting& setting = settings.meters[i];
      MeterStatus& status = meters_[i];
      status.number = static_cast<int>(i + 1); status.enabled = setting.enabled; status.channel = setting.channel; status.label = setting.label;
      status.model = setting.model; status.address = setting.address; status.line = setting.line == 2 ? 2 : 1;
      status.state = setting.enabled ? "waiting" : "disabled";
      if (!setting.enabled) continue;
      electrical::MeterConfig meter;
      meter.channel = setting.channel; meter.label = setting.label;
      meter.model = setting.model == "dc" ? electrical::pzem::Model::kDc : electrical::pzem::Model::kAc;
      meter.address = static_cast<std::uint8_t>(setting.address);
      const int line = setting.line == 2 ? 1 : 0;
      meters[line].push_back(meter); slots_[line].push_back(i);
    }
    const std::array<const config::BusSetting*, kLines> pins{&settings.bus, &settings.bus2};
    for (int k = 0; k < kLines; ++k) {
      bus_[k].rx = pins[k]->rx; bus_[k].tx = pins[k]->tx; bus_[k].de = pins[k]->de; bus_[k].baud = pins[k]->baud; bus_[k].poll_s = settings.bus.poll_s;
      bus_[k].state = slots_[k].empty() ? "disabled" : "starting";
      engine_[k] = std::make_unique<electrical::PzemBus>(std::move(meters[k]), static_cast<std::uint64_t>(settings.bus.poll_s) * 1000ULL);
    }
  }

  bool any_meter() const { return !slots_[0].empty() || !slots_[1].empty(); }
  bool line_in_use(int line) const { return line >= 0 && line < kLines && !slots_[line].empty(); }
  // The node's own analog inputs: asked for at every round, and their channels go after the meters' in the message.
  void set_analog_provider(std::function<std::vector<electrical::AnalogChannel>()> provider) { analog_ = std::move(provider); }
  void set_line_open(int line = 0) { if (line_in_use(line)) bus_[line].state = "running"; }
  void set_line_failed(const std::string& why, int line = 0) { if (line_in_use(line)) { bus_[line].state = "error"; bus_[line].error = why; } }

  struct Outcome {
    bool statuses = false;   // the states of the lines and of the meters were refreshed
    bool message = false;    // a round is done: message() is new
  };

  // One turn on the first line (and the second one, when it has a line to talk on): send the next request of each if one is due, take what arrives (waiting at most `wait_ms` in all),
  // refresh the states once a second and make the message once a round.
  Outcome step(uartbus::Line& line, const Clock& clock, unsigned wait_ms = 20) { return step(line, nullptr, clock, wait_ms); }
  Outcome step(uartbus::Line& first, uartbus::Line* second, const Clock& clock, unsigned wait_ms = 20) {
    Outcome outcome;
    uartbus::Line* lines[kLines] = {&first, second};
    const unsigned each = second != nullptr ? std::max(1u, wait_ms / 2) : wait_ms;
    for (int k = 0; k < kLines; ++k) {
      if (lines[k] == nullptr) continue;
      const std::vector<std::uint8_t> request = engine_[k]->next_tx(clock.uptime_ms());
      if (!request.empty()) {
        if (lines[k]->write(request.data(), request.size())) bus_[k].bytes_tx += static_cast<std::uint32_t>(request.size());
        else ++write_failures_;
      }
      const std::size_t n = lines[k]->read(buffer_, sizeof buffer_, each);
      if (n > 0) { engine_[k]->on_rx(buffer_, n, clock.uptime_ms()); bus_[k].bytes_rx += static_cast<std::uint32_t>(n); }
    }
    const std::uint64_t after = clock.uptime_ms();
    if (after - last_status_ms_ >= kStatusEveryMs) {
      last_status_ms_ = after;
      refresh(after);
      outcome.statuses = true;
    }
    if (after - last_publish_ms_ >= static_cast<std::uint64_t>(settings_.bus.poll_s) * 1000ULL) {
      last_publish_ms_ = after;
      const std::uint64_t stamp = clock.wall_clock_is_set() ? clock.wall_clock_ms() : after;
      std::vector<const electrical::MeterState*> fresh = engine_[0]->fresh(after);
      for (const electrical::MeterState* state : engine_[1]->fresh(after)) fresh.push_back(state);
      message_ = electrical::message_json(settings_.node_id, stamp, false, fresh, {}, "", analog_ ? analog_() : std::vector<electrical::AnalogChannel>{}, after);
      for (int k = 0; k < kLines; ++k) if (!slots_[k].empty()) ++bus_[k].messages;
      outcome.message = true;
    }
    return outcome;
  }

  const BusStatus& bus(int line = 0) const { return bus_[line == 1 ? 1 : 0]; }
  const std::array<MeterStatus, config::kMeterCount>& meters() const { return meters_; }
  const std::string& message() const { return message_; }
  std::uint32_t write_failures() const { return write_failures_; }

 private:
  void refresh(std::uint64_t now) {
    for (int k = 0; k < kLines; ++k) {
      for (std::size_t j = 0; j < slots_[k].size(); ++j) {
        const electrical::MeterState& state = engine_[k]->meters()[j];
        MeterStatus& s = meters_[slots_[k][j]];
        s.replies_ok = state.replies_ok; s.replies_bad = state.replies_bad; s.timeouts = state.timeouts;
        const bool fresh = state.have_reading && now - state.reading_at_ms <= engine_[k]->freshness_ms();
        s.have_reading = fresh;
        if (fresh) { s.voltage_v = state.reading.voltage_v; s.current_a = state.reading.current_a; s.power_w = state.reading.power_w; s.energy_kwh = state.reading.energy_kwh; s.alarm = state.reading.alarm; }
        s.error = fresh ? "" : result_text(state.last_result);
        // A meter that has no fresh reading and whose last exchange failed is silent (nothing came) or garbled (something came that was no valid reply), even if it worked before:
        // "waiting" is only for a meter not asked yet.
        s.state = fresh ? "reading" : state.misses_in_a_row > 0 ? (state.last_result == electrical::pzem::Result::kTooShort ? "silent" : "garbled") : "waiting";
      }
    }
  }

  config::Settings settings_;
  std::array<BusStatus, kLines> bus_;
  std::array<MeterStatus, config::kMeterCount> meters_;
  std::array<std::vector<std::size_t>, kLines> slots_;   // the panel's number of each meter of each line
  std::function<std::vector<electrical::AnalogChannel>()> analog_;
  std::array<std::unique_ptr<electrical::PzemBus>, kLines> engine_;
  std::uint8_t buffer_[64] = {};
  std::string message_;
  std::uint64_t last_publish_ms_ = 0, last_status_ms_ = 0;
  std::uint32_t write_failures_ = 0;
};

}  // namespace armor::manager
