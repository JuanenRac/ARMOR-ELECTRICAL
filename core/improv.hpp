// ARMOR-ELECTRICAL - Improv Wi-Fi over a serial line: the packets of the standard that lets a web page (ESP Web Tools) give a node its Wi-Fi network through the USB cable. No hardware.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// Improv Wi-Fi Serial (https://www.improv-wifi.com/serial/) is a small packet protocol sent over the same serial line as the log:
//   "IMPROV" | version 1 | type | length | data | checksum (the sum of every byte before it, modulo 256) | "\n"
// The node answers with its state (authorization required, ready, provisioning, provisioned), with an error code, and with the result of what it was asked: to take a Wi-Fi network
// (an RPC "Wi-Fi settings" with the SSID and the password), to identify itself, to say its state again or to describe the device. This file only builds and reads those packets, so
// it is tested on a computer; the firmware reads the bytes from the USB, calls what the node does with the network, and writes the bytes this file makes. Whatever else comes down
// the line (a person typing, a log) is ignored: only a whole, checked packet is ever acted upon.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace armor::improv {

enum class Type : std::uint8_t { kCurrentState = 0x01, kErrorState = 0x02, kRpc = 0x03, kRpcResult = 0x04 };
enum class State : std::uint8_t { kAuthorizationRequired = 0x01, kReady = 0x02, kProvisioning = 0x03, kProvisioned = 0x04 };
enum class Error : std::uint8_t { kNone = 0x00, kInvalidRpc = 0x01, kUnknownRpc = 0x02, kUnableToConnect = 0x03, kNotAuthorized = 0x04, kUnknown = 0xFF };
enum class Command : std::uint8_t { kUnknown = 0x00, kWifiSettings = 0x01, kIdentify = 0x02, kGetCurrentState = 0x03, kGetDeviceInfo = 0x04, kGetWifiNetworks = 0x05 };

constexpr std::size_t kMaxData = 255;

inline std::uint8_t checksum(const std::vector<std::uint8_t>& bytes) {
  std::uint32_t sum = 0;
  for (const std::uint8_t b : bytes) sum += b;
  return static_cast<std::uint8_t>(sum & 0xFF);
}

// The bytes of a packet of `type` carrying `data` (at most 255 bytes: what the length byte can say).
inline std::vector<std::uint8_t> packet(Type type, const std::vector<std::uint8_t>& data) {
  std::vector<std::uint8_t> out{'I', 'M', 'P', 'R', 'O', 'V', 0x01, static_cast<std::uint8_t>(type), static_cast<std::uint8_t>(data.size() > kMaxData ? kMaxData : data.size())};
  for (std::size_t i = 0; i < data.size() && i < kMaxData; ++i) out.push_back(data[i]);
  out.push_back(checksum(out));
  out.push_back('\n');
  return out;
}

inline std::vector<std::uint8_t> state_packet(State state) { return packet(Type::kCurrentState, {static_cast<std::uint8_t>(state)}); }
inline std::vector<std::uint8_t> error_packet(Error error) { return packet(Type::kErrorState, {static_cast<std::uint8_t>(error)}); }

// The result of an RPC: the command, then the length of what follows, then each string with its own length in front. A string is cut at 255 bytes.
inline std::vector<std::uint8_t> result_packet(Command command, const std::vector<std::string>& strings) {
  std::vector<std::uint8_t> body;
  for (const std::string& text : strings) {
    const std::size_t n = text.size() > kMaxData ? kMaxData : text.size();
    body.push_back(static_cast<std::uint8_t>(n));
    for (std::size_t i = 0; i < n; ++i) body.push_back(static_cast<std::uint8_t>(text[i]));
  }
  if (body.size() > kMaxData - 2) body.resize(kMaxData - 2);
  std::vector<std::uint8_t> data{static_cast<std::uint8_t>(command), static_cast<std::uint8_t>(body.size())};
  data.insert(data.end(), body.begin(), body.end());
  return packet(Type::kRpcResult, data);
}

struct Packet {
  Type type = Type::kRpc;
  std::vector<std::uint8_t> data;
};

// Reads bytes one at a time and gives a packet when a whole one with a right checksum has arrived. Anything before "IMPROV", after a bad packet or between packets is dropped.
class Parser {
 public:
  std::optional<Packet> feed(std::uint8_t byte) {
    if (buffer_.size() < 6) {
      if (byte == static_cast<std::uint8_t>("IMPROV"[buffer_.size()])) buffer_.push_back(byte);
      else { buffer_.clear(); if (byte == 'I') buffer_.push_back(byte); }
      return std::nullopt;
    }
    buffer_.push_back(byte);
    // 6 header bytes, version, type, length: then `length` data bytes, a checksum and the newline
    if (buffer_.size() == 7 && buffer_[6] != 0x01) { ++rejected_; buffer_.clear(); return std::nullopt; }   // a version this node does not speak
    if (buffer_.size() < 9) return std::nullopt;
    const std::size_t length = buffer_[8];
    const std::size_t total = 9 + length + 1;       // up to and with the checksum
    if (buffer_.size() < total) return std::nullopt;
    const std::uint8_t sent = buffer_[total - 1];
    std::vector<std::uint8_t> body(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(total - 1));
    const bool good = checksum(body) == sent;
    std::optional<Packet> result;
    if (good) {
      Packet p;
      p.type = static_cast<Type>(buffer_[7]);
      p.data.assign(buffer_.begin() + 9, buffer_.begin() + static_cast<std::ptrdiff_t>(9 + length));
      result = std::move(p);
    } else ++rejected_;
    buffer_.clear();   // the newline that follows is dropped by the "header" rule above
    return result;
  }
  std::uint32_t rejected() const { return rejected_; }

 private:
  std::vector<std::uint8_t> buffer_;
  std::uint32_t rejected_ = 0;
};

struct Rpc {
  Command command = Command::kUnknown;
  std::string ssid, password;   // only for the Wi-Fi settings
};

// The RPC of a packet's data: [command, length, data (length bytes)]. For the Wi-Fi settings the data is [ssid length, ssid, password length, password].
inline Error parse_rpc(const std::vector<std::uint8_t>& data, Rpc& out) {
  if (data.size() < 2) return Error::kInvalidRpc;
  const std::size_t length = data[1];
  if (data.size() != 2 + length) return Error::kInvalidRpc;
  out = Rpc{};
  switch (data[0]) {
    case 0x01: {
      out.command = Command::kWifiSettings;
      if (length < 2) return Error::kInvalidRpc;
      const std::size_t ssid_length = data[2];
      if (3 + ssid_length + 1 > data.size()) return Error::kInvalidRpc;
      out.ssid.assign(data.begin() + 3, data.begin() + static_cast<std::ptrdiff_t>(3 + ssid_length));
      const std::size_t password_length = data[3 + ssid_length];
      if (3 + ssid_length + 1 + password_length != data.size()) return Error::kInvalidRpc;
      out.password.assign(data.begin() + static_cast<std::ptrdiff_t>(3 + ssid_length + 1), data.end());
      if (out.ssid.empty() || out.ssid.size() > 32 || out.password.size() > 63) return Error::kInvalidRpc;
      return Error::kNone;
    }
    case 0x02: out.command = Command::kIdentify; return Error::kNone;
    case 0x03: out.command = Command::kGetCurrentState; return Error::kNone;
    case 0x04: out.command = Command::kGetDeviceInfo; return Error::kNone;
    case 0x05: out.command = Command::kGetWifiNetworks; return Error::kNone;
    default: return Error::kUnknownRpc;
  }
}

}  // namespace armor::improv
