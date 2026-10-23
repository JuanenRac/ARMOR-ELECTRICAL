// ARMOR-NODE-ELECTRICAL - the node's direct, authenticated HTTP API for a central that has no broker to rely on: what it may ask and what it may command, checked before anything moves. No hardware.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// While the server's broker is out of reach a separate panel (a central of the house) still has to read the zones of a node and move its siren and relays. The node therefore answers, when
// its setting `remote.http` is on, a small set of routes to anyone who presents the node's token as "Authorization: Bearer <token>":
//   GET  /api/v1/remote/state      the node's state: its relays (and, on an alarm node, the panel's state and its zones)
//   POST /api/v1/remote/relay      {"relay":"<name>","action":"on"|"off"|"toggle"}     moves one relay of the base board
// A token that is wrong, missing or not set answers 401 (and the node does not say which). The relays move only while `remote.http` is on AND the relay is enabled, exactly as for the
// broker's commands; and the node never closes a relay on its own account. This file only reads the header and the body, so it is tested on a computer.
#pragma once
#include <string>
#include <string_view>

#include "auth.hpp"
#include "config_common.hpp"
#include "json.hpp"
#include "relay_remote.hpp"

namespace armor::central {

constexpr std::size_t kMinTokenLength = 20;   // a token that can be guessed is no token

// The token of an "Authorization" header ("Bearer <token>"), or "" when it is not that.
inline std::string bearer_of(std::string_view header) {
  constexpr std::string_view prefix = "Bearer ";
  if (header.size() <= prefix.size() || header.substr(0, prefix.size()) != prefix) return "";
  return std::string(header.substr(prefix.size()));
}

// True when the request may be answered: the interface is on, a token is set, and the one presented is it (compared without leaking where they differ).
inline bool authorized(bool http_enabled, std::string_view stored_token, std::string_view authorization_header) {
  if (!http_enabled || stored_token.size() < kMinTokenLength) return false;
  const std::string presented = bearer_of(authorization_header);
  if (presented.empty()) return false;
  return auth::same_text(presented, stored_token);
}

// Whether a token may be stored: long enough and made of characters that can travel in a header without being quoted.
inline bool valid_token(std::string_view token) {
  if (token.size() < kMinTokenLength || token.size() > 64) return false;
  for (const char c : token) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == '+' || c == '/' || c == '=';
    if (!ok) return false;
  }
  return true;
}

struct RelayRequest {
  std::string relay;
  relays::Action action = relays::Action::kNone;
};

enum class Parse { kOk, kNotJson, kInvalid };

// {"relay":"<name>","action":"on"|"off"|"toggle"}: anything else is refused (a number of pulses, a level, a sentence is not a command to a relay).
inline Parse parse_relay_request(std::string_view body, RelayRequest& out) {
  json::Value document;
  if (!json::parse(body, document) || !document.is_object()) return Parse::kNotJson;
  const json::Value* relay = document.get("relay");
  const json::Value* action = document.get("action");
  if (relay == nullptr || !relay->is_string() || !config::valid_device_name(relay->text)) return Parse::kInvalid;
  if (action == nullptr || !action->is_string()) return Parse::kInvalid;
  const relays::Action parsed = relays::parse_command(action->text);
  if (parsed == relays::Action::kNone) return Parse::kInvalid;
  out.relay = relay->text;
  out.action = parsed;
  return Parse::kOk;
}

// The answer to a relay command: {"relay":"<name>","on":true|false}.
inline std::string relay_answer(const std::string& name, bool on) {
  json::Writer w;
  w.begin_object().field("relay", name).field("on", on).end_object();
  return w.str();
}

}  // namespace armor::central
