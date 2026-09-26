// ARMOR-ELECTRICAL - the serial line of the meters. Nothing here has run on a board.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include "uart_bus.hpp"

extern "C" {
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

namespace armor::uartbus {
namespace {
constexpr uart_port_t kUart = UART_NUM_1;

class HardwareLine final : public Line {
 public:
  ~HardwareLine() override { uart_driver_delete(kUart); }

  bool start(int rx, int tx, int baud, std::string& error) {
    uart_config_t config{};
    config.baud_rate = baud;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    if (uart_driver_install(kUart, 1024, 0, 0, nullptr, 0) != ESP_OK) { error = "driver"; return false; }
    if (uart_param_config(kUart, &config) != ESP_OK) { error = "baud"; uart_driver_delete(kUart); return false; }
    if (uart_set_pin(kUart, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) { error = "pins"; uart_driver_delete(kUart); return false; }
    return true;
  }

  std::size_t read(std::uint8_t* out, std::size_t capacity, unsigned wait_ms) override {
    const int n = uart_read_bytes(kUart, out, capacity, pdMS_TO_TICKS(wait_ms));
    return n > 0 ? static_cast<std::size_t>(n) : 0;
  }

  bool write(const std::uint8_t* data, std::size_t length) override {
    uart_flush_input(kUart);   // whatever came before the request is not its answer
    if (uart_write_bytes(kUart, data, length) < 0) return false;
    return uart_wait_tx_done(kUart, pdMS_TO_TICKS(500)) == ESP_OK;
  }
};
}  // namespace

std::unique_ptr<Line> open(int rx, int tx, int baud, std::string& error) {
  if (rx < 0 || tx < 0) { error = "pins"; return nullptr; }
  auto line = std::make_unique<HardwareLine>();
  if (!line->start(rx, tx, baud, error)) return nullptr;
  return line;
}

}  // namespace armor::uartbus
