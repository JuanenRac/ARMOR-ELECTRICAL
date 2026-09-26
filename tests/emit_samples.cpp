// ARMOR-ELECTRICAL - prints the messages a node would publish, one per line ("armor/electrical/<node>/state <json>"), for tests/check_samples.py.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "../core/electrical_json.hpp"

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
  return 0;
}
