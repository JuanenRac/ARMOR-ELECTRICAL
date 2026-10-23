// ARMOR-NODE-ELECTRICAL - the analog inputs of the base board: what the ADC saw on a pin, made into a channel of the node's message.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The firmware reads a pin many times (main/analog_inputs.cpp) and gives the samples, in millivolts, to this file. Nothing here touches hardware, so all of it is tested on a computer.
//   "dc": the average of the samples, minus the volts the sensor shows at zero, times the scale (a divider, or the amps a sensor gives per volt).
//   "ac": the effective (RMS) value of the samples around their own average, times the scale. Meant for a transformer or a clamp: a measure of the size of an alternating signal.
// A voltage is published as `voltage_v`; a current as `current_a`, and with a nominal voltage also as a power and an energy (the current times the voltage the circuit is assumed to have).
// The message's limits (a voltage is never negative, nothing leaves the contract's range) are kept here, so a wrong scale can never produce a message the server refuses.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "electrical_config.hpp"
#include "electrical_json.hpp"

namespace armor::analog {

inline double mean(const std::vector<double>& samples) {
  if (samples.empty()) return 0.0;
  double sum = 0.0;
  for (const double v : samples) sum += v;
  return sum / static_cast<double>(samples.size());
}

// The effective value of the signal around its own average.
inline double rms_about_mean(const std::vector<double>& samples) {
  if (samples.empty()) return 0.0;
  const double centre = mean(samples);
  double sum = 0.0;
  for (const double v : samples) sum += (v - centre) * (v - centre);
  return std::sqrt(sum / static_cast<double>(samples.size()));
}

// The quantity an input measures, in volts or amps, from samples in millivolts.
inline double quantity_of(const config::AnalogSetting& input, const std::vector<double>& millivolts) {
  double value = 0.0;
  if (input.mode == "ac") value = rms_about_mean(millivolts) / 1000.0 * std::fabs(input.scale);
  else value = (mean(millivolts) / 1000.0 - input.offset_v) * input.scale;
  if (std::fabs(value) < input.zero_below) value = 0.0;
  return value;
}

// The energy a current input has made at its nominal voltage since the node started: it only grows.
class Energy {
 public:
  void add(double power_w, std::uint64_t milliseconds) {
    if (power_w > 0.0) kwh_ += power_w * static_cast<double>(milliseconds) / 3.6e9;
  }
  double kwh() const { return kwh_; }
 private:
  double kwh_ = 0.0;
};

inline double clamp(double value, double lowest, double highest) { return std::min(highest, std::max(lowest, value)); }

// One input's channel. `elapsed_ms` is the time since the last reading (for the energy). A set of samples that is empty is no reading: the channel is not produced.
inline bool make_channel(const config::AnalogSetting& input, const std::vector<double>& millivolts, std::uint64_t elapsed_ms, Energy& energy, electrical::AnalogChannel& out) {
  if (millivolts.empty()) return false;
  const double value = quantity_of(input, millivolts);
  out = electrical::AnalogChannel{};
  out.id = input.channel;
  out.label = input.label;
  out.ac = input.mode == "ac";
  if (input.quantity == "voltage") {
    out.has_voltage = true;
    out.voltage_v = clamp(value, 0.0, 1000.0);
  } else {
    out.has_current = true;
    out.current_a = clamp(value, -1000.0, 1000.0);
    if (input.nominal_voltage_v > 0.0) {
      const double power = out.current_a * input.nominal_voltage_v;
      energy.add(power, elapsed_ms);
      out.has_power = true;
      out.power_estimated = true;      // current times a nominal voltage: apparent power taken as watts
      out.power_w = clamp(power, -1000000.0, 1000000.0);
      out.has_energy = true;
      out.energy_kwh = clamp(energy.kwh(), 0.0, 10000000.0);
    }
  }
  return true;
}

}  // namespace armor::analog
