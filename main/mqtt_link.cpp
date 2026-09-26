// ARMOR-ELECTRICAL - the node's link to the broker: the clock and the electrical message of the node.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The node publishes what its meters measure every round (armor/electrical/<node>/state, JSON, QoS 0, not retained: the server keeps the latest
// reading and marks a device stale when it stops). It sends nothing until the network, the clock and the broker are all there.
#include "mqtt_link.hpp"

#include <atomic>
#include <ctime>
extern "C" {
#include <sys/time.h>
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
}
#include "network.hpp"

namespace armor::mqtt_link {
namespace {
constexpr char kTag[] = "armor-mqtt";

config::Settings g_settings;
esp_mqtt_client_handle_t g_client = nullptr;
std::atomic<bool> g_connected{false};
std::atomic<bool> g_enabled{false};
std::atomic<std::uint32_t> g_published{0};
std::atomic<std::uint32_t> g_dropped{0};

void on_mqtt(void*, esp_event_base_t, int32_t event_id, void*) {
  switch (event_id) {
    case MQTT_EVENT_CONNECTED: g_connected = true; ESP_LOGI(kTag, "MQTT connected to %s", g_settings.mqtt.uri.c_str()); break;
    case MQTT_EVENT_DISCONNECTED: g_connected = false; ESP_LOGW(kTag, "MQTT disconnected"); break;
    case MQTT_EVENT_ERROR: ESP_LOGW(kTag, "MQTT error (the broker refused the identity, or is not reachable)"); break;
    default: break;
  }
}

// Waits for an address, then starts the clock and the broker connection once. The ports keep reading meanwhile.
void link_task(void*) {
  while (!network::has_ip()) vTaskDelay(pdMS_TO_TICKS(500));
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, g_settings.mqtt.ntp.c_str());
  esp_sntp_init();

  esp_mqtt_client_config_t config{};
  config.broker.address.uri = g_settings.mqtt.uri.c_str();
  config.credentials.client_id = g_settings.node_id.c_str();
  config.credentials.username = g_settings.mqtt.username.c_str();
  config.credentials.authentication.password = g_settings.mqtt.password.c_str();
  config.session.keepalive = g_settings.mqtt.heartbeat_s * 2;
#ifdef ARMOR_MQTT_HAS_CA  // certs/ca.pem exists: main/CMakeLists.txt embeds it
  extern const char ca_pem_start[] asm("_binary_ca_pem_start");
  config.broker.verification.certificate = ca_pem_start;
#endif
  g_client = esp_mqtt_client_init(&config);
  ESP_ERROR_CHECK(esp_mqtt_client_register_event(g_client, MQTT_EVENT_ANY, on_mqtt, nullptr));
  ESP_ERROR_CHECK(esp_mqtt_client_start(g_client));
  vTaskDelete(nullptr);
}
}  // namespace

void start(const config::Settings& settings) {
  g_settings = settings;
  if (!settings.mqtt.enabled || settings.mqtt.uri.empty()) {
    ESP_LOGW(kTag, "no broker is set up: the node serves its panel and reads its ports, and sends nothing");
    return;
  }
  g_enabled = true;
  xTaskCreate(link_task, "mqtt-link", 6144, nullptr, 4, nullptr);
}

bool connected() { return g_connected; }
bool clock_is_set() { return std::time(nullptr) > 1700000000; }

std::uint64_t wall_clock_ms() {
  timeval tv{};
  gettimeofday(&tv, nullptr);
  return static_cast<std::uint64_t>(tv.tv_sec) * 1000ULL + static_cast<std::uint64_t>(tv.tv_usec) / 1000ULL;
}

void publish(const std::string& topic, const std::string& payload) {
  if (!g_enabled) return;
  if (!g_connected || g_client == nullptr || !clock_is_set()) { ++g_dropped; return; }
  if (esp_mqtt_client_publish(g_client, topic.c_str(), payload.c_str(), static_cast<int>(payload.size()), 0, 0) >= 0) ++g_published;
  else ++g_dropped;
}

Status status() {
  Status s;
  s.enabled = g_enabled;
  s.connected = g_connected;
  s.clock_set = clock_is_set();
  s.published = g_published;
  s.dropped = g_dropped;
  return s;
}

}  // namespace armor::mqtt_link
