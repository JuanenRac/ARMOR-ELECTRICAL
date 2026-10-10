// ARMOR-ELECTRICAL - the relay outputs of the base board: GPIO pins driven from the panel.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <string>

#include "core/electrical_config.hpp"

namespace armor::relay_outputs {

// Drives every enabled relay's pin to its open level. Every relay starts open, always.
void start(const config::Settings& settings);

// Closes (on) or opens (off) relay `index` (0 to 7). False when there is no such enabled relay.
bool set(std::size_t index, bool on);

// [{"relay":1,"enabled":true,"name":"..","label":"..","pin":38,"on":false}, ...]
std::string json();

}  // namespace armor::relay_outputs
