// ARMOR-ELECTRICAL - the relay outputs of the base board. Nothing here has run on a board.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// What the node decides is in core/relay_bank.hpp and core/relay_remote.hpp (tested on a computer): which relay is closed, at what level its pin must be, which words the broker may
// say and when a relay falls back to open. This file only puts the level on the pin and talks to the broker. The level of a pin is set BEFORE the pin becomes an output, so a relay
// never sees a pulse of the wrong level while the node starts. Before the node runs, and while it restarts, a pin floats: the base board must pull every relay's driver to its open
// level with a resistor of its own.
#include "relay_outputs.hpp"

#include <memory>
#include <mutex>
extern "C" {
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
}
#include "core/relay_bank.hpp"
#include "core/relay_remote.hpp"
#include "mqtt_link.hpp"

namespace armor::relay_outputs {
namespace {
constexpr char kTag[] = "armor-relays";
constexpr unsigned kRepublishEveryMs = 30000;

std::mutex g_lock;
std::unique_ptr<relays::Bank> g_bank;
config::IoSettings g_settings;
std::string g_node_id;
relays::LinkWatch g_watch;
bool g_fell_back = false;

std::uint64_t now_ms() { return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL; }

void put(std::size_t index) {
  const int pin = g_bank->pin(index);
  if (pin >= 0) gpio_set_level(static_cast<gpio_num_t>(pin), g_bank->pin_level(index) ? 1 : 0);
}

// The state and the availability of one relay, as a device of the device layer. Called with the lock held.
void publish_one(std::size_t index) {
  if (!g_settings.remote.relays || !g_bank->enabled(index)) return;
  const std::string& name = g_bank->name(index);
  mqtt_link::publish(relays::topic(g_node_id, name, "state"), relays::report(g_bank->is_on(index)));
  mqtt_link::publish(relays::topic(g_node_id, name, "availability"), "online");
}
void publish_all_locked() { for (std::size_t i = 0; i < board::kRelayCount; ++i) publish_one(i); }

void drive(std::size_t index, bool on, const char* who) {
  g_bank->set(index, on);
  put(index);
  ESP_LOGW(kTag, "relay %u (%s) %s by %s", static_cast<unsigned>(index + 1), g_bank->name(index).c_str(), on ? "closed" : "opened", who);
  publish_one(index);
}

// Every half minute the state is said again (a server that restarted learns it), and a relay that was closed falls back to open when the broker has been lost for long enough.
void watch_task(void*) {
  std::uint64_t last_publish = 0;
  esp_task_wdt_add(nullptr);   // the fall back to open must not depend on a task that can hang unseen
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_task_wdt_reset();
    std::lock_guard<std::mutex> guard(g_lock);
    const std::uint64_t now = now_ms();
    g_watch.update(mqtt_link::connected(), now);
    if (!g_watch.lost()) g_fell_back = false;
    else if (!g_fell_back && relays::must_open(g_settings.remote.link_timeout_s, g_watch.lost_for_ms(now))) {
      g_fell_back = true;
      ESP_LOGW(kTag, "the broker has been out of reach for %d s: every relay falls back to open", g_settings.remote.link_timeout_s);
      for (std::size_t i = 0; i < board::kRelayCount; ++i) if (g_bank->is_on(i)) drive(i, false, "the loss of the broker");
    }
    if (mqtt_link::connected() && now - last_publish >= kRepublishEveryMs) { last_publish = now; publish_all_locked(); }
  }
}
}  // namespace

void start(const config::IoSettings& settings, const std::string& node_id) {
  std::lock_guard<std::mutex> guard(g_lock);
  g_settings = settings;
  g_node_id = node_id;
  g_bank = std::make_unique<relays::Bank>(settings);
  for (std::size_t i = 0; i < board::kRelayCount; ++i) {
    if (!g_bank->enabled(i)) continue;
    const gpio_num_t pin = static_cast<gpio_num_t>(g_bank->pin(i));
    gpio_reset_pin(pin);
    gpio_set_level(pin, g_bank->open_level(i) ? 1 : 0);   // first the level, then the direction
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    ESP_LOGI(kTag, "relay %u (%s) on GPIO %d, open", static_cast<unsigned>(i + 1), settings.relays[i].name.c_str(), static_cast<int>(pin));
  }
  if (!settings.remote.relays) return;
  // the broker may move them: listen for the commands, and say what they are every time the broker is (re)connected
  mqtt_link::subscribe(relays::command_filter(node_id),
      [](const std::string& topic, const std::string& payload) {
        const std::string name = relays::relay_of_command_topic(g_node_id, topic);
        if (!name.empty()) command(name, payload);
      },
      [] { std::lock_guard<std::mutex> guard(g_lock); publish_all_locked(); });
  xTaskCreate(watch_task, "relay-watch", 4096, nullptr, 3, nullptr);
  ESP_LOGI(kTag, "the broker may move the relays (armor/device/%s/<relay>/set)%s", node_id.c_str(), settings.remote.link_timeout_s > 0 ? ", and they fall back to open without it" : "");
}

bool set(std::size_t index, bool on) {
  std::lock_guard<std::mutex> guard(g_lock);
  if (!g_bank || !g_bank->enabled(index)) return false;
  drive(index, on, "the panel");
  return true;
}

Outcome command(const std::string& relay, const std::string& payload) {
  std::lock_guard<std::mutex> guard(g_lock);
  if (!g_bank || !g_settings.remote.relays) return Outcome::kRemoteOff;
  const int index = g_bank->find(relay);
  if (index < 0) { ESP_LOGW(kTag, "a command for \"%s\" was ignored: no such relay", relay.c_str()); return Outcome::kUnknownRelay; }
  const relays::Action action = relays::parse_command(payload);
  if (action == relays::Action::kNone) { ESP_LOGW(kTag, "a command for \"%s\" was ignored: not ON, OFF or TOGGLE", relay.c_str()); return Outcome::kNotUnderstood; }
  const std::size_t i = static_cast<std::size_t>(index);
  drive(i, action == relays::Action::kToggle ? !g_bank->is_on(i) : action == relays::Action::kOn, "the broker");
  return Outcome::kOk;
}

Outcome command_direct(const std::string& relay, relays::Action action, bool& now_on) {
  std::lock_guard<std::mutex> guard(g_lock);
  if (!g_bank) return Outcome::kUnknownRelay;
  const int index = g_bank->find(relay);
  if (index < 0) { ESP_LOGW(kTag, "a command from the central for \"%s\" was ignored: no such relay", relay.c_str()); return Outcome::kUnknownRelay; }
  if (action == relays::Action::kNone) return Outcome::kNotUnderstood;
  const std::size_t i = static_cast<std::size_t>(index);
  const bool on = action == relays::Action::kToggle ? !g_bank->is_on(i) : action == relays::Action::kOn;
  drive(i, on, "the central");
  now_on = g_bank->is_on(i);
  return Outcome::kOk;
}

std::string json() {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_bank ? g_bank->json() : "[]";
}

int index_of(const std::string& name) {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_bank ? g_bank->find(name) : -1;
}

bool remote_enabled() {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_settings.remote.relays;
}

}  // namespace armor::relay_outputs
