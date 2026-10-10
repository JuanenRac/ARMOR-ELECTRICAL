// ARMOR-ELECTRICAL - the Zigbee radio of the base board: UART2 to one TCP client, byte for byte. Nothing here has run on a board.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The node does not read what goes through: it is a cable. One client at a time, and only the addresses the settings name: the primary (the server) and, if set, a fallback (an alarm
// panel) that may take the radio over only while the primary has been away for a while (core/zigbee_bridge.hpp, Arbiter). A second connection from the client that holds the radio
// replaces the first (a client that vanished without closing would otherwise hold it until the TCP keep-alive noticed), and the primary takes the radio back from the fallback. Whatever the radio says while nobody is connected is dropped and counted.
// The radio's reset and boot lines are driven open-drain: low, or let go - the module has its own pull-ups, so the node never fights them.
#include "zigbee_bridge.hpp"

#include <cstring>
#include <memory>
#include <mutex>
extern "C" {
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

namespace armor::zigbee_bridge {
namespace {
constexpr char kTag[] = "armor-zigbee";
constexpr uart_port_t kUart = UART_NUM_2;

std::mutex g_lock;
zigbee::Status g_status;
config::ZigbeeSetting g_setting;
std::mutex g_radio_lock;   // one reset at a time
std::unique_ptr<zigbee::Arbiter> g_arbiter;   // only the bridge's task touches it

std::uint64_t now_ms() { return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL; }

void set_state(const char* state, const char* error = "") {
  std::lock_guard<std::mutex> guard(g_lock);
  g_status.state = state;
  g_status.error = error;
}

// An open-drain line that starts let go.
void prepare_line(int pin) {
  if (pin < 0) return;
  gpio_reset_pin(static_cast<gpio_num_t>(pin));
  gpio_set_level(static_cast<gpio_num_t>(pin), 1);
  gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_OUTPUT_OD);
  gpio_set_level(static_cast<gpio_num_t>(pin), 1);
}
void drive(int pin, bool low) {
  if (pin >= 0) gpio_set_level(static_cast<gpio_num_t>(pin), low ? 0 : 1);
}

bool open_uart(const config::ZigbeeSetting& z) {
  uart_config_t config{};
  config.baud_rate = z.baud;
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;
  if (uart_driver_install(kUart, 4096, 1024, 0, nullptr, 0) != ESP_OK) { set_state("error", "uart"); return false; }
  if (uart_param_config(kUart, &config) != ESP_OK || uart_set_pin(kUart, z.tx, z.rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
    uart_driver_delete(kUart);
    set_state("error", "pins");
    return false;
  }
  return true;
}

int listen_on(int port) {
  const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (fd < 0) return -1;
  const int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(static_cast<std::uint16_t>(port));
  if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0 || listen(fd, 1) != 0) { close(fd); return -1; }
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  return fd;
}

void close_client(int& client) {
  if (client >= 0) close(client);
  client = -1;
  g_arbiter->released(now_ms());
  std::lock_guard<std::mutex> guard(g_lock);
  g_status.client.clear();
  g_status.role.clear();
  g_status.state = "waiting";
}

// Sends everything or fails: the socket has a send timeout, so a client that stopped reading cannot hold the bridge for long.
bool send_all(int client, const std::uint8_t* data, std::size_t length) {
  std::size_t sent = 0;
  while (sent < length) {
    const int n = send(client, data + sent, length - sent, 0);
    if (n <= 0) return false;
    sent += static_cast<std::size_t>(n);
  }
  return true;
}

void bridge_task(void*) {
  int listener = -1;
  while ((listener = listen_on(g_setting.port)) < 0) {
    set_state("error", "listen");
    ESP_LOGE(kTag, "cannot listen on port %d: trying again in 5 s", g_setting.port);
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
  set_state("waiting");
  ESP_LOGI(kTag, "the Zigbee radio is on UART2 at %d baud, offered on TCP port %d to %s%s%s only", g_setting.baud, g_setting.port, g_setting.allowed_client.c_str(), g_setting.fallback_client.empty() ? "" : " and, as a fallback, ", g_setting.fallback_client.c_str());
  int client = -1;
  std::uint8_t buffer[512];
  for (;;) {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(listener, &readable);
    int highest = listener;
    if (client >= 0) { FD_SET(client, &readable); if (client > highest) highest = client; }
    timeval wait{0, 5000};
    if (select(highest + 1, &readable, nullptr, nullptr, &wait) > 0) {
      if (FD_ISSET(listener, &readable)) {
        sockaddr_in peer{};
        socklen_t length = sizeof peer;
        const int accepted = accept(listener, reinterpret_cast<sockaddr*>(&peer), &length);
        if (accepted >= 0) {
          const std::uint32_t address = ntohl(peer.sin_addr.s_addr);
          const zigbee::Role role = zigbee::classify(g_setting, address);
          if (g_arbiter->offer(role, now_ms()) != zigbee::Arbiter::Verdict::kAccept) {
            close(accepted);
            std::lock_guard<std::mutex> guard(g_lock);
            ++g_status.refused;
            ESP_LOGW(kTag, "a connection from %s was refused%s", net::ipv4_text(address).c_str(), role == zigbee::Role::kFallback ? " (the primary client is still there or has not been away long enough)" : "");
          } else {
            if (client >= 0) { close(client); g_arbiter->released(now_ms()); }   // the same client again, or the primary taking the radio back: the old connection is gone
            g_arbiter->accepted(role, now_ms());
            client = accepted;
            const int yes = 1;
            setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof yes);
            setsockopt(client, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof yes);
            const int idle = 30, interval = 10, count = 3;
            setsockopt(client, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof idle);
            setsockopt(client, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof interval);
            setsockopt(client, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof count);
            const timeval send_timeout{1, 0};
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof send_timeout);
            {
              std::lock_guard<std::mutex> guard(g_lock);
              g_status.state = "connected";
              g_status.client = net::ipv4_text(address);
              g_status.role = zigbee::role_text(role);
              g_status.takeovers = g_arbiter->takeovers();
              ++g_status.connections;
            }
            ESP_LOGI(kTag, "the %s client connected from %s", zigbee::role_text(role), net::ipv4_text(address).c_str());
          }
        }
      }
      if (client >= 0 && FD_ISSET(client, &readable)) {
        const int n = recv(client, buffer, sizeof buffer, 0);
        if (n <= 0) {
          ESP_LOGI(kTag, "the client closed the connection");
          close_client(client);
        } else {
          uart_write_bytes(kUart, buffer, static_cast<std::size_t>(n));
          std::lock_guard<std::mutex> guard(g_lock);
          g_status.to_radio += static_cast<std::uint64_t>(n);
        }
      }
    }
    const int n = uart_read_bytes(kUart, buffer, sizeof buffer, 0);
    if (n > 0) {
      if (client >= 0) {
        if (send_all(client, buffer, static_cast<std::size_t>(n))) {
          std::lock_guard<std::mutex> guard(g_lock);
          g_status.from_radio += static_cast<std::uint64_t>(n);
        } else {
          ESP_LOGW(kTag, "the client could not be written to: closing it");
          close_client(client);
        }
      } else {
        std::lock_guard<std::mutex> guard(g_lock);
        g_status.dropped += static_cast<std::uint64_t>(n);
      }
    }
  }
}
}  // namespace

void start(const config::Settings& settings) {
  g_setting = settings.zigbee;
  if (!g_setting.enabled) return;
  g_arbiter = std::make_unique<zigbee::Arbiter>(static_cast<std::uint64_t>(g_setting.fallback_after_s) * 1000ULL, now_ms());
  {
    std::lock_guard<std::mutex> guard(g_lock);
    g_status.state = "starting";
    g_status.has_reset = g_setting.reset >= 0;
    g_status.has_bootloader = g_setting.bootloader >= 0;
  }
  prepare_line(g_setting.reset);
  prepare_line(g_setting.bootloader);
  if (!open_uart(g_setting)) { ESP_LOGE(kTag, "the radio's serial line could not be opened"); return; }
  xTaskCreate(bridge_task, "zigbee", 6144, nullptr, 5, nullptr);
}

zigbee::Status status() { std::lock_guard<std::mutex> guard(g_lock); return g_status; }

bool reset_radio(bool bootloader) {
  if (!g_setting.enabled || g_setting.reset < 0 || (bootloader && g_setting.bootloader < 0)) return false;
  {
    std::lock_guard<std::mutex> guard(g_lock);
    if (g_status.state == "error" || g_status.state == "disabled") return false;
  }
  std::lock_guard<std::mutex> radio(g_radio_lock);
  ESP_LOGW(kTag, "the radio is reset%s", bootloader ? " into its boot loader" : "");
  for (const zigbee::Step& step : zigbee::radio_sequence(bootloader)) {
    drive(g_setting.reset, step.reset_low);
    drive(g_setting.bootloader, step.bootloader_low);
    if (step.hold_ms > 0) vTaskDelay(pdMS_TO_TICKS(step.hold_ms));
  }
  uart_flush_input(kUart);
  return true;
}

}  // namespace armor::zigbee_bridge
