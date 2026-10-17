// ARMOR-ELECTRICAL - the host and the port in the address of a broker ("mqtt://192.168.0.180:1883", "mqtts://broker.local", "ws://host/path"). Pure: tested on a computer.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The node asks whether a broker answers (a plain TCP connection) before it leaves the one it is using for it; for that it needs the host and the port the address names, with the
// default of the scheme when the address names none.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace armor::broker_uri {

struct HostPort {
  bool ok = false;
  std::string host;
  std::uint16_t port = 0;
};

inline HostPort parse(std::string_view uri) {
  HostPort out;
  const std::size_t scheme_end = uri.find("://");
  if (scheme_end == std::string_view::npos) return out;
  const std::string_view scheme = uri.substr(0, scheme_end);
  std::uint16_t port = 0;
  if (scheme == "mqtt" || scheme == "tcp") port = 1883;
  else if (scheme == "mqtts" || scheme == "ssl") port = 8883;
  else if (scheme == "ws") port = 80;
  else if (scheme == "wss") port = 443;
  else return out;
  std::string_view rest = uri.substr(scheme_end + 3);
  const std::size_t path = rest.find_first_of("/?#");
  if (path != std::string_view::npos) rest = rest.substr(0, path);
  const std::size_t at = rest.rfind('@');
  if (at != std::string_view::npos) rest = rest.substr(at + 1);   // a user name and password in the address are not part of where it is
  if (rest.empty()) return out;
  std::string_view host = rest, port_text;
  if (rest.front() == '[') {   // an IPv6 address: [::1]:1883
    const std::size_t close = rest.find(']');
    if (close == std::string_view::npos) return out;
    host = rest.substr(1, close - 1);
    if (close + 1 < rest.size()) {
      if (rest[close + 1] != ':') return out;
      port_text = rest.substr(close + 2);
    }
  } else {
    const std::size_t colon = rest.rfind(':');
    if (colon != std::string_view::npos) { host = rest.substr(0, colon); port_text = rest.substr(colon + 1); }
  }
  if (host.empty()) return out;
  if (!port_text.empty() || rest.back() == ':') {
    if (port_text.empty() || port_text.size() > 5) return out;
    unsigned value = 0;
    for (const char c : port_text) {
      if (c < '0' || c > '9') return out;
      value = value * 10 + static_cast<unsigned>(c - '0');
    }
    if (value == 0 || value > 65535) return out;
    port = static_cast<std::uint16_t>(value);
  }
  out.ok = true;
  out.host = std::string(host);
  out.port = port;
  return out;
}

}  // namespace armor::broker_uri
