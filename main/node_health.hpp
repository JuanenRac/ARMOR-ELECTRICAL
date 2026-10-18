// ARMOR-ELECTRICAL - tells the server how the node is doing (core/node_health.hpp): uptime, why it restarted, memory, link, broker reconnects, the last error.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <functional>
#include <string>

namespace armor::node_health {

struct Options {
  std::string node_id;
  std::string family;   // radar, solar, electrical, alarm or hmi
  // Hands one message to the broker (the node's mqtt_link::publish) and says whether the broker is connected (mqtt_link::connected).
  std::function<void(const std::string& topic, const std::string& payload)> publish;
  std::function<bool()> connected;
  unsigned every_s = 60;
};

// Starts the task that publishes the report: the first one about 20 s after the start, then every `every_s` seconds while the broker is connected. Call it after the network and the broker link are up.
void start(const Options& options);

// The mqtt link calls this each time the broker accepts the connection: the count in the report is how often that happened since the node started.
void note_broker_connected();

// The report as it would be published now (for the panel's status page).
std::string report_json(const std::string& node_id, const std::string& family);

}  // namespace armor::node_health
