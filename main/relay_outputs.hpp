// ARMOR-ELECTRICAL - the relay outputs of the base board: GPIO pins driven from the panel.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <string>

#include "core/io_config.hpp"
#include "core/relay_remote.hpp"

namespace armor::relay_outputs {

// Drives every enabled relay's pin to its open level. Every relay starts open, always. When the settings let the broker move the relays (`remote.relays`) the relays also become
// devices of the device layer (core/relay_remote.hpp): their state is published, a command from the broker is obeyed, and they fall back to open when the broker is lost for
// `remote.link_timeout_s`. Call it before mqtt_link::start().
void start(const config::IoSettings& settings, const std::string& node_id);

// Closes (on) or opens (off) relay `index` (0 to 7), from the panel. False when there is no such enabled relay.
bool set(std::size_t index, bool on);

// What a command from the broker did.
enum class Outcome { kOk, kUnknownRelay, kNotUnderstood, kRemoteOff };

// The index (0 to 7) of the enabled relay with this name, or -1.
int index_of(const std::string& name);

// Tells the relay with this name what the broker said (a word: ON or OFF; TOGGLE is refused, a repeated delivery would undo it).
Outcome command(const std::string& relay, const std::string& payload);

// What a central that holds the node's token (core/central_link.hpp) asked of a relay by name: moves it (on, off or toggle) when it is an enabled relay, whatever the broker's setting
// (this path has no broker); `now_on` says where the relay is after. kUnknownRelay when there is no such relay.
Outcome command_direct(const std::string& relay, relays::Action action, bool& now_on);

// [{"relay":1,"enabled":true,"name":"..","label":"..","pin":38,"on":false}, ...]
std::string json();

bool remote_enabled();

}  // namespace armor::relay_outputs
