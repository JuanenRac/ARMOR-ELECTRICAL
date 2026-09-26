// ARMOR-ELECTRICAL - the rules for switching, kept apart from any hardware and tested to exhaustion on a computer.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// SWITCHING IS OFF unless the node was built and set up to allow it (`Config::allowed`). These controllers are the SOFTWARE layer of the protection and never the
// only one: a source transfer must ALSO have a mechanical interlock between its two contactors, contactors sized and certified for the load, and the protections of an
// installation (see docs/SAFETY.md). Nothing here has switched anything: no hardware is driven by this project yet.
//
// What they guarantee, and what the tests check over a hundred thousand random steps:
//   - Opening is always allowed and always immediate: a request to open (or a fault, or the switching being off) drops every coil in the very next `tick`.
//   - Closing needs, all of them: the switching allowed, no fault, the node ARMED a moment before (a request without `arm` is refused), and, for a transfer, both
//     contactors CONFIRMED open by their auxiliary contacts for the whole dead time. A transfer never commands two sources at once.
//   - What the auxiliary contact says is checked against what was commanded: a contactor that does not close, or does not open, in time is a FAULT. A fault opens
//     everything and stays until `acknowledge` is called with both confirmed open; it never clears itself.
#pragma once
#include <cstdint>

namespace armor::electrical {

enum class Source { kNone, kA, kB };
enum class Fault { kNone, kDidNotClose, kDidNotOpen, kBothClosed, kDisabled };
enum class Refusal { kNone, kDisabled, kFault, kNotArmed, kNotConfirmedOpen };

struct Feedback { bool a_closed = false, b_closed = false; };   // the auxiliary contacts: true = confirmed closed
struct Coils { bool a = false, b = false; };                    // what to drive: true = energised = closed

struct TransferConfig {
  bool allowed = false;                        // the switching as a whole; off unless the firmware was built and set up for it
  std::uint32_t dead_time_ms = 2000;           // how long both are confirmed open before either may close
  std::uint32_t confirm_timeout_ms = 1000;     // how long a contactor may take to show what it was told
  std::uint32_t arm_window_ms = 10'000;        // how long an `arm` lasts
};

/// Two sources, one line, never both. `A` and `B` are, for example, the grid and an inverter's output.
class TransferController {
 public:
  explicit TransferController(TransferConfig config = {}) : config_(config) {}

  /// The operator's first step: the next request to close is accepted for `arm_window_ms`.
  void arm(std::uint64_t now_ms) { armed_until_ms_ = now_ms + config_.arm_window_ms; }
  bool armed(std::uint64_t now_ms) const { return now_ms < armed_until_ms_; }
  /// Withdraw an `arm` that was not used (a request that came with the wrong credentials). Never opens or closes anything.
  void disarm() { armed_until_ms_ = 0; }

  /// Ask for a source (or none). Opening (`kNone`) is always accepted. Returns why a request to close was refused.
  Refusal request(Source wanted, std::uint64_t now_ms) {
    if (wanted == Source::kNone) { wanted_ = Source::kNone; armed_until_ms_ = 0; return Refusal::kNone; }
    if (!config_.allowed) return Refusal::kDisabled;
    if (fault_ != Fault::kNone) return Refusal::kFault;
    if (!armed(now_ms)) return Refusal::kNotArmed;
    if (wanted == selected_ && phase_ != Phase::kOpening) { armed_until_ms_ = 0; return Refusal::kNone; }
    wanted_ = wanted;
    armed_until_ms_ = 0;   // one arm, one request
    return Refusal::kNone;
  }

  /// Advance with the auxiliary contacts as they are now. Call it often (every few tens of milliseconds).
  void tick(Feedback feedback, std::uint64_t now_ms) {
    if (fault_ != Fault::kNone) { coils_ = {}; return; }
    if (!config_.allowed) { if (coils_.a || coils_.b) coils_ = {}; wanted_ = Source::kNone; selected_ = Source::kNone; phase_ = Phase::kOpen; return; }
    if (feedback.a_closed && feedback.b_closed) { latch(Fault::kBothClosed); return; }
    switch (phase_) {
      case Phase::kOpen:
        coils_ = {};
        if (feedback.a_closed || feedback.b_closed) {               // something is closed that nobody asked for
          if (opened_since_ms_ != kNever) { opened_since_ms_ = kNever; }
          if (open_deadline_ms_ == kNever) open_deadline_ms_ = now_ms + config_.confirm_timeout_ms;
          else if (now_ms >= open_deadline_ms_) latch(Fault::kDidNotOpen);
          return;
        }
        open_deadline_ms_ = kNever;
        if (opened_since_ms_ == kNever) opened_since_ms_ = now_ms;
        if (wanted_ != Source::kNone && now_ms - opened_since_ms_ >= config_.dead_time_ms) {
          selected_ = wanted_;
          coils_ = {selected_ == Source::kA, selected_ == Source::kB};
          deadline_ms_ = now_ms + config_.confirm_timeout_ms;
          phase_ = Phase::kClosing;
        }
        return;
      case Phase::kClosing: {
        if (wanted_ != selected_) { open_all(now_ms); return; }   // changed its mind: open first
        const bool shows = selected_ == Source::kA ? feedback.a_closed : feedback.b_closed;
        const bool other = selected_ == Source::kA ? feedback.b_closed : feedback.a_closed;
        if (other) { latch(Fault::kBothClosed); return; }
        if (shows) { phase_ = Phase::kClosed; return; }
        if (now_ms >= deadline_ms_) latch(Fault::kDidNotClose);
        return;
      }
      case Phase::kClosed: {
        if (wanted_ != selected_) { open_all(now_ms); return; }
        const bool shows = selected_ == Source::kA ? feedback.a_closed : feedback.b_closed;
        if (!shows) { latch(Fault::kDidNotClose); return; }        // it fell out on its own
        return;
      }
      case Phase::kOpening:
        coils_ = {};
        if (!feedback.a_closed && !feedback.b_closed) { phase_ = Phase::kOpen; opened_since_ms_ = now_ms; open_deadline_ms_ = kNever; selected_ = Source::kNone; return; }
        if (now_ms >= deadline_ms_) latch(Fault::kDidNotOpen);
        return;
    }
  }

  /// Clear a fault: only when both contactors are confirmed open, and the switching is still allowed.
  bool acknowledge(Feedback feedback, std::uint64_t now_ms) {
    if (fault_ == Fault::kNone) return true;
    if (feedback.a_closed || feedback.b_closed) return false;
    fault_ = Fault::kNone; wanted_ = Source::kNone; selected_ = Source::kNone; phase_ = Phase::kOpen; opened_since_ms_ = now_ms; open_deadline_ms_ = kNever; coils_ = {};
    return true;
  }

  Coils coils() const { return coils_; }
  Source selected() const { return phase_ == Phase::kClosed ? selected_ : Source::kNone; }
  Source wanted() const { return wanted_; }
  Fault fault() const { return fault_; }
  bool closing() const { return phase_ == Phase::kClosing; }

 private:
  enum class Phase { kOpen, kClosing, kClosed, kOpening };
  static constexpr std::uint64_t kNever = ~0ULL;
  void open_all(std::uint64_t now_ms) { coils_ = {}; phase_ = Phase::kOpening; deadline_ms_ = now_ms + config_.confirm_timeout_ms; }
  void latch(Fault fault) { fault_ = fault; coils_ = {}; wanted_ = Source::kNone; }

  TransferConfig config_;
  Coils coils_;
  Source wanted_ = Source::kNone, selected_ = Source::kNone;
  Phase phase_ = Phase::kOpen;
  Fault fault_ = Fault::kNone;
  std::uint64_t armed_until_ms_ = 0, deadline_ms_ = 0, opened_since_ms_ = kNever, open_deadline_ms_ = kNever;
};

}  // namespace armor::electrical
