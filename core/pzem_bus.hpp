// ARMOR-ELECTRICAL - what a serial line with several energy meters does: ask one meter at a time, wait for its reply, go on to the next, and keep the last good reading
// of each. It touches no hardware: the firmware feeds it the bytes that arrive and the time, and sends the bytes it returns, so the whole exchange (the order of the
// requests, a meter that does not answer, a reply that is garbled or belongs to another address) is tested on a computer with a stand-in for the meters.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "pzem.hpp"

namespace armor::electrical {

// One meter of the line and the channel of the message it makes.
struct MeterConfig {
  std::string channel;          // the channel id in the message
  std::string label;            // its name, or empty
  pzem::Model model = pzem::Model::kAc;
  std::uint8_t address = 1;
};

struct MeterState {
  MeterConfig config;
  pzem::Reading reading;
  bool have_reading = false;
  std::uint64_t reading_at_ms = 0;
  std::uint32_t replies_ok = 0, replies_bad = 0, timeouts = 0;
  int misses_in_a_row = 0;
  pzem::Result last_result = pzem::Result::kOk;
};

class PzemBus {
 public:
  static constexpr std::uint64_t kReplyTimeoutMs = 400;
  /// A reading older than this is not published: a meter that stopped answering must not go on showing its last figures.
  static constexpr std::uint64_t kFreshMs = 10'000;

  PzemBus(std::vector<MeterConfig> meters, std::uint64_t cycle_ms) : cycle_ms_(cycle_ms) {
    for (MeterConfig& meter : meters) { MeterState state; state.config = std::move(meter); meters_.push_back(std::move(state)); }
  }

  /// The bytes to send now, or none. Call it often (every few tens of milliseconds).
  std::vector<std::uint8_t> next_tx(std::uint64_t now_ms) {
    if (meters_.empty()) return {};
    if (waiting_ && now_ms >= deadline_ms_) on_timeout(now_ms);
    if (waiting_ || now_ms < next_ms_) return {};
    std::array<std::uint8_t, pzem::kRequestLength> request{};
    if (!pzem::build_read_request(meters_[at_].config.model, meters_[at_].config.address, request)) { advance(now_ms); return {}; }
    waiting_ = true;
    buffer_.clear();
    last_request_.assign(request.begin(), request.end());
    deadline_ms_ = now_ms + kReplyTimeoutMs;
    return last_request_;
  }

  /// Bytes that arrived. A reply is whole when it has the length of the meter's answer (or an exception's five bytes).
  void on_rx(const std::uint8_t* data, std::size_t length, std::uint64_t now_ms) {
    if (!waiting_) return;                                  // nobody asked: noise or a late reply
    for (std::size_t i = 0; i < length && buffer_.size() < 64; ++i) buffer_.push_back(data[i]);
    MeterState& meter = meters_[at_];
    resync(meter.config.address);
    const std::size_t whole = pzem::reply_length(meter.config.model);
    const bool exception = buffer_.size() >= 2 && buffer_[1] == (0x04 | 0x80);
    if (buffer_.size() < (exception ? 5 : whole)) return;
    pzem::Reading reading;
    const pzem::Result result = pzem::decode(meter.config.model, meter.config.address, buffer_.data(), buffer_.size(), reading);
    meter.last_result = result;
    if (result == pzem::Result::kOk) {
      meter.reading = reading; meter.have_reading = true; meter.reading_at_ms = now_ms; meter.misses_in_a_row = 0; ++meter.replies_ok;
    } else { ++meter.replies_bad; ++meter.misses_in_a_row; }
    advance(now_ms);
  }

  const std::vector<MeterState>& meters() const { return meters_; }
  /// The meters that have a fresh reading, in order.
  std::vector<const MeterState*> fresh(std::uint64_t now_ms) const {
    std::vector<const MeterState*> out;
    for (const MeterState& meter : meters_) if (meter.have_reading && now_ms - meter.reading_at_ms <= kFreshMs) out.push_back(&meter);
    return out;
  }

 private:
  // An RS485 adapter that listens while it sends hands the request back before the answer, and a line can carry a stray byte: what comes before the answer is dropped. First the
  // echo of our own request, whole (it is never an answer: an answer starts with the meter's address and function 0x04 or 0x84, a request with its address and the same function
  // but a different length), then stray bytes in front of this meter's answer. Address 0xF8 answers whichever meter is on the line, so it is left alone.
  void resync(std::uint8_t address) {
    if (buffer_.size() >= last_request_.size() && !last_request_.empty() && std::equal(last_request_.begin(), last_request_.end(), buffer_.begin()))
      buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(last_request_.size()));
    if (address == 0xF8 || buffer_.empty() || buffer_.front() == address) return;
    // stray bytes in front: the answer starts where this meter's address is followed by its function (or the exception's); when there is no such place the buffer is left alone
    // and is judged as it is (an answer of another meter is reported as such, not thrown away)
    for (std::size_t i = 1; i + 1 < buffer_.size(); ++i) {
      if (buffer_[i] == address && (buffer_[i + 1] == 0x04 || buffer_[i + 1] == (0x04 | 0x80))) { buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(i)); return; }
    }
  }

  void on_timeout(std::uint64_t now_ms) {
    MeterState& meter = meters_[at_];
    ++meter.timeouts; ++meter.misses_in_a_row;
    meter.last_result = pzem::Result::kTooShort;
    advance(now_ms);
  }
  // On to the next meter; after the last one, wait for the rest of the cycle.
  void advance(std::uint64_t now_ms) {
    waiting_ = false;
    buffer_.clear();
    if (++at_ >= meters_.size()) { at_ = 0; next_ms_ = cycle_started_ms_ + cycle_ms_; if (next_ms_ < now_ms) next_ms_ = now_ms; cycle_started_ms_ = next_ms_; }
  }

  std::vector<MeterState> meters_;
  std::vector<std::uint8_t> buffer_, last_request_;
  std::uint64_t cycle_ms_, deadline_ms_ = 0, next_ms_ = 0, cycle_started_ms_ = 0;
  std::size_t at_ = 0;
  bool waiting_ = false;
};

}  // namespace armor::electrical
