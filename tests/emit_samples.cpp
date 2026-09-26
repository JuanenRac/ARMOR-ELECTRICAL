// ARMOR-ELECTRICAL - prints the messages a node would publish, one per line ("armor/electrical/<node>/state <json>"), for tests/check_samples.py.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "../core/electrical_json.hpp"
#include "../core/switch_set.hpp"

using namespace armor::electrical;

static std::vector<std::uint8_t> reply(std::uint8_t address, const std::vector<std::uint16_t>& registers) {
  std::vector<std::uint8_t> frame{address, 0x04, static_cast<std::uint8_t>(registers.size() * 2)};
  for (std::uint16_t r : registers) { frame.push_back(static_cast<std::uint8_t>(r >> 8)); frame.push_back(static_cast<std::uint8_t>(r & 0xFF)); }
  const std::uint16_t crc = pzem::crc16(frame.data(), frame.size());
  frame.push_back(static_cast<std::uint8_t>(crc & 0xFF)); frame.push_back(static_cast<std::uint8_t>(crc >> 8));
  return frame;
}

int main() {
  std::vector<MeterConfig> meters = {{"grid", "Grid input", pzem::Model::kAc, 1}, {"heater", "", pzem::Model::kAc, 2}, {"dc-bus", "Battery bus", pzem::Model::kDc, 7}};
  PzemBus bus(meters, 1000);
  std::map<int, std::vector<std::uint8_t>> answers;
  answers[1] = reply(1, {2305, 1234, 0, 2844, 0, 1500, 0, 500, 98, 0});
  answers[2] = reply(2, {2310, 8000, 0, 18500, 0, 30, 0, 499, 99, 0xFFFF});
  answers[7] = reply(7, {5214, 1420, 7404, 0, 25000, 0, 0, 0});
  for (std::uint64_t now = 0; now < 3000; now += 10) {
    const std::vector<std::uint8_t> out = bus.next_tx(now);
    if (!out.empty()) { const auto found = answers.find(out[0]); if (found != answers.end()) bus.on_rx(found->second.data(), found->second.size(), now + 5); }
  }
  const std::vector<const MeterState*> fresh = bus.fresh(3000);
  const std::string node = "electrical-1";
  std::printf("%s %s\n", topic(node).c_str(), message_json(node, 1000, false, fresh).c_str());
  std::printf("%s %s\n", topic(node).c_str(), message_json(node, 2000, true, fresh, {{"heater", {SwitchState::kClosed, "over_current"}}, {"grid", {SwitchState::kOpen, ""}}}).c_str());
  std::printf("%s %s\n", topic(node).c_str(), message_json(node, 3000, false, {}).c_str());

  // The switches of a node: the states it publishes and the answers it gives, for a node that may switch and for one that may not (stand-in contactors, nothing driven).
  for (bool allowed : {true, false}) {
    SwitchSet set(node, [](std::uint8_t* out, std::size_t n) { for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<std::uint8_t>(i * 17 + 3); return true; });
    SwitchConfig config;
    config.id = "transfer"; config.label = "Grid or inverter"; config.source_a = "grid"; config.source_b = "dc-bus";
    config.transfer = TransferConfig{allowed, 2000, 1000, 10'000};
    set.add(config);
    const auto state = [&](std::uint64_t at) { std::printf("%s %s\n", topic(node).c_str(), message_json(node, at, set.switching_enabled(), fresh, {}, set.switches_json(at)).c_str()); };
    const auto command = [&](const std::string& action, const std::string& token, std::uint64_t at) {
      const std::string text = std::string("{\"kind\":\"electrical_command\",\"node_id\":\"") + node + "\",\"timestamp_ms\":" + std::to_string(at) + ",\"command_id\":\"c0ffee0123456789\",\"switch\":\"transfer\",\"action\":\"" + action + "\"" +
                               (token.empty() ? "" : ",\"token\":\"" + token + "\"") + "}";
      std::printf("armor/electrical/%s/command %s\n", node.c_str(), text.c_str());
      const std::string answer = set.handle(text, at);
      std::printf("%s %s\n", result_topic(node).c_str(), answer.c_str());
      return answer;
    };
    for (std::uint64_t at = 0; at <= 2500; at += 10) set.tick(at);
    state(2500);
    const std::string armed = command("arm", "", 2600);
    state(2600);
    armor::json::Value parsed;
    armor::json::parse(armed, parsed);
    const std::string token = parsed.string_or("token", "0123456789abcdef");
    command("close_a", token, 2700);
    command("close_a", token, 2800);   // the same again: nothing to close with
    for (std::uint64_t at = 2700; at <= 4900; at += 10) { set.set_feedback("transfer", Feedback{set.coils("transfer").a && at > 4700, false}); set.tick(at); }
    state(5000);
    command("open", "", 5100);
    command("acknowledge", "", 5200);
    for (std::uint64_t at = 5100; at <= 5400; at += 10) { set.set_feedback("transfer", Feedback{false, false}); set.tick(at); }
    state(5500);
  }
  return 0;
}
