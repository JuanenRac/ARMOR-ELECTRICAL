// ARMOR-ELECTRICAL - lets the server control the node: restart, pause, resume, standby, identify, diagnostics and maintenance (core/control_gate.hpp has the rules and is tested on a computer).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstdint>
#include <functional>
#include <string>

#include "core/control_gate.hpp"

namespace armor::node_control {

struct Options {
  std::string node_id;
  // Hands one message to the broker (the node's mqtt_link::publish).
  std::function<void(const std::string& topic, const std::string& payload)> publish;
  // Hands one message to the broker RETAINED at QoS 1 (the node's mqtt_link::publish_retained): the planned-maintenance signal. Optional: without it the signal goes out as an ordinary message.
  std::function<void(const std::string& topic, const std::string& payload)> publish_retained;
  // What the node is in the middle of besides the firmware updates, uploads and slot copies it knows about (the solar gateway: a battery operation). Optional.
  std::function<control::Busy()> busy;
  // The node's work is to stop (paused or standby) or to start again (running). Called once at start with the state the node boots in (a node kept in standby boots into it), and then on
  // every change. It runs in the broker client's task or in the task of the one-second tick: it must not wait for long. Optional: a node may poll working() instead.
  std::function<void(control::State)> on_state;
  // Show the node (its LED, a banner) for this many seconds; 0 stops showing. NEVER a siren. Optional: a node that cannot show anything still answers that it did what it can.
  std::function<void(unsigned seconds)> identify;
  // Adds what only the node knows to the snapshot of a diagnostics order (it may fill psram_free, stack_min_free, rssi). Optional.
  std::function<void(control::Diagnostics&)> diagnostics_extra;
};

// Loads the standby kept in flash (a node that was put in standby boots straight into it: the state is known BEFORE the node's work starts, so call this right after the store is open and
// before the tasks that do the work), then listens to armor/control/<node>/command and armor/control/all/command and runs the one-second tick that ends a pause, an identify window and the
// maintenance flag. Call it BEFORE mqtt_link::start (the subscriptions are made when the broker connects). A node whose id is `all`, or not valid, is not controlled and always works.
void start(const Options& options);

// True while the node does its work: running. False while paused or in standby. Cheap and safe from any task; true until start() has said otherwise.
bool working();
control::State state();
// True from the moment a restart order was accepted until the node restarts: the guard does not count a missing task while it is, and it is not a guard restart.
bool restart_pending();

struct Status {
  control::State state = control::State::kRunning;
  std::uint64_t since_ms = 0;       // wall clock when the pause or standby began, 0 if unknown
  std::string by;                   // who asked for it (empty when running)
  long long paused_left_s = -1;     // seconds until a pause ends by itself, -1 when not paused
  bool maintenance = false;
  long long maintenance_left_s = -1;
  bool identifying = false;
};
Status status();

// The planned-maintenance signal (armor/control/<node>/maintenance, retained): tells the server this node is about to be quiet on purpose, so that it holds back the node-offline alert for a
// bounded time. `reason` is "update" or "upload" for the callers outside this module (a firmware install or slot copy, a firmware upload from the panel), `expected_s` is 1..3600. A tiny call
// that does nothing when node_control::start did not run (or the reason is not one of those). Call announce_maintenance_done with the same reason when the work ended WITHOUT a restart (it
// failed, or finished in place); a node that restarts says nothing more: it clears the signal itself when it is back and connected. Restarts, pauses, standby and operator windows announce
// themselves.
void announce_maintenance(const char* reason, unsigned expected_s);
void announce_maintenance_done(const char* reason);

// A firmware upload through the panel is under way while one of these exists: the node does not restart, pause or go to standby in the middle of it.
class UploadGuard {
 public:
  UploadGuard();
  ~UploadGuard();
  UploadGuard(const UploadGuard&) = delete;
  UploadGuard& operator=(const UploadGuard&) = delete;
};

}  // namespace armor::node_control
