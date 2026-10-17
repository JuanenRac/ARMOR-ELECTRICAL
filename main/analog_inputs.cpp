// ARMOR-ELECTRICAL - the analog inputs of the base board. Nothing here has run on a board.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// One task reads the enabled inputs in turn. A pin is sampled as fast as the converter allows for a fixed window (a whole number of mains periods for both 50 and 60 Hz: 200 ms for an
// effective value, 100 ms for an average), the samples are converted to millivolts with the chip's own calibration, and core/analog.hpp makes the quantity and the channel. Only ADC1 is
// used: the converter shared with the Wi-Fi radio is never touched. The input range is 0 to about 3.1 V (12 dB attenuation); what is wired to a pin must never go beyond 3.3 V.
#include "analog_inputs.hpp"

#include <mutex>
extern "C" {
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
}
#include "core/analog.hpp"
#include "entropy.hpp"

namespace armor::analoginputs {
namespace {
constexpr char kTag[] = "armor-analog";
constexpr std::uint64_t kFreshMs = 15000;   // a reading older than this is not in the message

std::mutex g_lock;
config::Settings g_settings;
std::array<InputStatus, board::kAnalogCount> g_status;
std::array<electrical::AnalogChannel, board::kAnalogCount> g_channels;
std::array<std::uint64_t, board::kAnalogCount> g_read_at_ms{};
std::array<bool, board::kAnalogCount> g_have{};

std::uint64_t now_ms() { return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL; }

void analog_task(void*) {
  adc_oneshot_unit_handle_t unit = nullptr;
  adc_oneshot_unit_init_cfg_t unit_config{};
  unit_config.unit_id = ADC_UNIT_1;
  if (adc_oneshot_new_unit(&unit_config, &unit) != ESP_OK) {
    ESP_LOGE(kTag, "the converter could not be started: the analog inputs are not read");
    std::lock_guard<std::mutex> guard(g_lock);
    for (std::size_t i = 0; i < board::kAnalogCount; ++i) if (g_settings.analogs[i].enabled) { g_status[i].state = "error"; g_status[i].error = "adc"; }
    vTaskDelete(nullptr);
    return;
  }
  std::array<adc_cali_handle_t, board::kAnalogCount> calibration{};
  std::array<bool, board::kAnalogCount> ready{};
  std::array<analog::Energy, board::kAnalogCount> energy;
  std::array<std::uint64_t, board::kAnalogCount> last_ms{};
  for (std::size_t i = 0; i < board::kAnalogCount; ++i) {
    const config::AnalogSetting& input = g_settings.analogs[i];
    if (!input.enabled) continue;
    const adc_channel_t channel = static_cast<adc_channel_t>(board::adc1_channel(input.pin));
    adc_oneshot_chan_cfg_t channel_config{};
    channel_config.atten = ADC_ATTEN_DB_12;
    channel_config.bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_cali_curve_fitting_config_t cali_config{};
    cali_config.unit_id = ADC_UNIT_1;
    cali_config.chan = channel;
    cali_config.atten = ADC_ATTEN_DB_12;
    cali_config.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_oneshot_config_channel(unit, channel, &channel_config) == ESP_OK && adc_cali_create_scheme_curve_fitting(&cali_config, &calibration[i]) == ESP_OK) {
      ready[i] = true;
      std::lock_guard<std::mutex> guard(g_lock);
      g_status[i].state = "waiting";
    } else {
      ESP_LOGE(kTag, "GPIO %d could not be set up as an analog input", input.pin);
      std::lock_guard<std::mutex> guard(g_lock);
      g_status[i].state = "error";
      g_status[i].error = "adc";
    }
  }
  std::vector<double> millivolts;
  millivolts.reserve(8000);
  esp_task_wdt_add(nullptr);   // a task that hangs here would leave the readings to go stale unseen: the watchdog restarts the board
  for (;;) {
    esp_task_wdt_reset();
    bool any = false;
    for (std::size_t i = 0; i < board::kAnalogCount; ++i) {
      const config::AnalogSetting& input = g_settings.analogs[i];
      if (!input.enabled || !ready[i]) continue;
      any = true;
      const adc_channel_t channel = static_cast<adc_channel_t>(board::adc1_channel(input.pin));
      const std::uint64_t window_us = input.mode == "ac" ? 200000ULL : 100000ULL;
      millivolts.clear();
      {
        std::lock_guard<std::mutex> adc(armor::adc_lock());   // nobody else touches the converter during a window (see entropy.cpp): a login must not spoil it
        const std::uint64_t started = static_cast<std::uint64_t>(esp_timer_get_time());
        while (static_cast<std::uint64_t>(esp_timer_get_time()) - started < window_us && millivolts.size() < 8000) {
          int raw = 0, mv = 0;
          if (adc_oneshot_read(unit, channel, &raw) == ESP_OK && adc_cali_raw_to_voltage(calibration[i], raw, &mv) == ESP_OK) millivolts.push_back(static_cast<double>(mv));
        }
      }
      const std::uint64_t at = now_ms();
      const std::uint64_t elapsed = last_ms[i] == 0 ? 0 : at - last_ms[i];
      last_ms[i] = at;
      electrical::AnalogChannel made;
      if (analog::make_channel(input, millivolts, elapsed, energy[i], made)) {
        std::lock_guard<std::mutex> guard(g_lock);
        g_channels[i] = made;
        g_read_at_ms[i] = at;
        g_have[i] = true;
        g_status[i].state = "reading";
        g_status[i].error.clear();
        g_status[i].millivolts = analog::mean(millivolts);
        g_status[i].value = analog::quantity_of(input, millivolts);
        g_status[i].samples = static_cast<std::uint32_t>(millivolts.size());
      } else {
        std::lock_guard<std::mutex> guard(g_lock);
        g_status[i].state = "error";
        g_status[i].error = "adc";
        g_have[i] = false;
      }
      esp_task_wdt_reset();
      vTaskDelay(1);   // let the other tasks run between two inputs
    }
    if (!any) vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
}  // namespace

void start(const config::Settings& settings) {
  g_settings = settings;
  bool any = false;
  for (std::size_t i = 0; i < board::kAnalogCount; ++i) {
    g_status[i].enabled = settings.analogs[i].enabled;
    g_status[i].state = settings.analogs[i].enabled ? "waiting" : "disabled";
    any = any || settings.analogs[i].enabled;
  }
  if (any) xTaskCreate(analog_task, "analog", 6144, nullptr, 2, nullptr);   // below the tasks that talk to the network and the relays
}

std::vector<electrical::AnalogChannel> channels() {
  std::vector<electrical::AnalogChannel> out;
  const std::uint64_t now = now_ms();
  std::lock_guard<std::mutex> guard(g_lock);
  for (std::size_t i = 0; i < board::kAnalogCount; ++i) if (g_have[i] && now - g_read_at_ms[i] <= kFreshMs) out.push_back(g_channels[i]);
  return out;
}

std::array<InputStatus, board::kAnalogCount> status() { std::lock_guard<std::mutex> guard(g_lock); return g_status; }

}  // namespace armor::analoginputs
