// ARMOR-ELECTRICAL - the meters at work: one task opens the serial line, asks the meters one at a time (core/pzem_bus.hpp) and hands the message of the node to the
// broker link. READ ONLY: nothing in this project writes to a meter or switches anything.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "core/electrical_config.hpp"

namespace armor::manager {

using Publish = std::function<void(const std::string& topic, const std::string& payload)>;

// Starts the task when at least one meter is enabled. `publish` gets the node's message (topic, JSON) every round.
void start(const config::Settings& settings, Publish publish);

struct BusStatus {
  std::string state = "disabled";   // disabled, starting, error, running
  std::string error;                // why the line did not open
  int rx = -1, tx = -1, baud = 0, poll_s = 0;
  std::uint32_t bytes_rx = 0, bytes_tx = 0, messages = 0;
};
BusStatus bus();

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
MeterStatus meter(std::size_t index);

// The last message of the node (JSON), or "" before the first.
std::string last_message();

}  // namespace armor::manager
