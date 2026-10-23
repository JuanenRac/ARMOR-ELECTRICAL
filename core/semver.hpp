// ARMOR-NODE-ELECTRICAL - comparing firmware versions: "x.y.z", or "x.y.z.b" from 0.8.0 on (an optional leading "v" is ignored, as GitHub's tags have one).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <string_view>

namespace armor::semver {

struct Version {
  int major = 0, minor = 0, patch = 0, build = 0;   // build is 0 for a version of three numbers (0.7.9 is 0.7.9.0)
  bool ok = false;   // false for anything that is not exactly (v)?digits.digits.digits(.digits)?
};

// At most this many digits in a number: a longer one would overflow, and no version of the family gets near it.
constexpr int kMaxDigits = 6;

inline Version parse(std::string_view text) {
  Version v;
  if (!text.empty() && text[0] == 'v') text.remove_prefix(1);
  int part = 0;
  long value = 0;
  int digits = 0;
  int numbers[4] = {0, 0, 0, 0};
  for (const char c : text) {
    if (c >= '0' && c <= '9') {
      if (++digits > kMaxDigits) return v;
      value = value * 10 + (c - '0');
    } else if (c == '.' && part < 3) {
      if (digits == 0) return v;
      numbers[part] = static_cast<int>(value);
      ++part; value = 0; digits = 0;
    } else return v;   // a pre-release or build suffix ("-rc1", "+build5"): treated as not a plain release, never offered as an update
  }
  if (digits == 0 || part < 2) return v;
  numbers[part] = static_cast<int>(value);
  v.major = numbers[0]; v.minor = numbers[1]; v.patch = numbers[2]; v.build = numbers[3];
  v.ok = true;
  return v;
}

inline bool less(const Version& a, const Version& b) {
  if (a.major != b.major) return a.major < b.major;
  if (a.minor != b.minor) return a.minor < b.minor;
  if (a.patch != b.patch) return a.patch < b.patch;
  return a.build < b.build;
}

// True only when both parse as plain releases and `candidate` is strictly newer than `current`.
inline bool is_newer(std::string_view candidate, std::string_view current) {
  const Version c = parse(candidate), r = parse(current);
  return c.ok && r.ok && less(r, c);
}

}  // namespace armor::semver
