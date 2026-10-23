// ARMOR-NODE-ELECTRICAL - does a topic match a subscription filter (the MQTT rules for + and #)? Used to hand a message the broker sent to whoever subscribed to it.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <string>
#include <string_view>

namespace armor::mqtt {

// `+` stands for exactly one level, `#` (only as the last level) for the rest. A topic that begins with `$` is never matched by a filter that begins with a wildcard.
inline bool topic_matches(std::string_view filter, std::string_view topic) {
  if (filter.empty() || topic.empty()) return false;
  if (topic.front() == '$' && (filter.front() == '+' || filter.front() == '#')) return false;
  std::size_t f = 0, t = 0;
  for (;;) {
    const std::size_t fe = filter.find('/', f), te = topic.find('/', t);
    const std::string_view fl = filter.substr(f, fe == std::string_view::npos ? std::string_view::npos : fe - f);
    const std::string_view tl = topic.substr(t, te == std::string_view::npos ? std::string_view::npos : te - t);
    if (fl == "#") return fe == std::string_view::npos;   // matches this level and everything below, but must be the last level of the filter
    if (fl != "+" && fl != tl) return false;
    if (fe == std::string_view::npos && te == std::string_view::npos) return true;            // both ended together
    if (fe == std::string_view::npos) return false;                                              // the topic is longer than the filter
    if (te == std::string_view::npos) return filter.substr(fe + 1) == "#";                       // the filter is longer: only "a/#" still matches "a"
    f = fe + 1;
    t = te + 1;
  }
}

// The last will of a node: what the broker publishes (and keeps) for it when the connection is lost without a goodbye. It goes on the node's health topic and says nothing else, so no health report
// can be mistaken for it; the server takes it as "this node went away" at once instead of waiting for the silence limit. An id with a character that would break the JSON gives "".
inline bool will_id_is_safe(std::string_view id) {
  if (id.empty() || id.size() > 64) return false;
  for (const char c : id) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  return true;
}
inline std::string will_topic(std::string_view node_id) { return will_id_is_safe(node_id) ? "armor/health/" + std::string(node_id) : std::string(); }
inline std::string will_payload(std::string_view node_id) { return will_id_is_safe(node_id) ? "{\"node_id\":\"" + std::string(node_id) + "\",\"online\":false}" : std::string(); }
// The other half: what the node says on the same topic each time it connects ("online": true). Both are kept (retained) by the broker, so the last one is always the state the node is in.
inline std::string birth_payload(std::string_view node_id) { return will_id_is_safe(node_id) ? "{\"node_id\":\"" + std::string(node_id) + "\",\"online\":true}" : std::string(); }

}  // namespace armor::mqtt
