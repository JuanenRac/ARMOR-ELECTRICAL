// ARMOR-ELECTRICAL - host tests of the rules every node shares and that guard its panel: the user store, sessions and the throttle on wrong passwords, the strict JSON reader and writer, the checks on
// what an operator types for the network, the node identifier, the topic filters of the broker and (where the node has them) the web policy and the layout of the Wi-Fi.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
// Nothing here touches hardware or a network: the password hash is a stand-in and the clock is a number the test moves.
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#if __has_include("../core/auth.hpp")
#include "../core/auth.hpp"
#include "../core/json.hpp"
#include "../core/net_text.hpp"
#include "../core/node_id.hpp"
#else
#include "../main/core/auth.hpp"
#include "../main/core/json.hpp"
#include "../main/core/net_text.hpp"
#include "../main/core/node_id.hpp"
#endif
// the web policy, the topic filters and the layout of the Wi-Fi are the shared copies only where a node has them (the radar node keeps its own)
#if __has_include("../core/netplan.hpp") && __has_include("../core/web_policy.hpp") && __has_include("../core/mqtt_topic.hpp")
#define SHARED_NETWORK_RULES 1
#include "../core/mqtt_topic.hpp"
#include "../core/netplan.hpp"
#include "../core/web_policy.hpp"
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

static bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// ---- what the operator types for the network -------------------------------------------------------------------------------------------

static void test_ipv4() {
  std::uint32_t value = 0;
  CHECK(net::parse_ipv4("192.168.0.50", value) && value == 0xC0A80032u);
  CHECK(net::parse_ipv4("0.0.0.0", value) && value == 0 && net::parse_ipv4("255.255.255.255", value) && value == 0xFFFFFFFFu);
  const char* bad[] = {"", "1.2.3", "1.2.3.4.5", "1.2.3.", ".1.2.3", "1..2.3", "256.1.1.1", "1.2.3.256", "010.1.1.1", "1.2.3.04", "1.2.3.4 ", " 1.2.3.4", "1.2.3.a", "1,2,3,4", "-1.2.3.4",
                       "1.2.3.1000", "0x1.2.3.4", "1.2.3.4/24", "1.2.3.4:80"};
  for (const char* text : bad) CHECK(!net::parse_ipv4(text, value));
  CHECK(net::parse_ipv4("0.1.2.3", value));   // a single zero is a number; only a zero followed by digits is refused
  // a failed parse leaves the caller's value alone
  value = 77;
  CHECK(!net::parse_ipv4("1.2.3.999", value) && value == 77);
  // text and back, for the corners of the range
  for (const std::uint32_t original : {0u, 1u, 0x7F000001u, 0xC0A80001u, 0xFFFFFFFFu, 0x0A000102u}) {
    std::uint32_t again = 1;
    CHECK(net::parse_ipv4(net::ipv4_text(original), again) && again == original);
  }
  CHECK(net::ipv4_text(0x0A000102u) == "10.0.1.2");
}

static void test_masks_and_addresses() {
  std::uint32_t mask = 0;
  CHECK(net::parse_ipv4("255.255.255.0", mask) && net::valid_netmask(mask));
  CHECK(net::valid_netmask(0xFFFFFFFCu) && net::valid_netmask(0x80000000u) && net::valid_netmask(0xFFFF0000u));   // /30 ... /1
  CHECK(!net::valid_netmask(0) && !net::valid_netmask(0xFFFFFFFFu) && !net::valid_netmask(0xFFFFFFFEu));          // /0, /32 and /31 leave no room for a host and a gateway
  CHECK(!net::valid_netmask(0xFF00FF00u) && !net::valid_netmask(0x00FFFFFFu) && !net::valid_netmask(0xFFFFFF01u));   // holes in the run of ones
  // every prefix from /1 to /30 is a good netmask and every other single-bit change of one is not
  for (int prefix = 1; prefix <= 30; ++prefix) {
    const std::uint32_t good = ~std::uint32_t{0} << (32 - prefix);
    CHECK(net::valid_netmask(good));
    if (prefix < 30) CHECK(!net::valid_netmask(good ^ (1u << (31 - prefix - 1))));
  }
  CHECK(net::usable_host_address(0x0A000001u) && net::usable_host_address(0xDFFFFFFFu) && net::usable_host_address(0x01000000u));
  CHECK(!net::usable_host_address(0) && !net::usable_host_address(0x00FFFFFFu) && !net::usable_host_address(0x7F000001u) && !net::usable_host_address(0xE0000001u) && !net::usable_host_address(0xFFFFFFFFu));
  const std::uint32_t net24 = 0xC0A80000u, host = 0xC0A80032u, other = 0xC0A90032u;
  CHECK(net::same_subnet(net24 | 5, host, 0xFFFFFF00u) && !net::same_subnet(other, host, 0xFFFFFF00u) && net::same_subnet(other, host, 0xFFFE0000u));
  CHECK(net::is_network_or_broadcast(0xC0A80000u, 0xFFFFFF00u) && net::is_network_or_broadcast(0xC0A800FFu, 0xFFFFFF00u) && !net::is_network_or_broadcast(host, 0xFFFFFF00u));
}

static void test_names() {
  CHECK(net::valid_hostname("armor-1") && net::valid_hostname("a") && net::valid_hostname(std::string(32, 'a')) && net::valid_hostname("a-b-c"));
  CHECK(!net::valid_hostname("") && !net::valid_hostname(std::string(33, 'a')) && !net::valid_hostname("-a") && !net::valid_hostname("a-") && !net::valid_hostname("A") && !net::valid_hostname("a_b") &&
        !net::valid_hostname("a.b") && !net::valid_hostname("a b") && !net::valid_hostname("\xC3\xA9"));
  CHECK(net::valid_host("broker.local") && net::valid_host("192.168.0.2") && net::valid_host("My_Host-1") && net::valid_host(std::string(253, 'a')));
  CHECK(!net::valid_host("") && !net::valid_host(std::string(254, 'a')) && !net::valid_host(".a") && !net::valid_host("-a") && !net::valid_host("a.") && !net::valid_host("a b") && !net::valid_host("a/b") &&
        !net::valid_host("a:1") && !net::valid_host("a@b") && !net::valid_host("a\n"));
  CHECK(net::valid_ssid("casa") && net::valid_ssid(std::string(32, 'x')) && net::valid_ssid("caf\xC3\xA9 wifi") && net::valid_ssid(" "));
  CHECK(!net::valid_ssid("") && !net::valid_ssid(std::string(33, 'x')) && !net::valid_ssid("a\nb") && !net::valid_ssid(std::string("a\0b", 3)) && !net::valid_ssid("a\x7f"));
  CHECK(net::valid_wpa_passphrase("12345678") && net::valid_wpa_passphrase(std::string(63, 'k')) && net::valid_wpa_passphrase("with spaces ok"));
  CHECK(!net::valid_wpa_passphrase("1234567") && !net::valid_wpa_passphrase(std::string(64, 'k')) && !net::valid_wpa_passphrase("") && !net::valid_wpa_passphrase("clave\xC3\xB1" "aaa") &&
        !net::valid_wpa_passphrase("tab\tinside!") && !net::valid_wpa_passphrase("del\x7f" "inside!"));
}

static void test_utf8() {
  std::size_t characters = 99;
  CHECK(net::valid_utf8("", &characters) && characters == 0);
  CHECK(net::valid_utf8("abc", &characters) && characters == 3);
  CHECK(net::valid_utf8("caf\xC3\xA9", &characters) && characters == 4);                   // 2 bytes
  CHECK(net::valid_utf8("\xE2\x82\xAC", &characters) && characters == 1);                  // 3 bytes: the euro sign
  CHECK(net::valid_utf8("\xF0\x9F\x98\x80", &characters) && characters == 1);              // 4 bytes: an emoji
  CHECK(net::valid_utf8("\xF4\x8F\xBF\xBF", &characters) && characters == 1);              // U+10FFFF, the last one
  CHECK(net::valid_utf8("\xC2\x80\xDF\xBF\xE0\xA0\x80\xEF\xBF\xBF", &characters) && characters == 4);   // the first and last of each length
  CHECK(net::valid_utf8("\xED\x9F\xBF", &characters));                                      // U+D7FF, just under the surrogates
  CHECK(net::valid_utf8("\xEE\x80\x80", &characters));                                      // U+E000, just over them
  const char* bad[] = {"\x80", "\xBF", "\xC0\x80", "\xC1\xBF", "\xE0\x80\x80", "\xE0\x9F\xBF", "\xF0\x80\x80\x80", "\xF0\x8F\xBF\xBF",   // continuation bytes alone, overlong forms
                       "\xED\xA0\x80", "\xED\xBF\xBF",                                                                                // surrogates
                       "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xF8\x88\x80\x80\x80", "\xFF", "\xFE",                                 // beyond U+10FFFF, the old 5 and 6 byte forms
                       "\xC3", "\xE2\x82", "\xF0\x9F\x98", "\xC3\x28", "\xE2\x28\xA1", "\xE2\x82\x28", "a\xC3"};                       // cut short or with a bad continuation
  for (const char* text : bad) CHECK(!net::valid_utf8(text));
  CHECK(!net::valid_utf8(std::string("\xC3", 1)));
}

static void test_node_id() {
  CHECK(node_id_is_valid("radar-1") && node_id_is_valid("a") && node_id_is_valid("0") && node_id_is_valid("a_b-c") && node_id_is_valid(std::string(64, 'a')));
  CHECK(!node_id_is_valid("") && !node_id_is_valid(std::string(65, 'a')) && !node_id_is_valid("-a") && !node_id_is_valid("_a") && !node_id_is_valid("A") && !node_id_is_valid("a b") &&
        !node_id_is_valid("a.b") && !node_id_is_valid("a/b") && !node_id_is_valid("a+") && !node_id_is_valid("\xC3\xA9"));
  static_assert(node_id_is_valid("x1") && !node_id_is_valid(""), "the rule is usable at compile time");
}

// ---- the strict JSON reader and writer -------------------------------------------------------------------------------------------------

static bool parses(const std::string& text) {
  json::Value value;
  return json::parse(text, value);
}

static void test_json_grammar() {
  CHECK(parses("{}") && parses("[]") && parses("  {\"a\" : [ 1 , 2 ] }\n") && parses("true") && parses("false") && parses("null") && parses("\"x\"") && parses("0") && parses("-0") && parses("[[],{}]"));
  const char* bad[] = {"", " ", "{", "}", "[", "]", "{\"a\"}", "{\"a\":}", "{\"a\":1,}", "[1,]", "[,1]", "[1 2]", "{\"a\":1 \"b\":2}", "{a:1}", "{'a':1}", "{\"a\":1}x", "[]]", "{} {}", "tru", "nul", "truee",
                       "nullx", "True", "NaN", "Infinity", "-Infinity", "+1", "01", "-01", "1.", ".5", "-", "1e", "1e+", "1.e3", "0x10", "1e999", "-1e999", "\"abc", "\"a\nb\"", "\"a\tb\"", "\"\\x41\"",
                       "\"\\u12\"", "\"\\u12G4\"", "\"\\", "// c\n1", "/* c */1", "[1,2,3", "{\"a\":[1,2}", "{\"a\":1}}"};
  for (const char* text : bad) { CHECK(!parses(text)); }
  CHECK(!parses(std::string("\"a\0b\"", 5)));   // a raw control character, even a NUL, is refused inside a string
}

static void test_json_limits() {
  auto nested = [](int levels) { return std::string(static_cast<std::size_t>(levels), '[') + std::string(static_cast<std::size_t>(levels), ']'); };
  CHECK(parses(nested(1)) && parses(nested(10)));
  CHECK(parses(nested(13)) && !parses(nested(14)) && !parses(nested(1000)));   // depth is bounded, so a hostile document cannot exhaust the stack
  std::string deep_object;
  for (int i = 0; i < 20; ++i) deep_object += "{\"a\":";
  deep_object += "1";
  deep_object += std::string(20, '}');
  CHECK(!parses(deep_object));
  // the text is bounded too
  const std::string filler(json::kMaxText - 10, 'x');
  CHECK(parses("\"" + filler + "\""));
  CHECK(!parses("\"" + std::string(json::kMaxText, 'x') + "\""));
  CHECK(!parses(std::string(json::kMaxText + 1, ' ') + "1"));
}

static void test_json_values() {
  json::Value v;
  CHECK(json::parse("{\"s\":\"a\\u00e9\\n\\\"\\\\\\/\",\"n\":-12.5e1,\"b\":true,\"z\":null,\"l\":[1,\"x\"],\"o\":{\"k\":2}}", v) && v.is_object());
  CHECK(v.get("s") != nullptr && v.get("s")->text == "a\xC3\xA9\n\"\\/");
  CHECK(v.get("n")->number == -125.0 && v.get("b")->boolean && v.get("z")->type == json::Type::kNull && v.get("l")->items.size() == 2 && v.get("o")->get("k")->number == 2);
  CHECK(v.get("missing") == nullptr && v.get("l")->get("x") == nullptr && v.items[0].get("x") == nullptr);   // get() on a non-object is nullptr, never a crash
  // escapes: a surrogate pair is one character beyond the first plane; a lone half is refused
  CHECK(json::parse("\"\\ud83d\\ude00\"", v) && v.text == "\xF0\x9F\x98\x80");
  CHECK(json::parse("\"\\u20ac\\u0041\"", v) && v.text == "\xE2\x82\xAC" "A");
  CHECK(json::parse("\"\\u0000\"", v) && v.text.size() == 1 && v.text[0] == '\0');
  CHECK(!parses("\"\\ud83d\"") && !parses("\"\\ud83dx\"") && !parses("\"\\ude00\"") && !parses("\"\\ud83d\\u0041\"") && !parses("\"\\ud83d\\ud83d\""));
  // typed readers fall back instead of guessing
  CHECK(json::parse("{\"i\":7,\"f\":1.5,\"big\":1e12,\"neg\":-3,\"s\":\"7\",\"t\":true}", v));
  CHECK(v.integer_or("i", -1, 0, 10) == 7 && v.integer_or("f", -1, 0, 10) == -1 && v.integer_or("big", -1, 0, 10) == -1 && v.integer_or("neg", 99, 0, 10) == 99 && v.integer_or("neg", 99, -5, 10) == -3);
  CHECK(v.integer_or("s", -1, 0, 10) == -1 && v.integer_or("none", 5, 0, 10) == 5 && v.integer_or("i", -1, 7, 7) == 7 && v.integer_or("i", -1, 8, 9) == -1);
  CHECK(v.string_or("s", "d") == "7" && v.string_or("i", "d") == "d" && v.string_or("none", "d") == "d");
  CHECK(v.number_or("f", 0) == 1.5 && v.number_or("s", 4) == 4 && v.bool_or("t", false) && !v.bool_or("i", false) && v.bool_or("none", true));
  // the first of a repeated name can never be told apart from the last: a repeat is refused wherever it is
  CHECK(!parses("{\"a\":1,\"b\":2,\"a\":3}") && parses("{\"a\":{\"a\":1},\"b\":{\"a\":1}}"));
}

static void test_json_writer() {
  CHECK(json::quote("plain") == "\"plain\"" && json::quote("") == "\"\"");
  CHECK(json::quote("a\"b\\c\n\r\t") == "\"a\\\"b\\\\c\\n\\r\\t\"");
  CHECK(json::quote(std::string("\x01\x1f", 2)) == "\"\\u0001\\u001f\"");
  CHECK(json::quote("caf\xC3\xA9") == "\"caf\xC3\xA9\"");   // text beyond ASCII goes through as it is
  // whatever is written is read back the same
  std::string every;
  for (int c = 1; c < 128; ++c) every += static_cast<char>(c);
  every += "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
  json::Value back;
  CHECK(json::parse(json::quote(every), back) && back.text == every);
  CHECK(json::parse(json::quote(std::string("a\0b", 3)), back) && back.text == std::string("a\0b", 3));
  json::Writer w;
  w.begin_object().field("s", "x").field("i", 5).field("l", 9000000000LL).field("u", 7u).field("b", false).field("d", 0.5).key("n").null().key("a").begin_array().integer(1).string("two").boolean(true).begin_object().end_object().end_array().key("o").begin_object().field("k", "v").end_object().end_object();
  CHECK(w.str() == "{\"s\":\"x\",\"i\":5,\"l\":9000000000,\"u\":7,\"b\":false,\"d\":0.5,\"n\":null,\"a\":[1,\"two\",true,{}],\"o\":{\"k\":\"v\"}}");
  CHECK(json::parse(w.str(), back) && back.is_object());
  json::Writer numbers;
  numbers.begin_array().number(0.1).number(-2.5).number(100.0).number(3.14159, 2).number(2.0, 3).number(1.0 / 0.0).number(0.0 / 0.0).number(123456.789).end_array();
  CHECK(numbers.str() == "[0.1,-2.5,100,3.14,2.000,null,null,123456.789]");
  json::Writer raw;
  raw.begin_object().key("pre").raw("{\"x\":1}").field("after", 1).end_object();
  CHECK(raw.str() == "{\"pre\":{\"x\":1},\"after\":1}");
  json::Writer empty;
  empty.begin_array().end_array();
  CHECK(empty.str() == "[]");
}

// ---- users, sessions and the throttle ---------------------------------------------------------------------------------------------------

static int hash_calls = 0;
static const auth::Hasher kHasher = [](std::string_view password, std::string_view salt) {
  ++hash_calls;
  return std::string("h(") + std::string(salt) + "|" + std::string(password) + ")";   // a stand-in: the same inputs always give the same output
};

static void test_credentials_rules() {
  using namespace auth;
  CHECK(valid_user_name("abc") && valid_user_name("a.b-c_d9") && valid_user_name(std::string(32, 'a')));
  CHECK(!valid_user_name("ab") && !valid_user_name("") && !valid_user_name(std::string(33, 'a')) && !valid_user_name("Admin") && !valid_user_name("a b") && !valid_user_name("a@b") && !valid_user_name("\xC3\xA9\xC3\xA9"));
  CHECK(valid_password("12345678") && valid_password(std::string(64, 'p')) && valid_password("with space") && valid_password("clave\xC3\xB1" "aaa"));
  CHECK(!valid_password("1234567") && !valid_password(std::string(65, 'p')) && !valid_password("") && !valid_password("tab\tinside") && !valid_password("nl\ninside!") && !valid_password("del\x7f" "inside"));
  CHECK(user_key("Admin") == user_key("admin") && user_key("admin") != user_key("admin2") && user_key("a") != user_key("b") && user_key("") != 0);
  CHECK(same_text("abc", "abc") && same_text("", "") && !same_text("abc", "abd") && !same_text("abc", "ab") && !same_text("ab", "abc") && !same_text("", "a"));
  const std::uint8_t bytes[] = {0x00, 0x0f, 0xa5, 0xff};
  CHECK(to_hex(bytes, 4) == "000fa5ff" && to_hex(bytes, 0).empty());
  CHECK(to_text(Role::kAdmin) == std::string("admin") && to_text(Role::kViewer) == std::string("viewer"));
}

static void test_setup_code_and_passkey() {
  using namespace auth;
  std::uint8_t all[256];
  for (int i = 0; i < 256; ++i) all[i] = static_cast<std::uint8_t>(i);
  const std::string code = setup_code_from(all, 256);
  CHECK(code.size() == 256);
  for (const char c : code) CHECK(((c >= 'A' && c <= 'Z') || (c >= '2' && c <= '9')) && c != 'I' && c != 'L' && c != 'O');   // nothing that looks like another character
  CHECK(setup_code_from(all, 0).empty() && setup_code_from(all, 8) == setup_code_from(all, 8));
  // the Bluetooth passkey: always six digits' worth, the same for the same code, another for another one
  for (const char* c : {"", "A", "ABCD2345", "ZZZZZZZZ"}) CHECK(ble_passkey_from(c) < 1000000u && ble_passkey_from(c) == ble_passkey_from(c));
  CHECK(ble_passkey_from("ABCD2345") != ble_passkey_from("ABCD2346") && ble_passkey_from("ABCD2345") != ble_passkey_from("abcd2345"));
}

static void test_user_store() {
  using namespace auth;
  UserStore store;
  CHECK(store.empty() && store.admins() == 0 && store.find("root") == nullptr);
  CHECK(store.add("ab", "longenough", Role::kAdmin, "01", kHasher) == Result::kInvalidName);
  CHECK(store.add("root", "short", Role::kAdmin, "01", kHasher) == Result::kWeakPassword);
  CHECK(store.empty());   // a refusal changes nothing
  CHECK(store.add("root", "longenough", Role::kAdmin, "01", kHasher) == Result::kOk && !store.empty() && store.admins() == 1);
  CHECK(store.add("root", "another-one", Role::kViewer, "02", kHasher) == Result::kExists);
  CHECK(store.find("root") != nullptr && store.find("root")->salt == "01" && store.find("root")->hash == "h(01|longenough)");
  Role role = Role::kViewer;
  CHECK(store.verify("root", "longenough", kHasher, role) && role == Role::kAdmin);
  role = Role::kViewer;
  CHECK(!store.verify("root", "longenougH", kHasher, role) && role == Role::kViewer);   // a wrong password gives no role
  CHECK(!store.verify("nobody", "longenough", kHasher, role) && !store.verify("", "", kHasher, role));
  // an unknown name costs one hash, like a known one, so the time does not say which names exist
  hash_calls = 0;
  store.verify("nobody", "x", kHasher, role);
  const int unknown_cost = hash_calls;
  hash_calls = 0;
  store.verify("root", "x", kHasher, role);
  CHECK(unknown_cost == 1 && hash_calls == 1);
  CHECK(store.add("view", "viewerpass", Role::kViewer, "03", kHasher) == Result::kOk && store.add("two", "twopasswd", Role::kViewer, "04", kHasher) == Result::kOk &&
        store.add("three", "threepass", Role::kViewer, "05", kHasher) == Result::kOk);
  CHECK(store.add("five", "fivepasswd", Role::kViewer, "06", kHasher) == Result::kTooMany && store.users().size() == kMaxUsers);
  // the only administrator can be neither removed nor made a viewer
  CHECK(store.remove("root") == Result::kLastAdmin && store.set_role("root", Role::kViewer) == Result::kLastAdmin && store.admins() == 1);
  CHECK(store.set_role("view", Role::kAdmin) == Result::kOk && store.admins() == 2 && store.set_role("root", Role::kViewer) == Result::kOk && store.admins() == 1);
  CHECK(store.set_role("root", Role::kAdmin) == Result::kOk && store.set_role("view", Role::kAdmin) == Result::kOk);   // an admin stays an admin
  CHECK(store.set_role("ghost", Role::kAdmin) == Result::kNotFound && store.remove("ghost") == Result::kNotFound);
  CHECK(store.remove("two") == Result::kOk && store.find("two") == nullptr && store.users().size() == 3);
  // a new password replaces the salt and the hash; the old one stops working
  CHECK(store.set_password("view", "short", "09", kHasher) == Result::kWeakPassword && store.set_password("ghost", "longenough", "09", kHasher) == Result::kNotFound);
  CHECK(store.verify("view", "viewerpass", kHasher, role));
  CHECK(store.set_password("view", "brandnewpw", "09", kHasher) == Result::kOk && !store.verify("view", "viewerpass", kHasher, role) && store.verify("view", "brandnewpw", kHasher, role));
  // the names are exact: no case folding
  CHECK(store.find("Root") == nullptr);
  store.clear();
  CHECK(store.empty());
}

static void test_user_store_persistence() {
  using namespace auth;
  UserStore store;
  store.add("root", "longenough", Role::kAdmin, "aa", kHasher);
  store.add("guest", "guestpass1", Role::kViewer, "bb", kHasher);
  const std::string saved = store.to_json();
  UserStore loaded;
  CHECK(loaded.from_json(saved) && loaded.users().size() == 2 && loaded.to_json() == saved);
  Role role = Role::kViewer;
  CHECK(loaded.verify("root", "longenough", kHasher, role) && role == Role::kAdmin && loaded.verify("guest", "guestpass1", kHasher, role) && role == Role::kViewer);
  CHECK(UserStore().to_json() == "{\"users\":[]}");
  UserStore none;
  CHECK(none.from_json("{\"users\":[]}") && none.empty());
  // a document that is not what the store wrote is refused whole, and the store keeps what it had
  const char* bad[] = {
      "", "[]", "{}", "{\"users\":{}}", "{\"users\":\"x\"}", "not json",
      "{\"users\":[{\"name\":\"root\",\"role\":\"god\",\"salt\":\"a\",\"hash\":\"b\"}]}",                         // an unknown role
      "{\"users\":[{\"name\":\"root\",\"role\":\"admin\",\"salt\":\"\",\"hash\":\"b\"}]}",                         // no salt
      "{\"users\":[{\"name\":\"root\",\"role\":\"admin\",\"salt\":\"a\",\"hash\":\"\"}]}",                         // no hash
      "{\"users\":[{\"name\":\"Root\",\"role\":\"admin\",\"salt\":\"a\",\"hash\":\"b\"}]}",                        // a name the store would never accept
      "{\"users\":[{\"name\":\"root\",\"role\":\"admin\",\"salt\":\"a\",\"hash\":\"b\"},{\"name\":\"root\",\"role\":\"viewer\",\"salt\":\"a\",\"hash\":\"b\"}]}",   // the same name twice
      "{\"users\":[{\"name\":\"user1\",\"role\":\"viewer\",\"salt\":\"a\",\"hash\":\"b\"},{\"name\":\"user2\",\"role\":\"viewer\",\"salt\":\"a\",\"hash\":\"b\"},{\"name\":\"user3\",\"role\":\"viewer\",\"salt\":\"a\",\"hash\":\"b\"},{\"name\":\"user4\",\"role\":\"viewer\",\"salt\":\"a\",\"hash\":\"b\"},{\"name\":\"user5\",\"role\":\"viewer\",\"salt\":\"a\",\"hash\":\"b\"}]}"};   // one too many
  for (const char* text : bad) {
    UserStore keeper;
    keeper.add("keep", "keeppasswd", Role::kAdmin, "cc", kHasher);
    CHECK(!keeper.from_json(text));
    CHECK(keeper.users().size() == 1 && keeper.find("keep") != nullptr);
  }
}

static void test_sessions() {
  using namespace auth;
  const std::string a(40, 'a'), b(40, 'b');
  SessionTable table;
  CHECK(table.touch(a, 0) == nullptr);
  table.create(a, "root", Role::kAdmin, 1000);
  const Session* session = table.touch(a, 2000);
  CHECK(session != nullptr && session->user == "root" && session->role == Role::kAdmin && session->last_seen_ms == 2000);
  CHECK(table.touch(std::string(31, 'a'), 2000) == nullptr && table.touch("", 2000) == nullptr && table.touch(b, 2000) == nullptr);   // a short or unknown token
  CHECK(!table.alive(std::string(31, 'a'), 2000) && table.alive(a, 2000) && !table.alive(b, 2000));
  // idle for exactly the allowance is still in; one millisecond more ends it, and the slot is freed
  CHECK(table.touch(a, 2000 + kSessionIdleMs) != nullptr);
  const std::uint64_t seen = 2000 + kSessionIdleMs;
  CHECK(table.alive(a, seen + kSessionIdleMs) && !table.alive(a, seen + kSessionIdleMs + 1));
  CHECK(table.touch(a, seen + kSessionIdleMs + 1) == nullptr && table.touch(a, seen + kSessionIdleMs + 2) == nullptr);
  // alive() never refreshes: asking does not keep a session going
  SessionTable quiet;
  quiet.create(a, "root", Role::kAdmin, 0);
  CHECK(quiet.alive(a, kSessionIdleMs) && !quiet.alive(a, kSessionIdleMs + 1));
  CHECK(quiet.touch(a, kSessionIdleMs + 1) == nullptr);
  // a remembered session lives thirty days
  SessionTable remembered;
  remembered.create(a, "root", Role::kViewer, 0, true);
  CHECK(remembered.alive(a, kRememberedIdleMs) && !remembered.alive(a, kRememberedIdleMs + 1) && remembered.touch(a, kSessionIdleMs * 100) != nullptr);
  // ending
  SessionTable ending;
  ending.create(a, "root", Role::kAdmin, 0);
  ending.create(b, "root", Role::kAdmin, 0);
  ending.create(std::string(40, 'c'), "guest", Role::kViewer, 0);
  ending.end(a);
  CHECK(ending.touch(a, 1) == nullptr && ending.touch(b, 1) != nullptr);
  ending.end_user("root");
  CHECK(ending.touch(b, 2) == nullptr && ending.touch(std::string(40, 'c'), 2) != nullptr);
  ending.clear();
  CHECK(ending.touch(std::string(40, 'c'), 3) == nullptr);
  // the same token again starts over in its own place: it never takes a second one
  SessionTable again;
  for (int i = 0; i < 6; ++i) again.create(std::string(40, static_cast<char>('a' + i)), "u", Role::kViewer, 100 + static_cast<std::uint64_t>(i));
  again.create(std::string(40, 'c'), "other", Role::kAdmin, 500);
  for (int i = 0; i < 6; ++i) CHECK(again.touch(std::string(40, static_cast<char>('a' + i)), 501) != nullptr);
  CHECK(again.touch(std::string(40, 'c'), 502)->user == "other" && again.touch(std::string(40, 'c'), 502)->role == Role::kAdmin);
}

static void test_login_throttle() {
  using namespace auth;
  LoginThrottle t;
  const std::uint32_t source = 0x0A000001u;
  for (int i = 0; i < 4; ++i) { CHECK(t.allowed(source, 1000)); t.failure(source, 1000); }
  CHECK(t.allowed(source, 1000) && t.wait_s(source, 1000) == 0);                       // four wrong passwords are still free
  t.failure(source, 1000);                                                              // the fifth
  CHECK(!t.allowed(source, 1000) && t.wait_s(source, 1000) == 30 && t.wait_s(source, 1001) == 30 && t.wait_s(source, 1000 + 29999) == 1);
  CHECK(!t.allowed(source, 1000 + 29999) && t.allowed(source, 1000 + 30000) && t.wait_s(source, 1000 + 30000) == 0);
  CHECK(t.allowed(source + 1, 1000));                                                   // another address is not touched
  // each further wrong password doubles the wait, up to five minutes
  const unsigned expected[] = {60, 120, 240, 300, 300, 300};
  std::uint64_t now = 100000;
  for (const unsigned wait : expected) { t.failure(source, now); CHECK(t.wait_s(source, now) == wait); now += static_cast<std::uint64_t>(wait) * 1000; CHECK(t.allowed(source, now)); }
  // a right password forgives everything
  t.success(source);
  CHECK(t.allowed(source, now) && t.wait_s(source, now) == 0);
  for (int i = 0; i < 4; ++i) t.failure(source, now);
  CHECK(t.allowed(source, now));   // the count started over
  // a source that was not blocked is let go of to make room, one that is blocked never is
  LoginThrottle crowd;
  for (std::uint32_t i = 0; i < 32; ++i) crowd.failure(500 + i, 1000);   // thirty-two places, one wrong password each
  CHECK(crowd.allowed(999, 1000));                                       // one more can come in: nobody is blocked
  crowd.failure(999, 1001);
  CHECK(crowd.allowed(999, 1001));
  // the shared counter blocks everybody after `limit` in a row, with the same growing wait, and a success clears it
  LoginThrottle shared;
  CHECK(shared.shared_allowed(0) && shared.shared_wait_s(0) == 0);
  shared.shared_failure(10, 3); shared.shared_failure(10, 3);
  CHECK(shared.shared_allowed(10));
  shared.shared_failure(10, 3);
  CHECK(!shared.shared_allowed(10) && shared.shared_wait_s(10) == 30 && shared.shared_allowed(10 + 30000));
  shared.shared_failure(40010, 3);
  CHECK(shared.shared_wait_s(40010) == 60);
  shared.shared_success();
  CHECK(shared.shared_allowed(40010) && shared.shared_wait_s(40010) == 0);
  // the per-source and the shared counters are independent
  CHECK(shared.allowed(source, 10));
}

// ---- the filters of the broker, the web policy and the layout of the Wi-Fi ---------------------------------------------------------------

#ifdef SHARED_NETWORK_RULES
static void test_topic_filters() {
  using armor::mqtt::topic_matches;
  CHECK(topic_matches("a/b/c", "a/b/c") && !topic_matches("a/b/c", "a/b/d") && !topic_matches("a/b/c", "a/b") && !topic_matches("a/b", "a/b/c") && !topic_matches("a/b", "a/b/"));
  CHECK(topic_matches("a/+/c", "a/x/c") && topic_matches("a/+/c", "a//c") && !topic_matches("a/+/c", "a/x/y/c") && !topic_matches("a/+/c", "a/c") && topic_matches("+", "a") && !topic_matches("+", "a/b"));
  CHECK(topic_matches("a/#", "a") && topic_matches("a/#", "a/b") && topic_matches("a/#", "a/b/c/d") && !topic_matches("a/#", "b/a") && !topic_matches("a/#", "ab") && topic_matches("#", "a/b/c"));
  CHECK(!topic_matches("a/#/c", "a/b/c") && !topic_matches("a/#/c", "a/c"));   // # is only ever the last level
  CHECK(topic_matches("armor/device/n1/+/set", "armor/device/n1/relay1/set") && !topic_matches("armor/device/n1/+/set", "armor/device/n2/relay1/set") &&
        !topic_matches("armor/device/n1/+/set", "armor/device/n1/relay1/state") && !topic_matches("armor/device/n1/+/set", "armor/device/n1/a/b/set"));
  // a topic that begins with $ (the broker's own) is never reached by a wildcard at the start
  CHECK(!topic_matches("#", "$SYS/broker") && !topic_matches("+/broker", "$SYS/broker") && topic_matches("$SYS/#", "$SYS/broker") && topic_matches("$SYS/broker", "$SYS/broker"));
  CHECK(!topic_matches("", "a") && !topic_matches("a", "") && !topic_matches("", ""));
  CHECK(topic_matches("sport/+", "sport/") && !topic_matches("sport/+", "sport") && topic_matches("sport/#", "sport"));
}

static void test_web_policy() {
  using namespace armor::webpolicy;
  using Mode = config::WebMode;
  CHECK(!serves_https(Mode::kHttp) && serves_https(Mode::kBoth) && serves_https(Mode::kHttps));
  CHECK(!http_redirects(Mode::kHttp) && !http_redirects(Mode::kBoth) && http_redirects(Mode::kHttps));
  CHECK(kHttpPort == 80 && kHttpsPort == 443);
  CHECK(clean_host("192.168.0.5") == "192.168.0.5" && clean_host("node.local:8080") == "node.local" && clean_host("armor-1") == "armor-1" && clean_host("a.b-c") == "a.b-c");
  const char* refused[] = {"", ":80", "evil.com/path", "ev il", "a_b", "[::1]", "-a", ".a", "a-", "a@b", "a#b", "a?b", "a\r\nb", "a\\b", "\xC3\xA9"};
  for (const char* host : refused) CHECK(clean_host(host).empty());
  CHECK(clean_host(std::string(253, 'a')) == std::string(253, 'a') && clean_host(std::string(254, 'a')).empty());
  CHECK(redirect_location("node.local", "/") == "https://node.local/" && redirect_location("node.local:80", "/a/b?x=1&y=2") == "https://node.local/a/b?x=1&y=2");
  // an odd request can never become a redirect to somewhere else
  CHECK(redirect_location("node.local", "//evil.com").empty() && redirect_location("node.local", "evil.com").empty() && redirect_location("node.local", "").empty() && redirect_location("", "/").empty());
  CHECK(redirect_location("evil.com/x", "/").empty() && redirect_location("node.local", "/a b").empty() && redirect_location("node.local", "/a\r\nSet-Cookie: x").empty() &&
        redirect_location("node.local", "/a\\b").empty() && redirect_location("node.local", "/\xC3\xA9").empty() && redirect_location("node.local", "/a\x7f").empty());
  CHECK(cookie_attributes(false, 1800) == "Path=/; HttpOnly; SameSite=Strict; Max-Age=1800" && cookie_attributes(true, 0) == "Path=/; HttpOnly; SameSite=Strict; Secure; Max-Age=0");
}

static void test_netplan() {
  using namespace armor::netplan;
  CHECK(std::string(to_text(Layout::kSetupAp)) == "setup-ap" && std::string(to_text(Layout::kStation)) == "wifi-station" && std::string(to_text(Layout::kStationWithAp)) == "wifi-station+ap" &&
        std::string(to_text(Layout::kAccessPoint)) == "wifi-ap" && std::string(to_text(Layout::kEthernet)) == "ethernet" && std::string(to_text(Layout::kEthernetWithAp)) == "ethernet+ap");
  config::Settings s;
  s.node_id = "node_one_a1b2c3";
  CHECK(hostname_for(s) == "armor-node-one-a1b2c3");
  s.node_id = std::string(64, 'x');
  CHECK(hostname_for(s) == "armor-" + std::string(26, 'x') && hostname_for(s).size() == 32);
  s.node_id = "a-----------------------------b";   // the cut falls on a hyphen: a name never ends in one
  CHECK(hostname_for(s).back() != '-' && hostname_for(s).size() <= 32);
  s.node_id = "n1";
  s.hostname = "custom-name";
  CHECK(hostname_for(s) == "custom-name");
  CHECK(upper("abcXYZ-09") == "ABCXYZ-09" && upper("").empty());

  // the setup access point: a node with no user opens only its own network, whatever the settings say
  s.hostname.clear();
  s.sta.enabled = true; s.sta.ssid = "home";
  s.ap.enabled = false;
  Plan setup = plan_network(s, true, "CODE1234", "a1b2c3", 7);
  CHECK(setup.layout == Layout::kSetupAp && setup.ap.enabled && setup.ap.setup && setup.ap.ssid == "ARMOR-SETUP-A1B2C3" && setup.ap.password == "CODE1234" && setup.ap.security == config::WifiSecurity::kWpa2 &&
        setup.ap.max_clients == 4 && !setup.station && setup.hostname == "armor-n1");
  // the normal modes (the cable decides on the s3-eth board; the host build of the other board has none)
  s.uplink = config::Uplink::kWifi;
  Plan station = plan_network(s, false, "", "a1b2c3", 7);
  CHECK(station.layout == Layout::kStation && station.station && !station.ap.enabled && !station.wired);
  s.ap.enabled = true; s.ap.ssid = "mine"; s.ap.password = "secret-key"; s.ap.channel = 0; s.ap.country = "DE"; s.ap.hidden = true; s.ap.max_clients = 3; s.ap.tx_power_dbm = 10; s.ap.bandwidth_mhz = 40;
  Plan both = plan_network(s, false, "", "a1b2c3", 7);
  CHECK(both.layout == Layout::kStationWithAp && both.station && both.ap.enabled && !both.ap.setup && both.ap.ssid == "mine" && both.ap.password == "secret-key" && both.ap.hidden && both.ap.max_clients == 3 &&
        both.ap.country == "DE" && both.ap.tx_power_dbm == 10 && both.ap.bandwidth_mhz == 40);
  CHECK(both.ap.channel == 6);   // 7 % 3 == 1 -> the second of 1, 6 and 11
  CHECK(plan_network(s, false, "", "a1b2c3", 9).ap.channel == 1 && plan_network(s, false, "", "a1b2c3", 11).ap.channel == 11);
  s.ap.channel = 13;
  CHECK(plan_network(s, false, "", "a1b2c3", 7).ap.channel == 13);
  s.ap.channel = 14;   // out of the settings' range: the node picks its own
  CHECK(plan_network(s, false, "", "a1b2c3", 7).ap.channel == 6);
  s.ap.channel = 0;
  s.sta.enabled = false;
  Plan only_ap = plan_network(s, false, "", "a1b2c3", 7);
  CHECK(only_ap.layout == Layout::kAccessPoint && !only_ap.station && only_ap.ap.enabled);
  if (board::kHasEthernet) {   // the s3-eth build (a node whose board has no cable, like the panel node, takes the other branch)
    s.uplink = config::Uplink::kEthernet;
    s.sta.enabled = true;
    Plan cable = plan_network(s, false, "", "a1b2c3", 7);
    CHECK(cable.wired && !cable.station && cable.layout == Layout::kEthernetWithAp && cable.ap.enabled);   // the cable wins: Wi-Fi is the access point only
    s.ap.enabled = false;
    Plan cable_only = plan_network(s, false, "", "a1b2c3", 7);
    CHECK(cable_only.layout == Layout::kEthernet && !cable_only.ap.enabled && cable_only.wired);
    Plan cable_setup = plan_network(s, true, "CODE1234", "a1b2c3", 7);
    CHECK(cable_setup.layout == Layout::kSetupAp && cable_setup.wired);   // set up from either: the cable stays up
  } else {
    s.uplink = config::Uplink::kEthernet;   // this board has no cable, so asking for one changes nothing
    s.sta.enabled = true;
    s.ap.enabled = false;
    Plan no_cable = plan_network(s, false, "", "a1b2c3", 7);
    CHECK(!no_cable.wired && no_cable.station && no_cable.layout == Layout::kStation);
  }
}
#endif  // SHARED_NETWORK_RULES

int main() {
  test_ipv4();
  test_masks_and_addresses();
  test_names();
  test_utf8();
  test_node_id();
  test_json_grammar();
  test_json_limits();
  test_json_values();
  test_json_writer();
  test_credentials_rules();
  test_setup_code_and_passkey();
  test_user_store();
  test_user_store_persistence();
  test_sessions();
  test_login_throttle();
#ifdef SHARED_NETWORK_RULES
  test_topic_filters();
  test_web_policy();
  test_netplan();
#endif
  (void)contains;
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
