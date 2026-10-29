// Wired as Waveshare's 1.75C ES8311 example: MCLK 16, BCLK 9, WS 45, data
// out 8, data in 10, amplifier enable 46. The codec sits on the shared I2C
// bus, which touch.cpp starts.

#include "audio.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <esp_heap_caps.h>

#include "es8311.h"
#include "src/ui/synth.h"

namespace audio {
namespace {

constexpr int PIN_MCLK = 16, PIN_BCLK = 9, PIN_WS = 45, PIN_DOUT = 8, PIN_DIN = 10, PIN_PA = 46;
constexpr int VOLUME = 70;  // 0..100
constexpr int FRAMES = 512;  // per write

I2SClass i2s;
QueueHandle_t cues;
int16_t *mono;

void task(void *) {
  static int16_t stereo[FRAMES * 2];
  for (;;) {
    int cue;
    xQueueReceive(cues, &cue, portMAX_DELAY);
    int n = synth::render(synth::Cue(cue), mono, synth::MAX_SAMPLES);
    for (int at = 0; at < n; at += FRAMES) {
      int k = n - at < FRAMES ? n - at : FRAMES;
      for (int i = 0; i < k; i++) stereo[i * 2] = stereo[i * 2 + 1] = mono[at + i];
      i2s.write((const uint8_t *)stereo, k * 4);
    }
  }
}

}  // namespace

bool begin() {
  mono = (int16_t *)heap_caps_malloc(synth::MAX_SAMPLES * 2, MALLOC_CAP_SPIRAM);
  if (!mono) return false;
  i2s.setPins(PIN_BCLK, PIN_WS, PIN_DOUT, PIN_DIN, PIN_MCLK);
  if (!i2s.begin(I2S_MODE_STD, synth::RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) return false;

  es8311_handle_t codec = es8311_create(0, ES8311_ADDRESS_0);
  if (!codec) return false;
  const es8311_clock_config_t clock = {
      .mclk_inverted = false,
      .sclk_inverted = false,
      .mclk_from_mclk_pin = true,
      .mclk_frequency = synth::RATE * 256,
      .sample_frequency = synth::RATE,
  };
  if (es8311_init(codec, &clock, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) return false;
  es8311_sample_frequency_config(codec, clock.mclk_frequency, clock.sample_frequency);
  es8311_voice_volume_set(codec, VOLUME, nullptr);

  pinMode(PIN_PA, OUTPUT);
  digitalWrite(PIN_PA, HIGH);

  cues = xQueueCreate(8, sizeof(int));
  xTaskCreatePinnedToCore(task, "audio", 4096, nullptr, 3, nullptr, 0);
  return true;
}

void play(int cue) {
  if (cues) xQueueSend(cues, &cue, 0);
}

}  // namespace audio
