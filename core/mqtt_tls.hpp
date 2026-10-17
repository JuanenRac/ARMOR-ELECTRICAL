// A.R.M.O.R. - the optional encryption of the link to the broker (mqtts) with the certificate of the installation's own authority. Pure: tested on a computer.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// Off by default: a node talks plain MQTT to its broker, as it always did. When the owner turns it on in the panel (Broker page) the node connects with mqtts:// and trusts ONE certificate
// authority only - the one whose certificate (PEM) is stored in the node's settings - and the broker's name or address must be among the names that certificate vouches for. This file
// decides what a stored authority may look like and what the settings must say when the encryption is on; the connection itself is main/mqtt_link.cpp.
#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace armor::mqtt_tls {

constexpr std::size_t kMaxCaPem = 2048;   // an authority made by ARMOR-DEVOPS is about 600 bytes; the room is for a larger key

inline constexpr std::string_view kBegin = "-----BEGIN CERTIFICATE-----";
inline constexpr std::string_view kEnd = "-----END CERTIFICATE-----";

inline bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// The text the panel sent, made the way it is stored: no spaces around it, line ends as "\n", one "\n" at the end. An empty text stays empty.
inline std::string normalize(std::string_view pem) {
  std::size_t first = 0, last = pem.size();
  while (first < last && is_space(pem[first])) ++first;
  while (last > first && is_space(pem[last - 1])) --last;
  std::string out;
  out.reserve(last - first + 1);
  for (std::size_t i = first; i < last; ++i) {
    if (pem[i] == '\r') continue;
    out.push_back(pem[i]);
  }
  if (!out.empty()) out.push_back('\n');
  return out;
}

// One certificate in PEM, nothing else: the BEGIN and END lines, a Base64 body whose length is a multiple of four (padding only at its end) and no second block, no key, no text around it.
inline bool ca_pem_is_valid(std::string_view pem) {
  const std::string text = normalize(pem);
  if (text.empty() || text.size() > kMaxCaPem) return false;
  if (text.compare(0, kBegin.size(), kBegin) != 0) return false;
  if (text.size() < kBegin.size() + kEnd.size() + 2) return false;
  if (text.compare(text.size() - kEnd.size() - 1, kEnd.size(), kEnd) != 0 || text.back() != '\n') return false;
  const std::string_view body(text.data() + kBegin.size(), text.size() - kBegin.size() - kEnd.size() - 1);
  std::size_t characters = 0, padding = 0;
  for (const char c : body) {
    if (c == '\n') continue;
    if (c == '=') { ++padding; ++characters; continue; }
    if (padding != 0) return false;   // data after the padding
    const bool base64 = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/';
    if (!base64) return false;
    ++characters;
  }
  if (padding > 2 || characters < 200 || characters % 4 != 0) return false;
  if (body.empty() || body.front() != '\n' || body.back() != '\n') return false;   // the BEGIN and END lines are lines of their own
  return true;
}

inline bool uri_is_tls(std::string_view uri) { return uri.substr(0, 8) == "mqtts://"; }

// The problems of the encryption settings. `Mqtt` has enabled, tls, uri, ca_pem and backup (each with a uri); `Problems` is the node's list of {path, code}. Codes: "bad_pem" (the stored
// authority is not one certificate in PEM), "required" (encryption on without an authority), "need_mqtts" (encryption on, but an address that is not mqtts://).
template <typename Mqtt, typename Problems>
void validate(const Mqtt& mqtt, Problems& problems) {
  using Problem = typename Problems::value_type;
  if (!mqtt.ca_pem.empty() && !ca_pem_is_valid(mqtt.ca_pem)) problems.push_back(Problem{"mqtt.ca_pem", "bad_pem"});
  if (!mqtt.tls || !mqtt.enabled) return;
  if (mqtt.ca_pem.empty()) problems.push_back(Problem{"mqtt.ca_pem", "required"});
  if (!mqtt.uri.empty() && !uri_is_tls(mqtt.uri)) problems.push_back(Problem{"mqtt.uri", "need_mqtts"});
  for (std::size_t i = 0; i < mqtt.backup.size(); ++i)
    if (!mqtt.backup[i].uri.empty() && !uri_is_tls(mqtt.backup[i].uri)) problems.push_back(Problem{"mqtt.backup." + std::to_string(i) + ".uri", "need_mqtts"});
}

}  // namespace armor::mqtt_tls
