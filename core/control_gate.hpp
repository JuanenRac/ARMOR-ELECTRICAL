// ARMOR-ELECTRICAL - the rules of the orders the server can give a node: restart, pause, resume, standby, identify, diagnostics and maintenance. No hardware.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The server (and nobody else: the broker's access list lets only its identity write armor/control/.../command) gives an order in two steps, as for a switch: `arm`, to which the node answers with a
// one-time token that belongs to THAT operation, and `execute` carrying the token within thirty seconds. A token is used up by the first execute that presents it, a wrong one (or one armed for
// another operation) disarms, an old one is refused, and an order that arrives twice finds no token the second time, so a message the broker held or somebody copied moves nothing.
//
// The node is in one of three states: running; paused (its work stops for 60 to 3600 seconds and starts again by itself; a restart ends it); standby (its work is off with no end and the state is
// kept across restarts, until a resume). Paused and standby are not a power-off: the node stays on the network, answers every order and keeps its outputs in their safe state. Besides the state there
// is a maintenance flag (the server holds back the node-offline alert while it is on) and an identify window (the node shows itself). Restart, pause and standby are refused while the node installs a
// firmware, receives an upload, copies a slot or runs a battery operation. There is no order that erases a node's settings or credentials, none for deep sleep and none that sets a wake timer.
// The order is the contract's `node_control_command` (ARMOR-COMMON), the answer its `node_control_result`.
// Everything here is a function of the text and the numbers handed in, so it is tested on a computer; main/node_control.cpp does the talking, the timer and the flash.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "json.hpp"
#include "node_id.hpp"

namespace armor::control {

constexpr const char* kBroadcast = "all";           // the node id on the topic that every node listens to (a node cannot be called so)
constexpr std::uint32_t kTokenLifeS = 30;           // how long the token of an arm is good for
constexpr std::uint32_t kMinUptimeS = 60;           // at most one restart a minute: a node that is younger than this refuses to restart
constexpr std::uint32_t kMinArmGapMs = 1000;        // an arm of a restart that comes a second after the last one is ignored (it would only change the token)
constexpr std::uint32_t kSlotSeconds = 30;          // the wait of one place in a rolling restart
constexpr int kMaxSlot = 63;
constexpr std::uint32_t kMaxDelayS = 600;
constexpr std::uint32_t kLeaveSeconds = 3;          // always: the answer `restarting` has to leave the node before it goes
constexpr unsigned kJitterSlots = 16;               // a rolling order without a place: the node picks one from its name

constexpr std::uint32_t kPauseMinS = 60, kPauseMaxS = 3600, kPauseDefaultS = 3600;
constexpr std::uint32_t kIdentifyMinS = 5, kIdentifyMaxS = 300, kIdentifyDefaultS = 30;
constexpr std::uint32_t kMaintenanceMinS = 60, kMaintenanceMaxS = 28800, kMaintenanceDefaultS = 3600;

enum class Operation { kRestart, kPause, kResume, kStandby, kIdentify, kDiagnostics, kMaintenanceOn, kMaintenanceOff };
enum class Action { kArm, kExecute };
enum class Mode { kNow, kRolling };
enum class Busy { kNone, kUpdate, kUpload, kSlotCopy, kBattery };
enum class State { kRunning, kPaused, kStandby };
enum class Refusal {
  kNone, kNotArmed, kInvalidToken, kTokenExpired, kRateLimited, kAlreadyRestarting, kAlreadyInState,
  kBusyUpdate, kBusyUpload, kBusySlotCopy, kBusyBattery, kUnavailable
};

inline const char* operation_name(Operation operation) {
  switch (operation) {
    case Operation::kRestart: return "restart";
    case Operation::kPause: return "pause";
    case Operation::kResume: return "resume";
    case Operation::kStandby: return "standby";
    case Operation::kIdentify: return "identify";
    case Operation::kDiagnostics: return "diagnostics";
    case Operation::kMaintenanceOn: return "maintenance_on";
    case Operation::kMaintenanceOff: return "maintenance_off";
  }
  return "diagnostics";
}

inline bool operation_from_name(std::string_view name, Operation& out) {
  for (const Operation candidate : {Operation::kRestart, Operation::kPause, Operation::kResume, Operation::kStandby, Operation::kIdentify, Operation::kDiagnostics,
                                    Operation::kMaintenanceOn, Operation::kMaintenanceOff}) {
    if (name == operation_name(candidate)) { out = candidate; return true; }
  }
  return false;
}

inline const char* state_name(State state) { return state == State::kPaused ? "paused" : state == State::kStandby ? "standby" : "running"; }

inline const char* refusal_name(Refusal refusal) {
  switch (refusal) {
    case Refusal::kNone: return "none";
    case Refusal::kNotArmed: return "not_armed";
    case Refusal::kInvalidToken: return "invalid_token";
    case Refusal::kTokenExpired: return "token_expired";
    case Refusal::kRateLimited: return "rate_limited";
    case Refusal::kAlreadyRestarting: return "already_restarting";
    case Refusal::kAlreadyInState: return "already_in_state";
    case Refusal::kBusyUpdate: return "busy_update";
    case Refusal::kBusyUpload: return "busy_upload";
    case Refusal::kBusySlotCopy: return "busy_slot_copy";
    case Refusal::kBusyBattery: return "busy_battery";
    case Refusal::kUnavailable: return "unavailable";
  }
  return "unavailable";
}

inline Refusal refusal_for(Busy busy) {
  switch (busy) {
    case Busy::kUpdate: return Refusal::kBusyUpdate;
    case Busy::kUpload: return Refusal::kBusyUpload;
    case Busy::kSlotCopy: return Refusal::kBusySlotCopy;
    case Busy::kBattery: return Refusal::kBusyBattery;
    case Busy::kNone: break;
  }
  return Refusal::kNone;
}

inline const char* mode_name(Mode mode) { return mode == Mode::kRolling ? "rolling" : "now"; }

// Restart, pause and standby stop what the node is in the middle of; the others are always allowed.
inline bool refused_while_busy(Operation operation) { return operation == Operation::kRestart || operation == Operation::kPause || operation == Operation::kStandby; }

// The seconds an operation lasts when the order names none, and the range the order may name (all zero: the operation takes no duration).
struct Span { std::uint32_t low, high, fallback; };
inline Span span_of(Operation operation) {
  switch (operation) {
    case Operation::kPause: return {kPauseMinS, kPauseMaxS, kPauseDefaultS};
    case Operation::kIdentify: return {kIdentifyMinS, kIdentifyMaxS, kIdentifyDefaultS};
    case Operation::kMaintenanceOn: return {kMaintenanceMinS, kMaintenanceMaxS, kMaintenanceDefaultS};
    default: break;
  }
  return {0, 0, 0};
}

inline std::string command_topic(const std::string& node_id) { return node_id_is_valid(node_id) ? "armor/control/" + node_id + "/command" : ""; }
inline std::string broadcast_topic() { return std::string("armor/control/") + kBroadcast + "/command"; }
inline std::string result_topic(const std::string& node_id) { return node_id_is_valid(node_id) ? "armor/control/" + node_id + "/result" : ""; }

// A node called `all` would take the broadcast for its own order: it gets no control at all.
inline bool node_can_be_controlled(const std::string& node_id) { return node_id_is_valid(node_id) && node_id != kBroadcast; }

namespace detail {
inline bool lower_alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }
inline bool code_of(const std::string& text, std::size_t lowest) {
  if (text.size() < lowest || text.size() > 32) return false;
  for (const char c : text) if (!lower_alnum(c)) return false;
  return true;
}
inline bool person_of(const std::string& text) {
  if (text.empty() || text.size() > 64) return false;
  for (const char c : text) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '@' || c == ' ' || c == '-')) return false;
  return true;
}
inline bool integer_in(const json::Value* v, long long lowest, long long highest, long long& out) {
  if (v == nullptr || !v->is_number() || !std::isfinite(v->number) || v->number != std::floor(v->number) || v->number < static_cast<double>(lowest) || v->number > static_cast<double>(highest)) return false;
  out = static_cast<long long>(v->number);
  return true;
}
}  // namespace detail

struct Command {
  std::string node_id, command_id, token, requested_by;
  Action action = Action::kArm;
  Operation operation = Operation::kRestart;
  Mode mode = Mode::kNow;
  std::uint32_t delay_s = 0;
  int slot = -1;                // -1: the order names no place
  std::uint32_t duration_s = 0; // 0: the order names no duration (the default of the operation applies)
};

// The seconds a pause, identify or maintenance lasts: what the order says, or the default of the operation.
inline std::uint32_t duration_of(const Command& command) {
  const Span span = span_of(command.operation);
  return command.duration_s != 0 ? command.duration_s : span.fallback;
}

// The order in `text`, or nothing (with the reason in `why`). Strict, as the schema and the rules of the contract are: every field of the contract present and of the right kind, no other field,
// an action, an operation and a mode from the lists, a token exactly on an execute (never for `all`: its token is one node's), a delay and a place only on the execute of a restart, a place only in
// the rolling mode, the rolling mode only for a restart, a duration only on the execute of pause, identify or maintenance_on and inside that operation's range.
// It does not check that the order is for this node: the caller compares `node_id` with its own and with `all`.
inline bool parse_command(std::string_view text, Command& out, std::string* why = nullptr) {
  auto fail = [&](const char* reason) { if (why != nullptr) *why = reason; return false; };
  json::Value doc;
  if (!json::parse(text, doc) || !doc.is_object()) return fail("not a JSON object");
  static const char* const known[] = {"kind", "node_id", "timestamp_ms", "command_id", "action", "operation", "mode", "delay_s", "slot", "duration_s", "token", "requested_by"};
  for (const std::string& name : doc.names) {
    bool found = false;
    for (const char* k : known) found = found || name == k;
    if (!found) return fail("a field that is not in the contract");
  }
  const json::Value* kind = doc.get("kind");
  if (kind == nullptr || !kind->is_string() || kind->text != "node_control_command") return fail("kind");
  const json::Value* node = doc.get("node_id");
  if (node == nullptr || !node->is_string() || !node_id_is_valid(node->text)) return fail("node_id");
  long long stamp = 0;
  if (!detail::integer_in(doc.get("timestamp_ms"), 0, 9007199254740991LL, stamp)) return fail("timestamp_ms");
  const json::Value* id = doc.get("command_id");
  if (id == nullptr || !id->is_string() || !detail::code_of(id->text, 8)) return fail("command_id");
  const json::Value* action = doc.get("action");
  if (action == nullptr || !action->is_string()) return fail("action");
  Command parsed;
  if (action->text == "arm") parsed.action = Action::kArm;
  else if (action->text == "execute") parsed.action = Action::kExecute;
  else return fail("action");
  const json::Value* operation = doc.get("operation");
  if (operation == nullptr || !operation->is_string() || !operation_from_name(operation->text, parsed.operation)) return fail("operation");
  const json::Value* mode = doc.get("mode");
  if (mode == nullptr || !mode->is_string()) return fail("mode");
  if (mode->text == "now") parsed.mode = Mode::kNow;
  else if (mode->text == "rolling") parsed.mode = Mode::kRolling;
  else return fail("mode");
  if (parsed.mode == Mode::kRolling && parsed.operation != Operation::kRestart) return fail("the rolling mode is only for a restart");
  const json::Value* who = doc.get("requested_by");
  if (who == nullptr || !who->is_string() || !detail::person_of(who->text)) return fail("requested_by");
  const bool executes = parsed.action == Action::kExecute;
  const json::Value* token = doc.get("token");
  if (executes != (token != nullptr)) return fail("a token goes on an execute and on nothing else");
  if (token != nullptr && (!token->is_string() || !detail::code_of(token->text, 16))) return fail("token");
  if (executes && node->text == kBroadcast) return fail("an execute is for one node");
  long long number = 0;
  if (doc.get("delay_s") != nullptr) {
    if (!executes || parsed.operation != Operation::kRestart) return fail("a delay goes on the execute of a restart");
    if (!detail::integer_in(doc.get("delay_s"), 0, kMaxDelayS, number)) return fail("delay_s");
    parsed.delay_s = static_cast<std::uint32_t>(number);
  }
  if (doc.get("slot") != nullptr) {
    if (!executes || parsed.operation != Operation::kRestart || parsed.mode != Mode::kRolling) return fail("a place goes on a rolling restart");
    if (!detail::integer_in(doc.get("slot"), 0, kMaxSlot, number)) return fail("slot");
    parsed.slot = static_cast<int>(number);
  }
  if (doc.get("duration_s") != nullptr) {
    const Span span = span_of(parsed.operation);
    if (!executes || span.high == 0) return fail("a duration goes on the execute of pause, identify or maintenance_on");
    if (!detail::integer_in(doc.get("duration_s"), span.low, span.high, number)) return fail("duration_s");
    parsed.duration_s = static_cast<std::uint32_t>(number);
  }
  parsed.node_id = node->text;
  parsed.command_id = id->text;
  parsed.requested_by = who->text;
  if (token != nullptr) parsed.token = token->text;
  out = parsed;
  return true;
}

// A place in a rolling restart for a node whose order names none: the same name always gives the same place, spread over sixteen of them.
inline unsigned jitter_slot(const std::string& node_id) {
  std::uint32_t hash = 2166136261u;   // FNV-1a
  for (const char c : node_id) { hash ^= static_cast<unsigned char>(c); hash *= 16777619u; }
  return hash % kJitterSlots;
}

// The seconds until the node restarts: the extra delay of the order, a few seconds for the answer to leave, and, in the rolling mode, thirty seconds for every place before this node's
// (the server gives the places, one to each node, so two nodes never restart in the same half minute; without a place the node picks one from its name).
inline std::uint32_t restart_in_s(const Command& command) {
  std::uint32_t wait = kLeaveSeconds + command.delay_s;
  if (command.mode == Mode::kRolling) wait += kSlotSeconds * (command.slot >= 0 ? static_cast<std::uint32_t>(command.slot) : jitter_slot(command.node_id));
  return wait;
}

// A token of 24 lowercase letters and digits from random bytes the caller took from the hardware generator (24 bytes; the bias of the remainder does not matter for a token that lives
// thirty seconds and is used once).
inline std::string token_from_bytes(const std::uint8_t* bytes, std::size_t count) {
  static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  std::string token;
  for (std::size_t i = 0; i < count && i < 24; ++i) token += alphabet[bytes[i] % 36];
  return token;
}

inline bool same_text(const std::string& a, const std::string& b) {   // no early exit: the time does not say how many characters were right
  unsigned diff = a.size() == b.size() ? 0u : 1u;
  const std::size_t n = a.size() < b.size() ? a.size() : b.size();
  for (std::size_t i = 0; i < n; ++i) diff |= static_cast<unsigned>(static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]));
  return diff == 0;
}

// ---- the state of the node -----------------------------------------------------------------------------------------------------------------

// What the node is doing now, which is what decides whether an order changes anything.
struct Situation {
  State state = State::kRunning;
  bool maintenance = false;
};

// Whether an order would find the node already as it asks.
inline bool already_in_state(Operation operation, const Situation& now) {
  switch (operation) {
    case Operation::kPause: return now.state != State::kRunning;       // a standby is not shortened into a pause that ends by itself
    case Operation::kStandby: return now.state == State::kStandby;
    case Operation::kResume: return now.state == State::kRunning;
    case Operation::kMaintenanceOn: return now.maintenance;
    case Operation::kMaintenanceOff: return !now.maintenance;
    default: break;
  }
  return false;
}

// What the node remembers between the two steps. One token at a time, bound to one operation; a new arm replaces the last one.
class Gate {
 public:
  struct Armed {
    Refusal refusal = Refusal::kNone;
    std::string token;   // only when the refusal is none
  };

  // Step one. `uptime_s` is how long the node has been running, `busy` what it is in the middle of, `fresh_token` a new token (see token_from_bytes).
  Armed arm(std::uint64_t now_ms, std::uint64_t uptime_s, Operation operation, Busy busy, const Situation& now, const std::string& fresh_token) {
    if (scheduled_) return {operation == Operation::kRestart ? Refusal::kAlreadyRestarting : Refusal::kUnavailable, {}};
    if (operation == Operation::kRestart && uptime_s < kMinUptimeS) return {Refusal::kRateLimited, {}};
    if (busy != Busy::kNone && refused_while_busy(operation)) { token_.clear(); return {refusal_for(busy), {}}; }
    if (already_in_state(operation, now)) return {Refusal::kAlreadyInState, {}};
    if (operation == Operation::kRestart && armed_before_ && now_ms - last_arm_ms_ < kMinArmGapMs) return {Refusal::kRateLimited, {}};
    if (operation == Operation::kRestart) { armed_before_ = true; last_arm_ms_ = now_ms; }
    token_ = fresh_token;
    operation_ = operation;
    expires_ms_ = now_ms + static_cast<std::uint64_t>(kTokenLifeS) * 1000ULL;
    return {Refusal::kNone, token_};
  }

  // Step two. The token is used up whatever comes of it: a wrong one disarms, one armed for another operation disarms, an old one is gone, one that was right but found the node busy is spent
  // too. On kNone the order is to be carried out (and a restart is now scheduled: nothing else is accepted until the node goes).
  Refusal execute(std::uint64_t now_ms, std::uint64_t uptime_s, Operation operation, Busy busy, const Situation& now, const std::string& token) {
    if (scheduled_) return operation == Operation::kRestart ? Refusal::kAlreadyRestarting : Refusal::kUnavailable;
    if (token_.empty()) return Refusal::kNotArmed;
    const bool expired = now_ms >= expires_ms_;
    const bool right = same_text(token_, token);
    const bool bound = operation_ == operation;
    token_.clear();
    if (expired) return Refusal::kTokenExpired;
    if (!right || !bound) return Refusal::kInvalidToken;
    if (operation == Operation::kRestart && uptime_s < kMinUptimeS) return Refusal::kRateLimited;
    if (busy != Busy::kNone && refused_while_busy(operation)) return refusal_for(busy);
    if (already_in_state(operation, now)) return Refusal::kAlreadyInState;
    if (operation == Operation::kRestart) scheduled_ = true;
    return Refusal::kNone;
  }

  bool armed() const { return !token_.empty(); }
  bool scheduled() const { return scheduled_; }

 private:
  std::string token_;
  Operation operation_ = Operation::kRestart;
  std::uint64_t expires_ms_ = 0;
  std::uint64_t last_arm_ms_ = 0;
  bool armed_before_ = false;
  bool scheduled_ = false;
};

// What changed when something was applied: the caller keeps the standby in flash, tells the node's work to stop or start, and the identify hardware to show or stop.
struct Change {
  bool state_changed = false;
  bool save_standby = false;    // the state is now standby: keep it (since, by) in flash
  bool clear_standby = false;   // the state left standby: forget it
  bool identify_changed = false;
};

// The state machine: running, paused and standby, the maintenance flag and the identify window, with the clock handed in (milliseconds since the node started). Pause, identify and maintenance
// windows end by themselves in tick(); a standby ends only with a resume; after a restart the machine starts running, except that restore_standby() puts back the standby that was kept.
class Machine {
 public:
  State state() const { return state_; }
  bool working() const { return state_ == State::kRunning; }
  bool maintenance() const { return maintenance_; }
  bool identifying() const { return identifying_; }
  std::uint64_t since_ms() const { return since_ms_; }          // the wall clock when the pause or standby began (0 when the node's clock was not set)
  const std::string& set_by() const { return by_; }
  Situation situation() const { return {state_, maintenance_}; }

  // Seconds left of the pause, the identify window and the maintenance window (-1: none is under way). A started window shows at least one second until it ends.
  long long pause_left_s(std::uint64_t now_ms) const { return state_ == State::kPaused ? left(paused_until_ms_, now_ms) : -1; }
  long long identify_left_s(std::uint64_t now_ms) const { return identifying_ ? left(identify_until_ms_, now_ms) : -1; }
  long long maintenance_left_s(std::uint64_t now_ms) const { return maintenance_ ? left(maintenance_until_ms_, now_ms) : -1; }

  // At boot: the standby that was kept in flash. Anything else (a pause, the maintenance flag) does not survive a restart.
  Change restore_standby(std::uint64_t since_ms, const std::string& by) {
    Change change;
    state_ = State::kStandby;
    since_ms_ = since_ms;
    by_ = detail::person_of(by) ? by : std::string();
    change.state_changed = true;
    return change;
  }

  Change pause(std::uint64_t now_ms, std::uint32_t duration_s, std::uint64_t wall_ms, const std::string& by) {
    Change change;
    if (state_ == State::kStandby) return change;   // a standby is never turned into a pause that ends by itself
    change.state_changed = state_ != State::kPaused;
    state_ = State::kPaused;
    paused_until_ms_ = now_ms + static_cast<std::uint64_t>(duration_s) * 1000ULL;
    since_ms_ = wall_ms;
    by_ = by;
    return change;
  }

  Change standby(std::uint64_t wall_ms, const std::string& by) {
    Change change;
    change.state_changed = state_ != State::kStandby;
    state_ = State::kStandby;
    since_ms_ = wall_ms;
    by_ = by;
    paused_until_ms_ = 0;
    change.save_standby = true;
    return change;
  }

  Change resume() {
    Change change;
    if (state_ == State::kRunning) return change;
    change.clear_standby = state_ == State::kStandby;
    change.state_changed = true;
    state_ = State::kRunning;
    since_ms_ = 0;
    by_.clear();
    paused_until_ms_ = 0;
    return change;
  }

  Change identify(std::uint64_t now_ms, std::uint32_t duration_s) {
    Change change;
    change.identify_changed = true;
    identifying_ = true;
    identify_until_ms_ = now_ms + static_cast<std::uint64_t>(duration_s) * 1000ULL;
    return change;
  }

  void maintenance_on(std::uint64_t now_ms, std::uint32_t duration_s) {
    maintenance_ = true;
    maintenance_until_ms_ = now_ms + static_cast<std::uint64_t>(duration_s) * 1000ULL;
  }
  void maintenance_off() { maintenance_ = false; maintenance_until_ms_ = 0; }

  // Once a second (or oftener): a pause that is over resumes the work, an identify window that is over stops, a maintenance that is over ends.
  Change tick(std::uint64_t now_ms) {
    Change change;
    if (state_ == State::kPaused && now_ms >= paused_until_ms_) {
      state_ = State::kRunning;
      since_ms_ = 0;
      by_.clear();
      change.state_changed = true;
    }
    if (identifying_ && now_ms >= identify_until_ms_) { identifying_ = false; change.identify_changed = true; }
    if (maintenance_ && now_ms >= maintenance_until_ms_) maintenance_off();
    return change;
  }

 private:
  static long long left(std::uint64_t until_ms, std::uint64_t now_ms) {
    if (now_ms >= until_ms) return 0;
    return static_cast<long long>((until_ms - now_ms + 999ULL) / 1000ULL);
  }
  State state_ = State::kRunning;
  bool maintenance_ = false;
  bool identifying_ = false;
  std::uint64_t since_ms_ = 0;
  std::uint64_t paused_until_ms_ = 0;
  std::uint64_t identify_until_ms_ = 0;
  std::uint64_t maintenance_until_ms_ = 0;
  std::string by_;
};

// ---- the standby kept in flash -------------------------------------------------------------------------------------------------------------

inline std::string standby_record(std::uint64_t since_ms, const std::string& by) {
  json::Writer w;
  w.begin_object().field("standby", true).key("since_ms").integer(static_cast<long long>(since_ms)).field("set_by", by).end_object();
  return w.str();
}

// What replaces the record when the node is resumed (flash holds one value under the key; this one says there is no standby).
inline std::string standby_cleared_record() { return "{\"standby\":false}"; }

// What was kept: the record is trusted only when it says standby and reads whole; anything else is a node that never was in standby.
inline bool parse_standby_record(std::string_view text, std::uint64_t& since_ms, std::string& by) {
  json::Value doc;
  if (!json::parse(text, doc) || !doc.is_object()) return false;
  const json::Value* on = doc.get("standby");
  if (on == nullptr || !on->is_bool() || !on->boolean) return false;
  long long since = 0;
  if (!detail::integer_in(doc.get("since_ms"), 0, 9007199254740991LL, since)) return false;
  const json::Value* who = doc.get("set_by");
  if (who == nullptr || !who->is_string() || (!who->text.empty() && !detail::person_of(who->text))) return false;
  since_ms = static_cast<std::uint64_t>(since);
  by = who->text;
  return true;
}

// ---- the answers --------------------------------------------------------------------------------------------------------------------------

inline void begin_result(json::Writer& w, const std::string& node_id, std::uint64_t timestamp_ms, const Command& command) {
  w.begin_object().field("kind", "node_control_result").field("node_id", node_id).key("timestamp_ms").integer(static_cast<long long>(timestamp_ms));
  w.field("command_id", command.command_id).field("action", command.action == Action::kArm ? "arm" : "execute").field("operation", operation_name(command.operation));
}

inline std::string armed_json(const std::string& node_id, std::uint64_t timestamp_ms, const Command& command, const std::string& token) {
  json::Writer w;
  begin_result(w, node_id, timestamp_ms, command);
  w.field("stage", "armed").field("accepted", true).field("refusal", "none").field("token", token).key("token_ttl_s").integer(kTokenLifeS).end_object();
  return w.str();
}

// The acknowledgement `node_restarting`: sent just before the node goes, so the server knows the silence that follows is the node's own doing.
inline std::string restarting_json(const std::string& node_id, std::uint64_t timestamp_ms, const Command& command, std::uint32_t in_s, std::uint64_t uptime_s, const std::string& firmware) {
  json::Writer w;
  begin_result(w, node_id, timestamp_ms, command);
  w.field("stage", "restarting").field("accepted", true).field("refusal", "none");
  w.field("reason", "requested_by_server").field("mode", mode_name(command.mode)).key("restart_in_s").integer(in_s > 3600 ? 3600 : in_s);
  w.key("uptime_s").integer(static_cast<long long>(uptime_s)).field("firmware", firmware).field("requested_by", command.requested_by).end_object();
  return w.str();
}

// What an order that was carried out leaves: the state the node is in, the maintenance flag, and the seconds left of the window the order started (pause, identify, maintenance_on).
inline std::string applied_json(const std::string& node_id, std::uint64_t timestamp_ms, const Command& command, const Machine& machine, std::uint64_t now_ms) {
  json::Writer w;
  begin_result(w, node_id, timestamp_ms, command);
  w.field("stage", "applied").field("accepted", true).field("refusal", "none");
  w.field("state", state_name(machine.state())).field("maintenance", machine.maintenance());
  long long until = -1;
  if (command.operation == Operation::kPause) until = machine.pause_left_s(now_ms);
  else if (command.operation == Operation::kIdentify) until = machine.identify_left_s(now_ms);
  else if (command.operation == Operation::kMaintenanceOn) until = machine.maintenance_left_s(now_ms);
  if (until >= 0) w.key("until_s").integer(until > 86400 ? 86400 : until);
  if (machine.state() != State::kRunning) w.key("since_ms").integer(static_cast<long long>(machine.since_ms()));
  w.field("requested_by", command.requested_by).end_object();
  return w.str();
}

// The numbers the snapshot of a `diagnostics` order is made of; the node reads them from its hardware. Nothing secret is in it, and there is no field that could hold one.
struct Diagnostics {
  std::string firmware;
  std::string reset_reason = "unknown";
  std::uint64_t uptime_s = 0;
  std::uint64_t heap_free = 0;
  std::uint64_t heap_min = 0;
  long long psram_free = -1;       // -1: the board has none
  long long stack_min_free = -1;   // -1: unknown
  bool has_rssi = false;
  int rssi = 0;
  bool mqtt_connected = false;
};

inline std::string diagnostics_json(const std::string& node_id, std::uint64_t timestamp_ms, const Command& command, const Diagnostics& d, const Machine& machine) {
  json::Writer w;
  begin_result(w, node_id, timestamp_ms, command);
  w.field("stage", "diagnostics").field("accepted", true).field("refusal", "none");
  w.key("diagnostics").begin_object().field("firmware", d.firmware).key("uptime_s").integer(static_cast<long long>(d.uptime_s)).field("reset_reason", d.reset_reason);
  const std::uint64_t heap_min = d.heap_min > d.heap_free ? d.heap_free : d.heap_min;
  w.key("heap_free").integer(static_cast<long long>(d.heap_free)).key("heap_min").integer(static_cast<long long>(heap_min));
  if (d.psram_free >= 0) w.key("psram_free").integer(d.psram_free);
  if (d.stack_min_free >= 0) w.key("stack_min_free").integer(d.stack_min_free);
  if (d.has_rssi) w.key("rssi").integer(d.rssi > 0 ? 0 : (d.rssi < -120 ? -120 : d.rssi));
  w.field("mqtt_connected", d.mqtt_connected).field("state", state_name(machine.state())).field("maintenance", machine.maintenance());
  w.end_object().end_object();
  return w.str();
}

inline std::string rejected_json(const std::string& node_id, std::uint64_t timestamp_ms, const Command& command, Refusal refusal) {
  json::Writer w;
  begin_result(w, node_id, timestamp_ms, command);
  w.field("stage", "rejected").field("accepted", false).field("refusal", refusal_name(refusal)).end_object();
  return w.str();
}

// ---- the planned-maintenance signal ------------------------------------------------------------------------------------------------------

// Why a node goes quiet on purpose. The order is the priority: when several are on at once, the first one is what the node says.
enum class Reason { kRestart, kStandby, kPause, kUpdate, kUpload, kOperator };
constexpr int kReasonCount = 6;
constexpr std::uint32_t kExpectedMaxS = 3600;          // the contract's limit for `expected_s`
constexpr std::uint32_t kRestartBootAllowanceS = 60;   // the time a restarting node is given to come back, on top of the wait before it goes

inline const char* reason_name(Reason reason) {
  switch (reason) {
    case Reason::kRestart: return "restart";
    case Reason::kStandby: return "standby";
    case Reason::kPause: return "pause";
    case Reason::kUpdate: return "update";
    case Reason::kUpload: return "upload";
    case Reason::kOperator: return "operator";
  }
  return "operator";
}
inline bool reason_from_name(std::string_view name, Reason& out) {
  for (int i = 0; i < kReasonCount; ++i) {
    const Reason candidate = static_cast<Reason>(i);
    if (name == reason_name(candidate)) { out = candidate; return true; }
  }
  return false;
}
inline std::string maintenance_topic(const std::string& node_id) { return node_id_is_valid(node_id) ? "armor/control/" + node_id + "/maintenance" : ""; }

// What to publish (retained) on the maintenance topic: `publish` says whether there is anything to say now.
struct Signal {
  bool publish = false;
  bool active = false;
  Reason reason = Reason::kRestart;
  std::uint32_t expected_s = 0;   // 0: the node gives no time (a standby has none)
  std::string by;
};

// Which planned-maintenance signal the node holds, with the clock handed in (milliseconds since the node started). Each reason is its own entry, so that a pause that ends does not clear the
// maintenance of an update that began meanwhile; what the node says is the entry with the highest priority. Every entry that has a time ends by itself when the time is over, unless the work it
// announced is still going (an update or an upload that is still running is renewed), so a node whose restart never came, or whose update failed without a word, does not keep a stale
// "active" for ever; a standby has no end and is cleared only by a resume. The signal is cleared when the node is back, and published retained either way, so the broker keeps nothing stale.
class MaintenanceSignal {
 public:
  bool active() const { return top() >= 0; }

  // The node is going quiet for `reason`, for about `expected_s` seconds (a standby has no end). Always says so (a fresh message).
  Signal raise(Reason reason, std::uint32_t expected_s, std::uint64_t now_ms, const std::string& by) {
    Slot& slot = slots_[static_cast<int>(reason)];
    if (expected_s > kExpectedMaxS) expected_s = kExpectedMaxS;
    slot.on = true;
    slot.span_s = reason == Reason::kStandby ? 0 : (expected_s == 0 ? 1 : expected_s);
    slot.until_ms = slot.span_s == 0 ? 0 : now_ms + static_cast<std::uint64_t>(slot.span_s) * 1000ULL;
    slot.by = detail::person_of(by) ? by : std::string();
    last_reason_ = reason;
    return say(now_ms);
  }

  // The node is no longer quiet for `reason` (the pause ended, the update failed or finished without a restart, the operator ended the window). Says something only if that reason was on:
  // what is left, or that there is nothing left.
  Signal lower(Reason reason, std::uint64_t now_ms) {
    Slot& slot = slots_[static_cast<int>(reason)];
    if (!slot.on) return Signal();
    slot = Slot();
    last_reason_ = reason;
    return say(now_ms);
  }

  // Once a second: the entries whose time is over end, except an update or an upload that is still going on (renewed).
  Signal tick(std::uint64_t now_ms, Busy busy) {
    bool changed = false;
    for (int i = 0; i < kReasonCount; ++i) {
      Slot& slot = slots_[i];
      if (!slot.on || slot.until_ms == 0 || now_ms < slot.until_ms) continue;
      const Reason reason = static_cast<Reason>(i);
      const bool going = (reason == Reason::kUpdate && (busy == Busy::kUpdate || busy == Busy::kSlotCopy)) || (reason == Reason::kUpload && busy == Busy::kUpload);
      if (going) { slot.until_ms = now_ms + static_cast<std::uint64_t>(slot.span_s) * 1000ULL; continue; }
      last_reason_ = reason;
      slot = Slot();
      changed = true;
    }
    return changed ? say(now_ms) : Signal();
  }

  // The broker connection is there (the first time after the boot, and after every reconnection): say what is true now. A node that boots into standby (`standby` true) says so and keeps it until
  // it is resumed; any other boot clears what a previous run left retained.
  Signal on_connected(bool standby, const std::string& by, std::uint64_t now_ms) {
    if (!booted_) {
      booted_ = true;
      if (standby) {
        Slot& slot = slots_[static_cast<int>(Reason::kStandby)];
        slot.on = true;
        slot.span_s = 0;
        slot.until_ms = 0;
        slot.by = detail::person_of(by) ? by : std::string();
      }
    }
    return say(now_ms);
  }

  // Seconds left of the entry that is said (0: none, or no end).
  std::uint32_t expected_now(std::uint64_t now_ms) const {
    const int i = top();
    if (i < 0 || slots_[i].until_ms == 0) return 0;
    if (slots_[i].until_ms <= now_ms) return 1;
    const std::uint64_t left = (slots_[i].until_ms - now_ms + 999ULL) / 1000ULL;
    return static_cast<std::uint32_t>(left > kExpectedMaxS ? kExpectedMaxS : left);
  }

 private:
  struct Slot {
    bool on = false;
    std::uint64_t until_ms = 0;
    std::uint32_t span_s = 0;
    std::string by;
  };
  int top() const {
    for (int i = 0; i < kReasonCount; ++i) if (slots_[i].on) return i;
    return -1;
  }
  Signal say(std::uint64_t now_ms) const {
    Signal signal;
    signal.publish = true;
    const int i = top();
    signal.active = i >= 0;
    signal.reason = i >= 0 ? static_cast<Reason>(i) : last_reason_;
    if (i >= 0) { signal.expected_s = expected_now(now_ms); signal.by = slots_[i].by; }
    return signal;
  }
  Slot slots_[kReasonCount];
  Reason last_reason_ = Reason::kRestart;
  bool booted_ = false;
};

// The message the node publishes retained on armor/control/<node>/maintenance (the contract's `node_maintenance`). The expected time is only given with an active signal and kept in 1..3600.
inline std::string maintenance_json(const std::string& node_id, std::uint64_t timestamp_ms, const Signal& signal, const std::string& firmware) {
  json::Writer w;
  w.begin_object().field("kind", "node_maintenance").field("node_id", node_id).key("timestamp_ms").integer(static_cast<long long>(timestamp_ms));
  w.field("active", signal.active).field("reason", reason_name(signal.reason));
  if (signal.active && signal.expected_s > 0) w.key("expected_s").integer(signal.expected_s > kExpectedMaxS ? kExpectedMaxS : signal.expected_s);
  w.field("firmware", firmware);
  if (!signal.by.empty() && detail::person_of(signal.by)) w.field("requested_by", signal.by);
  w.end_object();
  return w.str();
}

// ---- the whole of it ----------------------------------------------------------------------------------------------------------------------

// What the node knows when an order arrives.
struct Env {
  std::uint64_t now_ms = 0;      // since the node started (steady)
  std::uint64_t wall_ms = 0;     // the wall clock, 0 when the node's clock is not set
  std::uint64_t stamp_ms = 0;    // what goes in the answer's timestamp_ms: the wall clock when set, otherwise now_ms
  Busy busy = Busy::kNone;
  std::string fresh_token;       // a new token, in case this order is an arm
  Diagnostics diagnostics;       // only read for a diagnostics order
};

// What the node is to do about an order: say `json` (when not empty), then restart in `restart_in_s` seconds when `restart` is set, and act on `change` (the work stops or starts, the standby is
// kept or forgotten, the identify hardware shows or stops).
struct Reply {
  std::string json;
  bool restart = false;
  std::uint32_t restart_in_s = 0;
  Change change;
  Operation operation = Operation::kDiagnostics;
  std::string requested_by;      // who the server acted for (the planned-maintenance signal repeats it)
  bool carried_out = false;
  std::string why_not;           // a line for the log when the order was ignored or refused
};

class Controller {
 public:
  Machine& machine() { return machine_; }
  const Machine& machine() const { return machine_; }
  const Gate& gate() const { return gate_; }

  // One message that arrived on the node's own topic (`broadcast` false) or on the topic of every node (`broadcast` true). A message that is not a valid order for this node says nothing.
  Reply handle(const std::string& own_id, bool broadcast, std::string_view payload, const Env& env) {
    Reply reply;
    Command command;
    std::string why;
    if (!parse_command(payload, command, &why)) { reply.why_not = "an order was ignored: " + why; return reply; }
    if (broadcast ? command.node_id != kBroadcast : command.node_id != own_id) { reply.why_not = "an order for another node was ignored"; return reply; }
    if (broadcast && command.action != Action::kArm) { reply.why_not = "an order to everyone is only an arm: ignored"; return reply; }
    reply.operation = command.operation;
    reply.requested_by = command.requested_by;
    const Situation now = machine_.situation();
    if (command.action == Action::kArm) {
      const Gate::Armed armed = gate_.arm(env.now_ms, env.now_ms / 1000ULL, command.operation, env.busy, now, env.fresh_token);
      if (armed.refusal != Refusal::kNone) {
        reply.why_not = std::string("an arm was refused: ") + refusal_name(armed.refusal);
        reply.json = rejected_json(own_id, env.stamp_ms, command, armed.refusal);
        return reply;
      }
      reply.json = armed_json(own_id, env.stamp_ms, command, armed.token);
      return reply;
    }
    const Refusal refusal = gate_.execute(env.now_ms, env.now_ms / 1000ULL, command.operation, env.busy, now, command.token);
    if (refusal != Refusal::kNone) {
      reply.why_not = std::string("an order was refused: ") + refusal_name(refusal);
      reply.json = rejected_json(own_id, env.stamp_ms, command, refusal);
      return reply;
    }
    reply.carried_out = true;
    switch (command.operation) {
      case Operation::kRestart:
        reply.restart = true;
        reply.restart_in_s = restart_in_s(command);
        reply.json = restarting_json(own_id, env.stamp_ms, command, reply.restart_in_s, env.now_ms / 1000ULL, env.diagnostics.firmware);
        return reply;
      case Operation::kPause: reply.change = machine_.pause(env.now_ms, duration_of(command), env.wall_ms, command.requested_by); break;
      case Operation::kStandby: reply.change = machine_.standby(env.wall_ms, command.requested_by); break;
      case Operation::kResume: reply.change = machine_.resume(); break;
      case Operation::kIdentify: reply.change = machine_.identify(env.now_ms, duration_of(command)); break;
      case Operation::kMaintenanceOn: machine_.maintenance_on(env.now_ms, duration_of(command)); break;
      case Operation::kMaintenanceOff: machine_.maintenance_off(); break;
      case Operation::kDiagnostics:
        reply.json = diagnostics_json(own_id, env.stamp_ms, command, env.diagnostics, machine_);
        return reply;
    }
    reply.json = applied_json(own_id, env.stamp_ms, command, machine_, env.now_ms);
    return reply;
  }

  Change tick(std::uint64_t now_ms) { return machine_.tick(now_ms); }

 private:
  Gate gate_;
  Machine machine_;
};

// What the planned-maintenance signal does after an order was carried out: a restart is announced (with the time it needs to be back), a pause with its length, a standby with none; a resume
// ends both; an operator window follows the maintenance flag. Returns the last message worth publishing (publish false: nothing to say).
inline Signal follow_order(MaintenanceSignal& signal, const Reply& reply, const Machine& machine, std::uint64_t now_ms) {
  Signal out;
  auto keep = [&out](const Signal& s) { if (s.publish) out = s; };
  if (!reply.carried_out) return out;
  switch (reply.operation) {
    case Operation::kRestart: keep(signal.raise(Reason::kRestart, reply.restart_in_s + kRestartBootAllowanceS, now_ms, reply.requested_by)); break;
    case Operation::kPause: {
      const long long left = machine.pause_left_s(now_ms);
      keep(signal.raise(Reason::kPause, left > 0 ? static_cast<std::uint32_t>(left > 3600 ? 3600 : left) : 1u, now_ms, reply.requested_by));
      break;
    }
    case Operation::kStandby: keep(signal.raise(Reason::kStandby, 0, now_ms, reply.requested_by)); keep(signal.lower(Reason::kPause, now_ms)); break;
    case Operation::kResume: keep(signal.lower(Reason::kStandby, now_ms)); keep(signal.lower(Reason::kPause, now_ms)); break;
    case Operation::kMaintenanceOn: {
      const long long left = machine.maintenance_left_s(now_ms);
      keep(signal.raise(Reason::kOperator, left > 0 ? static_cast<std::uint32_t>(left > 3600 ? 3600 : left) : 1u, now_ms, reply.requested_by));
      break;
    }
    case Operation::kMaintenanceOff: keep(signal.lower(Reason::kOperator, now_ms)); break;
    case Operation::kIdentify:
    case Operation::kDiagnostics: break;
  }
  return out;
}

// Once a second: a pause that ended by itself ends its window, an operator window that ended with the flag ends, and every window whose time is over ends (an update or an upload that is
// still going is renewed). Returns the last message worth publishing.
inline Signal follow_tick(MaintenanceSignal& signal, const Machine& machine, const Change& change, std::uint64_t now_ms, Busy busy) {
  Signal out;
  auto keep = [&out](const Signal& s) { if (s.publish) out = s; };
  if (change.state_changed && machine.state() == State::kRunning) keep(signal.lower(Reason::kPause, now_ms));
  if (!machine.maintenance()) keep(signal.lower(Reason::kOperator, now_ms));
  keep(signal.tick(now_ms, busy));
  return out;
}

}  // namespace armor::control
