// ARMOR-ELECTRICAL - host tests of the clock settings (time zone, time server), the calendar arithmetic behind the offset and the backup lists keeping their passwords.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include <string>

#include "../core/electrical_config.hpp"
#include "../core/civil_time.hpp"
#include <cstdio>
using namespace armor;
static int fails = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++fails; } } while (0)
static void show(const char* what, const config::Problems& problems) {
  for (const auto& p : problems) std::printf("  [%s] problem %s %s\n", what, p.path.c_str(), p.code.c_str());
}
int main() {
  config::Settings base = config::default_settings("a1b2c3");
  base.sta.enabled = true; base.sta.ssid = "home"; base.sta.password = "home-password";
  config::Settings out;
  config::Problems problems;
  CHECK(base.time.ntp_enabled && base.time.zone == "UTC0" && base.time.ntp == "pool.ntp.org");
  config::Settings s = base;
  s.time.zone = "CET-1CEST,M3.5.0,M10.5.0/3"; s.time.ntp = "time.example.org"; s.time.ntp_enabled = false;
  s.sta.backup = {{"guest", "guest-password"}};
  s.mqtt.backup = {{"mqtt://10.0.0.5:1883", "u", "p"}, {"mqtt://10.0.0.6:1883", "u2", "p2"}};
  const std::string text = config::to_json(s, true);
  config::Settings back;
  const bool loaded = config::load(text, base, back, problems);
  show("roundtrip", problems);
  CHECK(loaded);
  CHECK(back.time.zone == s.time.zone && back.time.ntp == "time.example.org" && !back.time.ntp_enabled);
  CHECK(text.find("\"time\":{") != std::string::npos);
  CHECK(text.find("\"mqtt\":{") != std::string::npos);
  problems.clear();
  const bool inherited = config::load(R"({"sta":{"backup":[{"ssid":"guest"}]},"mqtt":{"backup":[{"uri":"mqtt://10.0.0.5:1883","username":"u"},{"uri":"mqtt://10.0.0.6:1883","username":"u2"}]}})", s, out, problems);
  show("inherit", problems);
  CHECK(inherited && out.sta.backup[0].password == "guest-password" && out.mqtt.backup[0].password == "p" && out.mqtt.backup[1].password == "p2");
  problems.clear();
  const bool other = config::load(R"({"sta":{"backup":[{"ssid":"other"}]}})", s, out, problems);
  show("other", problems);
  CHECK(other && out.sta.backup[0].password.empty());
  problems.clear();
  CHECK(!config::load(R"({"time":{"zone":"Europe/Madrid; rm"}})", base, out, problems));
  problems.clear();
  CHECK(!config::load(R"({"time":{"ntp_enabled":true,"ntp":"not a host"}})", base, out, problems));
  problems.clear();
  const bool legacy = config::load(R"({"mqtt":{"ntp":"ntp.old.example"}})", base, out, problems);
  show("legacy", problems);
  CHECK(legacy && out.time.ntp == "ntp.old.example");
  CHECK(civil::days_from_civil(1970, 1, 1) == 0 && civil::days_from_civil(2000, 3, 1) == 11017);
  std::printf(fails ? "FAILED\n" : "ok\n");
  return fails;
}
