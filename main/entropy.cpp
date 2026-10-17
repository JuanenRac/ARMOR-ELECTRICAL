// ARMOR-ELECTRICAL - random bytes for session tokens, salts and the setup code.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include "entropy.hpp"

extern "C" {
#include "bootloader_random.h"
#include "esp_bt.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
}

namespace armor {

std::mutex& adc_lock() {
  static std::mutex lock;
  return lock;
}

namespace {
// With the Wi-Fi or the Bluetooth running, the chip's generator is a true one by itself; the noise source of the converter must NOT be switched on then (the documentation of
// bootloader_random_enable warns that it is not safe while another part uses the radio front end or the converter, and it can leave them in a state that upsets the radio and the
// readings of the analogue inputs). It is for the first start only, when the setup code is made before any radio is up.
bool radio_is_running() {
  wifi_mode_t mode;
  return esp_wifi_get_mode(&mode) == ESP_OK || esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE;
}
}  // namespace

void random_bytes(std::uint8_t* out, std::size_t length) {
  std::lock_guard<std::mutex> guard(adc_lock());
  const bool noise_source = !radio_is_running();
  if (noise_source) bootloader_random_enable();
  esp_fill_random(out, length);
  if (noise_source) bootloader_random_disable();
  // Mix in the time of the call as well: it costs nothing and never makes the bytes worse.
  const std::uint64_t now = static_cast<std::uint64_t>(esp_timer_get_time());
  for (std::size_t i = 0; i < length; ++i) out[i] = static_cast<std::uint8_t>(out[i] ^ static_cast<std::uint8_t>(now >> ((i % 8) * 8)) ^ static_cast<std::uint8_t>(esp_random() >> 24));
}

}  // namespace armor
