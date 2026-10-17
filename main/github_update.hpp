// ARMOR-ELECTRICAL - checking GitHub's own releases for a newer firmware and installing it, next to the existing manual upload (web_server.cpp's
// /api/v1/ota): both end up in the same OTA slot through the same esp_ota_* calls, this one just gets its bytes from the Internet instead
// of the panel's own upload. Nothing here replaces the manual path - a board with no Internet route, or a fleet an operator wants to hold
// back, still updates exactly as it always has.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <string>

namespace armor::github_update {

// The repository releases are read from. Which file of a release is this board's image (and its SHA-256) is core/release_assets.hpp's: a node installs a release only when the
// image it downloaded has exactly the hash the release publishes - a truncated, altered or mixed-up download is refused.
constexpr const char* kRepo = "JuanenRac/ARMOR-ELECTRICAL";

struct CheckResult {
  bool ok = false;             // false: `error` says why (network, no release, no matching asset...)
  std::string error;
  std::string latest_version;  // the release's own tag, without a leading "v"
  std::string asset_url;       // empty when the latest release has no armor_electrical.bin attached
  std::string sha256;          // the hash the release publishes for it (64 lowercase hex digits); empty when it publishes none
  std::string signature_url;   // the `<image>.sig2` next to it: the signature of its manifest (empty when the release has none)
  std::string board;           // the board the offer is for: the manifest names it
  bool update_available = false;
};

// Asks GitHub's API for the newest release and compares it to the running firmware; `board_id` (board::kId) says which image is this node's. Blocks for the duration of the HTTPS request.
CheckResult check(const char* board_id);

struct InstallResult {
  bool ok = false;
  std::string error;
  std::string version;
  std::size_t bytes = 0;
};

// The install runs in a task of its own so the panel can show how far it is: start() returns at once, progress() says where it stands. `state` is
// "idle", "downloading", "verifying", "done" or "failed" (with `error`); a "done" install has already set the new image to boot and the caller restarts.
struct Progress {
  std::string state = "idle";
  std::size_t got = 0;
  std::size_t total = 0;
  std::string error;
  std::string version;
};
Progress progress();

// Copies the firmware that runs into the other slot (ota_0 <-> ota_1), so both hold the same version: a copy to fall back to, or to keep when the other
// slot is about to be used for something else. It runs in a task of its own like an install; the running slot is only read, the other one is erased and
// written, the image is checked at the end and nothing is set to boot (switching slots is a separate step). `state` is "idle", "copying", "done" or "failed".
struct CopyProgress {
  std::string state = "idle";
  std::size_t done = 0;
  std::size_t total = 0;
  std::string error;
  std::string version;
};
// The look for a newer release (and the install of it) in a task of their own: the web server has one thread, and a request to GitHub over TLS takes seconds, during which the panel
// would not answer. `start_check` returns at once; `check_progress` says "idle", "checking", "done" or "failed" and, when it is done, what was found. `start_latest` checks and then
// installs what it found, all in the task (progress() tells how far the install is).
struct CheckProgress {
  std::string state = "idle";
  CheckResult result;
};
bool start_check(const char* board_id);
CheckProgress check_progress();
bool start_latest(const char* board_id);

bool start_copy();   // false when an install or a copy is already running
CopyProgress copy_progress();
// Changes every time a slot is written: whoever caches what a slot holds starts again when it changes.
unsigned slot_generation();

// Downloads what check() offered (and only that: the address must be one of this repository's own release files) straight into the next OTA slot - the same checks as an upload (minimum
// size, the image's own magic byte, the project name) plus three more: the image must hash to the hash the release publishes; the version inside it must be the release's own and newer than
// the one that runs (an old image, legitimately signed, cannot be installed again); and the signature of the manifest (project, board, version, hash) must verify with the key of the
// releases. Only then is it set to boot. Does not restart; the caller decides when. One install at a time: a second call while one runs is refused ("ota_busy").
InstallResult install(const CheckResult& offer);

}  // namespace armor::github_update
