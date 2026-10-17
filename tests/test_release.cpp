// ARMOR-ELECTRICAL - host tests of the comparison of versions and of the choice of a release's firmware image: the update from GitHub never installs what is not this board's.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include <cstdio>
#include <string>

#include "../core/fw_trust.hpp"
#include "../core/json.hpp"
#include "../core/release_assets.hpp"
#include "../core/semver.hpp"

static int failures = 0;
static int checks = 0;
#define CHECK(condition)                                                              \
  do {                                                                                \
    ++checks;                                                                         \
    if (!(condition)) {                                                               \
      ++failures;                                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);                \
    }                                                                                 \
  } while (0)

using namespace armor;

static void test_semver() {
  using namespace semver;
  CHECK(is_newer("v0.4.6", "0.4.5") && is_newer("0.5.0", "0.4.9") && is_newer("1.0.0", "0.4.5") && is_newer("0.10.0", "0.9.9"));
  CHECK(!is_newer("0.4.5", "0.4.5") && !is_newer("0.4.4", "0.4.5") && !is_newer("0.9.9", "0.10.0"));
  CHECK(!is_newer("0.4.6-rc1", "0.4.5"));   // a pre-release is never offered
  CHECK(!is_newer("not-a-version", "0.4.5") && !is_newer("0.4.6", "also-not-a-version"));
  CHECK(!parse("").ok && !parse("1.2").ok && !parse("1.2.3.4.5").ok && !parse("1.2.x").ok && !parse("v").ok && !parse("..").ok && !parse("1.2.3.").ok && !parse("1..3").ok);
  // from 0.8.0 on a version has four numbers (0.7.9 is followed by 0.8.0.0): they compare as four, and a version of three is the same as the one with a zero build
  CHECK(parse("0.8.0.1").ok && parse("0.8.0.1").build == 1 && parse("0.8.0").build == 0);
  CHECK(is_newer("0.8.0.0", "0.7.9") && is_newer("0.8.0.1", "0.8.0.0") && is_newer("0.8.1.0", "0.8.0.9") && is_newer("0.9.0.0", "0.8.9.9") && is_newer("0.8.0.1", "0.8.0"));
  CHECK(!is_newer("0.8.0.0", "0.8.0") && !is_newer("0.8.0", "0.8.0.0") && !is_newer("0.8.0.0", "0.8.0.1") && !is_newer("0.7.9", "0.8.0.0"));
  // a number too long to be a real one is refused instead of overflowing
  CHECK(!parse("99999999999999999999.0.0").ok && !parse("1.2.1234567").ok && parse("1.2.123456").ok);
  CHECK(parse("v2.10.3").ok && parse("v2.10.3").major == 2 && parse("v2.10.3").minor == 10 && parse("v2.10.3").patch == 3);
}

static json::Value fake_release(const std::string& names) {
  // names: "a,b,c" -> assets with those names and a url "https://x/<name>"
  std::string text = "[";
  std::size_t at = 0;
  bool first = true;
  while (at <= names.size()) {
    std::size_t comma = names.find(',', at);
    const std::string name = names.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
    if (!name.empty()) { text += std::string(first ? "" : ",") + "{\"name\":\"" + name + "\",\"browser_download_url\":\"https://x/" + name + "\"}"; first = false; }
    if (comma == std::string::npos) break;
    at = comma + 1;
  }
  text += "]";
  json::Value document;
  json::parse(text, document);
  return document;
}

static void test_assets() {
  const std::string p = release::kAssetPrefix;
  // the names
  CHECK(release::asset_name("s3-wifi") == p + "-s3-wifi.bin" && release::asset_name("s3-eth") == p + "-s3-eth.bin" && release::asset_name(nullptr) == p + "-.bin");
  CHECK(release::legacy_asset_name("s3-eth") == p + ".bin" && release::legacy_asset_name("lcd7box") == p + ".bin" && release::legacy_asset_name("s3-wifi").empty() && release::legacy_asset_name(nullptr).empty());
  // a release with an image per board: each board gets its own, with the checksum next to it
  const json::Value both = fake_release(p + "-s3-eth.bin," + p + "-s3-eth.bin.sha256," + p + "-s3-wifi.bin," + p + "-s3-wifi.bin.sha256," + p + ".bin," + p + ".bin.sha256,notes.txt");
  const release::Picked wifi = release::pick(&both, "s3-wifi"), eth = release::pick(&both, "s3-eth");
  CHECK(wifi.image_url == "https://x/" + p + "-s3-wifi.bin" && wifi.checksum_url == "https://x/" + p + "-s3-wifi.bin.sha256" && !wifi.legacy);
  CHECK(eth.image_url == "https://x/" + p + "-s3-eth.bin" && eth.checksum_url == "https://x/" + p + "-s3-eth.bin.sha256" && !eth.legacy);
  // a release from before the naming: only the default board finds its image; the Wi-Fi board never takes it
  const json::Value old = fake_release(p + ".bin," + p + ".bin.sha256," + p + "-s3-wifi.bin.sha256");
  const release::Picked old_eth = release::pick(&old, "s3-eth"), old_wifi = release::pick(&old, "s3-wifi");
  CHECK(old_eth.image_url == "https://x/" + p + ".bin" && old_eth.checksum_url == "https://x/" + p + ".bin.sha256" && old_eth.legacy);
  CHECK(old_wifi.image_url.empty() && old_wifi.checksum_url.empty() && !old_wifi.legacy);
  CHECK(release::pick(&old, "lcd7box").image_url == "https://x/" + p + ".bin");
  // the checksum of one image is never used for another: an image with no checksum file is picked WITHOUT one (the caller refuses to install it)
  const json::Value no_sum = fake_release(p + "-s3-eth.bin," + p + ".bin.sha256");
  CHECK(release::pick(&no_sum, "s3-eth").image_url == "https://x/" + p + "-s3-eth.bin" && release::pick(&no_sum, "s3-eth").checksum_url.empty());
  // the signature of an image is the `.sig2` next to it, and only that one: another board's signature is never used, an image without one is picked without it, and the older `.sig`
  // (of the hash alone, for the nodes before the manifest) is not a signature a node of today accepts
  const json::Value signed_release = fake_release(p + "-s3-eth.bin," + p + "-s3-eth.bin.sha256," + p + "-s3-eth.bin.sig2," + p + "-s3-wifi.bin," + p + "-s3-wifi.bin.sha256," + p + "-s3-wifi.bin.sig2");
  CHECK(release::pick(&signed_release, "s3-eth").signature_url == "https://x/" + p + "-s3-eth.bin.sig2" && release::pick(&signed_release, "s3-wifi").signature_url == "https://x/" + p + "-s3-wifi.bin.sig2");
  const json::Value half_signed = fake_release(p + "-s3-eth.bin," + p + "-s3-eth.bin.sha256," + p + "-s3-wifi.bin.sig2");
  CHECK(release::pick(&half_signed, "s3-eth").signature_url.empty() && release::pick(&half_signed, "s3-eth").image_url == "https://x/" + p + "-s3-eth.bin");
  const json::Value only_old_signature = fake_release(p + "-s3-eth.bin," + p + "-s3-eth.bin.sha256," + p + "-s3-eth.bin.sig");
  CHECK(release::pick(&only_old_signature, "s3-eth").signature_url.empty());
  const json::Value old_signed = fake_release(p + ".bin," + p + ".bin.sha256," + p + ".bin.sig2");
  CHECK(release::pick(&old_signed, "s3-eth").signature_url == "https://x/" + p + ".bin.sig2" && release::pick(&old_signed, "s3-eth").legacy);
  // an image of another board is never taken, nothing at all gives nothing
  const json::Value other = fake_release("armor_other-s3-eth.bin," + p + "-lcd7box.bin");
  CHECK(release::pick(&other, "s3-eth").image_url.empty() && release::pick(&other, "s3-wifi").image_url.empty());
  const json::Value none = fake_release("");
  CHECK(release::pick(&none, "s3-eth").image_url.empty() && release::pick(nullptr, "s3-eth").image_url.empty());
  json::Value not_array; json::parse("{\"a\":1}", not_array);
  CHECK(release::pick(&not_array, "s3-eth").image_url.empty());
}

// Only what GitHub publishes for this very repository is downloaded.
static void test_official_urls() {
  const std::string repo = "JuanenRac/ARMOR-RADAR";
  const std::string base = "https://github.com/JuanenRac/ARMOR-RADAR/releases/download/";
  CHECK(release::is_official_url(base + "v0.6.7/armor_radar-s3-eth.bin", repo));
  CHECK(!release::is_official_url("http://github.com/JuanenRac/ARMOR-RADAR/releases/download/v0.6.7/armor_radar-s3-eth.bin", repo));
  CHECK(!release::is_official_url("https://github.com/JuanenRac/ARMOR-SOLAR/releases/download/v0.6.7/armor_radar-s3-eth.bin", repo));
  CHECK(!release::is_official_url("https://github.com.evil.example/JuanenRac/ARMOR-RADAR/releases/download/v1/a.bin", repo));
  CHECK(!release::is_official_url("https://github.com/JuanenRac/ARMOR-RADAR/releases/download/../../../evil/a.bin", repo));
  CHECK(!release::is_official_url(base + "v0.6.7/a.bin?x=1", repo) && !release::is_official_url(base + "v0.6.7/a.bin#x", repo) && !release::is_official_url(base + "v0.6.7/a b.bin", repo));
  CHECK(!release::is_official_url(base + "v0.6.7/sub/a.bin", repo) && !release::is_official_url(base + "a.bin", repo) && !release::is_official_url(base + "v0.6.7/", repo) && !release::is_official_url(base, repo));
  CHECK(!release::is_official_url(base + "v0.6.7/a.bin", "") && !release::is_official_url("", repo));
  CHECK(!release::is_official_url("https://github.com/JuanenRac/ARMOR-RADAR/releases/download/v1@evil/a.bin", repo));
}

// The signature binds the hash to the project, the board and the version: a hash signed for one image cannot be presented for another. The signature below was made with the key of the
// releases (tools/firmware_sign.mjs --manifest armor_radar s3-eth 0.6.7 <hash>).
static void test_manifest() {
  const std::string hash(64, 'a');
  const std::string signature = "13bf258dc4b954aca2e0e1d651b1621de59827b6c55587759ca390fcfd06b33b28860c86b1637ba013002fa829241307c0ffb508c87a405d2497560e3ba65101";
  CHECK(fw_trust::manifest_text("armor_radar", "s3-eth", "0.6.7", hash) == "armor-fw/1|armor_radar|s3-eth|0.6.7|" + hash);
  CHECK(fw_trust::manifest_signature_ok("armor_radar", "s3-eth", "0.6.7", hash, signature));
  CHECK(fw_trust::manifest_signature_ok("armor_radar", "s3-eth", "0.6.7", hash, signature + "\n"));            // the file ends with a line feed
  CHECK(!fw_trust::manifest_signature_ok("armor_radar", "s3-wifi", "0.6.7", hash, signature));                // another board
  CHECK(!fw_trust::manifest_signature_ok("armor_solar", "s3-eth", "0.6.7", hash, signature));                 // another project
  CHECK(!fw_trust::manifest_signature_ok("armor_radar", "s3-eth", "0.6.8", hash, signature));                 // another version: an old image presented as the new one
  CHECK(!fw_trust::manifest_signature_ok("armor_radar", "s3-eth", "0.6.7", std::string(64, 'b'), signature));  // another image
  CHECK(!fw_trust::manifest_signature_ok("armor_radar", "s3-eth", "0.6.7", hash, ""));
  CHECK(!fw_trust::manifest_signature_ok("armor_radar", "s3-eth", "0.6.7", hash, signature.substr(0, 126)));
  std::string flipped = signature;
  flipped[10] = flipped[10] == '0' ? '1' : '0';
  CHECK(!fw_trust::manifest_signature_ok("armor_radar", "s3-eth", "0.6.7", hash, flipped));
  // a part that could be read as two (a separator or a space inside it) is refused before anything is verified
  CHECK(!fw_trust::manifest_signature_ok("armor_radar|s3-eth", "", "0.6.7", hash, signature) && !fw_trust::manifest_signature_ok("armor_radar", "s3-eth", "0.6.7 ", hash, signature));
  // the signature of the hash alone (the `.sig` of the older releases) is not a manifest signature
  const std::string hash_signature(128, '0');
  CHECK(!fw_trust::manifest_signature_ok("armor_radar", "s3-eth", "0.6.7", hash, hash_signature));
}

int main() {
  test_semver();
  test_assets();
  test_official_urls();
  test_manifest();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
