// ARMOR-NODE-ELECTRICAL - the node's link to the broker: the clock and the messages the node publishes.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstdint>
#include <functional>
#include <string>

#include "core/electrical_config.hpp"

namespace armor::mqtt_link {

// Starts the task that waits for an address, sets the clock (SNTP) and connects to the broker of the settings. Does nothing when the settings have no
// broker (mqtt disabled or no address yet): the node then only serves its panel and shows its readings.
void start(const config::Settings& settings);

using MessageHandler = std::function<void(const std::string& topic, const std::string& payload)>;
using ConnectedHandler = std::function<void()>;
// Asks for the messages that match `filter` (the MQTT rules for + and #). Call it before start(). On every (re)connection of the broker the filter is subscribed again and
// `on_connected` (when there is one) is called; every COMPLETE message that matches goes to `on_message` - a message that arrives in pieces is dropped, as a command is a few bytes.
// The handlers run in the broker client's own task: they must not wait for long.
void subscribe(const std::string& filter, MessageHandler on_message, ConnectedHandler on_connected = nullptr);

bool connected();
bool clock_is_set();
std::uint64_t wall_clock_ms();

// Publishes one message of the node (its topic is the node's own, armor/<family>/<node>/...). Nothing is sent while the broker is not connected. A node whose clock is not set still
// publishes: its readings carry the time since it started (the callers do that), and the server stamps them with the time it receives them.
void publish(const std::string& topic, const std::string& payload);

// Publishes one message retained, at QoS 1: only the planned-maintenance signal (armor/control/<node>/maintenance) is sent so; everything else stays QoS 0 and not retained.
void publish_retained(const std::string& topic, const std::string& payload);

struct Status {
  bool enabled = false;
  bool connected = false;
  bool clock_set = false;
  std::uint32_t published = 0;
  std::uint32_t dropped = 0;   // readings that could not be sent (no broker, or a queue that was full)
};
Status status();

}  // namespace armor::mqtt_link
