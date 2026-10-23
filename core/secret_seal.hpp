// ARMOR-NODE-ELECTRICAL - sealing of the secrets a node keeps in flash: the cipher, the text it makes, the record that holds the key and the choice of what to do with a stored value. No hardware.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// An option of the node (security.seal_secrets, or the server's nodes.nvs_encryption) keeps ONLY the secrets that the node stores - the passwords of its Wi-Fi networks and of its broker, the token, the keys,
// the PIN, the users' password hashes, the private key of its certificate - as sealed text, and leaves everything else in the flash readable. The cipher is ChaCha20-Poly1305 (RFC 8439): small, with no table
// that could leak, the same text on a computer as on the chip, so what the host tests prove is what the node runs. A sealed value is "enc1:" and the base64 of nonce (12 bytes), cipher text and tag (16
// bytes); the name of the field (or of the stored value) is authenticated with it, so a sealed value cannot be moved under another name.
//
// What this does NOT do, and the panel says so: the key is a 32-byte record in a partition of the same flash chip (no eFuse is burned, and the flash itself is not encrypted), so whoever can read the whole
// flash with a programmer reads the key too. It keeps the secrets out of what a backup of the settings partition, a stray copy of the NVS or a reading of the store by software shows; it is not protection
// against someone who holds the board.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace armor::seal {

using Key = std::array<std::uint8_t, 32>;
constexpr char kPrefix[] = "enc1:";
constexpr std::size_t kNonceBytes = 12;
constexpr std::size_t kTagBytes = 16;

namespace detail {

inline std::uint32_t load32(const std::uint8_t* p) { return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24); }
inline void store32(std::uint8_t* p, std::uint32_t v) { p[0] = static_cast<std::uint8_t>(v); p[1] = static_cast<std::uint8_t>(v >> 8); p[2] = static_cast<std::uint8_t>(v >> 16); p[3] = static_cast<std::uint8_t>(v >> 24); }
inline std::uint32_t rotl(std::uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

inline void quarter(std::uint32_t* x, int a, int b, int c, int d) {
  x[a] += x[b]; x[d] = rotl(x[d] ^ x[a], 16);
  x[c] += x[d]; x[b] = rotl(x[b] ^ x[c], 12);
  x[a] += x[b]; x[d] = rotl(x[d] ^ x[a], 8);
  x[c] += x[d]; x[b] = rotl(x[b] ^ x[c], 7);
}

// One 64-byte block of the ChaCha20 stream.
inline void chacha_block(const Key& key, std::uint32_t counter, const std::uint8_t* nonce, std::uint8_t out[64]) {
  std::uint32_t state[16] = {0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u, 0, 0, 0, 0, 0, 0, 0, 0, counter, load32(nonce), load32(nonce + 4), load32(nonce + 8)};
  for (int i = 0; i < 8; ++i) state[4 + i] = load32(key.data() + 4 * i);
  std::uint32_t x[16];
  for (int i = 0; i < 16; ++i) x[i] = state[i];
  for (int round = 0; round < 10; ++round) {
    quarter(x, 0, 4, 8, 12); quarter(x, 1, 5, 9, 13); quarter(x, 2, 6, 10, 14); quarter(x, 3, 7, 11, 15);
    quarter(x, 0, 5, 10, 15); quarter(x, 1, 6, 11, 12); quarter(x, 2, 7, 8, 13); quarter(x, 3, 4, 9, 14);
  }
  for (int i = 0; i < 16; ++i) store32(out + 4 * i, x[i] + state[i]);
}

// ChaCha20 applied to `data` (the same call encrypts and decrypts), the counter starting at `first_counter`.
inline std::string chacha_xor(const Key& key, std::uint32_t first_counter, const std::uint8_t* nonce, std::string_view data) {
  std::string out(data.size(), '\0');
  std::uint8_t block[64];
  for (std::size_t offset = 0; offset < data.size(); offset += 64) {
    chacha_block(key, first_counter + static_cast<std::uint32_t>(offset / 64), nonce, block);
    const std::size_t n = data.size() - offset < 64 ? data.size() - offset : 64;
    for (std::size_t i = 0; i < n; ++i) out[offset + i] = static_cast<char>(static_cast<std::uint8_t>(data[offset + i]) ^ block[i]);
  }
  return out;
}

// Poly1305 over `message` with a one-time key of 32 bytes (the 26-bit limb form, which needs nothing wider than 64 bits).
inline std::array<std::uint8_t, 16> poly1305(const std::uint8_t key[32], const std::string& message) {
  const std::uint32_t r0 = load32(key) & 0x3ffffffu;
  const std::uint32_t r1 = (load32(key + 3) >> 2) & 0x3ffff03u;
  const std::uint32_t r2 = (load32(key + 6) >> 4) & 0x3ffc0ffu;
  const std::uint32_t r3 = (load32(key + 9) >> 6) & 0x3f03fffu;
  const std::uint32_t r4 = (load32(key + 12) >> 8) & 0x00fffffu;
  const std::uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
  std::uint32_t h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;
  const auto block = [&](const std::uint8_t* m, std::uint32_t hibit) {
    h0 += load32(m) & 0x3ffffffu;
    h1 += (load32(m + 3) >> 2) & 0x3ffffffu;
    h2 += (load32(m + 6) >> 4) & 0x3ffffffu;
    h3 += (load32(m + 9) >> 6) & 0x3ffffffu;
    h4 += (load32(m + 12) >> 8) | hibit;
    const std::uint64_t d0 = static_cast<std::uint64_t>(h0) * r0 + static_cast<std::uint64_t>(h1) * s4 + static_cast<std::uint64_t>(h2) * s3 + static_cast<std::uint64_t>(h3) * s2 + static_cast<std::uint64_t>(h4) * s1;
    std::uint64_t d1 = static_cast<std::uint64_t>(h0) * r1 + static_cast<std::uint64_t>(h1) * r0 + static_cast<std::uint64_t>(h2) * s4 + static_cast<std::uint64_t>(h3) * s3 + static_cast<std::uint64_t>(h4) * s2;
    std::uint64_t d2 = static_cast<std::uint64_t>(h0) * r2 + static_cast<std::uint64_t>(h1) * r1 + static_cast<std::uint64_t>(h2) * r0 + static_cast<std::uint64_t>(h3) * s4 + static_cast<std::uint64_t>(h4) * s3;
    std::uint64_t d3 = static_cast<std::uint64_t>(h0) * r3 + static_cast<std::uint64_t>(h1) * r2 + static_cast<std::uint64_t>(h2) * r1 + static_cast<std::uint64_t>(h3) * r0 + static_cast<std::uint64_t>(h4) * s4;
    std::uint64_t d4 = static_cast<std::uint64_t>(h0) * r4 + static_cast<std::uint64_t>(h1) * r3 + static_cast<std::uint64_t>(h2) * r2 + static_cast<std::uint64_t>(h3) * r1 + static_cast<std::uint64_t>(h4) * r0;
    std::uint32_t c = static_cast<std::uint32_t>(d0 >> 26); h0 = static_cast<std::uint32_t>(d0) & 0x3ffffffu;
    d1 += c; c = static_cast<std::uint32_t>(d1 >> 26); h1 = static_cast<std::uint32_t>(d1) & 0x3ffffffu;
    d2 += c; c = static_cast<std::uint32_t>(d2 >> 26); h2 = static_cast<std::uint32_t>(d2) & 0x3ffffffu;
    d3 += c; c = static_cast<std::uint32_t>(d3 >> 26); h3 = static_cast<std::uint32_t>(d3) & 0x3ffffffu;
    d4 += c; c = static_cast<std::uint32_t>(d4 >> 26); h4 = static_cast<std::uint32_t>(d4) & 0x3ffffffu;
    h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffffu; h1 += c;
  };
  const std::uint8_t* data = reinterpret_cast<const std::uint8_t*>(message.data());
  std::size_t left = message.size();
  while (left >= 16) { block(data, 1u << 24); data += 16; left -= 16; }
  if (left > 0) {
    std::uint8_t last[16] = {};
    for (std::size_t i = 0; i < left; ++i) last[i] = data[i];
    last[left] = 1;
    block(last, 0);
  }
  std::uint32_t c = h1 >> 26; h1 &= 0x3ffffffu; h2 += c;
  c = h2 >> 26; h2 &= 0x3ffffffu; h3 += c;
  c = h3 >> 26; h3 &= 0x3ffffffu; h4 += c;
  c = h4 >> 26; h4 &= 0x3ffffffu; h0 += c * 5;
  c = h0 >> 26; h0 &= 0x3ffffffu; h1 += c;
  std::uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffffu;
  std::uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffffu;
  std::uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffffu;
  std::uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffffu;
  const std::uint32_t g4 = h4 + c - (1u << 26);
  std::uint32_t mask = (g4 >> 31) - 1;   // all ones when h >= 2^130 - 5 (the subtraction did not go below zero)
  g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; const std::uint32_t g4m = g4 & mask;
  mask = ~mask;
  h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1; h2 = (h2 & mask) | g2; h3 = (h3 & mask) | g3; h4 = (h4 & mask) | g4m;
  const std::uint32_t w0 = h0 | (h1 << 26);
  const std::uint32_t w1 = (h1 >> 6) | (h2 << 20);
  const std::uint32_t w2 = (h2 >> 12) | (h3 << 14);
  const std::uint32_t w3 = (h3 >> 18) | (h4 << 8);
  std::uint64_t f = static_cast<std::uint64_t>(w0) + load32(key + 16);
  std::array<std::uint8_t, 16> tag{};
  store32(tag.data(), static_cast<std::uint32_t>(f));
  f = static_cast<std::uint64_t>(w1) + load32(key + 20) + (f >> 32);
  store32(tag.data() + 4, static_cast<std::uint32_t>(f));
  f = static_cast<std::uint64_t>(w2) + load32(key + 24) + (f >> 32);
  store32(tag.data() + 8, static_cast<std::uint32_t>(f));
  f = static_cast<std::uint64_t>(w3) + load32(key + 28) + (f >> 32);
  store32(tag.data() + 12, static_cast<std::uint32_t>(f));
  return tag;
}

inline void pad16(std::string& s) { while (s.size() % 16 != 0) s.push_back('\0'); }
inline void put64(std::string& s, std::uint64_t v) { for (int i = 0; i < 8; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFF)); }

inline std::array<std::uint8_t, 16> aead_tag(const Key& key, const std::uint8_t* nonce, std::string_view aad, const std::string& cipher) {
  std::uint8_t one_time[64];
  chacha_block(key, 0, nonce, one_time);
  std::string mac(aad);
  pad16(mac);
  mac += cipher;
  pad16(mac);
  put64(mac, aad.size());
  put64(mac, cipher.size());
  return poly1305(one_time, mac);
}

inline bool same_bytes(const std::uint8_t* a, const std::uint8_t* b, std::size_t n) {
  std::uint8_t difference = 0;
  for (std::size_t i = 0; i < n; ++i) difference |= static_cast<std::uint8_t>(a[i] ^ b[i]);
  return difference == 0;
}

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline std::string base64(const std::string& bytes) {
  std::string out;
  std::size_t i = 0;
  for (; i + 2 < bytes.size(); i += 3) {
    const std::uint32_t v = (static_cast<std::uint8_t>(bytes[i]) << 16) | (static_cast<std::uint8_t>(bytes[i + 1]) << 8) | static_cast<std::uint8_t>(bytes[i + 2]);
    out += kAlphabet[(v >> 18) & 63]; out += kAlphabet[(v >> 12) & 63]; out += kAlphabet[(v >> 6) & 63]; out += kAlphabet[v & 63];
  }
  if (i + 1 == bytes.size()) {
    const std::uint32_t v = static_cast<std::uint8_t>(bytes[i]) << 16;
    out += kAlphabet[(v >> 18) & 63]; out += kAlphabet[(v >> 12) & 63];
  } else if (i + 2 == bytes.size()) {
    const std::uint32_t v = (static_cast<std::uint8_t>(bytes[i]) << 16) | (static_cast<std::uint8_t>(bytes[i + 1]) << 8);
    out += kAlphabet[(v >> 18) & 63]; out += kAlphabet[(v >> 12) & 63]; out += kAlphabet[(v >> 6) & 63];
  }
  return out;   // no padding characters
}

inline bool unbase64(std::string_view text, std::string& out) {
  out.clear();
  if (text.size() % 4 == 1) return false;
  std::uint32_t acc = 0;
  int bits = 0;
  for (const char c : text) {
    int v = -1;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
    else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '+') v = 62;
    else if (c == '/') v = 63;
    if (v < 0) return false;
    acc = (acc << 6) | static_cast<std::uint32_t>(v);
    bits += 6;
    if (bits >= 8) { bits -= 8; out.push_back(static_cast<char>((acc >> bits) & 0xFF)); }
  }
  return true;
}

}  // namespace detail

// ---- one value -----------------------------------------------------------------------------------------------------------------

// ChaCha20-Poly1305 with `aad` authenticated: the cipher text then the tag of 16 bytes.
inline std::string encrypt(const Key& key, const std::uint8_t nonce[kNonceBytes], std::string_view aad, std::string_view plain) {
  std::string cipher = detail::chacha_xor(key, 1, nonce, plain);
  const std::array<std::uint8_t, 16> tag = detail::aead_tag(key, nonce, aad, cipher);
  cipher.append(reinterpret_cast<const char*>(tag.data()), tag.size());
  return cipher;
}

// The reverse; false (and `plain` empty) when the tag does not fit: a wrong key, another name, or a value that was changed.
inline bool decrypt(const Key& key, const std::uint8_t nonce[kNonceBytes], std::string_view aad, std::string_view cipher_and_tag, std::string& plain) {
  plain.clear();
  if (cipher_and_tag.size() < kTagBytes) return false;
  const std::string cipher(cipher_and_tag.substr(0, cipher_and_tag.size() - kTagBytes));
  const std::array<std::uint8_t, 16> expected = detail::aead_tag(key, nonce, aad, cipher);
  if (!detail::same_bytes(expected.data(), reinterpret_cast<const std::uint8_t*>(cipher_and_tag.data()) + cipher.size(), kTagBytes)) return false;
  plain = detail::chacha_xor(key, 1, nonce, cipher);
  return true;
}

inline bool is_sealed(std::string_view text) { return text.size() > sizeof(kPrefix) - 1 && text.substr(0, sizeof(kPrefix) - 1) == kPrefix; }

// "enc1:" + base64(nonce, cipher text, tag).
inline std::string seal_value(const Key& key, const std::uint8_t nonce[kNonceBytes], std::string_view aad, std::string_view plain) {
  std::string bytes(reinterpret_cast<const char*>(nonce), kNonceBytes);
  bytes += encrypt(key, nonce, aad, plain);
  return std::string(kPrefix) + detail::base64(bytes);
}

enum class Open { kPlain, kOpened, kBad };

// A value as stored: not sealed -> kPlain (`out` is the text as it is); sealed and right -> kOpened (`out` is the plain text); sealed but unreadable with this key or under this name -> kBad (`out` empty).
inline Open open_value(const Key& key, std::string_view text, std::string_view aad, std::string& out) {
  if (!is_sealed(text)) { out.assign(text); return Open::kPlain; }
  out.clear();
  std::string bytes;
  if (!detail::unbase64(text.substr(sizeof(kPrefix) - 1), bytes) || bytes.size() < kNonceBytes + kTagBytes) return Open::kBad;
  const std::string_view rest(bytes.data() + kNonceBytes, bytes.size() - kNonceBytes);
  return decrypt(key, reinterpret_cast<const std::uint8_t*>(bytes.data()), aad, rest, out) ? Open::kOpened : Open::kBad;
}

// ---- a JSON document: only the secrets of it --------------------------------------------------------------------------------------

// The names of the members that hold a secret (their value is a string): a password, a secret, a token, a key, a PIN, a salt or a hash - whatever the node keeps under such a name.
inline bool secret_name(std::string_view name) {
  std::string low;
  for (const char c : name) low.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
  for (const char* word : {"pass", "secret", "token", "psk", "key", "pin", "salt", "hash"}) if (low.find(word) != std::string::npos) return true;
  return false;
}

namespace detail {

// Walks a JSON text and calls `visit(name, begin, end)` for every string VALUE that is the value of a member (the span is the text between the quotes, as written, escapes included).
// Returns false when the text is not shaped like JSON (an unterminated string).
template <class Visit>
inline bool each_member_string(std::string_view json, Visit&& visit) {
  std::string_view pending;   // the name of the member whose value comes next
  bool have_pending = false;
  std::size_t i = 0;
  while (i < json.size()) {
    const char c = json[i];
    if (c != '"') {
      if (c == ':') { /* the name stays pending for the value that follows */ }
      else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++i; continue; }
      else have_pending = false;   // a number, a literal, a bracket or a comma: no string value for the pending name
      ++i;
      continue;
    }
    const std::size_t begin = i + 1;
    std::size_t j = begin;
    while (j < json.size() && json[j] != '"') j += json[j] == '\\' ? 2 : 1;
    if (j >= json.size()) return false;
    std::size_t after = j + 1;
    while (after < json.size() && (json[after] == ' ' || json[after] == '\t' || json[after] == '\n' || json[after] == '\r')) ++after;
    if (after < json.size() && json[after] == ':') {   // a member name
      pending = json.substr(begin, j - begin);
      have_pending = true;
    } else {                                           // a value
      if (have_pending) visit(pending, begin, j);
      have_pending = false;
    }
    i = j + 1;
  }
  return true;
}

}  // namespace detail

// The document with the string values of its secret members sealed (an empty value is left as it is: there is nothing to keep secret in it). `fill_nonce(uint8_t[12])` supplies fresh random bytes.
template <class Nonce>
inline std::string seal_json(const Key& key, std::string_view json, Nonce&& fill_nonce) {
  struct Span { std::string name; std::size_t begin, end; };
  std::vector<Span> spans;
  if (!detail::each_member_string(json, [&](std::string_view name, std::size_t begin, std::size_t end) {
        if (end > begin && secret_name(name) && !is_sealed(json.substr(begin, end - begin))) spans.push_back({std::string(name), begin, end});
      })) return std::string(json);
  std::string out;
  std::size_t at = 0;
  for (const Span& span : spans) {
    out.append(json.substr(at, span.begin - at));
    std::uint8_t nonce[kNonceBytes];
    fill_nonce(nonce);
    out += seal_value(key, nonce, span.name, json.substr(span.begin, span.end - span.begin));
    at = span.end;
  }
  out.append(json.substr(at));
  return out;
}

// The reverse: every sealed value (by its prefix, whatever the member is called) is opened. kPlain when nothing was sealed, kOpened when something was and all opened, kBad when one could not be
// (then `out` is empty).
inline Open open_json(const Key& key, std::string_view json, std::string& out) {
  struct Span { std::string name; std::size_t begin, end; };
  std::vector<Span> spans;
  if (!detail::each_member_string(json, [&](std::string_view name, std::size_t begin, std::size_t end) {
        if (is_sealed(json.substr(begin, end - begin))) spans.push_back({std::string(name), begin, end});
      })) { out.assign(json); return Open::kPlain; }
  if (spans.empty()) { out.assign(json); return Open::kPlain; }
  std::string result;
  std::size_t at = 0;
  for (const Span& span : spans) {
    std::string plain;
    if (open_value(key, json.substr(span.begin, span.end - span.begin), span.name, plain) != Open::kOpened) { out.clear(); return Open::kBad; }
    result.append(json.substr(at, span.begin - at));
    result += plain;
    at = span.end;
  }
  result.append(json.substr(at));
  out = result;
  return Open::kOpened;
}

// Whether a stored JSON document holds a sealed value.
inline bool json_has_sealed(std::string_view json) {
  bool found = false;
  detail::each_member_string(json, [&](std::string_view, std::size_t begin, std::size_t end) { if (is_sealed(json.substr(begin, end - begin))) found = true; });
  return found;
}

// ---- the key's record ------------------------------------------------------------------------------------------------------------

// What is written at the start of the key partition: "ARMKEY01", the 32 bytes of the key and a check of the key (FNV-1a, 4 bytes: it tells a torn or erased record from a good one, it protects nothing).
constexpr std::size_t kRecordBytes = 44;

inline std::uint32_t fnv1a(const std::uint8_t* data, std::size_t n) {
  std::uint32_t h = 2166136261u;
  for (std::size_t i = 0; i < n; ++i) { h ^= data[i]; h *= 16777619u; }
  return h;
}

inline std::array<std::uint8_t, kRecordBytes> key_record(const Key& key) {
  std::array<std::uint8_t, kRecordBytes> r{};
  const char magic[] = "ARMKEY01";
  for (int i = 0; i < 8; ++i) r[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(magic[i]);
  for (std::size_t i = 0; i < 32; ++i) r[8 + i] = key[i];
  detail::store32(r.data() + 40, fnv1a(key.data(), key.size()));
  return r;
}

inline bool parse_key_record(const std::uint8_t* data, std::size_t n, Key& key) {
  if (n < kRecordBytes) return false;
  const char magic[] = "ARMKEY01";
  for (int i = 0; i < 8; ++i) if (data[i] != static_cast<std::uint8_t>(magic[i])) return false;
  Key found;
  for (std::size_t i = 0; i < 32; ++i) found[i] = data[8 + i];
  if (detail::load32(data + 40) != fnv1a(found.data(), found.size())) return false;
  bool all_zero = true;
  for (const std::uint8_t b : found) if (b != 0) all_zero = false;
  if (all_zero) return false;
  key = found;
  return true;
}

// ---- what to do with a stored value ----------------------------------------------------------------------------------------------

enum class Step { kNothing, kSeal, kOpen };
// A value that is in flash as it is (plain or sealed) and what the node is asked to do: seal the plain ones when the option is on, open the sealed ones when it is off.
inline Step step_for(bool wanted, bool stored_sealed) {
  if (wanted && !stored_sealed) return Step::kSeal;
  if (!wanted && stored_sealed) return Step::kOpen;
  return Step::kNothing;
}

// The option the node follows: its own setting, or the server's request (the policy message of the alarm node) - the server can ask for more protection, never for less than the node was set to.
inline bool wanted(bool own_setting, bool server_request) { return own_setting || server_request; }

}  // namespace armor::seal
