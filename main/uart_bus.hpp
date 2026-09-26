// ARMOR-ELECTRICAL - the serial line of the meters: one hardware UART.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "core/serial_line.hpp"

namespace armor::uartbus {

// Opens the line on UART1, or returns nullptr with the reason in `error` ("driver", "pins", "baud").
std::unique_ptr<Line> open(int rx, int tx, int baud, std::string& error);

}  // namespace armor::uartbus
