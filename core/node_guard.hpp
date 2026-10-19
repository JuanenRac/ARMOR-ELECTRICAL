// ARMOR-ELECTRICAL - the node's own watchdog over its health: a task it needs that is not there, or an internal memory that stays too low, is told and put right by one clean restart - never a loop. No hardware.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The ESP32-S3 boards keep some 300 KB of internal memory for everything the radio, the network, the drivers and every task stack need. A node whose Bluetooth took the last of it ran on for
// days without its telemetry task (xTaskCreate answered "no memory" and nothing noticed), publishing a few dozen messages instead of tens of thousands. The rules below decide, from plain numbers:
//   * when the node is "in trouble": a task it registered as needed is missing, or the internal memory is under `kLowInternalBytes` (and stays so until it is over `kClearInternalBytes`);
//   * what to do once the trouble has lasted a minute: say it (log and a health report at once), and, if the node has not already restarted `kMaxRestartsPerHour` times in the last hour of
//     its running time, restart once, cleanly. After the third it stays up and repeats the alarm every ten minutes: a node that restarts for ever is worse than one that is short of memory;
//   * what the node remembers across a restart (a few numbers in memory that survives a software reset), so that the limit holds across the restarts it counts.
// main/node_guard.cpp reads the real values and acts on them; the rules are tested on a computer.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace armor::nodeguard {

// Under 16 KB of internal memory a connection is refused and a task cannot start (the figures of a stuck node were 11 to 20 KB). The memory log warns earlier (40 KB). These are starting values,
// to be compared with the readings of a real board.
constexpr std::uint32_t kLowInternalBytes = 16u * 1024u;
constexpr std::uint32_t kClearInternalBytes = 24u * 1024u;   // the gap keeps a figure that hovers around the line from starting and clearing the trouble all day
constexpr std::uint64_t kTroubleForMs = 60u * 1000u;         // how long the trouble must last before the node acts
constexpr std::uint64_t kRepeatAlarmMs = 10u * 60u * 1000u;  // when it cannot restart, how often it says so again
constexpr unsigned kMaxRestartsPerHour = 3;
// The internal memory Bluetooth needs to start (its tasks and the controller's buffers), more or less: under it the node does not start the radio, which would only leave it in the trouble the guard
// restarts for. A starting value, to be compared with the readings of a real board (the log says the memory before and after).
constexpr std::uint32_t kBluetoothNeedsBytes = 40u * 1024u;
constexpr std::uint32_t kWindowS = 3600;                     // "an hour" is an hour of running time: a node that is switched off does not count

struct Inputs {
  std::uint64_t now_ms = 0;
  std::uint32_t internal_free = 0;   // 0 together with internal_largest 0: the heap could not be read, which is not trouble
  std::uint32_t internal_largest = 0;
  unsigned missing_tasks = 0;        // tasks registered as needed that FreeRTOS does not know
};

enum class Action {
  kNone,
  kAlert,            // say it, and do not restart (the limit of restarts is used up, or it is the repeat of the alarm)
  kAlertAndRestart,  // say it and restart once, cleanly
};

struct State {
  bool trouble = false;
  bool memory_low = false;
  bool alerted = false;
  std::uint64_t trouble_since_ms = 0;
  std::uint64_t last_alarm_ms = 0;
};

inline bool memory_is_low(const Inputs& in, bool was_low) {
  if (in.internal_free == 0 && in.internal_largest == 0) return false;
  return was_low ? in.internal_free < kClearInternalBytes : in.internal_free < kLowInternalBytes;
}

// One look. `may_restart` is the answer of the ledger below. The state keeps what a single look cannot know: since when the trouble lasts and whether it was already said.
inline Action step(State& state, const Inputs& in, bool may_restart) {
  const bool low = memory_is_low(in, state.memory_low);
  const bool trouble = low || in.missing_tasks > 0;
  state.memory_low = low;
  if (!trouble) {
    state.trouble = false;
    state.alerted = false;
    return Action::kNone;
  }
  if (!state.trouble) {
    state.trouble = true;
    state.alerted = false;
    state.trouble_since_ms = in.now_ms;
    return Action::kNone;
  }
  const std::uint64_t lasted = in.now_ms >= state.trouble_since_ms ? in.now_ms - state.trouble_since_ms : 0;
  if (lasted < kTroubleForMs) return Action::kNone;
  if (!state.alerted) {
    state.alerted = true;
    state.last_alarm_ms = in.now_ms;
    return may_restart ? Action::kAlertAndRestart : Action::kAlert;
  }
  if (!may_restart && in.now_ms >= state.last_alarm_ms && in.now_ms - state.last_alarm_ms >= kRepeatAlarmMs) {
    state.last_alarm_ms = in.now_ms;
    return Action::kAlert;
  }
  return Action::kNone;
}

// What the node tells (the log line, and the last error of its health report, which holds 96 characters).
inline std::string reason_text(const std::vector<std::string>& missing_names, const Inputs& in, bool memory_low) {
  std::string text = "guard:";
  if (!missing_names.empty()) {
    text += missing_names.size() > 1 ? " tasks " : " task ";
    for (std::size_t i = 0; i < missing_names.size() && i < 3; ++i) text += (i ? "," : "") + missing_names[i];
    if (missing_names.size() > 3) text += ",...";
    text += " missing";
  }
  if (memory_low) {
    if (!missing_names.empty()) text += ";";
    text += " internal memory " + std::to_string((in.internal_free + 512u) / 1024u) + " KB for over a minute";
  }
  if (text.size() > 90) text.resize(90);
  return text;
}

// The restarts the guard has made, in seconds of RUNNING time (the sum of the uptimes of the boots), so that the count survives restarts but not a power cut. At most `kMaxRestartsPerHour` are kept.
struct Ledger {
  std::uint32_t base_s = 0;                      // running time before this boot
  std::uint32_t at_s[kMaxRestartsPerHour] = {};  // when each restart was asked, on the running clock
  std::uint32_t count = 0;                       // how many of at_s are used
  bool pending = false;                          // the last restart was asked by the guard (read once, at the next boot)

  std::uint32_t clock(std::uint32_t uptime_s) const { return base_s + uptime_s; }
  unsigned recent(std::uint32_t uptime_s) const {
    unsigned n = 0;
    const std::uint32_t now = clock(uptime_s);
    for (std::uint32_t i = 0; i < count && i < kMaxRestartsPerHour; ++i) if (now >= at_s[i] && now - at_s[i] < kWindowS) ++n;
    return n;
  }
  bool may_restart(std::uint32_t uptime_s) const { return recent(uptime_s) < kMaxRestartsPerHour; }
  // Asks for a restart now: the entries that left the window make room for it.
  void note_restart(std::uint32_t uptime_s) {
    const std::uint32_t now = clock(uptime_s);
    std::uint32_t kept[kMaxRestartsPerHour] = {};
    std::uint32_t n = 0;
    for (std::uint32_t i = 0; i < count && i < kMaxRestartsPerHour; ++i) if (now >= at_s[i] && now - at_s[i] < kWindowS) kept[n++] = at_s[i];
    if (n >= kMaxRestartsPerHour) n = kMaxRestartsPerHour - 1;   // never reached through may_restart(); keeps the list inside its array
    kept[n++] = now;
    for (std::uint32_t i = 0; i < kMaxRestartsPerHour; ++i) at_s[i] = i < n ? kept[i] : 0;
    count = n;
    base_s = now;
    pending = true;
  }
};

// What the next boot learns from the ledger it finds.
struct BootInfo {
  bool by_guard = false;        // this boot follows a restart the guard asked for
  unsigned restarts_in_hour = 0;
};

// The ledger is kept in memory that survives a software reset, with a check number so that the garbage of a power-on is not taken for a ledger.
inline std::uint32_t ledger_check(const Ledger& l) {
  std::uint32_t h = 2166136261u ^ 0x47524431u;
  auto mix = [&h](std::uint32_t v) { for (int i = 0; i < 4; ++i) { h ^= (v >> (8 * i)) & 0xFFu; h *= 16777619u; } };
  mix(l.base_s);
  for (std::uint32_t v : l.at_s) mix(v);
  mix(l.count);
  mix(l.pending ? 1u : 0u);
  return h;
}

// At boot: the ledger as found (valid only when its check number is right and the reset kept the memory: a software reset or a crash, not a power-on), and what it says about this boot.
inline Ledger ledger_at_boot(const Ledger& found, std::uint32_t found_check, bool reset_kept_memory, BootInfo& info) {
  info = BootInfo{};
  Ledger ledger;
  if (reset_kept_memory && found_check == ledger_check(found) && found.count <= kMaxRestartsPerHour) ledger = found;
  info.by_guard = ledger.pending;
  info.restarts_in_hour = ledger.recent(0);
  ledger.pending = false;   // read once
  return ledger;
}

// Whether Bluetooth may start at this boot. After a restart the guard asked for, it stays off: the radio is what took the memory, and the node must come back up without it. The owner turns it on
// again by switching the node off and on (or by restarting it from the panel, which is not a guard restart).
inline bool bluetooth_may_start(const BootInfo& info) { return !info.by_guard; }

// ---- the gate at the door of the panel -------------------------------------------------------------------------------------------------------------------------
// While the internal memory is short, a new connection to the panel is refused before anything is allocated for it (the socket is closed at once): a browser that opens six tabs must not take the
// last bytes from the MQTT link and the node's own work. Connections already open, the broker link and the sensors are not touched. It closes under 28 KB free or a biggest block under 12 KB and
// opens again over 40 KB and 16 KB (the gap keeps it from flapping). Starting values, to be compared with a real board.
constexpr std::uint32_t kGateCloseFreeBytes = 28u * 1024u;
constexpr std::uint32_t kGateCloseBlockBytes = 12u * 1024u;
constexpr std::uint32_t kGateOpenFreeBytes = 40u * 1024u;
constexpr std::uint32_t kGateOpenBlockBytes = 16u * 1024u;

struct GateThresholds {
  std::uint32_t close_free = kGateCloseFreeBytes, close_block = kGateCloseBlockBytes, open_free = kGateOpenFreeBytes, open_block = kGateOpenBlockBytes;
  // A threshold that makes no sense (the opening level under the closing one) is put right: a gate that opens below where it closes never closes.
  GateThresholds sane() const {
    GateThresholds t = *this;
    if (t.open_free < t.close_free) t.open_free = t.close_free;
    if (t.open_block < t.close_block) t.open_block = t.close_block;
    return t;
  }
};

struct Gate {
  bool closed = false;
  std::uint32_t transitions = 0;
  // True when the state changed. A heap that could not be read (both 0) leaves the gate as it is.
  bool step(std::uint32_t internal_free, std::uint32_t internal_largest, const GateThresholds& levels = GateThresholds{}) {
    if (internal_free == 0 && internal_largest == 0) return false;
    const GateThresholds t = levels.sane();
    const bool low = internal_free < t.close_free || internal_largest < t.close_block;
    const bool recovered = internal_free >= t.open_free && internal_largest >= t.open_block;
    const bool now_closed = closed ? !recovered : low;
    if (now_closed == closed) return false;
    closed = now_closed;
    ++transitions;
    return true;
  }
};

// Streams that stay open (events, long downloads) are capped, and none is opened while the gate is closed. (The panels of the nodes keep no stream open today; the rule is here for the one that does.)
constexpr unsigned kMaxOpenStreams = 4;
inline bool may_open_stream(unsigned open_now, bool gate_closed, unsigned cap = kMaxOpenStreams) { return !gate_closed && open_now < cap; }

inline std::string gate_text(bool closed, std::uint32_t internal_free, std::uint32_t internal_largest) {
  return std::string(closed ? "guard: no new panel connections while the memory is short" : "guard: panel connections accepted again") + " (internal " + std::to_string((internal_free + 512u) / 1024u) + "/" + std::to_string((internal_largest + 512u) / 1024u) + " KB)";
}

// ---- an allocation that failed ------------------------------------------------------------------------------------------------------------------------------------
// A failed `new` is not left to abort: the incident (free memory, biggest block, running time) is written in memory that survives a restart and the node restarts at once. The next boot says it
// ("oom heap=<free>/<largest>"). An out-of-memory in the middle of writing a firmware image, or a second one within a minute of the running time, is NOT turned into a restart: it aborts as
// before, so a loop stays visible instead of hiding behind clean restarts.
constexpr std::uint32_t kOomRepeatWindowS = 60;
constexpr std::uint32_t kOomMagic = 0x4F4F4D31u;

struct OomRecord {
  std::uint32_t magic = 0;          // kOomMagic when a record is there
  std::uint32_t free_bytes = 0;
  std::uint32_t largest_bytes = 0;
  std::uint32_t uptime_s = 0;       // running time of the boot that failed
  std::uint32_t run_clock_s = 0;    // the running clock (Ledger::clock) at the failure
  std::uint32_t check = 0;
};

inline std::uint32_t oom_check(const OomRecord& r) {
  std::uint32_t h = 2166136261u ^ 0x4F4F4D32u;
  for (std::uint32_t v : {r.magic, r.free_bytes, r.largest_bytes, r.uptime_s, r.run_clock_s}) for (int i = 0; i < 4; ++i) { h ^= (v >> (8 * i)) & 0xFFu; h *= 16777619u; }
  return h;
}
inline OomRecord make_oom(std::uint32_t free_bytes, std::uint32_t largest_bytes, std::uint32_t uptime_s, std::uint32_t run_clock_s) {
  OomRecord r;
  r.magic = kOomMagic; r.free_bytes = free_bytes; r.largest_bytes = largest_bytes; r.uptime_s = uptime_s; r.run_clock_s = run_clock_s;
  r.check = oom_check(r);
  return r;
}
inline bool oom_valid(const OomRecord& r) { return r.magic == kOomMagic && r.check == oom_check(r); }

enum class OomAction { kRestart, kAbort };
// `previous` is the record the last boot left (valid or not), `run_clock_s` the running clock now, `writing_firmware` whether an image is being written.
inline OomAction oom_decide(const OomRecord& previous, std::uint32_t run_clock_s, bool writing_firmware) {
  if (writing_firmware) return OomAction::kAbort;
  if (oom_valid(previous) && run_clock_s >= previous.run_clock_s && run_clock_s - previous.run_clock_s < kOomRepeatWindowS) return OomAction::kAbort;
  return OomAction::kRestart;
}
inline std::string oom_text(const OomRecord& r) { return "oom heap=" + std::to_string(r.free_bytes) + "/" + std::to_string(r.largest_bytes) + " after " + std::to_string(r.uptime_s) + " s"; }

// ---- the dead-man switch -----------------------------------------------------------------------------------------------------------------------------------------------
// A node that has not published anything to the broker for ten minutes while a broker is set up is stuck in a way nothing else notices: it restarts (through the same ledger, at most three an
// hour). After the third it stays up and carries on locally (its panel, its sensors, its relays), degraded, and says so. A node with no broker at all is never restarted by this.
constexpr std::uint64_t kDeadManSilenceMs = 10u * 60u * 1000u;

enum class DeadMan { kFine, kRestart, kDegraded };
inline DeadMan dead_man(bool broker_in_use, std::uint64_t now_ms, std::uint64_t last_publish_ms, bool may_restart) {
  if (!broker_in_use) return DeadMan::kFine;
  const std::uint64_t quiet = now_ms >= last_publish_ms ? now_ms - last_publish_ms : 0;
  if (quiet < kDeadManSilenceMs) return DeadMan::kFine;
  return may_restart ? DeadMan::kRestart : DeadMan::kDegraded;
}

// ---- the new firmware's probation and the settings after a start --------------------------------------------------------------------------------------------------
// A new image is confirmed only after it has run `kValidationWindowS` seconds with the panel up and the node reachable. The settings are read again `kSettingsCheckDelayS` seconds after the start and
// compared with what the first read said: a difference is a silent revert (a failed save, a damaged store) and is told.
constexpr std::uint32_t kValidationWindowS = 60;
constexpr std::uint32_t kSettingsCheckDelayS = 15;
inline bool image_may_be_confirmed(std::uint32_t seconds_running_ok, bool panel_up, bool reachable) { return panel_up && reachable && seconds_running_ok >= kValidationWindowS; }
inline bool settings_reverted(std::uint32_t first_read_hash, std::uint32_t later_read_hash) { return first_read_hash != later_read_hash; }

}  // namespace armor::nodeguard
