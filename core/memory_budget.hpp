// ARMOR-NODE-ELECTRICAL - how the node spends its memory: the two kinds it has, what is judged "low", where a big buffer goes and how often the log says it. No hardware.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The ESP32-S3 boards have two kinds of memory. The INTERNAL one (some 300 KB, of which the radio, the network and every task stack take their share) is the only one a
// driver, an interrupt or a flash write may touch; the EXTERNAL one (8 MB of PSRAM) is large and slower, and is not reachable while the flash is being written. A node that runs
// out of the first one fails (a connection refused, a task that does not start) while the second one is nearly empty, so the rules for where a thing goes live here, as pure
// functions of numbers: main/psram_mem.cpp reads the real values and acts on them, and the host tests exercise every rule.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace armor::membudget {

// What the heap says at one moment. `psram_total` is 0 on a board where the external memory did not start (the node then runs on the internal one alone).
struct Snapshot {
  std::uint32_t internal_free = 0;      // bytes of internal memory free now
  std::uint32_t internal_min = 0;       // the least that has been free since the node started
  std::uint32_t internal_largest = 0;   // the biggest single block that could be allocated now (a heap in pieces fails a big request while the sum looks fine)
  std::uint32_t psram_free = 0;
  std::uint32_t psram_total = 0;
  bool has_psram() const { return psram_total > 0; }
};

// The internal memory is "low" under 40 KB and "back to normal" over 48 KB (the gap keeps a figure that hovers around the line from raising and clearing the warning all day), or when
// the biggest block left is under 8 KB. The first number is a little above the 32 KB the sdkconfig keeps for the drivers (CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL): under it the node is
// eating into that reserve. These are the starting values: they are meant to be compared with the readings of a real board.
constexpr std::uint32_t kInternalLowBytes = 40u * 1024u;
constexpr std::uint32_t kInternalClearBytes = 48u * 1024u;
constexpr std::uint32_t kLargestBlockLowBytes = 8u * 1024u;

// Whether the internal memory is low. `was_low` is the answer of the last time (the gap between the two levels). A snapshot with nothing in it (a board that could not read its heap) is not low.
inline bool internal_is_low(const Snapshot& s, bool was_low) {
  if (s.internal_free == 0 && s.internal_largest == 0) return false;
  if (s.internal_largest < kLargestBlockLowBytes) return true;
  return was_low ? s.internal_free < kInternalClearBytes : s.internal_free < kInternalLowBytes;
}

enum class Place { kInternal, kPsram };

struct Choice {
  Place place = Place::kInternal;
  bool degraded = false;   // the external memory was wanted and was not used (absent or too full): the caller says so in the log
};

// The external memory always keeps this much for everything that allocates with the plain malloc (the strings and vectors of the panel, the TLS buffers).
constexpr std::uint32_t kPsramSpareBytes = 256u * 1024u;

// Where one big buffer or one task stack goes. `want_psram` is the caller's decision (it knows whether the thing is safe there: no DMA, no interrupt, never touched with the flash
// busy); this function only checks the memory is there. The answer is always usable: it falls back to the internal memory, and says it did.
inline Choice choose_place(bool want_psram, std::size_t bytes, const Snapshot& s) {
  Choice choice;
  if (!want_psram) return choice;
  if (s.has_psram() && static_cast<std::uint64_t>(s.psram_free) >= static_cast<std::uint64_t>(bytes) + kPsramSpareBytes) { choice.place = Place::kPsram; return choice; }
  choice.degraded = true;
  return choice;
}

// The log line of the memory: said every minute, and at once (but never twice in ten seconds) when the memory turns low or back to normal.
constexpr std::uint64_t kLogEveryMs = 60u * 1000u;
constexpr std::uint64_t kLogMinGapMs = 10u * 1000u;

enum class LogAction { kNone, kInfo, kWarn };

struct LogState {
  bool started = false;
  bool low = false;
  std::uint64_t last_ms = 0;
};

inline LogAction next_log(LogState& state, std::uint64_t now_ms, const Snapshot& s) {
  const bool low = internal_is_low(s, state.low);
  const bool changed = state.started && low != state.low;
  const std::uint64_t since = now_ms >= state.last_ms ? now_ms - state.last_ms : 0;
  const bool due = !state.started || since >= kLogEveryMs || (changed && since >= kLogMinGapMs);
  if (!due) return LogAction::kNone;
  state.started = true;
  state.low = low;
  state.last_ms = now_ms;
  return low ? LogAction::kWarn : LogAction::kInfo;
}

inline std::string kb(std::uint32_t bytes) { return std::to_string((bytes + 512u) / 1024u); }

// "memory: internal 71 KB free (least 60 KB, biggest block 36 KB), PSRAM 7712 KB free of 8192 KB, 7 task stacks (33 KB) in PSRAM" - or "no PSRAM" on a board without it.
inline std::string describe(const Snapshot& s, unsigned stacks_in_psram, std::uint32_t stack_bytes_in_psram) {
  std::string text = "memory: internal " + kb(s.internal_free) + " KB free (least " + kb(s.internal_min) + " KB, biggest block " + kb(s.internal_largest) + " KB)";
  if (s.has_psram()) text += ", PSRAM " + kb(s.psram_free) + " KB free of " + kb(s.psram_total) + " KB, " + std::to_string(stacks_in_psram) + " task stacks (" + kb(stack_bytes_in_psram) + " KB) in PSRAM";
  else text += ", no PSRAM";
  return text;
}

}  // namespace armor::membudget
