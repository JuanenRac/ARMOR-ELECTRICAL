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
#include "esp_rom_gpio.h"
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
int g_siren_index = -1;   // the relay the alarm owns, or -1
bool g_hold = false;      // the node is paused or in standby by the server's order: every relay is open and none moves

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

// Moves a relay. False when an interlock refused to close it (the other relay of the pair is closed): nothing moved, the pin is held where it was and the broker is told the state it still has.
// `force` is for the alarm's siren, which no interlock holds back.
bool drive(std::size_t index, bool on, const char* who, bool force = false) {
  if (!g_bank->set(index, on, force)) {
    const int other = on ? g_bank->blocked_by(index) : -1;
    if (other >= 0) ESP_LOGW(kTag, "relay %u (%s) was not closed by %s: it is interlocked with %s, which is closed", static_cast<unsigned>(index + 1), g_bank->name(index).c_str(), who, g_bank->name(static_cast<std::size_t>(other)).c_str());
    put(index);
    publish_one(index);
    return false;
  }
  put(index);
  ESP_LOGW(kTag, "relay %u (%s) %s by %s", static_cast<unsigned>(index + 1), g_bank->name(index).c_str(), on ? "closed" : "opened", who);
  publish_one(index);
  return true;
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
      for (std::size_t i = 0; i < board::kRelayCount; ++i) if (g_bank->is_on(i) && static_cast<int>(i) != g_siren_index) drive(i, false, "the loss of the broker");   // the siren is the alarm's, not the broker's
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
    // gpio_reset_pin would switch the pull-up on until the direction is set, which is a pulse of the wrong level for a relay that is closed by a high one (and a solid-state one
    // would follow it): the pad is only given to the GPIO, its pulls are off, the level is written, and then the pin becomes an output.
    esp_rom_gpio_pad_select_gpio(static_cast<std::uint32_t>(pin));
    gpio_pullup_dis(pin);
    gpio_pulldown_dis(pin);
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

void hold_open(bool hold) {
  std::lock_guard<std::mutex> guard(g_lock);
  g_hold = hold;
  if (!hold || !g_bank) return;   // before start() the relays are made open anyway
  ESP_LOGW(kTag, "the node's work is off: every relay is open and none moves until it is resumed");
  for (std::size_t i = 0; i < board::kRelayCount; ++i) if (g_bank->enabled(i) && g_bank->is_on(i)) drive(i, false, "the pause");   // the siren's too
}

bool set(std::size_t index, bool on) {
  std::lock_guard<std::mutex> guard(g_lock);
  if (!g_bank || !g_bank->enabled(index) || static_cast<int>(index) == g_siren_index || (g_hold && on)) return false;
  drive(index, on, "the panel");
  return true;
}

bool interlocked(std::size_t index, bool on) {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_bank && on && g_bank->enabled(index) && g_bank->blocked_by(index) >= 0;
}

void reserve_for_siren(std::size_t index) {
  std::lock_guard<std::mutex> guard(g_lock);
  g_siren_index = g_bank && g_bank->enabled(index) ? static_cast<int>(index) : -1;
}

void siren(bool on) {
  std::lock_guard<std::mutex> guard(g_lock);
  if (!g_bank || g_siren_index < 0) return;
  if (g_hold) on = false;   // paused or in standby: the siren stays silent whatever the alarm asks
  const std::size_t index = static_cast<std::size_t>(g_siren_index);
  if (g_bank->is_on(index) != on) { drive(index, on, "the alarm", true); return; }
  put(index);   // the same level again: nothing is said, but the pin is made to hold it
}

Outcome command(const std::string& relay, const std::string& payload) {
  std::lock_guard<std::mutex> guard(g_lock);
  if (!g_bank || !g_settings.remote.relays) return Outcome::kRemoteOff;
  if (g_hold) { ESP_LOGW(kTag, "a command for \"%s\" was ignored: the node's work is off", relay.c_str()); return Outcome::kRemoteOff; }
  const int index = g_bank->find(relay);
  if (index < 0) { ESP_LOGW(kTag, "a command for \"%s\" was ignored: no such relay", relay.c_str()); return Outcome::kUnknownRelay; }
  const relays::Action action = relays::parse_broker_command(payload);
  if (action == relays::Action::kNone) { ESP_LOGW(kTag, "a command for \"%s\" was ignored: not ON or OFF (a TOGGLE from the broker is refused: a message can be delivered twice)", relay.c_str()); return Outcome::kNotUnderstood; }
  const std::size_t i = static_cast<std::size_t>(index);
  if (index == g_siren_index) { ESP_LOGW(kTag, "a command for \"%s\" was ignored: it is the siren's relay", relay.c_str()); return Outcome::kReserved; }
  if (!drive(i, action == relays::Action::kToggle ? !g_bank->is_on(i) : action == relays::Action::kOn, "the broker")) return Outcome::kInterlocked;
  return Outcome::kOk;
}

Outcome command_direct(const std::string& relay, relays::Action action, bool& now_on) {
  std::lock_guard<std::mutex> guard(g_lock);
  if (!g_bank) return Outcome::kUnknownRelay;
  if (g_hold) { ESP_LOGW(kTag, "a command from the central for \"%s\" was ignored: the node's work is off", relay.c_str()); return Outcome::kRemoteOff; }
  const int index = g_bank->find(relay);
  if (index < 0) { ESP_LOGW(kTag, "a command from the central for \"%s\" was ignored: no such relay", relay.c_str()); return Outcome::kUnknownRelay; }
  if (action == relays::Action::kNone) return Outcome::kNotUnderstood;
  const std::size_t i = static_cast<std::size_t>(index);
  if (index == g_siren_index) return Outcome::kReserved;
  const bool on = action == relays::Action::kToggle ? !g_bank->is_on(i) : action == relays::Action::kOn;
  const bool moved = drive(i, on, "the central");
  now_on = g_bank->is_on(i);
  return moved ? Outcome::kOk : Outcome::kInterlocked;
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
