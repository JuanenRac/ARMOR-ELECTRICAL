// ARMOR-NODE-ELECTRICAL - random bytes for session tokens, salts and the setup code.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace armor {

// The lock shared by everything that uses the analogue-to-digital converter: the random generator borrows it as a noise source while it
// fills a buffer (the chip's generator is only truly random with the radio on or with this noise source enabled).
std::mutex& adc_lock();

// Fills `out` with random bytes. While no radio is running the converter's noise source is enabled for the moment it takes; once the Wi-Fi or the Bluetooth is up the generator needs no help.
void random_bytes(std::uint8_t* out, std::size_t length);

}  // namespace armor
