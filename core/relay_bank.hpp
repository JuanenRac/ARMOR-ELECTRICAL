// ARMOR-ELECTRICAL - the relay outputs of the base board: which ones exist, what each is told and at what level its pin must be, without any hardware.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The node drives a pin; the driver, the relay and the load behind it are the installer's. Every relay is open (off) when the node starts, whatever it was before: a node that
// restarts never closes anything by itself. These are plain relays that someone with the panel open switches - they are not the source-transfer controller of interlock.hpp,
// and a load that must not lose its supply (a freezer) has no business behind one of them without the installer's own protection.
#pragma once
#include <array>
#include <cstddef>
#include <string>

#include "board_s3.hpp"
#include "electrical_config.hpp"
#include "json.hpp"

namespace armor::relays {

class Bank {
 public:
  explicit Bank(const config::Settings& settings) : settings_(settings.relays) {}

  bool enabled(std::size_t index) const { return index < settings_.size() && settings_[index].enabled; }
  bool is_on(std::size_t index) const { return enabled(index) && on_[index]; }

  // Tells relay `index` to close (on) or open (off). False when there is no such relay or it is not enabled.
  bool set(std::size_t index, bool on) {
    if (!enabled(index)) return false;
    on_[index] = on;
    return true;
  }
  void open_all() { on_.fill(false); }

  // The level the pin must have for the relay's state: a relay that closes on a low pin has it inverted. An open relay of a disabled slot is "off" too.
  bool pin_level(std::size_t index) const {
    if (index >= settings_.size()) return false;
    const bool closed = is_on(index);
    return settings_[index].active_low ? !closed : closed;
  }
  // The level the pin has before the node drives it and for a slot that is not used: the relay open.
  bool open_level(std::size_t index) const { return index < settings_.size() && settings_[index].active_low; }

  int pin(std::size_t index) const { return index < settings_.size() ? settings_[index].pin : -1; }

  std::string json() const {
    json::Writer w;
    w.begin_array();
    for (std::size_t i = 0; i < settings_.size(); ++i) {
      const config::RelaySetting& r = settings_[i];
      w.begin_object().field("relay", static_cast<int>(i + 1)).field("enabled", r.enabled).field("name", r.name).field("label", r.label).field("pin", r.pin).field("on", is_on(i)).end_object();
    }
    w.end_array();
    return w.str();
  }

 private:
  std::array<config::RelaySetting, board::kRelayCount> settings_;
  std::array<bool, board::kRelayCount> on_{};
};

}  // namespace armor::relays
