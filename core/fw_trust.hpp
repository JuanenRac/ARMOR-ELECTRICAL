// ARMOR-ELECTRICAL - who may sign the firmware that a node installs by itself: the public key of the A.R.M.O.R. releases, and the check of a signature. Pure: tested on a computer.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// A release carries, next to each image, its SHA-256 (`<image>.sha256`) and an Ed25519 signature of that hash (`<image>.sig`, the 64 bytes as 128 hexadecimal digits) made with the
// private key of whoever publishes the releases. The node downloads the image, hashes it, and installs it only when the hash is the one published AND the signature of that hash
// verifies with the key below: somebody who can only change what a release holds on GitHub, and does not have the private key, cannot make a node install his firmware. The private key
// never leaves its owner's computer. The upload of a firmware by an administrator in the panel is not checked here: it is the administrator's own act.
//
// Changing the key means a firmware that holds the new one has to be installed first, by that upload or by cable; a node that was never given the new key will refuse the releases made with it.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "ed25519_verify.hpp"

namespace armor::fw_trust {

// The public key of the releases (the matching private key is in the hands of the maintainer, outside every repository).
constexpr std::uint8_t kPublicKey[32] = {0x66, 0x44, 0x78, 0xb1, 0x2a, 0x87, 0x99, 0xab, 0x18, 0x97, 0x0f, 0x58, 0xe9, 0x74, 0xf1, 0x4a, 0x6d, 0x0b, 0xf5, 0x62, 0x3f, 0x0a, 0x22, 0xc6, 0x5c, 0x3a, 0xaa, 0x32, 0x99, 0x70, 0x54, 0xd2};

// The 64 bytes of a signature written as 128 hexadecimal digits (upper or lower case); false for anything else.
inline bool parse_signature(const std::string& text, std::uint8_t out[64]) {
  std::size_t start = 0, end = text.size();
  while (start < end && (text[start] == ' ' || text[start] == '\n' || text[start] == '\r' || text[start] == '\t')) ++start;
  while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\n' || text[end - 1] == '\r' || text[end - 1] == '\t')) --end;
  if (end - start != 128) return false;
  for (std::size_t i = 0; i < 64; ++i) {
    int value = 0;
    for (std::size_t k = 0; k < 2; ++k) {
      const char c = text[start + 2 * i + k];
      int digit;
      if (c >= '0' && c <= '9') digit = c - '0';
      else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
      else return false;
      value = value * 16 + digit;
    }
    out[i] = static_cast<std::uint8_t>(value);
  }
  return true;
}

// True when `signature_text` is the signature, by the key of the releases, of the 32 bytes of the image's SHA-256.
inline bool image_signature_ok(const std::uint8_t sha256[32], const std::string& signature_text) {
  std::uint8_t signature[64];
  if (!parse_signature(signature_text, signature)) return false;
  return ed25519::verify(signature, sha256, 32, kPublicKey);
}

}  // namespace armor::fw_trust
