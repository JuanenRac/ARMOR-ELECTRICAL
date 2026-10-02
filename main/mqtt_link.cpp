// ARMOR-ELECTRICAL - the node's link to the broker: the clock and the messages the node publishes.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The node publishes what it reads as it is read (armor/<family>/<node>/..., JSON, QoS 0, not retained: the server keeps the latest
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

// Which saved broker the node is trying (0: the one above, 1..: settings.mqtt.backup[index-1]). A watchdog task switches to the
// next one, the same way network.cpp does for Wi-Fi, after the connection has stayed down for a while; it never touches a
// broker that is still working.
int g_broker_index = 0;

config::Broker current_broker(const config::Settings& s, int index) {
  if (index <= 0 || static_cast<std::size_t>(index) > s.mqtt.backup.size()) return {s.mqtt.uri, s.mqtt.username, s.mqtt.password};
  return s.mqtt.backup[static_cast<std::size_t>(index) - 1];
}
int broker_count(const config::Settings& s) { return 1 + static_cast<int>(s.mqtt.backup.size()); }

void on_mqtt(void*, esp_event_base_t, int32_t event_id, void*) {
  switch (event_id) {
    case MQTT_EVENT_CONNECTED: g_connected = true; ESP_LOGI(kTag, "MQTT connected to %s", current_broker(g_settings, g_broker_index).uri.c_str()); break;
    case MQTT_EVENT_DISCONNECTED: g_connected = false; ESP_LOGW(kTag, "MQTT disconnected"); break;
    case MQTT_EVENT_ERROR: ESP_LOGW(kTag, "MQTT error (the broker refused the identity, or is not reachable)"); break;
    default: break;
  }
}

// Builds and starts the MQTT client for one broker (the one above, or a backup); used both at start-up and whenever the
// watchdog switches to the next saved broker. The client this replaces, if any, must already be stopped and destroyed.
void start_client(const config::Broker& broker) {
  esp_mqtt_client_config_t config{};
  config.broker.address.uri = broker.uri.c_str();
  config.credentials.client_id = g_settings.node_id.c_str();
  config.credentials.username = broker.username.c_str();
  config.credentials.authentication.password = broker.password.c_str();
  config.session.keepalive = g_settings.mqtt.heartbeat_s * 2;
#ifdef ARMOR_MQTT_HAS_CA  // certs/ca.pem exists: main/CMakeLists.txt embeds it
  extern const char ca_pem_start[] asm("_binary_ca_pem_start");
  config.broker.verification.certificate = ca_pem_start;
#endif
  g_client = esp_mqtt_client_init(&config);
  ESP_ERROR_CHECK(esp_mqtt_client_register_event(g_client, MQTT_EVENT_ANY, on_mqtt, nullptr));
  ESP_ERROR_CHECK(esp_mqtt_client_start(g_client));
}

constexpr int kBrokerCheckEverySeconds = 10;
constexpr int kBrokerBadRoundsBeforeSwitch = 3;   // about 30 s disconnected before trying the next saved broker

// The same idea as network.cpp's Wi-Fi link watchdog, for the broker: tried only when the connection has stayed down for a
// while, and only when another saved broker exists - never while the one above still works.
void broker_watchdog_task(void*) {
  int bad_rounds = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(kBrokerCheckEverySeconds * 1000));
    if (g_connected) { bad_rounds = 0; continue; }
    const int total = broker_count(g_settings);
    if (total <= 1) continue;
    bad_rounds += 1;
    if (bad_rounds < kBrokerBadRoundsBeforeSwitch) continue;
    bad_rounds = 0;
    g_broker_index = (g_broker_index + 1) % total;
    const config::Broker broker = current_broker(g_settings, g_broker_index);
    ESP_LOGW(kTag, "the broker has not answered in a while: trying the next saved one (%s)", broker.uri.c_str());
    if (g_client != nullptr) { esp_mqtt_client_stop(g_client); esp_mqtt_client_destroy(g_client); g_client = nullptr; }
    start_client(broker);
  }
}

// Waits for an address, then starts the clock and the broker connection once. The ports keep reading meanwhile.
void link_task(void*) {
  while (!network::has_ip()) vTaskDelay(pdMS_TO_TICKS(500));
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, g_settings.mqtt.ntp.c_str());
  esp_sntp_init();

  start_client(current_broker(g_settings, g_broker_index));
  xTaskCreate(broker_watchdog_task, "mqtt-link-wd", 4096, nullptr, 2, nullptr);
  vTaskDelete(nullptr);
}
}  // namespace

void start(const config::Settings& settings) {
  g_settings = settings;
  g_broker_index = 0;
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
