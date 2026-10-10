// ARMOR-ELECTRICAL - the relay outputs of the base board: GPIO pins driven from the panel.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <string>

#include "core/electrical_config.hpp"

namespace armor::relay_outputs {

// Drives every enabled relay's pin to its open level. Every relay starts open, always. When the settings let the broker move the relays (`remote.relays`) the relays also become
// devices of the device layer (core/relay_remote.hpp): their state is published, a command from the broker is obeyed, and they fall back to open when the broker is lost for
// `remote.link_timeout_s`. Call it before mqtt_link::start().
void start(const config::Settings& settings);

// Closes (on) or opens (off) relay `index` (0 to 7), from the panel. False when there is no such enabled relay.
bool set(std::size_t index, bool on);

// What a command from the broker did.
enum class Outcome { kOk, kUnknownRelay, kNotUnderstood, kRemoteOff };

// Tells the relay with this name what the broker said (a word: ON, OFF, TOGGLE).
Outcome command(const std::string& relay, const std::string& payload);

// [{"relay":1,"enabled":true,"name":"..","label":"..","pin":38,"on":false}, ...]
std::string json();

bool remote_enabled();

}  // namespace armor::relay_outputs
