// ARMOR-ELECTRICAL - host tests of the node's direct HTTP API for a central: the token and the relay request, without hardware.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include <cstdio>
#include <string>

#include "../core/central_link.hpp"

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

// ---- the direct API for a central --------------------------------------------------------------------------------------------------------

static void test_central_link() {
  const std::string token = "0123456789abcdefghijKLMNOP";
  CHECK(central::bearer_of("Bearer abc") == "abc" && central::bearer_of("bearer abc").empty() && central::bearer_of("Bearer ").empty() && central::bearer_of("Basic abc").empty() && central::bearer_of("").empty());
  CHECK(central::authorized(true, token, "Bearer " + token));
  CHECK(!central::authorized(false, token, "Bearer " + token));                       // the interface is off
  CHECK(!central::authorized(true, token, "Bearer " + token + "x"));                  // a longer token
  CHECK(!central::authorized(true, token, "Bearer " + token.substr(1)));              // a shorter one
  CHECK(!central::authorized(true, token, "Bearer 0123456789abcdefghijKLMNOQ"));      // one character off
  CHECK(!central::authorized(true, token, ""));                                       // no header
  CHECK(!central::authorized(true, "", "Bearer "));                                   // no token set: nobody is let in, not even with an empty one
  CHECK(!central::authorized(true, "short", "Bearer short"));                         // a token too short to be one
  CHECK(central::valid_token(token) && !central::valid_token("short") && !central::valid_token(std::string(65, 'a')) && !central::valid_token("0123456789abcdefghij kl") && !central::valid_token("0123456789abcdefghij\"kl"));
  central::RelayRequest request;
  CHECK(central::parse_relay_request("{\"relay\":\"siren\",\"action\":\"on\"}", request) == central::Parse::kOk && request.relay == "siren" && request.action == relays::Action::kOn);
  CHECK(central::parse_relay_request("{\"relay\":\"pump\",\"action\":\"OFF\"}", request) == central::Parse::kOk && request.action == relays::Action::kOff);
  CHECK(central::parse_relay_request("{\"relay\":\"pump\",\"action\":\"toggle\"}", request) == central::Parse::kOk && request.action == relays::Action::kToggle);
  CHECK(central::parse_relay_request("{\"relay\":\"pump\",\"action\":\"pulse\"}", request) == central::Parse::kInvalid);
  CHECK(central::parse_relay_request("{\"relay\":\"Pump Room\",\"action\":\"on\"}", request) == central::Parse::kInvalid);
  CHECK(central::parse_relay_request("{\"relay\":3,\"action\":\"on\"}", request) == central::Parse::kInvalid);
  CHECK(central::parse_relay_request("{\"action\":\"on\"}", request) == central::Parse::kInvalid);
  CHECK(central::parse_relay_request("[1]", request) == central::Parse::kNotJson && central::parse_relay_request("not json", request) == central::Parse::kNotJson);
  CHECK(central::relay_answer("siren", true) == "{\"relay\":\"siren\",\"on\":true}");
}

int main() {
  test_central_link();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
