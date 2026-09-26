// ARMOR-ELECTRICAL - the meters at work, without the hardware: what the node does on every turn of its loop. It asks the meters one at a time (core/pzem_bus.hpp), feeds the bus
// what arrives, keeps the state of the line and of each meter as the panel shows it, and every round makes the message of the node. The serial line and the clock are handed
// in, so the firmware gives it a UART and the tests give it a stand-in that plays the meters. READ ONLY: nothing here writes to a meter or switches anything.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
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
  int rx = -1, tx = -1, baud = 0, poll_s = 0;
  std::uint32_t bytes_rx = 0, bytes_tx = 0, messages = 0;
};

struct MeterStatus {
  int number = 0;             // 1 to 16
  bool enabled = false;
  std::string channel, label, model;
  int address = 0;
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

  explicit Runner(const config::Settings& settings) : settings_(settings) {
    bus_.rx = settings.bus.rx; bus_.tx = settings.bus.tx; bus_.baud = settings.bus.baud; bus_.poll_s = settings.bus.poll_s;
    std::vector<electrical::MeterConfig> meters;
    for (std::size_t i = 0; i < config::kMeterCount; ++i) {
      const config::MeterSetting& setting = settings.meters[i];
      MeterStatus& status = meters_[i];
      status.number = static_cast<int>(i + 1); status.enabled = setting.enabled; status.channel = setting.channel; status.label = setting.label;
      status.model = setting.model; status.address = setting.address;
      status.state = setting.enabled ? "waiting" : "disabled";
      if (!setting.enabled) continue;
      electrical::MeterConfig meter;
      meter.channel = setting.channel; meter.label = setting.label;
      meter.model = setting.model == "dc" ? electrical::pzem::Model::kDc : electrical::pzem::Model::kAc;
      meter.address = static_cast<std::uint8_t>(setting.address);
      meters.push_back(meter); slots_.push_back(i);
    }
    bus_.state = slots_.empty() ? "disabled" : "starting";
    bus_engine_ = std::make_unique<electrical::PzemBus>(std::move(meters), static_cast<std::uint64_t>(settings.bus.poll_s) * 1000ULL);
  }

  bool any_meter() const { return !slots_.empty(); }
  void set_line_open() { bus_.state = "running"; }
  void set_line_failed(const std::string& why) { bus_.state = "error"; bus_.error = why; }

  struct Outcome {
    bool statuses = false;   // the states of the line and of the meters were refreshed
    bool message = false;    // a round is done: message() is new
  };

  // One turn: send the next request if one is due, take what arrives (waiting at most `wait_ms` for it), refresh the states once a second and make the message once a round.
  Outcome step(uartbus::Line& line, const Clock& clock, unsigned wait_ms = 20) {
    Outcome outcome;
    const std::uint64_t now = clock.uptime_ms();
    const std::vector<std::uint8_t> request = bus_engine_->next_tx(now);
    if (!request.empty()) {
      if (line.write(request.data(), request.size())) bus_.bytes_tx += static_cast<std::uint32_t>(request.size());
      else ++write_failures_;
    }
    const std::size_t n = line.read(buffer_, sizeof buffer_, wait_ms);
    if (n > 0) { bus_engine_->on_rx(buffer_, n, clock.uptime_ms()); bus_.bytes_rx += static_cast<std::uint32_t>(n); }
    const std::uint64_t after = clock.uptime_ms();
    if (after - last_status_ms_ >= kStatusEveryMs) {
      last_status_ms_ = after;
      refresh(after);
      outcome.statuses = true;
    }
    if (after - last_publish_ms_ >= static_cast<std::uint64_t>(settings_.bus.poll_s) * 1000ULL) {
      last_publish_ms_ = after;
      const std::uint64_t stamp = clock.wall_clock_is_set() ? clock.wall_clock_ms() : after;
      message_ = electrical::message_json(settings_.node_id, stamp, false, bus_engine_->fresh(after));
      ++bus_.messages;
      outcome.message = true;
    }
    return outcome;
  }

  const BusStatus& bus() const { return bus_; }
  const std::array<MeterStatus, config::kMeterCount>& meters() const { return meters_; }
  const std::string& message() const { return message_; }
  std::uint32_t write_failures() const { return write_failures_; }

 private:
  void refresh(std::uint64_t now) {
    for (std::size_t k = 0; k < slots_.size(); ++k) {
      const electrical::MeterState& state = bus_engine_->meters()[k];
      MeterStatus& s = meters_[slots_[k]];
      s.replies_ok = state.replies_ok; s.replies_bad = state.replies_bad; s.timeouts = state.timeouts;
      const bool fresh = state.have_reading && now - state.reading_at_ms <= electrical::PzemBus::kFreshMs;
      s.have_reading = fresh;
      if (fresh) { s.voltage_v = state.reading.voltage_v; s.current_a = state.reading.current_a; s.power_w = state.reading.power_w; s.energy_kwh = state.reading.energy_kwh; s.alarm = state.reading.alarm; }
      s.error = fresh ? "" : result_text(state.last_result);
      // A meter that has no fresh reading and whose last exchange failed is silent (nothing came) or garbled (something came that was no valid reply), even if it worked before:
      // "waiting" is only for a meter not asked yet.
      s.state = fresh ? "reading" : state.misses_in_a_row > 0 ? (state.last_result == electrical::pzem::Result::kTooShort ? "silent" : "garbled") : "waiting";
    }
  }

  config::Settings settings_;
  BusStatus bus_;
  std::array<MeterStatus, config::kMeterCount> meters_;
  std::vector<std::size_t> slots_;   // the panel's number of each meter of the bus
  std::unique_ptr<electrical::PzemBus> bus_engine_;
  std::uint8_t buffer_[64] = {};
  std::string message_;
  std::uint64_t last_publish_ms_ = 0, last_status_ms_ = 0;
  std::uint32_t write_failures_ = 0;
};

}  // namespace armor::manager
