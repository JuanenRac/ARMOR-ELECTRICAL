// ARMOR-NODE-ELECTRICAL - Improv Wi-Fi over the USB serial line. Nothing here has run on a board.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// What the packets mean is in core/improv.hpp (tested on a computer). This file reads the bytes the console receives (the chip's USB Serial/JTAG, polled: nothing is read when nothing was
// typed), runs the RPCs - the Wi-Fi network goes through the same settings document the panel and the Bluetooth set-up use, so it is checked and stored the same way - and writes the answers.
// A node that took a network restarts to join it, so it is told "provisioning" and nothing more at that point: it leaves a mark in flash, and the start that follows tells the page "provisioned" - with
// the address of the panel in the result - only once the node has the network and an address, or that it was unable to connect when it has not after a while. The line is the one the log comes out of, so a packet is written whole.
#include "improv_serial.hpp"

#include <unistd.h>
#include <memory>
#include <new>
#include <string>
#include <vector>
extern "C" {
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}
#include "api_shared.hpp"
#include "core/improv.hpp"
#include "core/json.hpp"
#include "network.hpp"
#include "node_store.hpp"
#include "web_server.hpp"

namespace armor::improv_serial {
namespace {
constexpr char kTag[] = "armor-improv";
constexpr char kWaitKey[] = "improv_wait";            // the mark that a network was given over the cable and the page has not been told how it went
constexpr std::uint32_t kJoinLimitMs = 45000;          // how long a start after Improv waits for the network before saying it could not connect
bool g_waiting = false;
std::uint32_t g_waited_ms = 0;

void send(const std::vector<std::uint8_t>& bytes) {
  std::size_t done = 0;
  int stalls = 0;
  while (done < bytes.size()) {
    const ssize_t n = write(STDOUT_FILENO, bytes.data() + done, bytes.size() - done);
    if (n <= 0) { if (++stalls > 40) return; vTaskDelay(pdMS_TO_TICKS(5)); continue; }   // a line nobody reads: give up rather than wait for ever
    done += static_cast<std::size_t>(n);
  }
}

// "Provisioned" says that the node joined the Wi-Fi network it was given, not that it has an address: a board with a cable has one without ever having used Improv, and told the page its job was done.
improv::State current_state() { return network::status().sta_connected && network::has_ip() ? improv::State::kProvisioned : improv::State::kReady; }

std::string json_string(const std::string& text) {
  json::Writer w;
  w.string(text);
  return w.str();
}

void take_wifi(const improv::Rpc& rpc) {
  send(improv::state_packet(improv::State::kProvisioning));
  // "uplink":"wifi": on a board with a cable the node would otherwise go on using it and never join the network it was just given
  const std::string document = "{\"uplink\":\"wifi\",\"sta\":{\"enabled\":true,\"ssid\":" + json_string(rpc.ssid) + ",\"password\":" + json_string(rpc.password) + "}}";
  config::Problems problems;
  bool restart_required = false;
  if (api::put_config(document, problems, restart_required) != api::PutResult::kSaved) {
    ESP_LOGW(kTag, "the Wi-Fi network from the Improv page was refused (%u problems)", static_cast<unsigned>(problems.size()));
    send(improv::error_packet(improv::Error::kInvalidRpc));
    send(improv::state_packet(improv::State::kReady));
    return;
  }
  ESP_LOGI(kTag, "the Wi-Fi network \"%s\" was set over the USB cable (Improv); the node restarts to join it", rpc.ssid.c_str());
  store::blob_write(kWaitKey, "1");   // the start after the restart answers the page
  web::restart_after(2500);
}

void answer(const improv::Rpc& rpc) {
  switch (rpc.command) {
    case improv::Command::kWifiSettings: take_wifi(rpc); break;
    case improv::Command::kIdentify: ESP_LOGI(kTag, "the Improv page asked the node to identify itself"); send(improv::result_packet(improv::Command::kIdentify, {})); break;
    case improv::Command::kGetCurrentState: send(improv::state_packet(current_state())); break;
    case improv::Command::kGetDeviceInfo: {
      const std::unique_ptr<config::Settings> s(new (std::nothrow) config::Settings(store::settings()));   // on the heap: the set-up task has a small stack
      send(improv::result_packet(improv::Command::kGetDeviceInfo, {"A.R.M.O.R. electrical", api::version_text(), "ESP32-S3", s ? s->node_name : std::string()}));
      break;
    }
    case improv::Command::kGetWifiNetworks: send(improv::result_packet(improv::Command::kGetWifiNetworks, {})); break;   // an empty answer ends the list: scanning is the panel's
    default: send(improv::error_packet(improv::Error::kUnknownRpc)); break;
  }
}

// The answer owed to the page after the restart that joined the network.
void settle_join() {
  const network::Status status = network::status();
  const improv::Join progress = improv::join_progress(status.sta_connected, network::has_ip(), g_waited_ms, kJoinLimitMs);
  if (progress == improv::Join::kWaiting) return;
  g_waiting = false;
  if (progress == improv::Join::kJoined) {
    send(improv::state_packet(improv::State::kProvisioned));
    send(improv::result_packet(improv::Command::kWifiSettings, {improv::panel_url(status.ip, web::tls_status().mode == "https")}));
    ESP_LOGI(kTag, "the node joined the network given over the cable (address %s)", status.ip.c_str());
  } else {
    send(improv::error_packet(improv::Error::kUnableToConnect));
    send(improv::state_packet(improv::State::kReady));
    ESP_LOGW(kTag, "the node did not join the network given over the cable within %u s", static_cast<unsigned>(kJoinLimitMs / 1000));
  }
}

void listen_task(void*) {
  improv::Parser parser;
  std::uint8_t chunk[64];
  for (;;) {
    if (g_waiting) { settle_join(); g_waited_ms += 25; }
    const ssize_t n = read(STDIN_FILENO, chunk, sizeof chunk);
    if (n <= 0) { vTaskDelay(pdMS_TO_TICKS(25)); continue; }
    for (ssize_t i = 0; i < n; ++i) {
      const std::optional<improv::Packet> packet = parser.feed(chunk[i]);
      if (!packet) continue;
      if (packet->type != improv::Type::kRpc) continue;
      improv::Rpc rpc;
      const improv::Error problem = improv::parse_rpc(packet->data, rpc);
      if (problem != improv::Error::kNone) { send(improv::error_packet(problem)); continue; }
      answer(rpc);
    }
  }
}
}  // namespace

void start() {
  std::string mark;
  if (store::blob_read(kWaitKey, mark) && mark == "1") {
    store::blob_write(kWaitKey, "");   // once: a later restart does not wait again
    g_waiting = true;
  }
  xTaskCreate(listen_task, "improv", 5120, nullptr, 2, nullptr);
}

}  // namespace armor::improv_serial
