// ARMOR-NODE-ELECTRICAL - how long Bluetooth set-up stays open: it closes itself after some minutes with nobody connected, and stays closed until the node is restarted by its owner. No hardware.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// In mode "setup" the node listens at every start, because until it has an address it cannot be reached any other way. Left open for ever the radio keeps a large share of the internal memory and
// a door that nobody uses, so the window shuts: `minutes` after the start (or after the last phone left) when nobody connected, and two minutes after the node has an address when it already has
// users. Mode "always" is the owner's explicit choice and has no window. A phone that is connected keeps the window open for as long as it stays. main/ble_provision.cpp reads the clock and
// closes the stack; this file only decides.
#pragma once
#include <cstdint>

namespace armor::blewindow {

constexpr unsigned kDefaultMinutes = 10;
constexpr unsigned kMinMinutes = 1;
constexpr unsigned kMaxMinutes = 120;
constexpr unsigned kAddressGraceSeconds = 120;

// The setting is a number of minutes; whatever is written is brought into the range.
inline unsigned clamp_minutes(long long minutes) {
  if (minutes < static_cast<long long>(kMinMinutes)) return kMinMinutes;
  if (minutes > static_cast<long long>(kMaxMinutes)) return kMaxMinutes;
  return static_cast<unsigned>(minutes);
}

enum class Verdict { kKeepOpen, kCloseIdle, kCloseHasAddress };

struct Watch {
  std::uint64_t idle_since_ms = 0;   // since when nobody has been connected
  unsigned address_s = 0;            // seconds in a row with an address, while nobody is connected

  // One look, `elapsed_s` seconds after the one before. A connected phone restarts both counts.
  void observe(std::uint64_t now_ms, bool connected, bool has_address, unsigned elapsed_s) {
    if (connected) { idle_since_ms = now_ms; address_s = 0; return; }
    address_s = has_address ? address_s + elapsed_s : 0;
  }
};

// `setup_mode_only` is false for mode "always" (never closes). `node_has_users` is false while the node still has to be set up (then only the idle time counts: the address does not close it).
inline Verdict judge(const Watch& watch, std::uint64_t now_ms, bool setup_mode_only, bool node_has_users, bool connected, unsigned minutes) {
  if (!setup_mode_only || connected) return Verdict::kKeepOpen;
  const std::uint64_t idle_ms = now_ms >= watch.idle_since_ms ? now_ms - watch.idle_since_ms : 0;
  if (idle_ms >= static_cast<std::uint64_t>(clamp_minutes(static_cast<long long>(minutes))) * 60000ULL) return Verdict::kCloseIdle;
  if (node_has_users && watch.address_s >= kAddressGraceSeconds) return Verdict::kCloseHasAddress;
  return Verdict::kKeepOpen;
}

}  // namespace armor::blewindow
