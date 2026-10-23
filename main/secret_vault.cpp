// ARMOR-NODE-ELECTRICAL - the key that seals the secrets the node keeps in flash. What is sealed and how is core/secret_seal.hpp (tested on a computer); this file holds the key in its partition.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include "secret_vault.hpp"

#include <cstring>
#include <mutex>
extern "C" {
#include "esp_log.h"
#include "esp_partition.h"
}
#include "core/secret_seal.hpp"
#include "entropy.hpp"

namespace armor::vault {
namespace {
constexpr char kTag[] = "armor-vault";
std::mutex g_lock;
seal::Key g_key{};
bool g_have_key = false;
bool g_own = false;
bool g_server = false;

const esp_partition_t* key_partition() {
  static const esp_partition_t* found = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, nullptr);
  return found;
}

// The stored values that are secrets, and whether the value is one document of which only the secret members are sealed ("config") or sealed whole.
enum class Kind { kNone, kWhole, kMembers };
Kind kind_of(const char* name) {
  if (std::strcmp(name, "config") == 0) return Kind::kMembers;
  for (const char* whole : {"users", "setup_code", "config_snap", "tls_key"}) if (std::strcmp(name, whole) == 0) return Kind::kWhole;
  return Kind::kNone;
}

void fresh_nonce(std::uint8_t nonce[seal::kNonceBytes]) { random_bytes(nonce, seal::kNonceBytes); }
}  // namespace

void load() {
  std::lock_guard<std::mutex> guard(g_lock);
  g_have_key = false;
  const esp_partition_t* part = key_partition();
  if (part == nullptr) return;
  std::uint8_t record[seal::kRecordBytes];
  if (esp_partition_read(part, 0, record, sizeof record) != ESP_OK) { ESP_LOGW(kTag, "the key partition could not be read"); return; }
  g_have_key = seal::parse_key_record(record, sizeof record, g_key);
  if (g_have_key) ESP_LOGI(kTag, "a key for the sealed secrets is held");
}

bool partition_exists() { return key_partition() != nullptr; }
bool have_key() { std::lock_guard<std::mutex> guard(g_lock); return g_have_key; }
void set_own(bool on) { std::lock_guard<std::mutex> guard(g_lock); g_own = on; }
void set_server_request(bool on) { std::lock_guard<std::mutex> guard(g_lock); g_server = on; }
bool server_request() { std::lock_guard<std::mutex> guard(g_lock); return g_server; }
bool wanted() { std::lock_guard<std::mutex> guard(g_lock); return seal::wanted(g_own, g_server); }
bool active() { std::lock_guard<std::mutex> guard(g_lock); return seal::wanted(g_own, g_server) && g_have_key; }

bool create_key() {
  std::lock_guard<std::mutex> guard(g_lock);
  if (g_have_key) return true;
  const esp_partition_t* part = key_partition();
  if (part == nullptr) { ESP_LOGW(kTag, "this node has no key partition (it was installed with an older partition table): the secrets stay as they are"); return false; }
  seal::Key key;
  random_bytes(key.data(), key.size());
  const auto record = seal::key_record(key);
  if (esp_partition_erase_range(part, 0, part->size) != ESP_OK || esp_partition_write(part, 0, record.data(), record.size()) != ESP_OK) { ESP_LOGE(kTag, "the key could not be written"); return false; }
  std::uint8_t back[seal::kRecordBytes];
  seal::Key read{};
  if (esp_partition_read(part, 0, back, sizeof back) != ESP_OK || !seal::parse_key_record(back, sizeof back, read) || read != key) { ESP_LOGE(kTag, "the key read back is not the one written"); return false; }
  g_key = key;
  g_have_key = true;
  ESP_LOGI(kTag, "a key for the sealed secrets was made");
  return true;
}

bool drop_key() {
  std::lock_guard<std::mutex> guard(g_lock);
  const esp_partition_t* part = key_partition();
  if (part == nullptr) return false;
  const bool erased = esp_partition_erase_range(part, 0, part->size) == ESP_OK;
  if (erased) { g_key.fill(0); g_have_key = false; ESP_LOGI(kTag, "the key was erased: nothing is sealed any more"); }
  return erased;
}

bool is_secret_value(const char* name) { return kind_of(name) != Kind::kNone; }

bool stored_sealed(const char* name, const std::string& stored) {
  switch (kind_of(name)) {
    case Kind::kWhole: return seal::is_sealed(stored);
    case Kind::kMembers: return seal::json_has_sealed(stored);
    default: return false;
  }
}

std::string reveal(const char* name, const std::string& stored, bool& ok) {
  ok = true;
  const Kind kind = kind_of(name);
  if (kind == Kind::kNone || !stored_sealed(name, stored)) return stored;
  std::lock_guard<std::mutex> guard(g_lock);
  std::string out;
  seal::Open result = seal::Open::kBad;
  if (g_have_key) result = kind == Kind::kWhole ? seal::open_value(g_key, stored, name, out) : seal::open_json(g_key, stored, out);
  if (result == seal::Open::kBad) {
    ok = false;
    ESP_LOGE(kTag, "\"%s\" is sealed and cannot be opened (%s)", name, g_have_key ? "not this key" : "no key");
    return std::string();
  }
  return out;
}

std::string conceal(const char* name, const std::string& plain, bool seal_it) {
  const Kind kind = kind_of(name);
  if (!seal_it || kind == Kind::kNone) return plain;
  std::lock_guard<std::mutex> guard(g_lock);
  if (!g_have_key) return plain;
  if (kind == Kind::kMembers) return seal::seal_json(g_key, plain, fresh_nonce);
  std::uint8_t nonce[seal::kNonceBytes];
  fresh_nonce(nonce);
  return seal::seal_value(g_key, nonce, name, plain);
}

}  // namespace armor::vault
