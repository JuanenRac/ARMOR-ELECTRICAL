// ARMOR-ELECTRICAL - what the panel does over HTTP: the status, the meters, the readings, the settings and the search for Wi-Fi networks.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <string>
#include <string_view>

#include "core/electrical_config.hpp"

namespace armor::api {

std::string version_text();
std::string status_json();          // node, network, broker, the line and the meters, as the Overview page shows them
std::string meters_json();          // {"bus":{...},"meters":[...],"catalog":[...]}: the state of the line and of the sixteen meters, and the pins the board offers
std::string readings_json();        // the last message of the node, in an array: what it publishes
std::string config_get_json();      // {"config":{...},"channel_auto":n,"firmware":"x.y.z"}: no password ever leaves the node
// The same document the flash keeps, secrets and all: an admin downloading the node's whole configuration to load onto an identical
// unit (manufacturing a batch of nodes), not something the ordinary panel pages ever call.
std::string config_export_json();
std::string problems_json(const config::Problems& problems);   // [{"path":..,"code":..}]

enum class PutResult { kSaved, kInvalid, kStorage };
// Applies a (partial) settings document on top of the stored one: checked in full, and stored only when nothing is wrong.
PutResult put_config(std::string_view document, config::Problems& problems, bool& restart_required);

// The Wi-Fi networks in range: {"networks":[{"ssid","rssi","channel","security"}]}, strongest first. False, with a code, when the radio is busy.
bool wifi_scan_json(std::string& data, std::string& error);

}  // namespace armor::api
