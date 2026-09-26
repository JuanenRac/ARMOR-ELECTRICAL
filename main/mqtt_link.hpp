// ARMOR-ELECTRICAL - the node's link to the broker: the clock and the messages the node publishes.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstdint>
#include <string>

#include "core/electrical_config.hpp"

namespace armor::mqtt_link {

// Starts the task that waits for an address, sets the clock (SNTP) and connects to the broker of the settings. Does nothing when the settings have no
// broker (mqtt disabled or no address yet): the node then only serves its panel and shows its readings.
void start(const config::Settings& settings);

bool connected();
bool clock_is_set();
std::uint64_t wall_clock_ms();

// Publishes one message of the node (its topic is the node's own, armor/<family>/<node>/...). Nothing is sent while the broker is not connected or the clock is not set: the server
// ignores a message older than the last one it accepted, so a timestamp from an unset clock must never leave the node.
void publish(const std::string& topic, const std::string& payload);

struct Status {
  bool enabled = false;
  bool connected = false;
  bool clock_set = false;
  std::uint32_t published = 0;
  std::uint32_t dropped = 0;   // readings that could not be sent (no broker, no clock)
};
Status status();

}  // namespace armor::mqtt_link
