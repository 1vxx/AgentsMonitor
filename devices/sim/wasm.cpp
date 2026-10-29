// The UI core's C interface for the review page. The page owns the font
// buffer (it must outlive the module) and reads frames and chimes back out of
// the module's memory.

#include <emscripten.h>

#include "gfx.h"
#include "synth.h"
#include "ui.h"

namespace {
uint16_t fb[gfx::W * gfx::H];
uint8_t rgba[gfx::W * gfx::H * 4];
int16_t pcm[synth::MAX_SAMPLES];
double drawMs;
}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE int am_init(const uint8_t *cjk, int cjkLen) { return ui::init(cjk, cjkLen); }

EMSCRIPTEN_KEEPALIVE void am_link(int state) { ui::link(ui::Link(state)); }

EMSCRIPTEN_KEEPALIVE int am_summary(const char *json) { return ui::summary(json); }

EMSCRIPTEN_KEEPALIVE void am_touch(int kind, int x, int y, double ms) { ui::touch(ui::Touch(kind), x, y, int64_t(ms)); }

EMSCRIPTEN_KEEPALIVE void am_press(double ms) { ui::press(int64_t(ms)); }

// Draws a frame and returns it as RGBA, with everything outside the round panel transparent
EMSCRIPTEN_KEEPALIVE uint8_t *am_frame(double ms, double wall) {
  double t0 = emscripten_get_now();
  ui::frame(int64_t(ms), int64_t(wall));
  ui::draw(fb, 0, gfx::H);
  drawMs = emscripten_get_now() - t0;
  for (int y = 0; y < gfx::H; y++) {
    float dy = y + 0.5f - gfx::CY;
    for (int x = 0; x < gfx::W; x++) {
      int i = y * gfx::W + x;
      uint16_t p = gfx::swap(fb[i]);
      int r = p >> 11, g = p >> 5 & 63, b = p & 31;
      float dx = x + 0.5f - gfx::CX;
      bool in = dx * dx + dy * dy <= gfx::CX * gfx::CX;
      rgba[i * 4] = uint8_t(r << 3 | r >> 2);
      rgba[i * 4 + 1] = uint8_t(g << 2 | g >> 4);
      rgba[i * 4 + 2] = uint8_t(b << 3 | b >> 2);
      rgba[i * 4 + 3] = in ? 255 : 0;
    }
  }
  return rgba;
}

EMSCRIPTEN_KEEPALIVE double am_draw_ms() { return drawMs; }

EMSCRIPTEN_KEEPALIVE int am_cue() { return ui::cue(); }

// Renders a chime into the module's sample buffer; returns how many samples
EMSCRIPTEN_KEEPALIVE int am_synth(int cue) { return synth::render(synth::Cue(cue), pcm, synth::MAX_SAMPLES); }

EMSCRIPTEN_KEEPALIVE int16_t *am_pcm() { return pcm; }

EMSCRIPTEN_KEEPALIVE const char *am_screen() { return ui::screen(); }
}
