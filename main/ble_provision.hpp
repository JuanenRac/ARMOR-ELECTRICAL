// ARMOR-ELECTRICAL - configuration over Bluetooth Low Energy, for an app on a phone (the same operations as the panel, framed as JSON on a GATT byte stream).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// For a node with no Ethernet cable (or no address yet): the phone finds "ARMOR-xxxxxx", connects, and configures the node's name, Wi-Fi station,
// address, broker and the rest, with the same set-up code and the same users as the panel. The protocol is in docs/BLE_PROVISIONING.md. Nothing
// here has run on a board.
#pragma once
#include "core/electrical_config.hpp"

namespace armor::ble_provision {

// Starts the Bluetooth stack and advertises, when the settings say so: mode "always", or mode "setup". With mode "off" nothing is started and the radio and memory of Bluetooth stay unused. Mode
// "setup" closes itself (core/ble_window.hpp): after the minutes of CONFIG_ARMOR_BLE_SETUP_MINUTES with nobody connected, or two minutes after a node that is set up has an address; the stack is then
// stopped and its memory freed, and it opens again only when the node is restarted. It does not start at all after a restart the node asked for itself because it was short of memory, or when
// the internal memory is under what it needs. True when advertising began.
bool start(const config::Settings& settings, bool setup_mode);

bool running();

}  // namespace armor::ble_provision
