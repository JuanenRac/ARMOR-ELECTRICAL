// ARMOR-ELECTRICAL - the node's link to the broker: the clock and the messages the node publishes.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The node publishes what it reads as it is read (armor/<family>/<node>/..., JSON, QoS 0, not retained: the server keeps the latest
// reading and marks a device stale when it stops). It sends nothing until the network, the clock and the broker are all there.
#include "mqtt_link.hpp"

#include <atomic>
#include <deque>
#include <mutex>
#include <ctime>
#include <vector>
extern "C" {
#include <sys/time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_random.h"
#include "mqtt_client.h"
}
#include "core/backoff.hpp"
#include "core/mqtt_topic.hpp"
#include "network.hpp"

namespace armor::mqtt_link {
namespace {
constexpr char kTag[] = "armor-mqtt";

config::Settings g_settings;
esp_mqtt_client_handle_t g_client = nullptr;
// The client is replaced when the node moves to another saved broker, while other tasks publish: every use of it, and its replacement, go through this lock. The old client is stopped
// AFTER it has been taken out under the lock, never inside it: stopping waits for the client's own task, which may be waiting for this lock in one of its callbacks.
std::mutex g_client_lock;
std::atomic<bool> g_connected{false};
retry::Backoff g_mqtt_backoff(1000, 60000);        // the wait before the broker is tried again grows from 1 s to 60 s (with a little scatter)
esp_timer_handle_t g_retry_timer = nullptr;
std::atomic<bool> g_retry_pending{false};
std::atomic<bool> g_enabled{false};
std::atomic<std::uint32_t> g_published{0};
std::atomic<std::uint32_t> g_dropped{0};

struct Subscription {
  std::string filter;
  MessageHandler on_message;
  ConnectedHandler on_connected;
};
std::vector<Subscription> g_subscriptions;   // filled before start(), read-only afterwards

// Which saved broker the node is trying (0: the one above, 1..: settings.mqtt.backup[index-1]). A watchdog task switches to the
// next one, the same way network.cpp does for Wi-Fi, after the connection has stayed down for a while; it never touches a
// broker that is still working.
int g_broker_index = 0;

// Hands one message to the client, if there is one; a negative number says it was not accepted.
int client_publish(const char* topic, const char* data, int length, int qos) {
  std::lock_guard<std::mutex> guard(g_client_lock);
  return g_client == nullptr ? -1 : esp_mqtt_client_publish(g_client, topic, data, length, qos, 0);
}

// publish() never waits for the client: the client holds its own lock while it runs the handlers of the incoming messages, and such a handler may be waiting for a lock of its module
// while the task that holds that lock publishes - each waiting for the other, for ever, with the watchdog of the idle tasks seeing nothing. A message goes into a short queue, in order,
// and a task of its own hands it to the client without holding anything. When the queue is full the oldest message is dropped (the newest state is the one that matters).
struct Outgoing { std::string topic; std::string payload; };
std::mutex g_out_lock;
std::deque<Outgoing> g_out;
SemaphoreHandle_t g_out_ready = nullptr;
constexpr std::size_t kOutMax = 48;

void outgoing_task(void*) {
  for (;;) {
    xSemaphoreTake(g_out_ready, pdMS_TO_TICKS(1000));
    for (;;) {
      Outgoing next;
      {
        std::lock_guard<std::mutex> guard(g_out_lock);
        if (g_out.empty()) break;
        next = std::move(g_out.front());
        g_out.pop_front();
      }
      if (g_connected && client_publish(next.topic.c_str(), next.payload.c_str(), static_cast<int>(next.payload.size()), 0) >= 0) ++g_published;
      else ++g_dropped;
    }
  }
}


config::Broker current_broker(const config::Settings& s, int index) {
  if (index <= 0 || static_cast<std::size_t>(index) > s.mqtt.backup.size()) return {s.mqtt.uri, s.mqtt.username, s.mqtt.password};
  return s.mqtt.backup[static_cast<std::size_t>(index) - 1];
}
int broker_count(const config::Settings& s) { return 1 + static_cast<int>(s.mqtt.backup.size()); }

// The client does not reconnect by itself (see start_client): after a failure it is asked to, after a wait that grows with every failure in a row. One timer, one try at a time.
void retry_now(void*) {
  g_retry_pending = false;
  esp_mqtt_client_handle_t client = nullptr;
  { std::lock_guard<std::mutex> guard(g_client_lock); client = g_client; }
  if (client != nullptr && !g_connected) esp_mqtt_client_reconnect(client);
}

void schedule_retry() {
  if (g_retry_timer == nullptr || g_retry_pending.exchange(true)) return;
  const std::uint32_t wait_ms = g_mqtt_backoff.next(esp_random());
  ESP_LOGW(kTag, "the broker will be tried again in %.1f s", wait_ms / 1000.0);
  esp_timer_start_once(g_retry_timer, static_cast<std::uint64_t>(wait_ms) * 1000ULL);
}

void on_mqtt(void*, esp_event_base_t, int32_t event_id, void* data) {
  auto* event = static_cast<esp_mqtt_event_handle_t>(data);
  switch (event_id) {
    case MQTT_EVENT_CONNECTED:
      g_connected = true;
      g_mqtt_backoff.reset();
      ESP_LOGI(kTag, "MQTT connected to %s", current_broker(g_settings, g_broker_index).uri.c_str());
      for (const Subscription& subscription : g_subscriptions) esp_mqtt_client_subscribe(event->client, subscription.filter.c_str(), 1);
      for (const Subscription& subscription : g_subscriptions) if (subscription.on_connected) subscription.on_connected();
      break;
    case MQTT_EVENT_DISCONNECTED: g_connected = false; ESP_LOGW(kTag, "MQTT disconnected"); schedule_retry(); break;
    case MQTT_EVENT_ERROR: ESP_LOGW(kTag, "MQTT error (the broker refused the identity, or is not reachable)"); schedule_retry(); break;
    case MQTT_EVENT_DATA: {
      if (event->data_len != event->total_data_len || event->current_data_offset != 0) break;   // a command is a few bytes: never a fragment
      const std::string topic(event->topic, static_cast<std::size_t>(event->topic_len));
      const std::string payload(event->data, static_cast<std::size_t>(event->data_len));
      for (const Subscription& subscription : g_subscriptions) if (subscription.on_message && mqtt::topic_matches(subscription.filter, topic)) subscription.on_message(topic, payload);
      break;
    }
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
  config.network.disable_auto_reconnect = true;   // the retries are ours (a wait that grows), not the client's fixed ten seconds
#ifdef ARMOR_MQTT_HAS_CA  // certs/ca.pem exists: main/CMakeLists.txt embeds it
  extern const char ca_pem_start[] asm("_binary_ca_pem_start");
  config.broker.verification.certificate = ca_pem_start;
#endif
  esp_mqtt_client_handle_t client = esp_mqtt_client_init(&config);
  ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, on_mqtt, nullptr));
  { std::lock_guard<std::mutex> guard(g_client_lock); g_client = client; }   // before it starts: its first events may publish
  ESP_ERROR_CHECK(esp_mqtt_client_start(client));
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
    esp_mqtt_client_handle_t old = nullptr;
    { std::lock_guard<std::mutex> guard(g_client_lock); old = g_client; g_client = nullptr; }
    if (old != nullptr) { esp_mqtt_client_stop(old); esp_mqtt_client_destroy(old); }
    start_client(broker);
  }
}

// Waits for an address, then starts the clock and the broker connection once. The ports keep reading meanwhile.
void link_task(void*) {
  while (!network::has_ip()) vTaskDelay(pdMS_TO_TICKS(500));
  // The clock itself is clock_sync.cpp's (it starts without a broker too).
  esp_timer_create_args_t retry{};
  retry.callback = &retry_now;
  retry.name = "mqtt-retry";
  esp_timer_create(&retry, &g_retry_timer);
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
  g_out_ready = xSemaphoreCreateBinary();
  xTaskCreate(outgoing_task, "mqtt-out", 4096, nullptr, 4, nullptr);
  xTaskCreate(link_task, "mqtt-link", 6144, nullptr, 4, nullptr);
}

void subscribe(const std::string& filter, MessageHandler on_message, ConnectedHandler on_connected) {
  if (!filter.empty()) g_subscriptions.push_back({filter, std::move(on_message), std::move(on_connected)});
}

bool connected() { return g_connected; }
bool clock_is_set() { return std::time(nullptr) > 1700000000; }

std::uint64_t wall_clock_ms() {
  timeval tv{};
  gettimeofday(&tv, nullptr);
  return static_cast<std::uint64_t>(tv.tv_sec) * 1000ULL + static_cast<std::uint64_t>(tv.tv_usec) / 1000ULL;
}

// A node without a clock still publishes: its readings carry the time since it started instead (see the callers), and the server stamps them with the time it receives them.
void publish(const std::string& topic, const std::string& payload) {
  if (!g_enabled || g_out_ready == nullptr) return;
  if (!g_connected) { ++g_dropped; return; }
  {
    std::lock_guard<std::mutex> guard(g_out_lock);
    if (g_out.size() >= kOutMax) { g_out.pop_front(); ++g_dropped; }
    g_out.push_back({topic, payload});
  }
  xSemaphoreGive(g_out_ready);
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
