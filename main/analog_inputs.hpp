// ARMOR-ELECTRICAL - the analog inputs of the base board: ADC1 pins read into channels of the node's message.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/board_s3.hpp"
#include "core/electrical_config.hpp"
#include "core/electrical_json.hpp"

namespace armor::analoginputs {

struct InputStatus {
  bool enabled = false;
  std::string state = "disabled";   // disabled, waiting (not read yet), reading, error
  std::string error;                // "adc": the pin could not be set up
  double millivolts = 0;            // the average at the pin, for the panel (the sensor's wiring is checked with it)
  double value = 0;                 // the quantity in its unit
  std::uint32_t samples = 0;        // how many samples the last reading took
};

// Starts the task when at least one input is enabled.
void start(const config::Settings& settings);

// The channels of the inputs with a fresh reading, for the node's message.
std::vector<electrical::AnalogChannel> channels();

std::array<InputStatus, board::kAnalogCount> status();

}  // namespace armor::analoginputs
