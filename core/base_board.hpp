// ARMOR-ELECTRICAL - the base board that carries the ESP32-S3 module: where the Zigbee radio, the relays and the analog inputs are wired by default.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The pins of the module itself (which GPIO exists, which belongs to the flash, the Ethernet controller or the USB) are core/board_s3.hpp's, shared by every node; this file is only
// what the electrical (and the alarm) base board adds, so that the shared file stays the same in every project.
#pragma once
#include <array>

#include "board_s3.hpp"

namespace armor::board {

// ---- the base board: the Zigbee radio, the relay outputs and the analog inputs --------------------------------------------------------------------
//
// The node's own pins, as the base board that carries the ESP32-S3 module would wire them (docs/BASE_BOARD.md has the whole proposal with the reasons). Every one of them is
// a setting in the panel; these are only where a node that was never configured looks first.

// Analog inputs are read by ADC1 (GPIO 1 to 10): ADC2 is shared with the Wi-Fi radio and may refuse a reading while it is in use, so it is never offered.
constexpr bool analog_capable(int gpio) { return gpio >= 1 && gpio <= 10 && pin_info(gpio).use != PinUse::kReserved; }
constexpr int adc1_channel(int gpio) { return gpio - 1; }   // GPIO 1 is ADC1 channel 0 ... GPIO 10 is channel 9

constexpr int kRelayCount = 8;
constexpr int kAnalogCount = 8;

// The Zigbee radio module (a CC2652P with a coordinator firmware) on UART2: its TX goes to `rx`, its RX to `tx`; `reset` drives its RESET_N and `bootloader` its
// serial-boot pin (DIO15, active low). -1: not wired.
struct ZigbeePins { int rx, tx, reset, bootloader; };

#if defined(ARMOR_BOARD_S3_ETH)
// GPIO 43 and 44 are UART0's pins, where the chip's ROM prints its start-up messages for a moment at every reset: nothing that must not twitch at start-up (a relay) goes there. They carry
// the radio's two control lines instead, which are open-drain and harmless to a radio that is reset while the node starts. Strapping pins (0, 3, 45, 46) are left out.
constexpr ZigbeePins kZigbeePins{18, 17, 43, 44};
constexpr std::array<int, kRelayCount> kRelayPins{38, 39, 40, 41, 42, 47, 48, 21};
// ADC1 pins left by the W5500 (GPIO 9 and 10 are its) and the camera connector (8): 1 to 7, with 3 a strapping pin that is not offered by default. Inputs 7 and 8 have no pin until one is chosen.
constexpr std::array<int, kAnalogCount> kAnalogPins{1, 2, 4, 5, 6, 7, -1, -1};
#else
constexpr ZigbeePins kZigbeePins{18, 17, 21, 47};
constexpr std::array<int, kRelayCount> kRelayPins{38, 39, 40, 41, 42, 11, 12, 13};
constexpr std::array<int, kAnalogCount> kAnalogPins{1, 2, 4, 5, 6, 7, 8, 9};
#endif


}  // namespace armor::board
