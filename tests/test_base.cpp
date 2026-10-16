// ARMOR-ELECTRICAL - host tests of what every node shares and that has no hardware: the schedule of retries, Improv Wi-Fi over serial and the stack report.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
// Nothing was run on a board; the packets are the ones the standard (https://www.improv-wifi.com/serial/) describes.
#include <cstdio>
#include <string>
#include <vector>

#include "../core/auth.hpp"
#include "../core/backoff.hpp"
#include "../core/improv.hpp"
#include "../core/task_health.hpp"

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

// ---- the schedule of retries -----------------------------------------------------------------------------------------------------------

static void test_ble_passkey() {
  const std::uint32_t a = armor::auth::ble_passkey_from("ABCDEFGH23"), b = armor::auth::ble_passkey_from("ABCDEFGH24");
  CHECK(a < 1000000 && b < 1000000);
  CHECK(a == armor::auth::ble_passkey_from("ABCDEFGH23"));   // the same code, the same PIN: the node keeps nothing more
  CHECK(a != b);                                              // another code, another PIN
  int different = 0;
  for (char c = 'A'; c < 'Z'; ++c) if (armor::auth::ble_passkey_from(std::string("CODE") + c) != armor::auth::ble_passkey_from("CODEA")) ++different;
  CHECK(different >= 23);                                     // spread, not a pattern
}

static void test_backoff() {
  retry::Backoff plain(1000, 30000, 0);   // no scatter: the schedule itself
  const std::uint32_t expected[] = {1000, 2000, 4000, 8000, 16000, 30000, 30000, 30000};
  for (const std::uint32_t wait : expected) CHECK(plain.next(12345) == wait);
  CHECK(plain.failures() >= 8);
  plain.reset();
  CHECK(plain.failures() == 0 && plain.next(1) == 1000);   // a try that worked starts the schedule over
  // with scatter: within 20 % of the plain wait, never above the ceiling, never zero
  retry::Backoff scattered(1000, 30000, 20);
  for (std::uint32_t random : {0u, 1u, 20u, 40u, 41u, 999u, 4294967295u}) {
    retry::Backoff b(1000, 30000, 20);
    b.next(random); b.next(random);                       // two failures: the third wait is 4000 ms
    const std::uint32_t wait = b.next(random);
    CHECK(wait >= 3200 && wait <= 4800);
  }
  for (int i = 0; i < 40; ++i) { const std::uint32_t wait = scattered.next(static_cast<std::uint32_t>(i) * 2654435761u); CHECK(wait >= 1 && wait <= 30000); }
  // odd settings are made sensible: a ceiling under the first wait is the first wait, a first wait of zero is one millisecond
  retry::Backoff odd(0, 0, 500);
  CHECK(odd.next(7) >= 1 && odd.next(7) <= 2);
  // many nodes that lost the same router at the same moment do not wait the same: different random numbers give different waits
  retry::Backoff a(1000, 60000, 20), b(1000, 60000, 20);
  a.next(1); b.next(2);
  CHECK(a.next(3) != b.next(9));
}

// ---- Improv Wi-Fi over serial ---------------------------------------------------------------------------------------------------------

static std::vector<std::uint8_t> rpc_wifi(const std::string& ssid, const std::string& password) {
  std::vector<std::uint8_t> data{0x01, static_cast<std::uint8_t>(2 + ssid.size() + password.size()), static_cast<std::uint8_t>(ssid.size())};
  data.insert(data.end(), ssid.begin(), ssid.end());
  data.push_back(static_cast<std::uint8_t>(password.size()));
  data.insert(data.end(), password.begin(), password.end());
  return improv::packet(improv::Type::kRpc, data);
}

static std::optional<improv::Packet> feed_all(improv::Parser& parser, const std::vector<std::uint8_t>& bytes) {
  std::optional<improv::Packet> last;
  for (const std::uint8_t b : bytes) if (auto p = parser.feed(b)) last = p;
  return last;
}

static void test_improv_packets() {
  // the state packet of the standard: IMPROV, version 1, type 1, length 1, the state, the checksum and a newline
  const std::vector<std::uint8_t> ready = improv::state_packet(improv::State::kReady);
  const std::vector<std::uint8_t> known{'I', 'M', 'P', 'R', 'O', 'V', 0x01, 0x01, 0x01, 0x02, 0x00, '\n'};
  std::vector<std::uint8_t> sum_of(known.begin(), known.begin() + 10);
  CHECK(improv::checksum(sum_of) == static_cast<std::uint8_t>((73 + 77 + 80 + 82 + 79 + 86 + 1 + 1 + 1 + 2) & 0xFF));
  CHECK(ready.size() == 12 && ready[7] == 0x01 && ready[8] == 1 && ready[9] == 0x02 && ready.back() == '\n' && ready[10] == improv::checksum(sum_of));
  const std::vector<std::uint8_t> error = improv::error_packet(improv::Error::kUnableToConnect);
  CHECK(error[7] == 0x02 && error[9] == 0x03);
  // an RPC result: command, the length of what follows, then each string with its length in front
  const std::vector<std::uint8_t> result = improv::result_packet(improv::Command::kGetDeviceInfo, {"ab", "c"});
  CHECK(result[7] == 0x04 && result[8] == 7 && result[9] == 0x04 && result[10] == 5 && result[11] == 2 && result[12] == 'a' && result[13] == 'b' && result[14] == 1 && result[15] == 'c');
  const std::vector<std::uint8_t> empty = improv::result_packet(improv::Command::kWifiSettings, {});
  CHECK(empty[8] == 2 && empty[9] == 0x01 && empty[10] == 0);
  // a packet never carries more than 255 bytes of data
  const std::vector<std::uint8_t> big = improv::packet(improv::Type::kRpcResult, std::vector<std::uint8_t>(400, 'x'));
  CHECK(big[8] == 255 && big.size() == 9 + 255 + 2);
}

static void test_improv_parser() {
  improv::Parser parser;
  // the Wi-Fi settings, with noise (log lines, a person typing) before and between packets
  std::vector<std::uint8_t> stream;
  for (const char c : std::string("I (123) armor-net: link up\r\nIMP garbage IMPROV\r\n")) stream.push_back(static_cast<std::uint8_t>(c));
  const std::vector<std::uint8_t> wifi = rpc_wifi("HomeRouter", "una-clave-larga");
  stream.insert(stream.end(), wifi.begin(), wifi.end());
  const std::optional<improv::Packet> got = feed_all(parser, stream);
  CHECK(got.has_value() && got->type == improv::Type::kRpc);
  improv::Rpc rpc;
  CHECK(got && improv::parse_rpc(got->data, rpc) == improv::Error::kNone && rpc.command == improv::Command::kWifiSettings && rpc.ssid == "HomeRouter" && rpc.password == "una-clave-larga");
  // a wrong checksum is dropped and counted, and the next good packet still arrives
  std::vector<std::uint8_t> broken = rpc_wifi("a", "b");
  broken[broken.size() - 2] ^= 0x55;
  const std::uint32_t before = parser.rejected();
  CHECK(!feed_all(parser, broken).has_value() && parser.rejected() == before + 1);
  CHECK(feed_all(parser, rpc_wifi("a", "b")).has_value());
  // a version that is not 1 is not ours
  std::vector<std::uint8_t> future = rpc_wifi("a", "b");
  future[6] = 0x02;
  CHECK(!feed_all(parser, future).has_value());
  CHECK(feed_all(parser, rpc_wifi("c", "")).has_value());
  // a packet fed one byte at a time and one cut in the middle by another header
  improv::Parser slow;
  std::optional<improv::Packet> seen;
  for (const std::uint8_t b : rpc_wifi("x", "y")) if (auto p = slow.feed(b)) seen = p;
  CHECK(seen.has_value());
  improv::Parser cut;
  std::vector<std::uint8_t> half = rpc_wifi("abc", "def");
  half.resize(11);
  const std::vector<std::uint8_t> whole = rpc_wifi("zz", "yy");
  half.insert(half.end(), whole.begin(), whole.end());
  const std::optional<improv::Packet> after_cut = feed_all(cut, half);
  CHECK(after_cut.has_value() || cut.rejected() >= 1);   // the cut one is lost; the stream is not wedged
  CHECK(feed_all(cut, rpc_wifi("q", "r")).has_value());
  // an empty stream and a lone byte do nothing
  improv::Parser nothing;
  CHECK(!nothing.feed('I') && !nothing.feed('x') && !nothing.feed(0));
}

static void test_improv_rpc() {
  improv::Rpc rpc;
  CHECK(improv::parse_rpc({0x02, 0x00}, rpc) == improv::Error::kNone && rpc.command == improv::Command::kIdentify);
  CHECK(improv::parse_rpc({0x03, 0x00}, rpc) == improv::Error::kNone && rpc.command == improv::Command::kGetCurrentState);
  CHECK(improv::parse_rpc({0x04, 0x00}, rpc) == improv::Error::kNone && rpc.command == improv::Command::kGetDeviceInfo);
  CHECK(improv::parse_rpc({0x05, 0x00}, rpc) == improv::Error::kNone && rpc.command == improv::Command::kGetWifiNetworks);
  CHECK(improv::parse_rpc({0x77, 0x00}, rpc) == improv::Error::kUnknownRpc);
  CHECK(improv::parse_rpc({}, rpc) == improv::Error::kInvalidRpc && improv::parse_rpc({0x02}, rpc) == improv::Error::kInvalidRpc);
  CHECK(improv::parse_rpc({0x02, 0x05}, rpc) == improv::Error::kInvalidRpc);   // the length does not match the data
  // the Wi-Fi settings: the lengths must add up, the SSID is 1 to 32 bytes and the password at most 63
  CHECK(improv::parse_rpc({0x01, 0x03, 0x01, 'a', 0x00}, rpc) == improv::Error::kNone && rpc.ssid == "a" && rpc.password.empty());
  CHECK(improv::parse_rpc({0x01, 0x03, 0x05, 'a', 0x00}, rpc) == improv::Error::kInvalidRpc);          // the SSID says 5 bytes and has 1
  CHECK(improv::parse_rpc({0x01, 0x03, 0x00, 'a', 0x00}, rpc) == improv::Error::kInvalidRpc);          // an empty SSID
  std::vector<std::uint8_t> long_ssid{0x01, 0x00, 33};
  long_ssid.insert(long_ssid.end(), 33, 'a'); long_ssid.push_back(0); long_ssid[1] = static_cast<std::uint8_t>(long_ssid.size() - 2);
  CHECK(improv::parse_rpc(long_ssid, rpc) == improv::Error::kInvalidRpc);
  std::vector<std::uint8_t> long_pass{0x01, 0x00, 1, 'a', 64};
  long_pass.insert(long_pass.end(), 64, 'p'); long_pass[1] = static_cast<std::uint8_t>(long_pass.size() - 2);
  CHECK(improv::parse_rpc(long_pass, rpc) == improv::Error::kInvalidRpc);
  CHECK(improv::parse_rpc({0x01, 0x01, 0x00}, rpc) == improv::Error::kInvalidRpc);
}

// ---- the stack report -----------------------------------------------------------------------------------------------------------------------

static void test_task_health() {
  const std::vector<health::TaskInfo> tasks{{"main", 2048, 1, 'B'}, {"meters", 300, 5, 'B'}, {"mqtt", 300, 5, 'r'}, {"idle", 900, 0, 'R'}};
  CHECK(health::low_count(tasks) == 2 && health::is_low(tasks[1]) && !health::is_low(tasks[0]) && !health::is_low({"edge", 512, 0, 'B'}) && health::is_low({"edge", 511, 0, 'B'}));
  const std::string text = health::tasks_json(tasks);
  // the least room first, a tie by name
  CHECK(text.find("\"meters\"") < text.find("\"mqtt\"") && text.find("\"mqtt\"") < text.find("\"idle\"") && text.find("\"idle\"") < text.find("\"main\""));
  CHECK(contains(text, "{\"name\":\"meters\",\"stack_free\":300,\"priority\":5,\"state\":\"B\",\"low\":true}") && contains(text, "\"name\":\"main\",\"stack_free\":2048,\"priority\":1,\"state\":\"B\",\"low\":false"));
  CHECK(health::tasks_json({}) == "[]" && health::low_count({}) == 0);
}

// ---- the sessions of the panel ------------------------------------------------------------------------------------------------------------

static void test_remembered_session_is_not_taken_after_half_an_hour() {
  using namespace armor::auth;
  SessionTable table;
  const std::string phone(40, 'p');
  table.create(phone, "admin", Role::kAdmin, 1000, /*remembered=*/true);
  // five more people come in 31 minutes later and fill the table (six places); the phone's session is remembered, so it is not idle for thirty days
  const std::uint64_t later = 1000 + 31ULL * 60ULL * 1000ULL;
  for (int i = 0; i < 5; ++i) table.create(std::string(40, static_cast<char>('a' + i)), "user", Role::kAdmin, later);
  CHECK(table.touch(phone, later + 1) != nullptr);      // it is still there
  // a plain session that has been idle too long is the one that goes when the table is full
  SessionTable full;
  for (int i = 0; i < 6; ++i) full.create(std::string(40, static_cast<char>('a' + i)), "user", Role::kAdmin, 1000);
  full.create(std::string(40, 'z'), "new", Role::kAdmin, 1000 + 31ULL * 60ULL * 1000ULL);
  CHECK(full.touch(std::string(40, 'z'), 1000 + 31ULL * 60ULL * 1000ULL + 1) != nullptr);
  // with nothing idle, the one idle the longest is replaced
  SessionTable busy;
  for (int i = 0; i < 6; ++i) busy.create(std::string(40, static_cast<char>('a' + i)), "user", Role::kAdmin, 1000 + static_cast<std::uint64_t>(i) * 10);
  busy.create(std::string(40, 'z'), "new", Role::kAdmin, 2000);
  CHECK(busy.touch(std::string(40, 'a'), 2001) == nullptr && busy.touch(std::string(40, 'b'), 2001) != nullptr);
}

int main() {
  test_ble_passkey();
  test_remembered_session_is_not_taken_after_half_an_hour();
  test_backoff();
  test_improv_packets();
  test_improv_parser();
  test_improv_rpc();
  test_task_health();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
