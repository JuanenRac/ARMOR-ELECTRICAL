// ARMOR-ELECTRICAL - see github_update.hpp.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include "github_update.hpp"

#include <atomic>
#include <cctype>
#include <memory>
#include <mutex>
#include <cstdint>
#include <cstring>
#include <new>
extern "C" {
#include "esp_app_desc.h"
#include "esp_image_format.h"
#include "esp_partition.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
}
#include "core/json.hpp"
#include "core/fw_trust.hpp"
#include "core/release_assets.hpp"
#include "core/semver.hpp"
#include "psram_mem.hpp"

namespace armor::github_update {
namespace {
constexpr const char* kTag = "github_update";
constexpr std::size_t kMaxApiResponse = 24 * 1024;   // the release's own JSON, a changelog included; anything bigger is not trusted
constexpr std::size_t kMinFirmware = 100 * 1024;
constexpr std::size_t kMaxFirmware = 0x300000;   // the size of an application slot in partitions.csv: a bigger image fits nowhere
// GitHub answers a download with a redirect to a storage server whose address is long and signed (the headers of that answer add up to more than 5 KB): the header buffer must hold
// them. Its size, and why it is above 16384 bytes, is told in release_assets.hpp.
int header_buffer() { return release::header_buffer_size(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)); }
constexpr int kMaxRedirects = 5;

// Opens the connection and reads the headers, following redirects (a release asset is always one): `status` and `length` are those of the final answer.
bool open_following_redirects(esp_http_client_handle_t client, int& length, int& status) {
  for (int hop = 0; hop <= kMaxRedirects; ++hop) {
    if (esp_http_client_open(client, 0) != ESP_OK) return false;
    length = esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);
    if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308) return true;
    if (esp_http_client_set_redirection(client) != ESP_OK) return true;   // no usable Location: the 3xx is reported as it is
    esp_http_client_close(client);
  }
  return true;
}

// Where the install in progress stands (see start()).
std::mutex g_lock;
Progress g_progress;
CopyProgress g_copy;
bool g_running = false;
std::atomic<unsigned> g_generation{0};
struct Job { const char* board = nullptr; };
std::atomic<bool> g_installing{false};   // install() runs once at a time, whoever calls it (it has one download buffer and one slot)
CheckProgress g_check;
bool g_checking = false;

// A GET with no body, read fully into a bounded buffer. False on any network, HTTP or size problem.
bool get_bounded_once(const std::string& url, const char* accept, std::string& out, std::string& error) {
  esp_http_client_config_t config{};
  config.url = url.c_str();
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.timeout_ms = 15000;
  config.buffer_size = header_buffer();
  const unsigned internal_before = static_cast<unsigned>(esp_get_free_internal_heap_size());
  esp_http_client_handle_t client = esp_http_client_init(&config);
  ESP_LOGI(kTag, "HTTP client opened with a %d-byte header buffer: %u bytes of internal memory free before, %u after", config.buffer_size, internal_before, static_cast<unsigned>(esp_get_free_internal_heap_size()));
  if (client == nullptr) { error = "client_init"; return false; }
  esp_http_client_set_header(client, "User-Agent", "ARMOR-ELECTRICAL");
  if (accept != nullptr) esp_http_client_set_header(client, "Accept", accept);
  bool ok = false;
  int length = 0, status = 0;
  if (open_following_redirects(client, length, status)) {
    if (status != 200) { error = "http_" + std::to_string(status); }
    else if (length > 0 && static_cast<std::size_t>(length) > kMaxApiResponse) { error = "too_large"; }
    else {
      char chunk[512];
      ok = true;
      for (;;) {
        const int n = esp_http_client_read(client, chunk, sizeof chunk);
        if (n < 0) { ok = false; error = "read_error"; break; }
        if (n == 0) break;
        if (out.size() + static_cast<std::size_t>(n) > kMaxApiResponse) { ok = false; error = "too_large"; break; }
        out.append(chunk, static_cast<std::size_t>(n));
      }
    }
  } else {
    error = "connect_failed";
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return ok;
}

// A connection that fails is tried again: the node may be answering a browser at the same moment, and a handshake that finds no memory left for a moment is not a lost network. Each
// attempt starts from nothing (the client is made and freed inside), and the wait lets the other connection finish and give its memory back.
bool get_bounded(const std::string& url, const char* accept, std::string& out, std::string& error) {
  for (int attempt = 1; attempt <= 3; ++attempt) {
    out.clear();
    error.clear();
    if (get_bounded_once(url, accept, out, error)) return true;
    if (error != "connect_failed" && error != "read_error") return false;
    ESP_LOGW(kTag, "the connection to GitHub failed (attempt %d of 3, %u bytes of internal memory free)", attempt, static_cast<unsigned>(esp_get_free_internal_heap_size()));
    if (attempt < 3) vTaskDelay(pdMS_TO_TICKS(2500));
  }
  return false;
}

// The 64 hex digits at the start of a `sha256sum` line (a bare hash also works), in lowercase; empty when the text does not start with one.
std::string parse_sha256(const std::string& text) {
  std::size_t start = 0;
  while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) ++start;
  if (text.size() < start + 64) return "";
  std::string hash = text.substr(start, 64);
  for (char& c : hash) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!std::isxdigit(static_cast<unsigned char>(c))) return "";
  }
  if (text.size() > start + 64 && std::isxdigit(static_cast<unsigned char>(text[start + 64]))) return "";   // longer than a SHA-256
  return hash;
}

std::string to_hex(const std::uint8_t* bytes, std::size_t count) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  for (std::size_t i = 0; i < count; ++i) { out += digits[bytes[i] >> 4]; out += digits[bytes[i] & 15]; }
  return out;
}
}  // namespace

CheckResult check(const char* board_id) {
  CheckResult result;
  std::string body;
  const std::string url = std::string("https://api.github.com/repos/") + kRepo + "/releases/latest";
  if (!get_bounded(url, "application/vnd.github+json", body, result.error)) return result;
  json::Value document;
  if (!json::parse(body, document) || !document.is_object()) { result.error = "bad_json"; return result; }
  const json::Value* tag = document.get("tag_name");
  if (tag == nullptr || !tag->is_string() || tag->text.empty()) { result.error = "no_release"; return result; }
  std::string_view tag_text = tag->text;
  if (!tag_text.empty() && tag_text[0] == 'v') tag_text.remove_prefix(1);
  result.latest_version = std::string(tag_text);
  const json::Value* assets = document.get("assets");
  // The image built for this board wins; the plain name (the default board's, from a release that predates the naming) only when there is none.
  const release::Picked picked = release::pick(assets, board_id);
  result.asset_url = picked.image_url;
  const std::string checksum_url = picked.checksum_url;
  const std::string signature_url = picked.signature_url;
  result.board = board_id != nullptr ? board_id : "";
  result.signature_url = signature_url;
  // Everything this node will download comes out of the release's JSON; it is used only if it is a file of this repository's own releases.
  if ((!result.asset_url.empty() && !release::is_official_url(result.asset_url, kRepo)) || (!checksum_url.empty() && !release::is_official_url(checksum_url, kRepo)) ||
      (!signature_url.empty() && !release::is_official_url(signature_url, kRepo))) {
    result.asset_url.clear();
    result.signature_url.clear();
    result.error = "bad_url";
    return result;
  }
  // The release's JSON (several assets make it 15 KB or more) and its parsed tree are no longer needed: give the memory back before the next
  // TLS connection (the hash), which is what the node was short of when it said it could not get the checksum.
  document = json::Value();
  std::string().swap(body);
  const bool release_is_newer = semver::is_newer(result.latest_version, esp_app_get_description()->version);
  // A newer release that carries no image built for THIS board (a board that was not part of the publication) is said so, instead of passing for "up to date".
  if (release_is_newer && result.asset_url.empty()) { result.error = "no_asset_for_board"; return result; }
  const bool newer = !result.asset_url.empty() && release_is_newer;
  if (newer) {
    // A newer release is offered only with the hash that lets the image be checked: without it the node says so instead of installing blind.
    std::string text, problem;
    if (checksum_url.empty()) { result.error = "no_checksum"; return result; }
    if (!get_bounded(checksum_url, nullptr, text, problem)) { ESP_LOGW(kTag, "the checksum of the release could not be read: %s", problem.c_str()); result.error = release::fetch_error(problem, "checksum"); return result; }
    result.sha256 = parse_sha256(text);
    if (result.sha256.empty()) { result.error = "no_checksum"; return result; }
    // ... and with the signature that says who published that hash: a release that is not signed is not offered (install() checks the signature again, with its own download of it).
    if (signature_url.empty()) { result.error = "no_signature"; return result; }
  }
  result.ok = true;
  result.update_available = newer;
  return result;
}

InstallResult install(const CheckResult& offer) {
  InstallResult result;
  const std::string& asset_url = offer.asset_url;
  const std::string& expected_sha256 = offer.sha256;
  if (asset_url.empty()) { result.error = "no_asset"; return result; }
  if (!release::is_official_url(asset_url, kRepo)) { result.error = "bad_url"; return result; }
  if (expected_sha256.size() != 64) { result.error = "no_checksum"; return result; }
  if (offer.signature_url.empty()) { result.error = "no_signature"; return result; }
  if (!release::is_official_url(offer.signature_url, kRepo)) { result.error = "bad_url"; return result; }
  if (g_installing.exchange(true)) { result.error = "ota_busy"; return result; }
  struct Done { ~Done() { g_installing = false; } } done;
  esp_http_client_config_t config{};
  config.url = asset_url.c_str();
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.timeout_ms = 20000;
  config.buffer_size = header_buffer();
  const unsigned internal_before = static_cast<unsigned>(esp_get_free_internal_heap_size());
  esp_http_client_handle_t client = esp_http_client_init(&config);
  ESP_LOGI(kTag, "HTTP client opened with a %d-byte header buffer: %u bytes of internal memory free before, %u after", config.buffer_size, internal_before, static_cast<unsigned>(esp_get_free_internal_heap_size()));
  if (client == nullptr) { result.error = "client_init"; return result; }
  esp_http_client_set_header(client, "User-Agent", "ARMOR-ELECTRICAL");
  int length = 0, status = 0;
  bool opened = false;
  for (int attempt = 1; attempt <= 3 && !opened; ++attempt) {
    opened = open_following_redirects(client, length, status);
    if (!opened && attempt < 3) { ESP_LOGW(kTag, "the download did not connect (attempt %d of 3)", attempt); esp_http_client_close(client); vTaskDelay(pdMS_TO_TICKS(2500)); }
  }
  if (!opened) { result.error = "connect_failed"; esp_http_client_cleanup(client); return result; }
  if (status != 200 || length <= 0 || static_cast<std::size_t>(length) < kMinFirmware || static_cast<std::size_t>(length) > kMaxFirmware) {
    result.error = status != 200 ? "http_" + std::to_string(status) : "bad_size";
    esp_http_client_close(client); esp_http_client_cleanup(client);
    return result;
  }
  const std::size_t total = static_cast<std::size_t>(length);
  { std::lock_guard<std::mutex> guard(g_lock); g_progress.total = total; g_progress.got = 0; g_progress.state = "downloading"; }
  const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
  if (target == nullptr) { result.error = "no_partition"; esp_http_client_close(client); esp_http_client_cleanup(client); return result; }
  // The slot is smaller than the largest image the check above allows (the limit above is the table's, a board may carry a smaller slot): an image that does not fit is said so, instead of failing inside the write with a message that says nothing.
  if (total > target->size) { result.error = "bad_size"; esp_http_client_close(client); esp_http_client_cleanup(client); return result; }
  esp_ota_handle_t handle = 0;
  ++g_generation;   // beginning an update erases the slot: what was known of it is gone
  if (esp_ota_begin(target, total, &handle) != ESP_OK) { result.error = "ota_begin"; esp_http_client_close(client); esp_http_client_cleanup(client); return result; }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  std::unique_ptr<std::uint8_t[]> buffer(new (std::nothrow) std::uint8_t[2048]);
  if (!buffer) { result.error = "no_memory"; esp_ota_abort(handle); esp_http_client_close(client); esp_http_client_cleanup(client); return result; }
  std::uint8_t* const chunk = buffer.get();
  constexpr std::size_t kChunk = 2048;
  std::size_t got = 0;
  bool first = true, failed = false;
  while (got < total) {
    const int n = esp_http_client_read(client, reinterpret_cast<char*>(chunk), kChunk);
    if (n < 0) { result.error = "read_error"; failed = true; break; }
    if (n == 0) { result.error = "short_read"; failed = true; break; }
    if (first) {
      first = false;
      if (chunk[0] != 0xE9) { result.error = "not_firmware"; failed = true; break; }   // the magic byte of an ESP32 image
    }
    if (esp_ota_write(handle, chunk, static_cast<std::size_t>(n)) != ESP_OK) { result.error = "ota_write"; failed = true; break; }
    mbedtls_sha256_update(&sha, chunk, static_cast<std::size_t>(n));
    got += static_cast<std::size_t>(n);
    { std::lock_guard<std::mutex> guard(g_lock); g_progress.got = got; }
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  if (failed) { esp_ota_abort(handle); mbedtls_sha256_free(&sha); return result; }
  { std::lock_guard<std::mutex> guard(g_lock); g_progress.state = "verifying"; }
  std::uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  if (to_hex(digest, sizeof digest) != expected_sha256) {
    // What arrived is not what the release published: the slot is left as it was and nothing is set to boot.
    esp_ota_abort(handle);
    ESP_LOGE(kTag, "the downloaded image does not match the hash of the release: refused");
    result.error = "checksum_mismatch";
    return result;
  }
  // The hash is the one the release publishes. What the image says about itself (its project and its version) is read once it is complete, and then the signature of the manifest -
  // project, board, version and hash - says it was published by whoever holds the private key of the A.R.M.O.R. releases. Nothing is set to boot before all of it holds.
  std::string signature_text, problem;
  if (!get_bounded(offer.signature_url, nullptr, signature_text, problem)) {
    esp_ota_abort(handle);
    ESP_LOGE(kTag, "the signature of the release could not be read (%s): refused", problem.c_str());
    result.error = release::fetch_error(problem, "signature");
    return result;
  }
  if (esp_ota_end(handle) != ESP_OK) { result.error = "image_invalid"; return result; }
  esp_app_desc_t description{};
  if (esp_ota_get_partition_description(target, &description) != ESP_OK || std::strcmp(description.project_name, esp_app_get_description()->project_name) != 0) {
    result.error = "wrong_firmware";
    return result;
  }
  const std::string image_version = description.version;
  if (!offer.latest_version.empty() && image_version != offer.latest_version) {
    ESP_LOGE(kTag, "the image says it is %s and the release says %s: refused", image_version.c_str(), offer.latest_version.c_str());
    result.error = "version_mismatch";
    return result;
  }
  if (!semver::is_newer(image_version, esp_app_get_description()->version)) {
    ESP_LOGE(kTag, "the image is %s and the node runs %s: an image that is not newer is refused", image_version.c_str(), esp_app_get_description()->version);
    result.error = "not_newer";
    return result;
  }
  if (!fw_trust::manifest_signature_ok(description.project_name, offer.board, image_version, expected_sha256, signature_text)) {
    ESP_LOGE(kTag, "the signature of the release does not match this image (project, board, version and hash): refused");
    result.error = "bad_signature";
    return result;
  }
  if (esp_ota_set_boot_partition(target) != ESP_OK) { result.error = "ota_boot"; return result; }
  ESP_LOGW(kTag, "firmware %s (%u bytes) installed from GitHub in %s", description.version, static_cast<unsigned>(total), target->label);
  result.ok = true;
  result.version = description.version;
  result.bytes = total;
  return result;
}

namespace {
void install_task(void* raw) {
  std::unique_ptr<Job> job(static_cast<Job*>(raw));
  InstallResult result;
  if (job->board != nullptr) {
    // The look for the newest release is part of the task (the panel's request returned long ago): what was found is what is installed.
    const CheckResult found = check(job->board);
    if (!found.ok) result.error = found.error;
    else if (found.asset_url.empty()) result.error = "no_asset";
    else if (!found.update_available) result.error = "up_to_date";
    else if (found.sha256.empty()) result.error = "no_checksum";
    else result = install(found);
  } else {
    result.error = "no_asset";
  }
  {
    std::lock_guard<std::mutex> guard(g_lock);
    g_progress.state = result.ok ? "done" : "failed";
    g_progress.error = result.error;
    g_progress.version = result.version;
    g_running = false;
  }
  vTaskDelete(nullptr);
}
}  // namespace

namespace {
void finish_check(const CheckResult& found) {
  std::lock_guard<std::mutex> guard(g_lock);
  g_check.result = found;
  g_check.state = found.ok ? "done" : "failed";
  g_checking = false;
}

// The look for the newest release reads from GitHub over TLS and writes nothing to the flash, so its stack (12 KB, the look is made with JSON and the TLS client on it) can be in the
// external RAM. A task that ends would leave that stack behind, so one worker waits for the next look for good; where there is no external RAM the old way is kept (a task made for the look
// and gone after it), which holds the 12 KB of internal memory only while it runs.
const char* g_check_board = nullptr;
TaskHandle_t g_check_worker = nullptr;
bool g_check_worker_tried = false;

void check_worker(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    finish_check(check(g_check_board));
  }
}

void check_task(void* raw) {
  finish_check(check(static_cast<const char*>(raw)));
  vTaskDelete(nullptr);
}
}  // namespace

bool start_check(const char* board_id) {
  {
    std::lock_guard<std::mutex> guard(g_lock);
    if (g_checking || g_running) return false;
    g_checking = true;
    g_check = CheckProgress{};
    g_check.state = "checking";
  }
  if (!g_check_worker_tried) {   // only the one caller that got g_checking above gets here at a time
    g_check_worker_tried = true;
    memory::start_task(check_worker, "github-check", 12288, nullptr, 4, &g_check_worker, false);
  }
  if (g_check_worker != nullptr) {
    g_check_board = board_id;
    xTaskNotifyGive(g_check_worker);
    return true;
  }
  if (xTaskCreate(check_task, "github-check", 12288, const_cast<char*>(board_id), 4, nullptr) != pdPASS) {
    std::lock_guard<std::mutex> guard(g_lock);
    g_checking = false;
    g_check.state = "failed";
    g_check.result.error = "no_task";
  }
  return true;
}

CheckProgress check_progress() {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_check;
}

bool start_latest(const char* board_id) {
  {
    std::lock_guard<std::mutex> guard(g_lock);
    if (g_running || g_checking) return false;
    g_running = true;
    g_progress = Progress{};
    g_progress.state = "downloading";
  }
  Job* job = new Job{board_id};
  if (xTaskCreate(install_task, "github-ota", 12288, job, 4, nullptr) != pdPASS) {
    delete job;
    std::lock_guard<std::mutex> guard(g_lock);
    g_running = false;
    g_progress.state = "failed";
    g_progress.error = "no_task";
  }
  return true;
}

Progress progress() {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_progress;
}

unsigned slot_generation() { return g_generation.load(); }

bool begin_manual_write() {
  if (g_installing.exchange(true)) return false;
  ++g_generation;
  return true;
}
void end_manual_write() { ++g_generation; g_installing = false; }

namespace {
void finish_copy(const char* error, const std::string& version) {
  std::lock_guard<std::mutex> guard(g_lock);
  g_copy.state = error == nullptr ? "done" : "failed";
  g_copy.error = error == nullptr ? "" : error;
  g_copy.version = version;
  g_running = false;
}

void copy_task(void*) {
  const esp_partition_t* from = esp_ota_get_running_partition();
  const esp_partition_t* to = esp_ota_get_next_update_partition(nullptr);
  if (from == nullptr || to == nullptr || from == to) { finish_copy("no_slot", ""); vTaskDelete(nullptr); return; }
  // How long the image is: the running slot is much bigger than the firmware in it, and only the firmware is copied.
  const esp_partition_pos_t position{from->address, from->size};
  esp_image_metadata_t metadata{};
  if (esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &position, &metadata) != ESP_OK || metadata.image_len == 0) { finish_copy("source_invalid", ""); vTaskDelete(nullptr); return; }
  const std::size_t total = metadata.image_len;
  { std::lock_guard<std::mutex> guard(g_lock); g_copy.total = total; g_copy.done = 0; }
  esp_ota_handle_t handle = 0;
  ++g_generation;
  if (esp_ota_begin(to, total, &handle) != ESP_OK) { finish_copy("ota_begin", ""); vTaskDelete(nullptr); return; }
  constexpr std::size_t kChunk = 4096;
  std::unique_ptr<std::uint8_t[]> buffer(new (std::nothrow) std::uint8_t[kChunk]);
  if (!buffer) { esp_ota_abort(handle); finish_copy("no_memory", ""); vTaskDelete(nullptr); return; }
  for (std::size_t offset = 0; offset < total;) {
    const std::size_t n = total - offset < kChunk ? total - offset : kChunk;
    if (esp_partition_read(from, offset, buffer.get(), n) != ESP_OK) { esp_ota_abort(handle); finish_copy("read_failed", ""); vTaskDelete(nullptr); return; }
    if (esp_ota_write(handle, buffer.get(), n) != ESP_OK) { esp_ota_abort(handle); finish_copy("ota_write", ""); vTaskDelete(nullptr); return; }
    offset += n;
    { std::lock_guard<std::mutex> guard(g_lock); g_copy.done = offset; }
    if ((offset / kChunk) % 16 == 0) vTaskDelay(1);   // the other tasks of the node go on while the flash is written
  }
  if (esp_ota_end(handle) != ESP_OK) { finish_copy("image_invalid", ""); vTaskDelete(nullptr); return; }
  ++g_generation;
  esp_app_desc_t description{};
  const bool known = esp_ota_get_partition_description(to, &description) == ESP_OK;
  ESP_LOGW(kTag, "the firmware of %s was copied to %s (%u bytes)", from->label, to->label, static_cast<unsigned>(total));
  finish_copy(nullptr, known ? description.version : "");
  vTaskDelete(nullptr);
}
}  // namespace

bool start_copy() {
  {
    std::lock_guard<std::mutex> guard(g_lock);
    if (g_running) return false;
    g_running = true;
    g_copy = CopyProgress{};
    g_copy.state = "copying";
  }
  if (xTaskCreate(copy_task, "slot-copy", 6144, nullptr, 4, nullptr) != pdPASS) finish_copy("no_task", "");
  return true;
}

CopyProgress copy_progress() {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_copy;
}

}  // namespace armor::github_update
