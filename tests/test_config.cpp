// ARMOR-ELECTRICAL - host tests of the node's settings: the defaults, the serial line and its meters, and the stored document.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include <cstdio>
#include <string>

#include "../core/electrical_config.hpp"

static int failures = 0;
static int checks = 0;
#define CHECK(condition)                                                              \
  do {                                                                                \
    ++checks;                                                                         \
    if (!(condition)) {                                                               \
      ++failures;                                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);                \
    }                                                                                 \
  } while (0)

using namespace armor;

static config::Settings valid_settings() {
  config::Settings s = config::default_settings("a1b2c3");
  s.sta.enabled = true;
  s.sta.ssid = "casa";
  s.sta.password = "una-clave-larga";
  return s;
}
static bool has(const config::Problems& problems, const std::string& path, const std::string& code) {
  for (const config::Problem& p : problems) if (p.path == path && p.code == code) return true;
  return false;
}

static void test_defaults() {
  const config::Settings s = config::default_settings("a1b2c3");
  CHECK(s.node_id == "electrical-a1b2c3" && s.node_name == s.node_id && s.meters.size() == 16);
  CHECK(s.bus.rx == 16 && s.bus.tx == 15 && s.bus.baud == 9600 && s.bus.poll_s == 2);
  CHECK(!s.meters[0].enabled && s.meters[0].channel == "meter1" && s.meters[15].channel == "meter16" && s.meters[0].model == "ac" && s.meters[0].address == 1);
  // no Wi-Fi at all could not be reached: the defaults are not valid until one of the two ways in is on
  CHECK(has(config::validate(s), "sta.enabled", "required"));
  CHECK(config::validate(valid_settings()).empty());
  CHECK(std::string(config::default_settings("a1b2c3").ap.ssid) == "ARMOR-ELECTRICAL");
  for (int gpio : {26, 30, 33, 35, 37, 19, 20, 22, 49, -1}) CHECK(!board::assignable(gpio));
  for (int gpio : {0, 3, 43, 44, 45, 46, 48, 1, 16, 47}) CHECK(board::assignable(gpio));
}

static void test_meters() {
  config::Settings s = valid_settings();
  s.meters[0] = {true, "grid", "Red", "ac", 1};
  s.meters[1] = {true, "heater", "", "ac", 2};
  s.meters[2] = {true, "dc-bus", "Bus CC", "dc", 3};
  CHECK(config::validate(s).empty());
  // a channel name is a device name: lowercase, digits, - and _
  s.meters[2].channel = "Bad Name";
  CHECK(has(config::validate(s), "meters.2.channel", "invalid"));
  s.meters[2].channel = "";
  CHECK(has(config::validate(s), "meters.2.channel", "required"));
  s.meters[2].channel = "heater";
  CHECK(has(config::validate(s), "meters.2.channel", "conflict"));
  s.meters[2].channel = "dc-bus";
  // two meters cannot share an address on one line, and an address is 1 to 247
  s.meters[2].address = 2;
  CHECK(has(config::validate(s), "meters.2.address", "conflict"));
  s.meters[2].address = 248;
  CHECK(has(config::validate(s), "meters.2.address", "range"));
  s.meters[2].address = 0;
  CHECK(has(config::validate(s), "meters.2.address", "range"));
  s.meters[2].address = 247;
  CHECK(config::validate(s).empty());
  s.meters[2].model = "pzem";
  CHECK(has(config::validate(s), "meters.2.model", "invalid"));
  s.meters[2].model = "dc";
  // a label of more than 40 characters is refused
  s.meters[2].label = std::string(41, 'a');
  CHECK(has(config::validate(s), "meters.2.label", "invalid"));
  s.meters[2].label = std::string(40, 'a');
  CHECK(config::validate(s).empty());
  // a disabled meter is not checked at all (not even a repeated channel)
  s.meters[5] = {false, "grid", "", "zzz", 1};
  CHECK(config::validate(s).empty());
}

static void test_line() {
  config::Settings s = valid_settings();
  s.meters[0] = {true, "grid", "", "ac", 1};
  CHECK(config::validate(s).empty());
  s.bus.rx = -1;
  CHECK(has(config::validate(s), "bus.rx", "required"));
  s.bus.rx = 16; s.bus.tx = -1;
  CHECK(has(config::validate(s), "bus.tx", "required"));
  s.bus.tx = 33;
  CHECK(has(config::validate(s), "bus.tx", "reserved"));
  s.bus.tx = 16;
  CHECK(has(config::validate(s), "bus.tx", "conflict"));
  s.bus.tx = 15;
  s.bus.baud = 4801;
  CHECK(has(config::validate(s), "bus.baud", "range"));
  s.bus.baud = 115200;
  CHECK(config::validate(s).empty());
  s.bus.poll_s = 0;
  CHECK(has(config::validate(s), "bus.poll_s", "range"));
  s.bus.poll_s = 3601;
  CHECK(has(config::validate(s), "bus.poll_s", "range"));
  s.bus.poll_s = 60;
  // with no meter enabled the line is not checked: it has nothing to carry
  s.bus.rx = -1; s.bus.tx = -1; s.meters[0].enabled = false;
  CHECK(config::validate(s).empty());
}

static void test_document() {
  config::Settings s = valid_settings();
  s.bus.baud = 19200; s.bus.poll_s = 5;
  s.meters[0] = {true, "grid", "Red", "ac", 1};
  s.meters[3] = {true, "pv-dc", "Campo solar", "dc", 9};
  const std::string stored = config::to_json(s, true), shown = config::to_json(s, false);
  CHECK(stored.find("una-clave-larga") != std::string::npos && shown.find("una-clave-larga") == std::string::npos && shown.find("\"password_set\":true") != std::string::npos);
  CHECK(shown.find("\"meters\":[") != std::string::npos && shown.find("\"bus\":{\"rx\":16,\"tx\":15,\"baud\":19200,\"poll_s\":5}") != std::string::npos);
  config::Settings back;
  config::Problems problems;
  CHECK(config::load(stored, config::default_settings("000000"), back, problems) && back.sta.password == "una-clave-larga" && back.node_id == s.node_id);
  CHECK(back.bus.baud == 19200 && back.bus.poll_s == 5 && back.meters[0].enabled && back.meters[0].channel == "grid" && back.meters[0].label == "Red");
  CHECK(back.meters[3].enabled && back.meters[3].model == "dc" && back.meters[3].address == 9 && !back.meters[1].enabled);
  // what is written is read back to the same document
  CHECK(config::to_json(back, true) == stored);
  // a section sent without a password keeps the stored one, "password_clear" erases it
  config::Settings edited;
  CHECK(config::load("{\"sta\":{\"enabled\":true,\"ssid\":\"otra\"}}", s, edited, problems) && edited.sta.ssid == "otra" && edited.sta.password == "una-clave-larga");
  CHECK(config::load("{\"sta\":{\"password_clear\":true}}", s, edited, problems) && edited.sta.password.empty());
  // the panel may send only the meters it edits: the rest keeps its stored value
  problems.clear();
  CHECK(config::load("{\"meters\":[{\"enabled\":true,\"channel\":\"grid\",\"address\":1},{\"enabled\":true,\"channel\":\"oven\",\"address\":4}]}", config::default_settings("a1b2c3"), edited, problems) == false);
  problems.clear();
  CHECK(config::load("{\"meters\":[{\"enabled\":true,\"channel\":\"grid\",\"address\":1},{\"enabled\":true,\"channel\":\"oven\",\"address\":4}]}", valid_settings(), edited, problems));
  CHECK(edited.meters[1].channel == "oven" && edited.meters[1].address == 4 && edited.meters[2].channel == "meter3");
  problems.clear();
  CHECK(!config::load("{\"meters\":[{\"address\":\"one\"}]}", s, edited, problems) && has(problems, "meters.0.address", "invalid"));
  problems.clear();
  CHECK(!config::load("{\"meters\":[{\"address\":999}]}", s, edited, problems) && has(problems, "meters.0.address", "range"));
  problems.clear();
  CHECK(!config::load("{\"meters\":\"all\"}", s, edited, problems) && has(problems, "meters", "invalid"));
  problems.clear();
  std::string too_many = "{\"meters\":[";
  for (int i = 0; i < 17; ++i) too_many += std::string(i ? "," : "") + "{}";
  CHECK(!config::load(too_many + "]}", s, edited, problems) && has(problems, "meters", "invalid"));
  problems.clear();
  CHECK(!config::load("{\"bus\":{\"rx\":99}}", s, edited, problems) && has(problems, "bus.rx", "range"));
  problems.clear();
  CHECK(!config::load("{\"bus\":{\"baud\":\"fast\"}}", s, edited, problems) && has(problems, "bus.baud", "invalid"));
  problems.clear();
  CHECK(!config::load("not json", s, edited, problems) && has(problems, "", "not_json"));
  problems.clear();
  CHECK(!config::load("{\"mqtt\":{\"enabled\":true,\"uri\":\"http://x\"}}", s, edited, problems) && has(problems, "mqtt.uri", "invalid"));
  problems.clear();
  CHECK(config::load("{\"mqtt\":{\"enabled\":true,\"uri\":\"mqtt://192.168.0.180:18883\",\"username\":\"electrical-1\",\"password\":\"x\"},\"web\":{\"mode\":\"https\"}}", s, edited, problems) && edited.mqtt.enabled && edited.web == config::WebMode::kHttps);
  problems.clear();
  CHECK(config::load("{\"system\":{\"auto_restart_hours\":12}}", s, edited, problems) && edited.auto_restart_hours == 12);
  problems.clear();
  CHECK(!config::load("{\"system\":{\"auto_restart_hours\":5}}", s, edited, problems) && has(problems, "system.auto_restart_hours", "invalid"));
}

static void test_wifi_board_has_no_ethernet() {
  config::Settings s = valid_settings();
  s.uplink = config::Uplink::kEthernet;
  CHECK(has(config::validate(s), "uplink", "not_available"));
  config::Settings back;
  config::Problems problems;
  CHECK(!config::load("{\"uplink\":\"ethernet\"}", valid_settings(), back, problems) && has(problems, "uplink", "not_available"));
  CHECK(config::to_json(valid_settings(), false).find("\"uplink\":\"wifi\"") != std::string::npos);
}

int main() {
  test_defaults();
  test_meters();
  test_line();
  test_document();
  test_wifi_board_has_no_ethernet();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
