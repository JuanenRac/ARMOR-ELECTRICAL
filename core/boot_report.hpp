// ARMOR-NODE-ELECTRICAL - what a node says about the way it last started: the report of a dirty boot. No hardware.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// A node that restarts because it crashed, because a watchdog bit, because the supply sagged, because it ran out of memory or because its own guard restarted it, tells so ONCE after it
// is back: why (the cause and the reset reason of the chip), how long it had been running, the internal memory it had left a moment before (the guard writes it in memory that survives a
// software reset every two seconds), whether a core dump is waiting in flash, and whether the restart was asked for on purpose (the server's order, the panel) or was the guard's. A clean
// start (power on, the reset pin, a restart someone asked for) says nothing: the report is not there at all.
// The report travels inside the node's health report (the `boot` member of the node_health contract) for the first ten minutes of the run; the server keeps it once, in the node's log of
// restarts and in the History. main/node_guard.cpp reads the real values; the rules below are tested on a computer.
#pragma once
#include <cstdint>
#include <string>

#include "json.hpp"
#include "node_guard.hpp"

namespace armor::bootreport {

// How long the report stays in the health reports after a start. The server tells a repeated report from a new one by the moment the node started.
constexpr std::uint32_t kReportForS = 600;

enum class Cause {
  kNone,
  kPanic, kTaskWatchdog, kInterruptWatchdog, kOtherWatchdog, kBrownout, kPowerGlitch, kCpuLockup,
  kOutOfMemory,
  kGuardMissingTask, kGuardLowMemory, kGuardDeadMan, kGuard,
};

inline const char* cause_name(Cause cause) {
  switch (cause) {
    case Cause::kPanic: return "panic";
    case Cause::kTaskWatchdog: return "task_watchdog";
    case Cause::kInterruptWatchdog: return "interrupt_watchdog";
    case Cause::kOtherWatchdog: return "other_watchdog";
    case Cause::kBrownout: return "brownout";
    case Cause::kPowerGlitch: return "power_glitch";
    case Cause::kCpuLockup: return "cpu_lockup";
    case Cause::kOutOfMemory: return "out_of_memory";
    case Cause::kGuardMissingTask: return "guard_missing_task";
    case Cause::kGuardLowMemory: return "guard_low_memory";
    case Cause::kGuardDeadMan: return "guard_dead_man";
    case Cause::kGuard: return "guard";
    case Cause::kNone: break;
  }
  return "none";
}

// The cause the chip's own reset reason says (the words of the node_health contract), or none when the reason is an ordinary one.
inline Cause cause_of_reset(const std::string& reset_reason) {
  if (reset_reason == "panic") return Cause::kPanic;
  if (reset_reason == "task_wdt") return Cause::kTaskWatchdog;
  if (reset_reason == "int_wdt") return Cause::kInterruptWatchdog;
  if (reset_reason == "other_wdt") return Cause::kOtherWatchdog;
  if (reset_reason == "brownout") return Cause::kBrownout;
  if (reset_reason == "power_glitch") return Cause::kPowerGlitch;
  if (reset_reason == "cpu_lockup") return Cause::kCpuLockup;
  return Cause::kNone;
}

// ---- what the guard keeps across a restart -----------------------------------------------------------------------------------------------------------------------

// Why the guard asked for its restart. Written right before the restart, read once at the next boot.
enum class Why : std::uint32_t { kNone = 0, kMissingTask = 1, kLowMemory = 2, kDeadMan = 3 };
constexpr std::uint32_t kWhyMagic = 0x57485931u;

struct WhyRecord {
  std::uint32_t magic = 0;
  std::uint32_t why = 0;
  std::uint32_t check = 0;
};
inline std::uint32_t why_check(const WhyRecord& r) { return (r.magic ^ 0x9E3779B9u) + r.why * 2654435761u; }
inline WhyRecord make_why(Why why) {
  WhyRecord r;
  r.magic = kWhyMagic;
  r.why = static_cast<std::uint32_t>(why);
  r.check = why_check(r);
  return r;
}
inline bool why_valid(const WhyRecord& r) { return r.magic == kWhyMagic && r.check == why_check(r) && r.why >= 1 && r.why <= 3; }

// The last look the guard took before the node went down: how long it had been up and the internal memory. A few seconds old at most (the guard looks every two seconds).
constexpr std::uint32_t kCrashMagic = 0x43525331u;
struct CrashRecord {
  std::uint32_t magic = 0;
  std::uint32_t uptime_s = 0;
  std::uint32_t internal_free = 0;
  std::uint32_t internal_largest = 0;
  std::uint32_t check = 0;
};
inline std::uint32_t crash_check(const CrashRecord& r) {
  std::uint32_t h = 2166136261u ^ 0x43525332u;
  for (std::uint32_t v : {r.magic, r.uptime_s, r.internal_free, r.internal_largest}) for (int i = 0; i < 4; ++i) { h ^= (v >> (8 * i)) & 0xFFu; h *= 16777619u; }
  return h;
}
inline CrashRecord make_crash(std::uint32_t uptime_s, std::uint32_t internal_free, std::uint32_t internal_largest) {
  CrashRecord r;
  r.magic = kCrashMagic;
  r.uptime_s = uptime_s;
  r.internal_free = internal_free;
  r.internal_largest = internal_largest > internal_free ? internal_free : internal_largest;
  r.check = crash_check(r);
  return r;
}
inline bool crash_valid(const CrashRecord& r) { return r.magic == kCrashMagic && r.check == crash_check(r); }

// ---- the report ---------------------------------------------------------------------------------------------------------------------------------------------------

// What the boot code knows about the start that just happened.
struct Facts {
  std::string reset_reason = "unknown";   // the words of the contract (nodehealth::reset_reason_text)
  bool kept_memory = false;               // the reset left the memory that survives a software reset as it was (a software reset or a crash, not a power-on)
  bool by_guard = false;                  // the ledger says the guard asked for the last restart
  bool deliberate = false;                // someone asked for the last restart on purpose
  WhyRecord why;                          // what the guard wrote about its restart (valid only when kept_memory)
  nodeguard::OomRecord oom;               // the record an out-of-memory left (valid only when it checks)
  CrashRecord crash;                      // the guard's last look (valid only when kept_memory and it checks)
  bool coredump_present = false;
};

struct Report {
  bool dirty = false;
  Cause cause = Cause::kNone;
  std::string reset_reason = "unknown";
  bool has_uptime_before = false;
  std::uint32_t uptime_before_s = 0;
  bool has_heap = false;
  std::uint32_t heap_internal_free = 0;
  std::uint32_t heap_internal_largest = 0;
  bool coredump = false;
  bool deliberate = false;
  bool guard = false;
};

inline Cause guard_cause(const WhyRecord& why, bool kept) {
  if (!kept || !why_valid(why)) return Cause::kGuard;
  switch (static_cast<Why>(why.why)) {
    case Why::kMissingTask: return Cause::kGuardMissingTask;
    case Why::kLowMemory: return Cause::kGuardLowMemory;
    case Why::kDeadMan: return Cause::kGuardDeadMan;
    case Why::kNone: break;
  }
  return Cause::kGuard;
}

// The report of a start. An out-of-memory comes first (it is the most telling), then the guard's own restart, then the reset reason of the chip. Nothing else is dirty: a power-on, the
// reset pin, a restart asked for on purpose and a plain software restart leave `dirty` false.
inline Report classify(const Facts& facts) {
  Report out;
  out.reset_reason = facts.reset_reason;
  out.coredump = facts.coredump_present;
  out.deliberate = facts.deliberate;
  out.guard = facts.by_guard;
  const bool oom = facts.kept_memory && nodeguard::oom_valid(facts.oom);
  if (oom) out.cause = Cause::kOutOfMemory;
  else if (facts.by_guard) out.cause = guard_cause(facts.why, facts.kept_memory);
  else out.cause = cause_of_reset(facts.reset_reason);
  out.dirty = out.cause != Cause::kNone;
  if (!out.dirty) return out;
  if (oom) {
    out.has_uptime_before = true;
    out.uptime_before_s = facts.oom.uptime_s;
    out.has_heap = true;
    out.heap_internal_free = facts.oom.free_bytes;
    out.heap_internal_largest = facts.oom.largest_bytes > facts.oom.free_bytes ? facts.oom.free_bytes : facts.oom.largest_bytes;
  } else if (facts.kept_memory && crash_valid(facts.crash)) {
    out.has_uptime_before = true;
    out.uptime_before_s = facts.crash.uptime_s;
    out.has_heap = true;
    out.heap_internal_free = facts.crash.internal_free;
    out.heap_internal_largest = facts.crash.internal_largest > facts.crash.internal_free ? facts.crash.internal_free : facts.crash.internal_largest;
  }
  return out;
}

// Whether the health report carries it now: only a dirty start, and only during the first minutes of the run.
inline bool should_report(const Report& report, std::uint64_t uptime_s) { return report.dirty && uptime_s < kReportForS; }

// The `boot` member of the health report (the contract's node_health): the cause, the reset reason, the last look before the end when there is one, the core dump and who asked for the restart.
inline void write_json(json::Writer& w, const Report& r) {
  w.begin_object().field("cause", cause_name(r.cause)).field("reset_reason", r.reset_reason);
  if (r.has_uptime_before) w.key("uptime_before_s").integer(static_cast<long long>(r.uptime_before_s));
  if (r.has_heap) {
    w.key("heap_internal_free").integer(static_cast<long long>(r.heap_internal_free));
    w.key("heap_internal_largest").integer(static_cast<long long>(r.heap_internal_largest > r.heap_internal_free ? r.heap_internal_free : r.heap_internal_largest));
  }
  w.field("coredump", r.coredump).field("deliberate", r.deliberate).field("guard", r.guard).end_object();
}

// ---- the state of the gate at the door of the panel ---------------------------------------------------------------------------------------------------------

// The memory gate as the health report tells it: open or closed, how many times it changed, and the levels it works by (so a console can say what "closed" means for this firmware).
struct GateInfo {
  bool closed = false;
  std::uint32_t transitions = 0;
  nodeguard::GateThresholds levels;
};

inline void write_gate_json(json::Writer& w, const GateInfo& g) {
  const nodeguard::GateThresholds t = g.levels.sane();
  w.begin_object().field("closed", g.closed).key("transitions").integer(static_cast<long long>(g.transitions));
  w.key("close_free").integer(t.close_free).key("close_block").integer(t.close_block).key("open_free").integer(t.open_free).key("open_block").integer(t.open_block).end_object();
}

// What the settings checks of the start found, said while they are news (the same ten minutes): the snapshot taken before an update was put back, or the settings read again fifteen seconds
// after the start were not what had been read first and were written again.
struct ConfigCheck {
  const char* snapshot = "";   // "", "restored" or "failed"
  const char* reverted = "";   // "", "reapplied" or "failed"
  bool any() const { return *snapshot != '\0' || *reverted != '\0'; }
};
inline void write_config_check_json(json::Writer& w, const ConfigCheck& c) {
  w.begin_object();
  if (*c.snapshot != '\0') w.field("snapshot", c.snapshot);
  if (*c.reverted != '\0') w.field("reverted", c.reverted);
  w.end_object();
}

}  // namespace armor::bootreport
