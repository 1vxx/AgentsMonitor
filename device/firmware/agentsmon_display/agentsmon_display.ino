// AgentsMonitor's display, for the Waveshare ESP32-S3-Touch-AMOLED-1.75C.
//
// The screens are device/ui, the same code the review page runs (build.sh
// copies it into src/). Core 1 reads touch, feeds the UI summaries and draws
// each frame a band of rows at a time in internal RAM; each band goes to the
// panel by DMA while the next is drawn. Core 0 fetches summaries over Wi-Fi
// and plays chimes.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#include "audio.h"
#include "net.h"
#include "panel.h"
#include "src/cjk10.h"
#include "src/ui/synth.h"
#include "src/ui/ui.h"
#include "touch.h"

static char *json;
constexpr size_t JSON_MAX = 32 * 1024;
constexpr int BAND_ROWS = 32;  // even, as the panel wants; 30 KB a band

static int64_t nowMs() { return esp_timer_get_time() / 1000; }

void setup() {
  Serial.begin(115200);
  // Over USB with no terminal reading, a write would wait for room and stall
  // the animation; drop the log instead
  Serial.setTxTimeoutMs(0);
  // Core 1 draws without pause, so its idle task never runs to feed the
  // watchdog; the watchdog still covers core 0
  disableCore1WDT();
  delay(300);

  // The two band buffers, in internal DMA-capable RAM, taken before Wi-Fi
  // claims its share
  ui::Stream out = {{nullptr, nullptr}, BAND_ROWS, panel::send, panel::wait};
  for (auto &b : out.buf) {
    b = (uint16_t *)heap_caps_malloc(466 * BAND_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!b) Serial.println("display: no internal RAM for the bands");
  }
  if (!panel::begin()) Serial.println("display: panel init failed");
  panel::brightness(220);

  uint32_t t0 = millis();
  if (!ui::init(CJK10, sizeof CJK10)) Serial.println("display: no memory for the UI");
  ui::stream(out);
  Serial.printf("display: UI ready in %u ms\n", millis() - t0);
  if (!touch::begin()) Serial.println("display: touch not found");
  if (!audio::begin()) Serial.println("display: audio init failed");
  audio::play(synth::DONE);  // a soft chime says the speaker works

  json = (char *)ps_malloc(JSON_MAX);
  net::begin();
  Serial.printf("display: running, %u KB PSRAM and %u KB internal free\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024,
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
}

void loop() {
  static int link = -1;
  static uint32_t statAt = millis(), frames = 0, frameUs = 0;

  int s = net::state();
  if (s != link) ui::link(ui::Link(s)), link = s;
  if (net::take(json, JSON_MAX)) ui::summary(json);

  touch::Event e = touch::poll();
  if (e.kind != touch::NONE) ui::touch(ui::Touch(e.kind - 1), e.x, e.y, nowMs());

  uint32_t t0 = micros();
  ui::frame(nullptr, nowMs());
  frameUs += micros() - t0;
  frames++;

  for (int c; (c = ui::cue()) >= 0;) audio::play(c);

  uint32_t ms = millis();
  if (ms - statAt >= 1000) {
    char line[320], parts[160];
    ui::timings(parts, sizeof parts);
    snprintf(line, sizeof line, "up %lus  fps %.1f  frame %.1f ms  screen %s  link %d  wifi %d | %s", (unsigned long)(ms / 1000),
             frames * 1000.f / (ms - statAt), frameUs / 1000.f / frames, ui::screen(), link, int(WiFi.status()), parts);
    Serial.println(line);
    statAt = ms, frames = 0, frameUs = 0;
  }
}
