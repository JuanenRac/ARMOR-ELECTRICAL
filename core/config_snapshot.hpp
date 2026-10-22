// ARMOR-ELECTRICAL - a copy of the settings kept in flash right before a firmware is installed, and the check that the settings survived the start. No hardware.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// An update replaces the program, not the settings in flash; but a new program may read the old settings differently (a field it no longer accepts is left at its first value, see
// stored_load.hpp) and a power cut during the first start may leave the stored text damaged. So:
//   * before an install starts (from GitHub or from the panel's upload) the node copies the stored settings text to a second place in flash, with the version of the firmware that wrote it and
//     a check number;
//   * at every start the node reads the stored settings with the tolerant loader, and the copy too; the copy is used only when the stored text cannot be read at all, or when the copy is
//     read with fewer fields dropped;
//   * fifteen seconds after the start the stored text is read again and compared with what the node expects (the text it read first, or the one it wrote since): a difference is a silent
//     revert (a failed save, a damaged store), the settings in memory are written again once and the outcome is told in the health report.
// The functions here decide from text and numbers; main/node_store.cpp does the flash.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "node_guard.hpp"

namespace armor::snapshot {

// A copy bigger than this is not made: the settings partition is small (64 KB) and holds the users, the certificate and the key too.
constexpr std::size_t kMaxBytes = 12 * 1024;
constexpr char kMagic[] = "ARMORSNAP1";

inline std::uint32_t fnv1a(std::string_view text) {
  std::uint32_t h = 2166136261u;
  for (const char c : text) { h ^= static_cast<unsigned char>(c); h *= 16777619u; }
  return h;
}

inline std::string hex8(std::uint32_t value) {
  static const char digits[] = "0123456789abcdef";
  std::string out(8, '0');
  for (int i = 7; i >= 0; --i) { out[static_cast<std::size_t>(i)] = digits[value & 0xFu]; value >>= 4; }
  return out;
}

inline bool version_text_ok(std::string_view v) {
  if (v.empty() || v.size() > 16) return false;
  for (const char c : v) if (!((c >= '0' && c <= '9') || c == '.')) return false;
  return true;
}

// "ARMORSNAP1 <firmware> <check>\n<settings text>". Empty when there is nothing to copy or the text is too big.
inline std::string make(std::string_view firmware, std::string_view settings_text) {
  if (settings_text.empty() || settings_text.size() > kMaxBytes || !version_text_ok(firmware)) return std::string();
  std::string blob = std::string(kMagic) + " " + std::string(firmware) + " " + hex8(fnv1a(settings_text)) + "\n";
  blob.append(settings_text);
  return blob;
}

struct Parsed {
  std::string firmware;
  std::string settings;
};

// The copy, when the blob is one and its check number is right.
inline bool parse(std::string_view blob, Parsed& out) {
  const std::size_t magic = sizeof kMagic - 1;
  if (blob.size() < magic + 1 + 1 + 1 + 8 + 1 + 1 || blob.compare(0, magic, kMagic) != 0 || blob[magic] != ' ') return false;
  const std::size_t first = magic + 1;
  const std::size_t space = blob.find(' ', first);
  const std::size_t newline = blob.find('\n', first);
  if (space == std::string_view::npos || newline == std::string_view::npos || space > newline || newline - space - 1 != 8) return false;
  const std::string_view firmware = blob.substr(first, space - first);
  if (!version_text_ok(firmware)) return false;
  const std::string_view check = blob.substr(space + 1, 8);
  const std::string_view settings = blob.substr(newline + 1);
  if (settings.empty() || settings.size() > kMaxBytes || hex8(fnv1a(settings)) != check) return false;
  out.firmware = std::string(firmware);
  out.settings = std::string(settings);
  return true;
}

enum class Pick { kCurrent, kSnapshot };

// Which text the node starts with. `*_ok` is whether the tolerant loader got settings out of the text at all, `*_dropped` how many fields it had to leave at their first values.
inline Pick pick_source(bool current_ok, std::size_t current_dropped, bool snapshot_ok, std::size_t snapshot_dropped) {
  if (!snapshot_ok) return Pick::kCurrent;
  if (!current_ok) return Pick::kSnapshot;
  return snapshot_dropped < current_dropped ? Pick::kSnapshot : Pick::kCurrent;
}

enum class Recheck { kFine, kWriteAgain };

// The check made fifteen seconds after the start: the stored text is compared with the text the node expects to be there (`expected_hash`, which the node moves whenever it saves). A stored
// text that cannot be read counts as different. A node that keeps no stored settings yet (`expecting_stored` false) has nothing to compare.
inline Recheck recheck(bool expecting_stored, std::uint32_t expected_hash, bool readable, std::uint32_t read_hash) {
  if (!expecting_stored) return Recheck::kFine;
  return (!readable || nodeguard::settings_reverted(expected_hash, read_hash)) ? Recheck::kWriteAgain : Recheck::kFine;
}

// The words of the health report for the outcomes.
inline const char* snapshot_outcome(bool restored, bool failed) { return failed ? "failed" : restored ? "restored" : ""; }
inline const char* reverted_outcome(Recheck found, bool written_and_confirmed) {
  if (found == Recheck::kFine) return "";
  return written_and_confirmed ? "reapplied" : "failed";
}

}  // namespace armor::snapshot
