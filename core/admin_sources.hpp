// ARMOR-NODE-ELECTRICAL - which networks may use the panel: the house by default, and the ones its owner adds.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The panel is meant for the house (or a VPN into it), never for the Internet: if a port of the router is opened towards a node by mistake, whoever comes from outside must not
// be able to try a password there. Everything but the public look at the node's identity (`GET /api/v1/session`) is refused to an address that is not in the list. The list is a
// setting of the panel (`web.admin_sources`): empty means the private ranges of a house (10/8, 172.16/12, 192.168/16, the link-local 169.254/16, 127/8 and 100.64/10, where
// Tailscale and some VPNs live); otherwise it is a comma-separated list of IPv4 addresses and networks (`192.168.0.0/24, 203.0.113.9`). Nothing here touches the hardware.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace armor::adminsrc {

struct Network { std::uint32_t base = 0, mask = 0; };

constexpr std::size_t kMaxEntries = 8;
constexpr std::size_t kMaxText = 120;

// "a.b.c.d" -> the address as a number (host order); false for anything else (also a part with a leading zero).
inline bool parse_address(std::string_view text, std::uint32_t& out) {
  std::uint32_t value = 0;
  int parts = 0;
  std::size_t i = 0;
  while (true) {
    const std::size_t start = i;
    unsigned number = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') { number = number * 10 + static_cast<unsigned>(text[i] - '0'); if (number > 255) return false; ++i; }
    if (i == start || i - start > 3) return false;
    if (i - start > 1 && text[start] == '0') return false;   // no leading zeros ("010" is read as octal by some tools), as net::parse_ipv4
    value = (value << 8) | number;
    if (++parts > 4) return false;
    if (i == text.size()) break;
    if (text[i] != '.') return false;
    ++i;
  }
  if (parts != 4) return false;
  out = value;
  return true;
}

// One entry: an address or an address/prefix. The base is cut to the prefix, so "192.168.0.7/24" means the network 192.168.0.0/24.
inline bool parse_entry(std::string_view text, Network& out) {
  while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
  while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
  if (text.empty()) return false;
  const std::size_t slash = text.find('/');
  std::uint32_t address = 0;
  if (!parse_address(slash == std::string_view::npos ? text : text.substr(0, slash), address)) return false;
  unsigned prefix = 32;
  if (slash != std::string_view::npos) {
    const std::string_view digits = text.substr(slash + 1);
    if (digits.empty() || digits.size() > 2) return false;
    prefix = 0;
    for (const char c : digits) { if (c < '0' || c > '9') return false; prefix = prefix * 10 + static_cast<unsigned>(c - '0'); }
    if (prefix > 32) return false;
  }
  out.mask = prefix == 0 ? 0u : ~std::uint32_t{0} << (32 - prefix);
  out.base = address & out.mask;
  return true;
}

// The whole list; false when any entry is not valid or there are too many (an empty text is valid and means the defaults).
inline bool parse_list(std::string_view text, std::vector<Network>& out) {
  out.clear();
  if (text.size() > kMaxText) return false;
  bool blank_text = true;
  for (const char c : text) if (c != ' ') blank_text = false;
  if (blank_text) return true;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i < text.size() && text[i] != ',') continue;
    Network network;
    if (out.size() >= kMaxEntries || !parse_entry(text.substr(start, i - start), network)) return false;   // a stray comma is an empty entry, which is not valid
    out.push_back(network);
    start = i + 1;
  }
  return true;
}

inline bool valid_list(std::string_view text) { std::vector<Network> ignored; return parse_list(text, ignored); }

inline bool inside(std::uint32_t address, const Network& network) { return (address & network.mask) == network.base; }

// The private ranges of a house.
inline bool is_private(std::uint32_t a) {
  static const Network kPrivate[] = {
      {0x0A000000u, 0xFF000000u},   // 10.0.0.0/8
      {0xAC100000u, 0xFFF00000u},   // 172.16.0.0/12
      {0xC0A80000u, 0xFFFF0000u},   // 192.168.0.0/16
      {0xA9FE0000u, 0xFFFF0000u},   // 169.254.0.0/16
      {0x7F000000u, 0xFF000000u},   // 127.0.0.0/8
      {0x64400000u, 0xFFC00000u},   // 100.64.0.0/10
  };
  for (const Network& network : kPrivate) if (inside(a, network)) return true;
  return false;
}

// May this address use the panel? An empty list means the private ranges; an address the node could not read (0) is never allowed.
inline bool allowed(std::uint32_t address, std::string_view list_text) {
  if (address == 0) return false;
  std::vector<Network> networks;
  if (!parse_list(list_text, networks) || networks.empty()) return is_private(address);
  for (const Network& network : networks) if (inside(address, network)) return true;
  return false;
}

}  // namespace armor::adminsrc
