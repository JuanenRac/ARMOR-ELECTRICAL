// ARMOR-ELECTRICAL - reading the Peacefair PZEM energy meters over their Modbus-RTU serial link: the PZEM-004T v3 (AC, up to 260 V, a current transformer for the current)
// and the PZEM-017 (DC, up to 300 V, a shunt). READ ONLY: only the request that reads the input registers is built. The requests that write (a new address, the alarm
// thresholds, the reset of the energy counter) exist in the meters but are not built anywhere in this project.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// From the makers' public description of the two meters; NOTHING here has been connected to a meter.
//
//   Serial 9600 baud 8N1 (the PZEM-017 is set at the factory to 9600 too, and can be changed). Modbus RTU, function 0x04 (read input registers).
//   Request:  address, 0x04, first register (2 bytes), count (2 bytes), CRC-16/MODBUS (low byte first).
//   Reply:    address, 0x04, byte count, the registers (2 bytes each, high byte first), CRC-16/MODBUS. A refusal answers function | 0x80 and an exception code.
//   PZEM-004T v3, ten registers from 0x0000:  voltage (0.1 V), current (0.001 A, two registers, low word first), power (0.1 W, two registers), energy (1 Wh, two
//                                             registers), frequency (0.1 Hz), power factor (0.01), alarm (0xFFFF when above the threshold).
//   PZEM-017, eight registers from 0x0000:    voltage (0.01 V), current (0.01 A), power (0.1 W, two registers), energy (1 Wh, two registers), the high- and the low-voltage
//                                             alarm (0xFFFF when active).
// Address 0xF8 answers whichever meter is on the line: only for a bench with one meter; on a shared line every meter has its own address.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace armor::electrical::pzem {

// CRC-16/MODBUS: polynomial 0xA001 (reflected), start 0xFFFF. Sent low byte first.
inline std::uint16_t crc16(const std::uint8_t* data, std::size_t length) {
  std::uint16_t crc = 0xFFFF;
  for (std::size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc & 1) ? static_cast<std::uint16_t>((crc >> 1) ^ 0xA001) : static_cast<std::uint16_t>(crc >> 1);
  }
  return crc;
}

enum class Model { kAc, kDc };   // PZEM-004T v3, PZEM-017

constexpr std::size_t register_count(Model model) { return model == Model::kAc ? 10 : 8; }
// The whole reply: address, function, byte count, the registers and the CRC.
constexpr std::size_t reply_length(Model model) { return 5 + 2 * register_count(model); }
constexpr std::size_t kRequestLength = 8;

// The request that reads every register of the meter at `address` (1 to 247, or 0xF8).
inline bool build_read_request(Model model, std::uint8_t address, std::array<std::uint8_t, kRequestLength>& out) {
  if (address == 0 || (address > 247 && address != 0xF8)) return false;
  out = {address, 0x04, 0x00, 0x00, 0x00, static_cast<std::uint8_t>(register_count(model)), 0, 0};
  const std::uint16_t crc = crc16(out.data(), 6);
  out[6] = static_cast<std::uint8_t>(crc & 0xFF);
  out[7] = static_cast<std::uint8_t>(crc >> 8);
  return true;
}

struct Reading {
  double voltage_v = 0, current_a = 0, power_w = 0, energy_kwh = 0;
  bool has_frequency = false, has_power_factor = false;
  double frequency_hz = 0, power_factor = 0;
  bool alarm = false;
};

enum class Result { kOk, kTooShort, kCrc, kAddress, kException, kFunction, kLength, kRange };

// A reply checked and read. The meter's own answer is what counts: a wrong address, an exception, a length that is not the one asked for and a CRC that does not
// fit are refused, and so is a reading no real meter could give (a voltage above 1000 V, say), so a garbled frame that happens to pass the CRC does not become a value.
inline Result decode(Model model, std::uint8_t address, const std::uint8_t* data, std::size_t length, Reading& out) {
  if (length < 5) return Result::kTooShort;
  if (data[1] == (0x04 | 0x80)) {   // an exception: address, function | 0x80, code, CRC
    if (length != 5) return Result::kLength;
    const std::uint16_t crc = crc16(data, 3);
    if (data[3] != (crc & 0xFF) || data[4] != (crc >> 8)) return Result::kCrc;
    return Result::kException;
  }
  if (length < reply_length(model)) return Result::kTooShort;
  if (length != reply_length(model)) return Result::kLength;
  const std::uint16_t crc = crc16(data, length - 2);
  if (data[length - 2] != (crc & 0xFF) || data[length - 1] != (crc >> 8)) return Result::kCrc;
  if (address != 0xF8 && data[0] != address) return Result::kAddress;
  if (data[1] != 0x04) return Result::kFunction;
  if (data[2] != 2 * register_count(model)) return Result::kLength;
  std::array<std::uint32_t, 10> r{};
  for (std::size_t i = 0; i < register_count(model); ++i) r[i] = (static_cast<std::uint32_t>(data[3 + 2 * i]) << 8) | data[4 + 2 * i];
  Reading reading;
  if (model == Model::kAc) {
    reading.voltage_v = r[0] / 10.0;
    reading.current_a = (r[1] | (r[2] << 16)) / 1000.0;
    reading.power_w = (r[3] | (r[4] << 16)) / 10.0;
    reading.energy_kwh = (r[5] | (r[6] << 16)) / 1000.0;
    reading.has_frequency = true; reading.frequency_hz = r[7] / 10.0;
    reading.has_power_factor = true; reading.power_factor = r[8] / 100.0;
    reading.alarm = r[9] == 0xFFFF;
  } else {
    reading.voltage_v = r[0] / 100.0;
    reading.current_a = r[1] / 100.0;
    reading.power_w = (r[2] | (r[3] << 16)) / 10.0;
    reading.energy_kwh = (r[4] | (r[5] << 16)) / 1000.0;
    reading.alarm = r[6] == 0xFFFF || r[7] == 0xFFFF;
  }
  if (reading.voltage_v > 1000 || reading.current_a > 1000 || reading.power_w > 1'000'000 || reading.energy_kwh > 10'000'000 || reading.frequency_hz > 100 || reading.power_factor > 1.0) return Result::kRange;
  out = reading;
  return Result::kOk;
}

}  // namespace armor::electrical::pzem
