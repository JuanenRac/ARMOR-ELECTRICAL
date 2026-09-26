// ARMOR-ELECTRICAL - the serial line of the meters: one hardware UART.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace armor::uartbus {

// The open line. 8 data bits, no parity, one stop bit.
class Line {
 public:
  virtual ~Line() = default;
  // Waits up to `wait_ms` for bytes and returns how many came (0 when none did).
  virtual std::size_t read(std::uint8_t* out, std::size_t capacity, unsigned wait_ms) = 0;
  // Sends the bytes and returns when they are on the line. False when they could not be sent.
  virtual bool write(const std::uint8_t* data, std::size_t length) = 0;
};

// Opens the line on UART1, or returns nullptr with the reason in `error` ("driver", "pins", "baud").
std::unique_ptr<Line> open(int rx, int tx, int baud, std::string& error);

}  // namespace armor::uartbus
