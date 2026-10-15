// ARMOR-ELECTRICAL - how long to wait before trying again: a wait that grows with every failure and falls back to the first one when a try works. No hardware.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// A link that has gone away (the Wi-Fi network, the broker) is tried again at once and then less and less often, up to a ceiling, with a little scatter so that many nodes that lost
// the same router at the same moment do not all knock again at the same instant. The random number is handed in, so the schedule is tested on a computer.
#pragma once
#include <cstdint>

namespace armor::retry {

class Backoff {
 public:
  // The first wait, the longest wait, and how far (in percent, up or down) a wait may be scattered.
  constexpr Backoff(std::uint32_t first_ms, std::uint32_t max_ms, std::uint32_t scatter_percent = 20) : first_ms_(first_ms == 0 ? 1 : first_ms), max_ms_(max_ms < first_ms ? first_ms : max_ms), scatter_(scatter_percent > 90 ? 90 : scatter_percent) {}

  // The wait before the next try (and the count of failures goes up). `random` is any 32-bit random number.
  std::uint32_t next(std::uint32_t random) {
    std::uint64_t wait = first_ms_;
    for (std::uint32_t i = 0; i < attempts_ && wait < max_ms_; ++i) wait *= 2;
    if (wait > max_ms_) wait = max_ms_;
    if (attempts_ < 63) ++attempts_;
    if (scatter_ == 0) return static_cast<std::uint32_t>(wait);
    const std::uint32_t span = 2 * scatter_ + 1;                       // percent points from -scatter to +scatter
    const std::uint64_t factor = 100 - scatter_ + random % span;      // 100 - scatter ... 100 + scatter
    std::uint64_t scattered = wait * factor / 100;
    if (scattered > max_ms_) scattered = max_ms_;                      // the ceiling is a ceiling
    return static_cast<std::uint32_t>(scattered == 0 ? 1 : scattered);
  }

  // The try worked: the next failure starts from the first wait again.
  void reset() { attempts_ = 0; }
  std::uint32_t failures() const { return attempts_; }

 private:
  std::uint32_t first_ms_, max_ms_, scatter_;
  std::uint32_t attempts_ = 0;
};

}  // namespace armor::retry
