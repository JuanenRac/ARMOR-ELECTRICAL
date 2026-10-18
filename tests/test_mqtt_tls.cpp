// ARMOR-ELECTRICAL - host tests of the optional encryption of the link to the broker: what a stored authority may look like, what the settings must say when it is on, and that a document keeps it.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
// The certificate below was made with OpenSSL for these tests (a throw-away authority); no broker was needed.
#include <cstdio>
#include <string>

#if __has_include("../core/electrical_config.hpp")
#include "../core/electrical_config.hpp"
#else
#include "../main/core/node_config.hpp"
#endif

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

static const char* const kCa =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBmDCCAT+gAwIBAgIUQLkEyBVuP1RKaBqpjdJScIZtEkMwCgYIKoZIzj0EAwIw\n"
    "IjEgMB4GA1UEAwwXRXhhbXBsZSBpbnN0YWxsYXRpb24gQ0EwHhcNMjYxMDE3MjMw\n"
    "NTU3WhcNMzYxMDE0MjMwNTU3WjAiMSAwHgYDVQQDDBdFeGFtcGxlIGluc3RhbGxh\n"
    "dGlvbiBDQTBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABI2Zk0WCkqAxPovCcnEx\n"
    "KW/OIV8bWBsmP23yDLQGJ0JI/aA/Wg12yMKcoew7IwK8kSGkj/3ZmA1ymJPDq9Vo\n"
    "eeajUzBRMB0GA1UdDgQWBBQR5ub2h9ZOgyk3tyQnHpo85XlCtTAfBgNVHSMEGDAW\n"
    "gBQR5ub2h9ZOgyk3tyQnHpo85XlCtTAPBgNVHRMBAf8EBTADAQH/MAoGCCqGSM49\n"
    "BAMCA0cAMEQCIGp2K/KoxeEg3hnMjZHx3IHz1KXQwxQv67wIB6M3wbpgAiBvufpc\n"
    "illdWlnY9mNsSIiPi5QM1j/9ojJ+r9o/Wo8sOA==\n"
    "-----END CERTIFICATE-----\n";

static bool has(const config::Problems& problems, const std::string& path, const std::string& code) {
  for (const auto& problem : problems) if (problem.path == path && problem.code == code) return true;
  return false;
}
static bool has_path(const config::Problems& problems, const std::string& path) {
  for (const auto& problem : problems) if (problem.path == path) return true;
  return false;
}
// The text with its line ends written the way JSON writes them.
static std::string escaped(const std::string& text) {
  std::string out;
  for (const char c : text) { if (c == '\n') out += "\\n"; else if (c == '\r') out += "\\r"; else out.push_back(c); }
  return out;
}
static std::string with_crlf(const std::string& text) {
  std::string out;
  for (const char c : text) { if (c == '\n') out += "\r\n"; else out.push_back(c); }
  return out;
}
// What a save that carries only these members of "mqtt" does to `base`.
static config::Settings loaded(const std::string& mqtt_members, config::Problems& problems, const config::Settings& base) {
  config::Settings out = base;
  config::load("{\"mqtt\":{" + mqtt_members + "}}", base, out, problems);
  return out;
}

static void test_pem_shape() {
  using mqtt_tls::ca_pem_is_valid;
  CHECK(ca_pem_is_valid(kCa));
  CHECK(ca_pem_is_valid(std::string("\n  ") + kCa + "\n\n"));                 // spaces around it do not matter
  CHECK(ca_pem_is_valid(with_crlf(kCa)) && mqtt_tls::normalize(with_crlf(kCa)) == kCa);   // a browser may send CRLF; it is stored with LF
  CHECK(mqtt_tls::normalize("").empty() && mqtt_tls::normalize(" \n ").empty());
  CHECK(!ca_pem_is_valid(""));
  CHECK(!ca_pem_is_valid("hello"));
  CHECK(!ca_pem_is_valid(std::string("-----BEGIN PRIV") + "ATE KEY-----\nMIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgabcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ==\n-----END PRIVATE KEY-----\n"));
  CHECK(!ca_pem_is_valid(std::string(kCa) + kCa));                           // one authority, not a chain: only that one is trusted
  CHECK(!ca_pem_is_valid(std::string("note\n") + kCa));                      // nothing around it
  CHECK(!ca_pem_is_valid(std::string(kCa) + "note\n"));
  std::string cut = kCa;
  cut.erase(cut.find("-----END"));
  CHECK(!ca_pem_is_valid(cut));                                              // no END line
  std::string no_begin = kCa;
  no_begin.replace(0, 11, "-----BEGON ");
  CHECK(!ca_pem_is_valid(no_begin));
  std::string bad_char = kCa;
  bad_char[40] = '!';
  CHECK(!ca_pem_is_valid(bad_char));                                         // not Base64
  std::string bad_length = kCa;
  bad_length.erase(90, 1);
  CHECK(!ca_pem_is_valid(bad_length));                                       // Base64 whose length is not a multiple of four
  std::string pad_inside = kCa;
  pad_inside[100] = '=';
  CHECK(!ca_pem_is_valid(pad_inside));                                       // padding in the middle
  CHECK(!ca_pem_is_valid(std::string("-----BEGIN CERTIFICATE-----\n") + std::string(2100, 'A') + "\n-----END CERTIFICATE-----\n"));   // longer than the room
  CHECK(!ca_pem_is_valid("-----BEGIN CERTIFICATE-----\nAAAA\n-----END CERTIFICATE-----\n"));   // far too short to be a certificate
  CHECK(!ca_pem_is_valid("-----BEGIN CERTIFICATE-----A-----END CERTIFICATE-----"));         // the lines are lines of their own
  CHECK(mqtt_tls::uri_is_tls("mqtts://192.168.0.180:8883") && !mqtt_tls::uri_is_tls("mqtt://192.168.0.180:18883") && !mqtt_tls::uri_is_tls(""));
}

static void test_defaults_are_off() {
  const config::Settings s = config::default_settings("a1b2c3");
  CHECK(!s.mqtt.tls && s.mqtt.ca_pem.empty());
  CHECK(!has_path(config::validate(s), "mqtt.ca_pem") && !has_path(config::validate(s), "mqtt.tls"));
  const std::string shown = config::to_json(s, false);
  CHECK(shown.find("\"tls\":false") != std::string::npos && shown.find("\"ca_pem\":\"\"") != std::string::npos);
}

static void test_round_trip() {
  config::Settings s = config::default_settings("a1b2c3");
  s.mqtt.enabled = true; s.mqtt.uri = "mqtts://192.168.0.180:8883"; s.mqtt.tls = true; s.mqtt.ca_pem = kCa;
  for (const bool secrets : {true, false}) {
    const std::string text = config::to_json(s, secrets);
    CHECK(text.find("\"tls\":true") != std::string::npos);
    CHECK(text.find(escaped(kCa)) != std::string::npos);                    // the line ends travel escaped
    config::Settings back = config::default_settings("a1b2c3");
    config::Problems problems;
    config::load(text, config::default_settings("a1b2c3"), back, problems);
    CHECK(!has_path(problems, "mqtt.ca_pem") && !has_path(problems, "mqtt.tls"));
    CHECK(back.mqtt.tls && back.mqtt.ca_pem == kCa && back.mqtt.uri == s.mqtt.uri);
  }
  // an older document has neither member: the encryption stays off; and a save that does not mention them leaves a node that had it on as it was
  config::Problems problems;
  const config::Settings older = loaded("\"enabled\":true,\"uri\":\"mqtt://192.168.0.180:18883\"", problems, config::default_settings("a1b2c3"));
  CHECK(!older.mqtt.tls && older.mqtt.ca_pem.empty() && !has_path(problems, "mqtt.tls"));
  config::Problems again;
  const config::Settings kept = loaded("\"heartbeat_s\":20", again, s);
  CHECK(kept.mqtt.tls && kept.mqtt.ca_pem == kCa && kept.mqtt.heartbeat_s == 20);
  // CRLF from a browser is stored with LF
  config::Problems p3;
  const config::Settings from_browser = loaded("\"tls\":true,\"ca_pem\":\"" + escaped(with_crlf(kCa)) + "\"", p3, config::default_settings("a1b2c3"));
  CHECK(from_browser.mqtt.ca_pem == kCa && !has_path(p3, "mqtt.ca_pem"));
  // an empty text clears it
  config::Problems p4;
  const config::Settings cleared = loaded("\"tls\":false,\"ca_pem\":\"\"", p4, s);
  CHECK(!cleared.mqtt.tls && cleared.mqtt.ca_pem.empty());
}

static void test_refusals() {
  config::Settings s = config::default_settings("a1b2c3");
  s.mqtt.enabled = true; s.mqtt.uri = "mqtts://192.168.0.180:8883"; s.mqtt.tls = true;
  // on, with no authority
  CHECK(has(config::validate(s), "mqtt.ca_pem", "required"));
  // on, with something that is not an authority
  s.mqtt.ca_pem = "hello";
  CHECK(has(config::validate(s), "mqtt.ca_pem", "bad_pem"));
  s.mqtt.ca_pem = std::string(kCa) + kCa;
  CHECK(has(config::validate(s), "mqtt.ca_pem", "bad_pem"));
  // a text that is not PEM is refused even with the encryption off
  s.mqtt.tls = false; s.mqtt.ca_pem = "hello";
  CHECK(has(config::validate(s), "mqtt.ca_pem", "bad_pem"));
  // on, with the right authority, but a plain address
  s.mqtt.tls = true; s.mqtt.ca_pem = kCa; s.mqtt.uri = "mqtt://192.168.0.180:18883";
  CHECK(has(config::validate(s), "mqtt.uri", "need_mqtts") && !has_path(config::validate(s), "mqtt.ca_pem"));
  // the saved brokers follow the same rule
  s.mqtt.uri = "mqtts://192.168.0.180:8883";
  s.mqtt.backup.push_back({"mqtt://192.168.0.181:18883", "u", "p"});
  CHECK(has(config::validate(s), "mqtt.backup.0.uri", "need_mqtts"));
  s.mqtt.backup[0].uri = "mqtts://192.168.0.181:8883";
  CHECK(!has_path(config::validate(s), "mqtt.backup.0.uri") && !has_path(config::validate(s), "mqtt.uri") && !has_path(config::validate(s), "mqtt.ca_pem"));
  // with the broker off the address is not required to be anything
  s.mqtt.enabled = false; s.mqtt.uri = "mqtt://192.168.0.180:18883";
  CHECK(!has(config::validate(s), "mqtt.uri", "need_mqtts"));
  // a text longer than the limit is refused on reading and the stored authority is left as it was
  config::Settings with_ca = config::default_settings("a1b2c3");
  with_ca.mqtt.ca_pem = kCa;
  config::Problems problems;
  const config::Settings after = loaded("\"ca_pem\":\"" + std::string(2100, 'A') + "\"", problems, with_ca);
  CHECK(has(problems, "mqtt.ca_pem", "too_long") && after.mqtt.ca_pem == kCa);
  // a switch that is not a switch
  config::Problems p2;
  loaded("\"tls\":\"yes\"", p2, with_ca);
  CHECK(has(p2, "mqtt.tls", "invalid"));
  // a document that carries a text that is not an authority does not become valid settings
  config::Problems p3;
  const config::Settings bad = loaded("\"enabled\":true,\"uri\":\"mqtts://192.168.0.180:8883\",\"tls\":true,\"ca_pem\":\"-----BEGIN CERTIFICATE-----\\nAAAA\\n-----END CERTIFICATE-----\\n\"", p3, config::default_settings("a1b2c3"));
  CHECK(has(config::validate(bad), "mqtt.ca_pem", "bad_pem"));
}

int main() {
  test_pem_shape();
  test_defaults_are_off();
  test_round_trip();
  test_refusals();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
