// ARMOR-ELECTRICAL - who may sign the firmware that a node installs by itself: the public key of the A.R.M.O.R. releases, and the check of a signature. Pure: tested on a computer.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// A release carries, next to each image, its SHA-256 (`<image>.sha256`) and two Ed25519 signatures made with the private key of whoever publishes the releases (each 64 bytes as 128
// hexadecimal digits): `<image>.sig` of that hash alone (what the nodes before the manifest check verify) and `<image>.sig2` of the manifest of the image (project, board, version and hash:
// manifest_text below), which is the only one a node checks now. The node downloads the image, hashes it, and installs it only when the hash is the one published AND the signature of the manifest
// verifies with the key below, and the version inside the image is newer than the one it runs: somebody who can only change what a release holds on GitHub, and does not have the private key, cannot make a node install his firmware. The private key
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

// The text a release signs besides the hash: the image's project (the name inside the image itself), the board it was built for, its version and its SHA-256. The signature of the hash alone
// cannot tell a new image from an old one, or one board's from another's, that was signed legitimately in the past; this one can, because every part of it is checked by the node.
inline std::string manifest_text(const std::string& project, const std::string& board, const std::string& version, const std::string& sha256_hex) {
  return "armor-fw/1|" + project + "|" + board + "|" + version + "|" + sha256_hex;
}

// True when `signature_text` is the signature, by the key of the releases, of the manifest of this image (`<image>.sig2`). A part that holds a space or a '|' is refused: it could
// be read as another part.
inline bool manifest_signature_ok(const std::string& project, const std::string& board, const std::string& version, const std::string& sha256_hex, const std::string& signature_text) {
  for (const std::string* part : {&project, &board, &version, &sha256_hex}) {
    if (part->empty()) return false;
    for (const char c : *part) if (c == '|' || c == ' ' || c == '\n' || c == '\r' || c == '\t') return false;
  }
  std::uint8_t signature[64];
  if (!parse_signature(signature_text, signature)) return false;
  const std::string message = manifest_text(project, board, version, sha256_hex);
  return ed25519::verify(signature, reinterpret_cast<const std::uint8_t*>(message.data()), message.size(), kPublicKey);
}

}  // namespace armor::fw_trust
