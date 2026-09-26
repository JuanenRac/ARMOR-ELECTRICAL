// ARMOR-ELECTRICAL - the switches of a node and the commands they take (see docs/SWITCHING.md and docs/ELECTRICAL_MESSAGES.md).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
//   armor/electrical/<node>/command   server -> node   kind "electrical_command": arm, close_a, close_b, open or acknowledge on one switch.
//   armor/electrical/<node>/result    node -> server   kind "electrical_result": whether the request was accepted and, when not, why.
//   The state of every switch travels in the ordinary state message (`switches`, see `switches_json`), and it is what the auxiliary contacts show.
//
// This is the layer between a message and `TransferController`: it parses a command strictly, refuses what it should, and answers. It drives no hardware and is NOT
// linked into the firmware: no image of this project creates a `SwitchSet`, so no node can act on a command yet, and the node's state message says
// `switching_enabled: false`. Everything here is tested on a computer with stand-in contactors (tests/test_switch_set.cpp).
//
// Closing is two steps, and the second cannot be replayed:
//   1. `arm`: the node answers with a one-time token (64 random bits) and arms the controller for `arm_window_ms`.
//   2. `close_a` or `close_b` carrying that token. The token is used up by the first close that presents it and by a wrong one (a wrong token also disarms).
//   So an old command that turns up again (a broker that held it, a captured message) finds no token to match and moves nothing. `open` needs no token and is
//   always accepted; `acknowledge` clears a latched fault only with both contactors confirmed open.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "electrical_json.hpp"
#include "interlock.hpp"
#include "json.hpp"

namespace armor::electrical {

enum class Action { kArm, kCloseA, kCloseB, kOpen, kAcknowledge };
enum class Answer { kNone, kDisabled, kFault, kNotArmed, kNotConfirmedOpen, kUnknownSwitch, kBadToken, kNotSupported };

inline const char* action_name(Action action) {
  switch (action) {
    case Action::kArm: return "arm";
    case Action::kCloseA: return "close_a";
    case Action::kCloseB: return "close_b";
    case Action::kOpen: return "open";
    case Action::kAcknowledge: return "acknowledge";
  }
  return "arm";
}
inline const char* answer_name(Answer answer) {
  switch (answer) {
    case Answer::kNone: return "none";
    case Answer::kDisabled: return "disabled";
    case Answer::kFault: return "fault";
    case Answer::kNotArmed: return "not_armed";
    case Answer::kNotConfirmedOpen: return "not_confirmed_open";
    case Answer::kUnknownSwitch: return "unknown_switch";
    case Answer::kBadToken: return "bad_token";
    case Answer::kNotSupported: return "not_supported";
  }
  return "not_supported";
}
inline const char* fault_name(Fault fault) {
  switch (fault) {
    case Fault::kNone: return "none";
    case Fault::kDidNotClose: return "did_not_close";
    case Fault::kDidNotOpen: return "did_not_open";
    case Fault::kBothClosed: return "both_closed";
    case Fault::kDisabled: return "disabled";
  }
  return "none";
}
inline const char* side_name(Source source) { return source == Source::kA ? "a" : source == Source::kB ? "b" : "none"; }

inline std::string command_topic(const std::string& node_id) { return valid_name(node_id, 64) ? "armor/electrical/" + node_id + "/command" : ""; }
inline std::string result_topic(const std::string& node_id) { return valid_name(node_id, 64) ? "armor/electrical/" + node_id + "/result" : ""; }

/// A command id or a token: lowercase letters and digits, eight to thirty-two.
inline bool valid_code(const std::string& text) {
  if (text.size() < 8 || text.size() > 32) return false;
  for (char c : text) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
  return true;
}

struct SwitchCommand {
  std::string node_id, command_id, switch_id, token;
  Action action = Action::kOpen;
};

/// The command in `text`, or nothing (with the reason in `why`). Strict, as the schema is: every field of the contract present and of the right kind, no other field, an
/// action from the list, and a token exactly on close_a and close_b. It does not check that the command is for this node: the caller compares `node_id`.
inline bool parse_command(std::string_view text, SwitchCommand& out, std::string* why = nullptr) {
  auto fail = [&](const char* reason) { if (why != nullptr) *why = reason; return false; };
  json::Value doc;
  if (!json::parse(text, doc) || !doc.is_object()) return fail("not a JSON object");
  static const char* const known[] = {"kind", "node_id", "timestamp_ms", "command_id", "switch", "action", "token"};
  for (const std::string& name : doc.names) {
    bool found = false;
    for (const char* k : known) found = found || name == k;
    if (!found) return fail("a field that is not in the contract");
  }
  const json::Value* kind = doc.get("kind");
  if (kind == nullptr || !kind->is_string() || kind->text != "electrical_command") return fail("kind");
  const json::Value* node = doc.get("node_id");
  if (node == nullptr || !node->is_string() || !valid_name(node->text, 64)) return fail("node_id");
  const json::Value* stamp = doc.get("timestamp_ms");
  if (stamp == nullptr || !stamp->is_number() || stamp->number < 0 || stamp->number != static_cast<double>(static_cast<long long>(stamp->number))) return fail("timestamp_ms");
  const json::Value* id = doc.get("command_id");
  if (id == nullptr || !id->is_string() || !valid_code(id->text)) return fail("command_id");
  const json::Value* target = doc.get("switch");
  if (target == nullptr || !target->is_string() || !valid_name(target->text, 32)) return fail("switch");
  const json::Value* action = doc.get("action");
  if (action == nullptr || !action->is_string()) return fail("action");
  Action parsed;
  if (action->text == "arm") parsed = Action::kArm;
  else if (action->text == "close_a") parsed = Action::kCloseA;
  else if (action->text == "close_b") parsed = Action::kCloseB;
  else if (action->text == "open") parsed = Action::kOpen;
  else if (action->text == "acknowledge") parsed = Action::kAcknowledge;
  else return fail("action");
  const json::Value* token = doc.get("token");
  const bool closes = parsed == Action::kCloseA || parsed == Action::kCloseB;
  if (closes != (token != nullptr)) return fail("a token goes on close_a and close_b and on nothing else");
  if (token != nullptr && (!token->is_string() || !valid_code(token->text))) return fail("token");
  out = SwitchCommand{node->text, id->text, target->text, token != nullptr ? token->text : std::string(), parsed};
  return true;
}

/// The answer to a command. `token` (only in an accepted arm) is the one-time token the close must carry.
inline std::string result_json(const std::string& node_id, std::uint64_t timestamp_ms, const SwitchCommand& command, Answer answer, const std::string& token = "") {
  json::Writer w;
  w.begin_object().field("kind", "electrical_result").field("node_id", node_id).key("timestamp_ms").integer(static_cast<long long>(timestamp_ms));
  w.field("command_id", command.command_id).field("switch", command.switch_id).field("action", action_name(command.action));
  w.field("accepted", answer == Answer::kNone).field("refusal", answer_name(answer));
  if (answer == Answer::kNone && command.action == Action::kArm && !token.empty()) w.field("token", token);
  w.end_object();
  return w.str();
}

struct SwitchConfig {
  std::string id;              // the switch's own name in messages (lowercase letters, digits, - and _)
  std::string label;           // for a person, up to forty characters; may be empty
  std::string source_a, source_b;   // the channel that measures each source; may be empty
  TransferConfig transfer;     // `allowed` is false unless the firmware was built and set up to switch
};

/// Fills a buffer with random bytes; false when it cannot (then no arm is granted).
using RandomBytes = std::function<bool(std::uint8_t*, std::size_t)>;

class SwitchSet {
 public:
  static constexpr std::size_t kMaxSwitches = 4;

  SwitchSet(std::string node_id, RandomBytes random) : node_id_(std::move(node_id)), random_(std::move(random)) {}

  /// Add a switch. Refused (false) for an invalid or repeated id, a label that is too long, or a fifth switch.
  bool add(const SwitchConfig& config) {
    if (!valid_name(config.id, 32) || entries_.size() >= kMaxSwitches || find(config.id) != nullptr) return false;
    if (config.label.size() > 40 || (!config.source_a.empty() && !valid_name(config.source_a, 32)) || (!config.source_b.empty() && !valid_name(config.source_b, 32))) return false;
    entries_.push_back(Entry{config, TransferController(config.transfer), {}, {}, 0});
    return true;
  }

  std::size_t size() const { return entries_.size(); }
  /// True when at least one switch is allowed to switch: what the state message says as `switching_enabled`.
  bool switching_enabled() const { for (const Entry& e : entries_) if (e.config.transfer.allowed) return true; return false; }

  /// The auxiliary contacts of a switch as read now.
  void set_feedback(const std::string& id, Feedback feedback) { if (Entry* e = find(id)) e->feedback = feedback; }

  /// Advance every controller with the contacts as they are. Call it every few tens of milliseconds.
  void tick(std::uint64_t now_ms) {
    for (Entry& e : entries_) {
      e.controller.tick(e.feedback, now_ms);
      if (!e.token.empty() && now_ms >= e.token_until_ms) e.token.clear();
      if (e.controller.fault() != Fault::kNone) e.token.clear();
    }
  }

  /// What to drive for a switch (nothing energised when it is unknown).
  Coils coils(const std::string& id) const { const Entry* e = find(id); return e != nullptr ? e->controller.coils() : Coils{}; }
  Fault fault(const std::string& id) const { const Entry* e = find(id); return e != nullptr ? e->controller.fault() : Fault::kNone; }
  Source selected(const std::string& id) const { const Entry* e = find(id); return e != nullptr ? e->controller.selected() : Source::kNone; }

  /// One command as it arrived: the result to publish, or an empty string when there is nothing to answer (the text is not a command of the contract, or it is for
  /// another node). Never throws. A close that arrives twice moves nothing the second time: its token is spent.
  std::string handle(std::string_view text, std::uint64_t now_ms) {
    SwitchCommand command;
    if (!parse_command(text, command) || command.node_id != node_id_) return "";
    std::string token;
    const Answer answer = act(command, now_ms, token);
    return result_json(node_id_, now_ms, command, answer, token);
  }

  /// The `switches` array of the state message (an empty string when the node has no switch, so the message has none).
  std::string switches_json(std::uint64_t now_ms) const {
    if (entries_.empty()) return "";
    json::Writer w;
    w.begin_array();
    for (const Entry& e : entries_) {
      const Feedback f = e.feedback;
      const bool fault = e.controller.fault() != Fault::kNone;
      const Source contacts = f.a_closed && !f.b_closed ? Source::kA : f.b_closed && !f.a_closed ? Source::kB : Source::kNone;
      w.begin_object().field("id", e.config.id).field("kind", "transfer");
      if (!e.config.label.empty()) w.field("label", e.config.label);
      if (!e.config.source_a.empty()) w.field("source_a", e.config.source_a);
      if (!e.config.source_b.empty()) w.field("source_b", e.config.source_b);
      w.field("a_closed", f.a_closed).field("b_closed", f.b_closed).field("selected", side_name(contacts));
      w.field("wanted", side_name(fault ? Source::kNone : e.controller.wanted())).field("closing", !fault && e.controller.closing());
      w.field("armed", e.config.transfer.allowed && !fault && e.controller.armed(now_ms)).field("fault", fault_name(e.controller.fault()));
      w.end_object();
    }
    w.end_array();
    return w.str();
  }

 private:
  struct Entry {
    SwitchConfig config;
    TransferController controller;
    Feedback feedback;
    std::string token;               // the token of the arm that is waiting for its close; empty when none
    std::uint64_t token_until_ms;
  };

  Entry* find(const std::string& id) { for (Entry& e : entries_) if (e.config.id == id) return &e; return nullptr; }
  const Entry* find(const std::string& id) const { for (const Entry& e : entries_) if (e.config.id == id) return &e; return nullptr; }

  static bool same(const std::string& a, const std::string& b) {   // without stopping at the first difference
    unsigned char diff = static_cast<unsigned char>(a.size() ^ b.size());
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) diff = static_cast<unsigned char>(diff | (a[i] ^ b[i]));
    return diff == 0;
  }

  Answer act(const SwitchCommand& command, std::uint64_t now_ms, std::string& token_out) {
    Entry* e = find(command.switch_id);
    if (e == nullptr) return Answer::kUnknownSwitch;
    TransferController& c = e->controller;
    if (command.action == Action::kOpen) {            // always accepted, always immediate, and it withdraws any arm
      e->token.clear();
      c.request(Source::kNone, now_ms);
      return Answer::kNone;
    }
    if (!e->config.transfer.allowed) return Answer::kDisabled;
    if (command.action == Action::kAcknowledge) return c.acknowledge(e->feedback, now_ms) ? Answer::kNone : Answer::kNotConfirmedOpen;
    if (c.fault() != Fault::kNone) return Answer::kFault;
    if (command.action == Action::kArm) {
      std::uint8_t bytes[8];
      if (!random_ || !random_(bytes, sizeof bytes)) return Answer::kNotSupported;
      static const char hex[] = "0123456789abcdef";
      e->token.clear();
      for (std::uint8_t byte : bytes) { e->token += hex[byte >> 4]; e->token += hex[byte & 15]; }
      e->token_until_ms = now_ms + e->config.transfer.arm_window_ms;
      c.arm(now_ms);
      token_out = e->token;
      return Answer::kNone;
    }
    // close_a or close_b
    if (e->token.empty()) return Answer::kNotArmed;
    if (!same(e->token, command.token)) { e->token.clear(); c.disarm(); return Answer::kBadToken; }
    e->token.clear();                                 // one arm, one request
    switch (c.request(command.action == Action::kCloseA ? Source::kA : Source::kB, now_ms)) {
      case Refusal::kNone: return Answer::kNone;
      case Refusal::kDisabled: return Answer::kDisabled;
      case Refusal::kFault: return Answer::kFault;
      case Refusal::kNotArmed: return Answer::kNotArmed;
      case Refusal::kNotConfirmedOpen: return Answer::kNotConfirmedOpen;
    }
    return Answer::kNotSupported;
  }

  std::string node_id_;
  RandomBytes random_;
  std::vector<Entry> entries_;
};

}  // namespace armor::electrical
