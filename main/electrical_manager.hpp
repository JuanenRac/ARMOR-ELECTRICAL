// ARMOR-ELECTRICAL - the meters at work: one task opens the serial line, asks the meters one at a time (core/pzem_bus.hpp) and hands the message of the node to the
// broker link. READ ONLY: nothing in this project writes to a meter or switches anything.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "core/electrical_config.hpp"
#include "core/meter_runner.hpp"   // BusStatus and MeterStatus

namespace armor::manager {

using Publish = std::function<void(const std::string& topic, const std::string& payload)>;

// Starts the task when at least one meter is enabled. `publish` gets the node's message (topic, JSON) every round.
void start(const config::Settings& settings, Publish publish);

BusStatus bus();

MeterStatus meter(std::size_t index);

// The last message of the node (JSON), or "" before the first.
std::string last_message();

}  // namespace armor::manager
