// ARMOR-ELECTRICAL - what the panel does over HTTP: the status, the ports, the readings, the settings and the search for Wi-Fi networks.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include "api_shared.hpp"

#include <cstring>
extern "C" {
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
}
#include "core/board_s3.hpp"
#include "core/json.hpp"
#include "mqtt_link.hpp"
#include "network.hpp"
#include "node_store.hpp"
#include "electrical_manager.hpp"
#include "web_server.hpp"

namespace armor::api {

std::string version_text() { return esp_app_get_description()->version; }

const char* reset_reason_text() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "crash";
    case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    default: return "other";
  }
}

void write_network(json::Writer& w) {
  const network::Status n = network::status();
  w.key("network").begin_object().field("board", n.board).field("ethernet_available", n.ethernet_available).field("ethernet_ok", n.ethernet_ok)
      .field("layout", n.layout).field("link_up", n.link_up).field("has_ip", n.has_ip).field("ip", n.ip).field("netmask", n.netmask).field("gateway", n.gateway)
      .field("dns", n.dns).field("mac", n.mac).field("ap_active", n.ap_active).field("ap_setup", n.ap_setup).field("ap_ssid", n.ap_ssid).field("ap_channel", n.ap_channel)
      .field("ap_clients", n.ap_clients).field("sta_connected", n.sta_connected).field("sta_ssid", n.sta_ssid).field("sta_error", n.sta_error).field("sta_rssi", n.sta_rssi).end_object();
}

void write_bus(json::Writer& w) {
  const manager::BusStatus b = manager::bus();
  w.begin_object().field("state", b.state).field("error", b.error).field("rx", b.rx).field("tx", b.tx).field("baud", b.baud).field("poll_s", b.poll_s)
      .field("bytes_rx", static_cast<long long>(b.bytes_rx)).field("bytes_tx", static_cast<long long>(b.bytes_tx)).field("messages", static_cast<long long>(b.messages)).end_object();
}

void write_meter(json::Writer& w, std::size_t index) {
  const manager::MeterStatus s = manager::meter(index);
  w.begin_object().field("meter", s.number).field("enabled", s.enabled).field("channel", s.channel).field("label", s.label).field("model", s.model).field("address", s.address)
      .field("state", s.state).field("error", s.error).field("replies_ok", static_cast<long long>(s.replies_ok)).field("replies_bad", static_cast<long long>(s.replies_bad))
      .field("timeouts", static_cast<long long>(s.timeouts));
  if (s.have_reading) {
    w.key("reading").begin_object().key("voltage_v").number(s.voltage_v, 1).key("current_a").number(s.current_a, 3).key("power_w").number(s.power_w, 1).key("energy_kwh").number(s.energy_kwh, 3)
        .field("alarm", s.alarm).end_object();
  }
  w.end_object();
}

std::string status_json() {
  const config::Settings s = store::settings();
  const mqtt_link::Status m = mqtt_link::status();
  const esp_partition_t* running = esp_ota_get_running_partition();
  json::Writer w;
  w.begin_object().field("node_id", s.node_id).field("name", s.node_name).field("version", version_text()).field("uptime_s", static_cast<long long>(esp_timer_get_time() / 1000000LL))
      .field("reset_reason", reset_reason_text()).field("heap_free", static_cast<long long>(esp_get_free_heap_size())).field("heap_min", static_cast<long long>(esp_get_minimum_free_heap_size()))
      .field("psram_free", static_cast<long long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM))).field("partition", running != nullptr ? running->label : "?");
  write_network(w);
  w.key("mqtt").begin_object().field("enabled", m.enabled).field("connected", m.connected).field("clock_set", m.clock_set).field("published", static_cast<long long>(m.published))
      .field("dropped", static_cast<long long>(m.dropped)).end_object();
  const web::TlsStatus tls = web::tls_status();
  w.key("web").begin_object().field("mode", tls.mode).field("https", tls.running).field("cert_sha256", tls.fingerprint).end_object();
  w.key("bus");
  write_bus(w);
  w.key("meters").begin_array();
  for (std::size_t i = 0; i < config::kMeterCount; ++i) write_meter(w, i);
  w.end_array().end_object();
  return w.str();
}

std::string meters_json() {
  json::Writer w;
  w.begin_object().key("bus");
  write_bus(w);
  w.key("meters").begin_array();
  for (std::size_t i = 0; i < config::kMeterCount; ++i) write_meter(w, i);
  w.end_array().key("catalog").begin_array();
  for (int gpio = board::kFirstGpio; gpio <= board::kLastGpio; ++gpio) {
    const board::PinInfo info = board::pin_info(gpio);
    if (info.reason == board::Reserved::kNoSuchPin) continue;
    const char* use = info.use == board::PinUse::kFree ? "free" : info.use == board::PinUse::kCaution ? "caution" : "reserved";
    const char* reason = info.reason == board::Reserved::kFlash ? "flash" : info.reason == board::Reserved::kPsram ? "psram" : info.reason == board::Reserved::kUsb ? "usb" : info.reason == board::Reserved::kEthernet ? "ethernet" : info.reason == board::Reserved::kCamera ? "camera" : "";
    w.begin_object().field("gpio", gpio).field("use", use).field("reason", reason).field("note", info.note).field("on_header", info.on_header).end_object();
  }
  w.end_array().end_object();
  return w.str();
}

std::string readings_json() {
  const std::string message = manager::last_message();
  return message.empty() ? "[]" : "[" + message + "]";
}

std::string config_get_json() {
  const config::Settings s = store::settings();
  json::Writer w;
  w.begin_object().key("config").raw(config::to_json(s, false)).field("channel_auto", config::effective_channel(s.ap, store::mac_sum())).field("firmware", version_text()).end_object();
  return w.str();
}

std::string config_export_json() {
  const config::Settings s = store::settings();
  json::Writer w;
  w.begin_object().key("config").raw(config::to_json(s, true)).field("firmware", version_text()).end_object();
  return w.str();
}

std::string problems_json(const config::Problems& problems) {
  json::Writer w;
  w.begin_array();
  for (const config::Problem& problem : problems) w.begin_object().field("path", problem.path).field("code", problem.code).end_object();
  w.end_array();
  return w.str();
}

namespace {
// What changed in a way that needs a restart: everything but the node's display name and the panel language.
bool needs_restart(config::Settings before, config::Settings after) {
  before.node_name = after.node_name = "";
  before.language = after.language = "en";
  return config::to_json(before, true) != config::to_json(after, true);
}
}  // namespace

PutResult put_config(std::string_view document, config::Problems& problems, bool& restart_required) {
  const config::Settings before = store::settings();
  config::Settings after;
  problems.clear();
  if (!config::load(document, before, after, problems)) return PutResult::kInvalid;
  if (!store::save_settings(after)) return PutResult::kStorage;
  restart_required = needs_restart(before, after);
  return PutResult::kSaved;
}

bool wifi_scan_json(std::string& data, std::string& error) {
  std::vector<network::ScanEntry> found;
  if (!network::scan(found, error)) return false;
  json::Writer w;
  w.begin_object().key("networks").begin_array();
  for (const network::ScanEntry& entry : found) w.begin_object().field("ssid", entry.ssid).field("rssi", entry.rssi).field("channel", entry.channel).field("security", entry.security).end_object();
  w.end_array().end_object();
  data = w.str();
  return true;
}

}  // namespace armor::api
