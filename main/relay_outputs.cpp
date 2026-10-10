// ARMOR-ELECTRICAL - the relay outputs of the base board. Nothing here has run on a board.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// What the node decides is in core/relay_bank.hpp (tested on a computer): which relay is closed and at what level its pin must be. This file only puts that level on the pin. The level of
// a pin is set BEFORE the pin becomes an output, so a relay never sees a pulse of the wrong level while the node starts. Before the node runs, and while it restarts, a pin floats:
// the base board must pull every relay's driver to its open level with a resistor of its own.
#include "relay_outputs.hpp"

#include <memory>
#include <mutex>
extern "C" {
#include "driver/gpio.h"
#include "esp_log.h"
}
#include "core/relay_bank.hpp"

namespace armor::relay_outputs {
namespace {
constexpr char kTag[] = "armor-relays";
std::mutex g_lock;
std::unique_ptr<relays::Bank> g_bank;

void put(std::size_t index) {
  const int pin = g_bank->pin(index);
  if (pin >= 0) gpio_set_level(static_cast<gpio_num_t>(pin), g_bank->pin_level(index) ? 1 : 0);
}
}  // namespace

void start(const config::Settings& settings) {
  std::lock_guard<std::mutex> guard(g_lock);
  g_bank = std::make_unique<relays::Bank>(settings);
  for (std::size_t i = 0; i < board::kRelayCount; ++i) {
    if (!g_bank->enabled(i)) continue;
    const gpio_num_t pin = static_cast<gpio_num_t>(g_bank->pin(i));
    gpio_reset_pin(pin);
    gpio_set_level(pin, g_bank->open_level(i) ? 1 : 0);   // first the level, then the direction
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    ESP_LOGI(kTag, "relay %u (%s) on GPIO %d, open", static_cast<unsigned>(i + 1), settings.relays[i].name.c_str(), static_cast<int>(pin));
  }
}

bool set(std::size_t index, bool on) {
  std::lock_guard<std::mutex> guard(g_lock);
  if (!g_bank || !g_bank->set(index, on)) return false;
  put(index);
  ESP_LOGW(kTag, "relay %u %s", static_cast<unsigned>(index + 1), on ? "closed" : "opened");
  return true;
}

std::string json() {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_bank ? g_bank->json() : "[]";
}

}  // namespace armor::relay_outputs
