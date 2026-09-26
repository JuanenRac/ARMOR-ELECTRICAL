// ARMOR-ELECTRICAL - host tests of the Bluetooth configuration channel: the framing, the requests, who may ask what, and the setting that turns it on.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
// The radio (NimBLE) is not here: it builds with the firmware and has not run on a board. Everything a phone could ask, and the rules for answering it, are.
#include <cstdio>
#include <string>
#include <vector>

#include "../core/auth.hpp"
#include "../core/ble_dispatch.hpp"
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
static bool has_problem(const config::Problems& problems, const char* path, const char* code) {
  for (const config::Problem& problem : problems) if (problem.path == path && problem.code == code) return true;
  return false;
}

// ---- Bluetooth configuration channel -------------------------------------------------------------------------------------------

namespace {
// A backend that remembers what it was asked, and holds two users.
class FakeBackend : public ble::Backend {
 public:
  bool users = true;
  std::uint64_t clock = 1000;
  int restarts = 0;
  std::string last_put;
  int put_code = 0;
  bool scan_ok = true;
  auth::UserStore store;
  bool has_users() override { return users; }
  std::string hello_json() override { return R"({"node_id":"armor-a1b2c3","setup":)" + std::string(users ? "false" : "true") + "}"; }
  bool setup_code_ok(std::string_view code) override { return code == "TESTCODE01"; }
  auth::Result add_administrator(std::string_view user, std::string_view password) override {
    const auth::Result r = store.add(user, password, auth::Role::kAdmin, "salt", [](std::string_view p, std::string_view) { return std::string(p); });
    if (r == auth::Result::kOk) users = true;
    return r;
  }
  bool verify(std::string_view user, std::string_view password, auth::Role& role) override {
    return store.verify(user, password, [](std::string_view p, std::string_view) { return std::string(p); }, role);
  }
  std::string config_json() override { return R"({"config":{"node":{"id":"armor-a1b2c3"}},"channel_auto":6})"; }
  int config_put(std::string_view document, std::string& data) override {
    last_put = std::string(document);
    data = put_code == 0 ? R"({"restart_required":true})" : R"({"problems":[{"path":"ap.ssid","code":"required"}]})";
    return put_code;
  }
  std::string status_json() override { return R"({"uptime_s":5})"; }
  bool wifi_scan(std::string& data, std::string& error) override {
    if (!scan_ok) { error = "wifi_busy"; return false; }
    data = R"({"networks":[{"ssid":"home","rssi":-50}]})";
    return true;
  }
  void restart_soon() override { ++restarts; }
  std::uint64_t now_ms() override { return clock; }
};

std::string ask(FakeBackend& backend, ble::Session& session, auth::LoginThrottle& throttle, const std::string& request_text) {
  ble::Request request;
  if (!ble::parse_request(request_text, request)) return "NOT_A_REQUEST";
  return ble::handle(backend, session, throttle, request);
}
}  // namespace

static void test_ble_framing() {
  const std::string message = R"({"id":1,"op":"hello"})";
  const std::string framed = ble::frame(message);
  CHECK(framed.size() == message.size() + 2 && static_cast<unsigned char>(framed[0]) == 0 && static_cast<unsigned char>(framed[1]) == message.size());
  CHECK(ble::frame("").empty() && ble::frame(std::string(ble::kMaxMessage + 1, 'x')).empty() && !ble::frame(std::string(ble::kMaxMessage, 'x')).empty());
  // any way of cutting the stream gives the same message back
  for (std::size_t cut : {1u, 2u, 3u, 7u, 20u, 500u}) {
    ble::Assembler assembler;
    std::string got;
    int messages = 0;
    for (const std::string& piece : ble::chunks_of(framed, cut)) {
      if (assembler.feed(reinterpret_cast<const std::uint8_t*>(piece.data()), piece.size(), got)) ++messages;
    }
    CHECK(messages == 1 && got == message && assembler.pending() == 0);
  }
  CHECK(ble::chunks_of(framed, 0).empty());
  // two messages written back to back come out one by one
  ble::Assembler two;
  const std::string both = framed + ble::frame(R"({"id":2,"op":"status"})");
  std::string first, second;
  CHECK(two.feed(reinterpret_cast<const std::uint8_t*>(both.data()), both.size(), first) && first == message);
  CHECK(two.take(second) && second == R"({"id":2,"op":"status"})" && !two.take(second));
  // a length of zero, or too large, is dropped, and the next message is understood
  ble::Assembler bad;
  std::string out;
  const std::uint8_t zero[2] = {0, 0};
  CHECK(!bad.feed(zero, 2, out) && bad.errors() == 1);
  const std::uint8_t huge[2] = {0xFF, 0xFF};
  CHECK(!bad.feed(huge, 2, out) && bad.errors() == 2);
  CHECK(bad.feed(reinterpret_cast<const std::uint8_t*>(framed.data()), framed.size(), out) && out == message);
}

static void test_ble_requests() {
  ble::Request r;
  CHECK(ble::parse_request(R"({"id":7,"op":"config.get"})", r) && r.id == 7 && r.op == "config.get" && r.args.is_object());
  CHECK(ble::parse_request(R"({"id":8,"op":"login","args":{"user":"a","password":"b"}})", r) && r.args.string_or("user", "") == "a");
  for (const char* bad : {"", "[]", R"({"op":"x"})", R"({"id":1})", R"({"id":-1,"op":"x"})", R"({"id":1.5,"op":"x"})", R"({"id":"1","op":"x"})", R"({"id":1,"op":""})", R"({"id":1,"op":5})"}) CHECK(!ble::parse_request(bad, r));
  CHECK(ble::ok_response(3) == R"({"id":3,"ok":true,"data":{}})");
  CHECK(ble::ok_response(3, R"({"a":1})") == R"({"id":3,"ok":true,"data":{"a":1}})");
  CHECK(ble::error_response(4, "forbidden") == R"({"id":4,"ok":false,"error":"forbidden"})");
  CHECK(ble::error_response(4, "invalid", R"({"problems":[]})") == R"({"id":4,"ok":false,"error":"invalid","data":{"problems":[]}})");
}

static void test_ble_dispatch() {
  FakeBackend backend;
  ble::Session session;
  auth::LoginThrottle throttle;
  // a node with users: hello is open, everything else needs a login
  CHECK(ask(backend, session, throttle, R"({"id":1,"op":"hello"})").find(R"("ok":true)") != std::string::npos);
  CHECK(ask(backend, session, throttle, R"({"id":2,"op":"config.get"})") == R"({"id":2,"ok":false,"error":"unauthorized"})");
  CHECK(ask(backend, session, throttle, R"({"id":3,"op":"setup","args":{"code":"TESTCODE01","user":"admin","password":"long enough pw"}})") == R"({"id":3,"ok":false,"error":"forbidden"})");
  CHECK(backend.store.add("admin", "correct horse", auth::Role::kAdmin, "s", [](std::string_view p, std::string_view) { return std::string(p); }) == auth::Result::kOk);
  CHECK(backend.store.add("viewer", "view only pw", auth::Role::kViewer, "s", [](std::string_view p, std::string_view) { return std::string(p); }) == auth::Result::kOk);
  CHECK(ask(backend, session, throttle, R"({"id":4,"op":"login","args":{"user":"admin","password":"nope"}})") == R"({"id":4,"ok":false,"error":"wrong_credentials"})");
  CHECK(!session.authenticated);
  // a viewer can read but not change
  CHECK(ask(backend, session, throttle, R"({"id":5,"op":"login","args":{"user":"viewer","password":"view only pw"}})") == R"({"id":5,"ok":true,"data":{"role":"viewer"}})");
  CHECK(ask(backend, session, throttle, R"({"id":6,"op":"config.get"})").find("channel_auto") != std::string::npos);
  CHECK(ask(backend, session, throttle, R"({"id":7,"op":"status"})").find("uptime_s") != std::string::npos);
  CHECK(ask(backend, session, throttle, R"({"id":8,"op":"config.put","args":{"config":{"node":{"name":"x"}}}})") == R"({"id":8,"ok":false,"error":"forbidden"})");
  CHECK(ask(backend, session, throttle, R"({"id":9,"op":"wifi.scan"})") == R"({"id":9,"ok":false,"error":"forbidden"})");
  CHECK(ask(backend, session, throttle, R"({"id":10,"op":"reboot"})") == R"({"id":10,"ok":false,"error":"forbidden"})" && backend.restarts == 0);
  // an administrator can
  CHECK(ask(backend, session, throttle, R"({"id":11,"op":"login","args":{"user":"admin","password":"correct horse"}})") == R"({"id":11,"ok":true,"data":{"role":"admin"}})");
  CHECK(ask(backend, session, throttle, R"({"id":12,"op":"config.put","args":{"config":{"node":{"name":"North \"gate\""},"ap":{"enabled":true},"pins":[{"gpio":39,"scale":0.5}]}}})") == R"({"id":12,"ok":true,"data":{"restart_required":true}})");
  json::Value put;
  CHECK(json::parse(backend.last_put, put) && put.get("node")->string_or("name", "") == "North \"gate\"" && put.get("ap")->bool_or("enabled", false) && put.get("pins")->items[0].number_or("scale", 0) == 0.5);
  CHECK(ask(backend, session, throttle, R"({"id":13,"op":"config.put","args":{}})") == R"({"id":13,"ok":false,"error":"invalid"})");
  backend.put_code = 422;
  CHECK(ask(backend, session, throttle, R"({"id":14,"op":"config.put","args":{"config":{"ap":{"enabled":true}}}})").find(R"("error":"invalid","data":{"problems")") != std::string::npos);
  CHECK(ask(backend, session, throttle, R"({"id":15,"op":"wifi.scan"})").find("home") != std::string::npos);
  backend.scan_ok = false;
  CHECK(ask(backend, session, throttle, R"({"id":16,"op":"wifi.scan"})") == R"({"id":16,"ok":false,"error":"wifi_busy"})");
  CHECK(ask(backend, session, throttle, R"({"id":17,"op":"reboot"})") == R"({"id":17,"ok":true,"data":{}})" && backend.restarts == 1);
  CHECK(ask(backend, session, throttle, R"({"id":18,"op":"logout"})") == R"({"id":18,"ok":true,"data":{}})" && !session.authenticated);
  CHECK(ask(backend, session, throttle, R"({"id":19,"op":"config.get"})").find("unauthorized") != std::string::npos);
  CHECK(ask(backend, session, throttle, R"({"id":20,"op":"login","args":{"user":"admin","password":"correct horse"}})").find("ok\":true") != std::string::npos);
  CHECK(ask(backend, session, throttle, R"({"id":21,"op":"frobnicate"})") == R"({"id":21,"ok":false,"error":"unknown_op"})");

  // five wrong passwords lock the channel, whatever the next one is; time lets it through again
  ble::Session other;
  for (int i = 0; i < 5; ++i) ask(backend, other, throttle, R"({"id":30,"op":"login","args":{"user":"admin","password":"wrong"}})");
  const std::string locked = ask(backend, other, throttle, R"({"id":31,"op":"login","args":{"user":"admin","password":"correct horse"}})");
  CHECK(locked.find("too_many_attempts") != std::string::npos && locked.find("wait_s") != std::string::npos && !other.authenticated);
  backend.clock += 31000;
  CHECK(ask(backend, other, throttle, R"({"id":32,"op":"login","args":{"user":"admin","password":"correct horse"}})").find("ok\":true") != std::string::npos);

  // a node with no user: only hello and set-up answer, and the set-up needs the code
  FakeBackend fresh;
  fresh.users = false;
  ble::Session first;
  auth::LoginThrottle fresh_throttle;
  CHECK(ask(fresh, first, fresh_throttle, R"({"id":1,"op":"config.get"})") == R"({"id":1,"ok":false,"error":"setup_required"})");
  CHECK(ask(fresh, first, fresh_throttle, R"({"id":2,"op":"login","args":{"user":"a","password":"b"}})") == R"({"id":2,"ok":false,"error":"setup_required"})");
  CHECK(ask(fresh, first, fresh_throttle, R"({"id":3,"op":"setup","args":{"code":"WRONGCODE1","user":"admin","password":"long enough pw"}})") == R"({"id":3,"ok":false,"error":"wrong_code"})" && !fresh.users);
  CHECK(ask(fresh, first, fresh_throttle, R"({"id":4,"op":"setup","args":{"code":"TESTCODE01","user":"ab","password":"long enough pw"}})") == R"({"id":4,"ok":false,"error":"invalid_name"})");
  CHECK(ask(fresh, first, fresh_throttle, R"({"id":5,"op":"setup","args":{"code":"TESTCODE01","user":"admin","password":"short"}})") == R"({"id":5,"ok":false,"error":"weak_password"})");
  CHECK(ask(fresh, first, fresh_throttle, R"({"id":6,"op":"setup","args":{"code":"TESTCODE01","user":"admin","password":"long enough pw"}})") == R"({"id":6,"ok":true,"data":{"restart_required":true}})");
  CHECK(fresh.users && fresh.restarts == 0 && first.authenticated && first.role == auth::Role::kAdmin);
  // the code is throttled too
  FakeBackend guessing;
  guessing.users = false;
  ble::Session guess;
  auth::LoginThrottle guess_throttle;
  for (int i = 0; i < 5; ++i) ask(guessing, guess, guess_throttle, R"({"id":1,"op":"setup","args":{"code":"NOPENOPE01","user":"admin","password":"long enough pw"}})");
  CHECK(ask(guessing, guess, guess_throttle, R"({"id":2,"op":"setup","args":{"code":"TESTCODE01","user":"admin","password":"long enough pw"}})").find("too_many_attempts") != std::string::npos && !guessing.users);
}

static void test_ble_setting() {
  config::Settings s = valid_settings();
  CHECK(s.ble == config::BleMode::kSetup);
  config::Settings out;
  config::Problems problems;
  CHECK(config::load(R"({"ble":{"mode":"always"}})", s, out, problems) && out.ble == config::BleMode::kAlways);
  problems.clear();
  CHECK(config::load(R"({"ble":{"mode":"off"}})", s, out, problems) && out.ble == config::BleMode::kOff && config::to_json(out, true).find(R"("ble":{"mode":"off"})") != std::string::npos);
  problems.clear();
  CHECK(!config::load(R"({"ble":{"mode":"loud"}})", s, out, problems) && has_problem(problems, "ble.mode", "invalid"));
}

int main() {
  test_ble_framing();
  test_ble_requests();
  test_ble_dispatch();
  test_ble_setting();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
