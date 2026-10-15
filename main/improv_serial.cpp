// ARMOR-ELECTRICAL - Improv Wi-Fi over the USB serial line. Nothing here has run on a board.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// What the packets mean is in core/improv.hpp (tested on a computer). This file reads the bytes the console receives (the chip's USB Serial/JTAG, polled: nothing is read when nothing was
// typed), runs the RPCs - the Wi-Fi network goes through the same settings document the panel and the Bluetooth set-up use, so it is checked and stored the same way - and writes the answers.
// A node that took a network restarts a moment later to join it: the page is told "provisioned" before that. The line is the one the log comes out of, so a packet is written whole.
#include "improv_serial.hpp"

#include <unistd.h>
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

void send(const std::vector<std::uint8_t>& bytes) {
  std::size_t done = 0;
  int stalls = 0;
  while (done < bytes.size()) {
    const ssize_t n = write(STDOUT_FILENO, bytes.data() + done, bytes.size() - done);
    if (n <= 0) { if (++stalls > 40) return; vTaskDelay(pdMS_TO_TICKS(5)); continue; }   // a line nobody reads: give up rather than wait for ever
    done += static_cast<std::size_t>(n);
  }
}

improv::State current_state() { return network::has_ip() ? improv::State::kProvisioned : improv::State::kReady; }

std::string json_string(const std::string& text) {
  json::Writer w;
  w.string(text);
  return w.str();
}

void take_wifi(const improv::Rpc& rpc) {
  send(improv::state_packet(improv::State::kProvisioning));
  const std::string document = "{\"sta\":{\"enabled\":true,\"ssid\":" + json_string(rpc.ssid) + ",\"password\":" + json_string(rpc.password) + "}}";
  config::Problems problems;
  bool restart_required = false;
  if (api::put_config(document, problems, restart_required) != api::PutResult::kSaved) {
    ESP_LOGW(kTag, "the Wi-Fi network from the Improv page was refused (%u problems)", static_cast<unsigned>(problems.size()));
    send(improv::error_packet(improv::Error::kInvalidRpc));
    send(improv::state_packet(improv::State::kReady));
    return;
  }
  ESP_LOGI(kTag, "the Wi-Fi network \"%s\" was set over the USB cable (Improv); the node restarts to join it", rpc.ssid.c_str());
  send(improv::state_packet(improv::State::kProvisioned));
  send(improv::result_packet(improv::Command::kWifiSettings, {}));
  web::restart_after(2500);
}

void answer(const improv::Rpc& rpc) {
  switch (rpc.command) {
    case improv::Command::kWifiSettings: take_wifi(rpc); break;
    case improv::Command::kIdentify: ESP_LOGI(kTag, "the Improv page asked the node to identify itself"); send(improv::result_packet(improv::Command::kIdentify, {})); break;
    case improv::Command::kGetCurrentState: send(improv::state_packet(current_state())); break;
    case improv::Command::kGetDeviceInfo: {
      const config::Settings s = store::settings();
      send(improv::result_packet(improv::Command::kGetDeviceInfo, {"A.R.M.O.R. electrical", api::version_text(), "ESP32-S3", s.node_name}));
      break;
    }
    case improv::Command::kGetWifiNetworks: send(improv::result_packet(improv::Command::kGetWifiNetworks, {})); break;   // an empty answer ends the list: scanning is the panel's
    default: send(improv::error_packet(improv::Error::kUnknownRpc)); break;
  }
}

void listen_task(void*) {
  improv::Parser parser;
  std::uint8_t chunk[64];
  for (;;) {
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

void start() { xTaskCreate(listen_task, "improv", 5120, nullptr, 2, nullptr); }

}  // namespace armor::improv_serial
