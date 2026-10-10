// ARMOR-ELECTRICAL - the Zigbee radio of the base board: the serial line lent to one client of the network over TCP.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <string>

#include "core/io_config.hpp"
#include "core/zigbee_bridge.hpp"

namespace armor::zigbee_bridge {

// Opens the radio's UART (UART2) and listens on the TCP port when the settings turn the bridge on; does nothing otherwise. Call it once the network is up.
void start(const config::IoSettings& settings);

zigbee::Status status();

// Resets the radio (RESET_N low, then let go), or resets it into its serial boot loader to be given a firmware. False when the bridge is off or the line is not wired.
bool reset_radio(bool bootloader);

}  // namespace armor::zigbee_bridge
