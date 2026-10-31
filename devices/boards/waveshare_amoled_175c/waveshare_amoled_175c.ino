// AgentsMonitor's display, for the Waveshare ESP32-S3-Touch-AMOLED-1.75C.
//
// The screens are a theme from devices/themes on devices/core, the same code
// the review page runs (build.sh copies them into src/). Core 1 reads touch,
// the side buttons and which way up the board stands, feeds the UI summaries
// and works out each frame; then both cores draw it a band of rows at a time
// in internal RAM, and each band goes to the panel by DMA while more are drawn.
// Core 0 also fetches summaries over Wi-Fi and plays chimes.
//
// Upside down is the board's business alone: the theme draws the same frame,
// each band goes out back to front to the rows opposite, and touches are
// turned to match. The CO5300 has flip bits, but with a window a band at a time
// they left the picture in stripes.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#include <atomic>

#include "audio.h"
#include "button.h"
#include "data.h"
#include "net.h"
#include "panel.h"
#include "src/cjk10.h"
#include "src/ui/gfx.h"
#include "src/ui/jpeg.h"
#include "src/ui/synth.h"
#include "src/ui/ui.h"
#include "tilt.h"
#include "touch.h"

static char *json;
constexpr size_t JSON_MAX = 32 * 1024;
constexpr int W = 466, H = 466;
constexpr int BAND_ROWS = 16;  // even, as the panel wants; 15 KB a band
constexpr int BANDS = (H + BAND_ROWS - 1) / BAND_ROWS;
constexpr int BUFFERS = 4;  // a band being drawn on each core, and two on their way out

// Band n, counted from start-up, is drawn in buffer n % BUFFERS. A frame's
// bands go to whichever core is free next, and to the panel in order.
static uint16_t *buffers[BUFFERS];
static std::atomic<int> nextBand;
static uint32_t firstBand;                // the count at this frame's first band
static volatile uint32_t drawn[BUFFERS];  // n + 1 once band n is drawn
static uint32_t queued;                   // bands handed to the panel
static bool upsideDown;                   // set between frames
static SemaphoreHandle_t sendLock, helperGo, helperDone;

static int64_t nowMs() { return esp_timer_get_time() / 1000; }

static int bandRows(int b) { return b * BAND_ROWS + BAND_ROWS < H ? BAND_ROWS : H - b * BAND_ROWS; }

// The nth band of a frame to go out. The panel's top comes first, the way it
// scans, so an upside-down frame tears no differently: that is its last band.
static int bandAt(int n) { return upsideDown ? BANDS - 1 - n : n; }

// Half a turn, in place
static void IRAM_ATTR turnBand(uint16_t *band, int rows) {
  uint16_t *a = band, *b = band + rows * W - 1;
  while (a < b) {
    uint16_t p = *a;
    *a++ = *b;
    *b-- = p;
  }
}

// Draws bands of the frame until none are left; both cores run it
static void drawBands() {
  for (int b; (b = nextBand.fetch_add(1)) < BANDS;) {
    uint32_t n = firstBand + b;
    int k = n % BUFFERS;
    // The band drawn in this buffer before must be out first
    while (panel::sent() + BUFFERS <= n) {
    }
    int at = bandAt(b);
    ui::draw(buffers[k], at * BAND_ROWS, at * BAND_ROWS + bandRows(at));
    if (upsideDown) turnBand(buffers[k], bandRows(at));
    drawn[k] = n + 1;
    // Queue it, and any after it the other core finished first
    xSemaphoreTake(sendLock, portMAX_DELAY);
    while (drawn[queued % BUFFERS] == queued + 1) {
      int q = bandAt(queued - firstBand), y0 = q * BAND_ROWS, rows = bandRows(q);
      panel::send(buffers[queued % BUFFERS], upsideDown ? H - y0 - rows : y0, rows);
      queued++;
    }
    xSemaphoreGive(sendLock);
  }
}

// Core 0's share of the work of each frame
static void (*job)(int half, void *arg);
static void *jobArg;

static void helper(void *) {
  for (;;) {
    xSemaphoreTake(helperGo, portMAX_DELAY);
    job(1, jobArg);
    xSemaphoreGive(helperDone);
  }
}

// Runs fn's half 1 on core 0 while half 0 runs here, on core 1
static void onBoth(void (*fn)(int half, void *arg), void *arg) {
  job = fn, jobArg = arg;
  xSemaphoreGive(helperGo);
  fn(0, arg);
  xSemaphoreTake(helperDone, portMAX_DELAY);
}

void setup() {
  Serial.begin(115200);
  // Over USB with no terminal reading, a write would wait for room and stall
  // the animation; drop the log instead
  Serial.setTxTimeoutMs(0);
  // Core 1 draws without pause, so its idle task never runs to feed the
  // watchdog; the watchdog still covers core 0
  disableCore1WDT();
  delay(300);

  // The band buffers, in internal DMA-capable RAM, taken before Wi-Fi claims its share
  for (auto &b : buffers) {
    b = (uint16_t *)heap_caps_malloc(W * BAND_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!b) Serial.println("display: no internal RAM for the bands");
  }
  if (!panel::begin()) Serial.println("display: panel init failed");
  panel::brightness(220);

  uint32_t t0 = millis();
  size_t theirs = 0;
  if (data::open(theirs)) Serial.printf("display: %u KB of theme data\n", unsigned(theirs / 1024));
  jpeg::decode = data::decode;
  if (!ui::init(CJK10, sizeof CJK10, nullptr, theirs, theirs ? data::read : nullptr)) Serial.println("display: no memory for the UI");
  Serial.printf("display: UI ready in %u ms\n", millis() - t0);
  if (!touch::begin()) Serial.println("display: touch not found");
  if (!button::begin()) Serial.println("display: power chip not found, PWR button off");
  if (!tilt::begin()) Serial.println("display: motion sensor not found, staying this way up");
  if (!audio::begin()) Serial.println("display: audio init failed");
  audio::play(synth::DONE);  // a soft chime says the speaker works

  sendLock = xSemaphoreCreateMutex();
  helperGo = xSemaphoreCreateBinary();
  helperDone = xSemaphoreCreateBinary();
  // Above the network task, below Wi-Fi's own and the chimes
  xTaskCreatePinnedToCore(helper, "draw", 6144, nullptr, 2, nullptr, 0);
  gfx::split = onBoth;

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
  if (e.kind != touch::NONE) {
    if (upsideDown) e.x = W - 1 - e.x, e.y = H - 1 - e.y;
    ui::touch(ui::Touch(e.kind - 1), e.x, e.y, nowMs());
  }
  button::Event b = button::poll();
  if (b == button::PRESS) ui::press(nowMs());
  if (b == button::HOLD) ui::summon(nowMs());

  // Not under a finger, whose drag would jump across the screen
  bool turned = tilt::upsideDown();
  if (!touch::held()) upsideDown = turned;

  uint32_t t0 = micros();
  ui::frame(nowMs());
  firstBand = queued;
  nextBand = 0;
  onBoth([](int, void *) { drawBands(); }, nullptr);
  frameUs += micros() - t0;
  frames++;

  for (int c; (c = ui::cue()) >= 0;) audio::play(c);
  char key[96];
  for (int64_t endedAt; ui::seen(key, sizeof key, &endedAt);) net::seen(key, endedAt);

  uint32_t ms = millis();
  if (ms - statAt >= 1000) {
    char line[400], parts[240];
    ui::timings(parts, sizeof parts);
    const float *g = tilt::gravity();
    snprintf(line, sizeof line, "up %lus  fps %.1f  frame %.1f ms  screen %s  link %d  wifi %d  ram %uK  g %.2f %.2f %.2f%s | %s",
             (unsigned long)(ms / 1000), frames * 1000.f / (ms - statAt), frameUs / 1000.f / frames, ui::screen(), link, int(WiFi.status()),
             unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), g[0], g[1], g[2], upsideDown ? " flipped" : "", parts);
    Serial.println(line);
    statAt = ms, frames = 0, frameUs = 0;
  }
}
