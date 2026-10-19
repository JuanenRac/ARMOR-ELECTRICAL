// ARMOR-ELECTRICAL - which file of a GitHub release is the firmware of THIS board, and which is its checksum. Pure: tested on a computer.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// A release attaches one standalone application image per board, `armor_electrical-<board>.bin` (for example `armor_electrical-s3-wifi.bin`), and next to each its SHA-256, `<image>.sha256`, and the
// signature of its manifest, `<image>.sig2` (see fw_trust.hpp; the older `<image>.sig`, of the hash alone, is for the nodes before that check and is not used here).
// A release that predates that naming has only `armor_electrical.bin`: the image of the project's DEFAULT board (s3-eth, or lcd7box for the touch panel). A node finds the image built for
// its own board; the plain name is accepted only on the default board, and a node of any other board never installs an image that is not its own.
#pragma once
#include <string>

#include "json.hpp"

namespace armor::release {

constexpr const char* kAssetPrefix = "armor_electrical";

// Only what GitHub itself publishes for this repository is downloaded and installed: https://github.com/<owner>/<repo>/releases/download/<tag>/<file>, with nothing but one tag and one file
// name after it. (An address that comes out of the release's JSON is still checked: a node that asks for any address it is handed can be made to install from anywhere.)
inline bool is_official_url(const std::string& url, const std::string& repo) {
  const std::string prefix = "https://github.com/" + repo + "/releases/download/";
  if (repo.empty() || url.compare(0, prefix.size(), prefix) != 0) return false;
  std::size_t slashes = 0;
  for (std::size_t i = prefix.size(); i < url.size(); ++i) {
    const char c = url[i];
    if (c == '/') ++slashes;
    else if (c == '?' || c == '#' || c == '\\' || c == '@' || c <= ' ' || c == '%') return false;
  }
  return slashes == 1 && url.size() > prefix.size() + 3 && url.find("..") == std::string::npos && url.back() != '/';
}

// The buffer the HTTP client keeps for the headers of an answer. GitHub redirects a download to a storage address whose headers add up to more than 5 KB (a signed Location of
// about a thousand characters, a security policy, cookies): a buffer of 4 KB made the client give up with "out of buffer". The sdkconfig sends an allocation of 16384 bytes or less to
// INTERNAL memory (the part the TLS handshake needs, and there is only some 65 to 80 KB of it free then) and a bigger one to the external RAM, so 20480 bytes is both roomy and free of
// cost for the internal heap. A board with no external RAM (or none free) takes the smallest buffer that holds those headers.
constexpr int kHeaderBufferExternal = 20480;
constexpr int kHeaderBufferInternal = 6144;
inline int header_buffer_size(std::size_t free_external_bytes) { return free_external_bytes >= 2u * static_cast<std::size_t>(kHeaderBufferExternal) ? kHeaderBufferExternal : kHeaderBufferInternal; }

// What to tell when a file of a release (`what` is "checksum" or "signature") could not be read: the real reason, never "the release has none" (that is only for a release that does
// not carry the file). `problem` is what the download said: connect_failed, read_error, client_init, http_<status> or too_large.
inline std::string fetch_error(const std::string& problem, const std::string& what) {
  if (problem.compare(0, 5, "http_") == 0) return problem;
  if (problem == "too_large") return what + "_too_large";
  return what + "_unreachable";
}

inline std::string asset_name(const char* board_id) { return std::string(kAssetPrefix) + "-" + (board_id != nullptr ? board_id : "") + ".bin"; }

// The plain name, for the board whose image a release without the board's name carries; "" for every other board.
inline std::string legacy_asset_name(const char* board_id) {
  const std::string board = board_id != nullptr ? board_id : "";
  return board == "s3-eth" || board == "lcd7box" ? std::string(kAssetPrefix) + ".bin" : "";
}

struct Picked {
  std::string image_url;      // empty: the release has no image for this board
  std::string checksum_url;   // empty: it has the image but not the checksum file next to it
  std::string signature_url;  // empty: it has the image but not the signature of its manifest (`<image>.sig2`)
  bool legacy = false;        // the image is the one with the plain name
};

// `assets` is the "assets" array of the release's JSON (each with "name" and "browser_download_url"). The image built for the board wins; the plain name is the fallback.
inline Picked pick(const json::Value* assets, const char* board_id) {
  Picked picked;
  if (assets == nullptr || !assets->is_array()) return picked;
  const std::string wanted = asset_name(board_id), legacy = legacy_asset_name(board_id);
  std::string wanted_url, wanted_sum, wanted_sig, legacy_url, legacy_sum, legacy_sig;
  for (const json::Value& asset : assets->items) {
    const std::string name = asset.string_or("name", ""), url = asset.string_or("browser_download_url", "");
    if (name == wanted) wanted_url = url;
    else if (name == wanted + ".sha256") wanted_sum = url;
    else if (name == wanted + ".sig2") wanted_sig = url;
    else if (!legacy.empty() && name == legacy) legacy_url = url;
    else if (!legacy.empty() && name == legacy + ".sha256") legacy_sum = url;
    else if (!legacy.empty() && name == legacy + ".sig2") legacy_sig = url;
  }
  if (!wanted_url.empty()) { picked.image_url = wanted_url; picked.checksum_url = wanted_sum; picked.signature_url = wanted_sig; }
  else if (!legacy_url.empty()) { picked.image_url = legacy_url; picked.checksum_url = legacy_sum; picked.signature_url = legacy_sig; picked.legacy = true; }
  return picked;
}

}  // namespace armor::release
