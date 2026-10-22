// ARMOR-ELECTRICAL - what the node keeps in flash: its settings and its panel users.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include "node_store.hpp"

#include <cctype>
#include <cstring>
#include <mutex>
extern "C" {
#include "esp_log.h"
#include "esp_mac.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
}
#include "core/config_snapshot.hpp"
#include "core/node_guard.hpp"
#include "entropy.hpp"

namespace armor::store {
namespace {
constexpr char kTag[] = "armor-store";
constexpr char kNamespace[] = "armor";
constexpr char kSettingsKey[] = "config";
constexpr char kUsersKey[] = "users";
constexpr char kSetupCodeKey[] = "setup_code";
constexpr char kSnapshotKey[] = "config_snap";   // the copy of the settings made before a firmware is installed (an NVS key is at most 15 characters)
constexpr unsigned kHashIterations = 10000;

std::mutex g_lock;
config::Settings g_settings;
bool g_stored = false;
auth::UserStore g_users;
std::string g_setup_code;
std::uint8_t g_mac[6]{};
// What the node expects to find in flash under the settings key: the check number of the text it read at the start or wrote since. Moved by every save (under g_lock).
bool g_expecting = false;
std::uint32_t g_expected_hash = 0;
bootreport::ConfigCheck g_check;   // what the checks of this start found, for the health report

// PBKDF2-HMAC-SHA256, 10 000 rounds, 32 bytes, written as hexadecimal. The salt (text) is used as it is.
std::string hash_password(std::string_view password, std::string_view salt) {
  std::uint8_t out[32];
  const int rc = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, reinterpret_cast<const unsigned char*>(password.data()), password.size(),
                                              reinterpret_cast<const unsigned char*>(salt.data()), salt.size(), kHashIterations, sizeof out, out);
  if (rc != 0) return "";
  return auth::to_hex(out, sizeof out);
}

std::string new_salt() {
  std::uint8_t bytes[16];
  random_bytes(bytes, sizeof bytes);
  return auth::to_hex(bytes, sizeof bytes);
}

bool read_blob(const char* key, std::string& out) {
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
  std::size_t size = 0;
  bool ok = false;
  if (nvs_get_blob(handle, key, nullptr, &size) == ESP_OK && size > 0 && size <= 32 * 1024) {
    out.resize(size);
    ok = nvs_get_blob(handle, key, out.data(), &size) == ESP_OK;
  }
  nvs_close(handle);
  return ok;
}

bool write_blob(const char* key, const std::string& value) {
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
  bool ok = nvs_set_blob(handle, key, value.data(), value.size()) == ESP_OK && nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}

// The build's values as the first settings.
config::Settings first_settings() {
  config::Settings s = config::default_settings(mac_tail());
  if (std::strlen(CONFIG_ARMOR_NODE_ID) > 0 && node_id_is_valid(CONFIG_ARMOR_NODE_ID)) { s.node_id = CONFIG_ARMOR_NODE_ID; s.node_name = CONFIG_ARMOR_NODE_ID; }
  if (std::strlen(CONFIG_ARMOR_MQTT_URI) > 0) {
    s.mqtt.enabled = true;
    s.mqtt.uri = CONFIG_ARMOR_MQTT_URI;
    s.mqtt.username = CONFIG_ARMOR_MQTT_USERNAME;
    s.mqtt.password = CONFIG_ARMOR_MQTT_PASSWORD;
  }
  s.time.ntp = CONFIG_ARMOR_NTP_SERVER;
  s.mqtt.heartbeat_s = CONFIG_ARMOR_HEARTBEAT_S;
  return s;
}

// The stored settings are compared with what the node expects (the text it read, or the one it wrote since) and, when they differ, written once more from the settings in memory. The outcome
// goes to the health report (config_check.reverted). Runs once, in the task below.
void verify_settings() {
  std::lock_guard<std::mutex> guard(g_lock);
  std::string stored;
  const bool readable = read_blob(kSettingsKey, stored);
  const snapshot::Recheck found = snapshot::recheck(g_expecting, g_expected_hash, readable, snapshot::fnv1a(stored));
  if (found == snapshot::Recheck::kFine) return;
  ESP_LOGE(kTag, "the stored settings are not what the node read or wrote: writing them again once");
  const std::string again = config::to_json(g_settings, true);
  std::string back;
  const bool confirmed = write_blob(kSettingsKey, again) && read_blob(kSettingsKey, back) && back == again;
  if (confirmed) g_expected_hash = snapshot::fnv1a(again);
  g_check.reverted = snapshot::reverted_outcome(found, confirmed);
  if (confirmed) ESP_LOGW(kTag, "the settings were written again and read back");
  else ESP_LOGE(kTag, "the settings could not be written again");
}

void settings_check_task(void*) {
  vTaskDelay(pdMS_TO_TICKS(nodeguard::kSettingsCheckDelayS * 1000));
  verify_settings();
  vTaskDelete(nullptr);
}
}  // namespace

void init() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(kTag, "the NVS partition is unusable (%s): erasing it", esp_err_to_name(err));
    const esp_err_t erased = nvs_flash_erase();
    if (erased != ESP_OK) ESP_LOGE(kTag, "the NVS partition could not be erased: %s", esp_err_to_name(erased));
    err = nvs_flash_init();
  }
  // Without storage the node still starts, on the default settings and without remembering anything: it can be reached and set up again, which a node that aborts in a loop cannot.
  if (err != ESP_OK) ESP_LOGE(kTag, "the settings storage could not be started (%s): running on the defaults, nothing will be kept", esp_err_to_name(err));
  esp_read_mac(g_mac, ESP_MAC_WIFI_STA);

  std::lock_guard<std::mutex> guard(g_lock);
  std::string text;
  g_settings = first_settings();
  const bool have_text = read_blob(kSettingsKey, text);
  config::Settings loaded;   // task-stack-ok: start-up code in the main task (16 KB stack)
  config::Problems dropped;
  // Settings written by another version of the firmware are read as far as they can be: members this version does not know are ignored, and one that it cannot accept is left at its first value
  // (the rest is kept), so an update never sends the node back to its first settings.
  const bool loaded_ok = have_text && config::load_stored(text, first_settings(), loaded, dropped);
  // The copy made before the last install (core/config_snapshot.hpp): used when the stored text cannot be read at all, or when the copy is read with fewer fields dropped.
  std::string copy_blob;
  snapshot::Parsed copy;
  config::Settings copy_settings;   // task-stack-ok: start-up code in the main task (16 KB stack)
  config::Problems copy_dropped;
  const bool have_copy = read_blob(kSnapshotKey, copy_blob) && snapshot::parse(copy_blob, copy);
  const bool copy_ok = have_copy && config::load_stored(copy.settings, first_settings(), copy_settings, copy_dropped);
  if (snapshot::pick_source(loaded_ok, dropped.size(), copy_ok, copy_dropped.size()) == snapshot::Pick::kSnapshot) {
    g_settings = copy_settings;
    g_stored = true;
    const bool written = write_blob(kSettingsKey, copy.settings);
    if (written) text = copy.settings;
    g_check.snapshot = snapshot::snapshot_outcome(written, !written);
    ESP_LOGW(kTag, "the stored settings %s: the copy made before the last update (firmware %s) %s", have_text ? "were damaged or lost fields" : "were missing", copy.firmware.c_str(), written ? "was put back" : "is used, but could not be written back");
  } else if (loaded_ok) {
    g_settings = loaded;
    g_stored = true;
    for (const config::Problem& problem : dropped) ESP_LOGW(kTag, "stored setting %s left at its first value (%s)", problem.path.c_str(), problem.code.c_str());
  } else if (have_text) {
    ESP_LOGE(kTag, "the stored settings could not be read: using the first settings");
    for (const config::Problem& problem : dropped) ESP_LOGE(kTag, "  %s at %s", problem.code.c_str(), problem.path.c_str());
    if (have_copy) g_check.snapshot = snapshot::snapshot_outcome(false, true);   // there was a copy and it did not help
  }
  g_expecting = g_stored;
  g_expected_hash = snapshot::fnv1a(text);
  if (read_blob(kUsersKey, text) && !g_users.from_json(text)) ESP_LOGE(kTag, "the stored users could not be read: the node is back in setup");
  // The board's code: first from the fleet secret (the owner can always work it out again), then the one built into the image, and last a
  // random one that is kept in flash so it is the same at every start. It only ever opens a node with no administrator yet (the panel and
  // Bluetooth both refuse it once there is one) and, afterwards, the node's rescue network (network.cpp), which needs a password that still
  // exists and that the owner can learn - the old way erased it the moment the administrator was created, and the rescue network
  // could then not start.
  const std::size_t secret_length = std::strlen(CONFIG_ARMOR_FLEET_SECRET);
  if (secret_length >= 16) {
    // HMAC-SHA256 of the MAC (twelve lowercase hexadecimal digits) with the fleet secret, ten symbols of it.
    const std::string mac_text = auth::to_hex(g_mac, sizeof g_mac);
    std::uint8_t digest[32];
    if (mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), reinterpret_cast<const unsigned char*>(CONFIG_ARMOR_FLEET_SECRET), secret_length,
                        reinterpret_cast<const unsigned char*>(mac_text.data()), mac_text.size(), digest) == 0) g_setup_code = auth::setup_code_from(digest, 10);
  }
  if (!g_setup_code.empty()) { /* derived from the fleet secret */ }
  else if (std::strlen(CONFIG_ARMOR_SETUP_CODE) >= 8) {
    g_setup_code = CONFIG_ARMOR_SETUP_CODE;
    for (char& c : g_setup_code) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));   // the panel's form writes the code in capitals, so a code typed in lower case here would never match
  }
  else if (read_blob(kSetupCodeKey, text) && text.size() >= 8) g_setup_code = text;
  else {
    std::uint8_t bytes[8];
    random_bytes(bytes, sizeof bytes);
    g_setup_code = auth::setup_code_from(bytes, sizeof bytes);
    write_blob(kSetupCodeKey, g_setup_code);
  }
  // Fifteen seconds after the start the stored text is read again (a task of its own, with its stack in the internal memory: it touches the flash, and it ends when it is done).
  xTaskCreate(settings_check_task, "cfg-check", 4096, nullptr, 1, nullptr);
}

std::string mac_tail() { return auth::to_hex(g_mac + 3, 3); }
unsigned mac_sum() { unsigned sum = 0; for (std::uint8_t b : g_mac) sum += b; return sum; }

config::Settings settings() {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_settings;
}
bool settings_are_stored() {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_stored;
}

std::string settings_json_full() {
  std::lock_guard<std::mutex> guard(g_lock);
  return config::to_json(g_settings, true);
}

bool save_settings(const config::Settings& s) {
  std::lock_guard<std::mutex> guard(g_lock);
  const std::string text = config::to_json(s, true);
  if (!write_blob(kSettingsKey, text)) return false;
  g_settings = s;
  g_stored = true;
  g_expecting = true;
  g_expected_hash = snapshot::fnv1a(text);
  return true;
}

bool snapshot_settings(const char* firmware) {
  std::string text;
  if (!read_blob(kSettingsKey, text)) return false;
  const std::string blob = snapshot::make(firmware != nullptr ? firmware : "", text);
  if (blob.empty()) return false;   // nothing to copy, or too big to keep next to the users and the certificate
  if (!write_blob(kSnapshotKey, blob)) { ESP_LOGW(kTag, "the settings could not be copied before the update"); return false; }
  ESP_LOGI(kTag, "the settings were copied before the update (%u bytes)", static_cast<unsigned>(text.size()));
  return true;
}

bootreport::ConfigCheck config_check() {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_check;
}

bool blob_read(const char* key, std::string& out) { return read_blob(key, out); }
bool blob_write(const char* key, const std::string& value) { return write_blob(key, value); }

bool users_empty() {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_users.empty();
}

std::string users_json() {
  std::lock_guard<std::mutex> guard(g_lock);
  json::Writer w;
  w.begin_array();
  for (const auth::User& user : g_users.users()) w.begin_object().field("name", user.name).field("role", auth::to_text(user.role)).end_object();
  w.end_array();
  return w.str();
}

namespace {
bool persist_users() { return write_blob(kUsersKey, g_users.to_json()); }
}  // namespace

// A change of the users is made on the list, written to flash, and kept only if the write worked: when it did not, the list is what it was (the change used to stay in memory, and the
// first administrator of a node that could not write would vanish at the next restart without anybody having been told).
auth::Result user_add(std::string_view name, std::string_view password, auth::Role role) {
  std::lock_guard<std::mutex> guard(g_lock);
  const auth::UserStore before = g_users;
  const auth::Result result = g_users.add(name, password, role, new_salt(), hash_password);
  if (result == auth::Result::kOk && !persist_users()) { g_users = before; return auth::Result::kStorage; }
  return result;
}

bool user_verify(std::string_view name, std::string_view password, auth::Role& role) {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_users.verify(name, password, hash_password, role);
}

auth::Result user_set_password(std::string_view name, std::string_view password) {
  std::lock_guard<std::mutex> guard(g_lock);
  const auth::UserStore before = g_users;
  const auth::Result result = g_users.set_password(name, password, new_salt(), hash_password);
  if (result == auth::Result::kOk && !persist_users()) { g_users = before; return auth::Result::kStorage; }
  return result;
}

auth::Result user_set_role(std::string_view name, auth::Role role) {
  std::lock_guard<std::mutex> guard(g_lock);
  const auth::UserStore before = g_users;
  const auth::Result result = g_users.set_role(name, role);
  if (result == auth::Result::kOk && !persist_users()) { g_users = before; return auth::Result::kStorage; }
  return result;
}

auth::Result user_remove(std::string_view name) {
  std::lock_guard<std::mutex> guard(g_lock);
  const auth::UserStore before = g_users;
  const auth::Result result = g_users.remove(name);
  if (result == auth::Result::kOk && !persist_users()) { g_users = before; return auth::Result::kStorage; }
  return result;
}

bool factory_reset() {
  std::lock_guard<std::mutex> guard(g_lock);
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
  const bool ok = nvs_erase_all(handle) == ESP_OK && nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}

const std::string& setup_code() { return g_setup_code; }

}  // namespace armor::store
