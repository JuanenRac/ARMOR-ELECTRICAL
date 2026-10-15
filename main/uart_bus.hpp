// ARMOR-ELECTRICAL - the serial lines of the meters: a hardware UART each (UART1 for the first line, UART0 for the second one: the console is on the USB).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "core/serial_line.hpp"

namespace armor::uartbus {

// Opens line 0 or 1, or returns nullptr with the reason in `error` ("driver", "pins", "baud", "mode"). A `de` pin of 0 or more turns the UART into RS485 half duplex with that
// pin as the adapter's driver-enable (the UART raises it while it sends and the echo is not read back); -1 leaves the line plain TTL.
std::unique_ptr<Line> open(int line, int rx, int tx, int de, int baud, std::string& error);

}  // namespace armor::uartbus
