#include "synth.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace synth {
namespace {

// One struck note: a fundamental with two quieter partials, each dying away
// at its own rate, after a 6 ms attack, and a second voice a few cents sharp
// that beats slowly against the first
struct Note {
  float at;      // seconds into the cue
  float freq;    // Hz
  float level;   // 0..1
  float decay;   // seconds for the fundamental to fall to 1/e
  float bright;  // how loud the upper partials start, 0..1
};

struct Shape {
  const Note *notes;
  int count;
  float length;  // seconds of notes, before the reverb's tail
};

// Waiting for you: a rising fourth, E5 then A5
const Note ATTENTION_NOTES[] = {{0, 659.25f, 0.5f, 0.45f, 0.35f}, {0.15f, 880.f, 0.55f, 0.6f, 0.35f}};
// Still waiting: the second note alone, softer
const Note REMIND_NOTES[] = {{0, 880.f, 0.32f, 0.45f, 0.25f}};
// A turn finished: a light high note, C6, with G6 a breath behind
const Note DONE_NOTES[] = {{0, 1046.5f, 0.28f, 0.32f, 0.2f}, {0.07f, 1568.f, 0.12f, 0.25f, 0.1f}};
// A turn failed: a falling fourth, low and dark, A4 then E4
const Note ERROR_NOTES[] = {{0, 440.f, 0.48f, 0.35f, 0.1f}, {0.16f, 329.63f, 0.52f, 0.5f, 0.1f}};

const Shape SHAPES[CUES] = {
    {ATTENTION_NOTES, 2, 1.4f},
    {REMIND_NOTES, 1, 1.0f},
    {DONE_NOTES, 2, 0.8f},
    {ERROR_NOTES, 2, 1.2f},
};

constexpr float TAIL_S = 0.7f;  // the reverb rings on this long after the notes
constexpr float WET = 0.22f;

// A small Schroeder reverb: four damped combs in parallel, two allpasses in
// series, delays scaled from Freeverb's to 24 kHz
void reverb(float *buf, int n) {
  static const int COMB[4] = {607, 646, 695, 738}, PASS[2] = {122, 302};
  static float combBuf[4][738], passBuf[2][302];
  memset(combBuf, 0, sizeof combBuf);
  memset(passBuf, 0, sizeof passBuf);
  int ci[4] = {0}, pi[2] = {0};
  float low[4] = {0};
  for (int i = 0; i < n; i++) {
    float in = buf[i] * 0.25f, out = 0;
    for (int k = 0; k < 4; k++) {
      float y = combBuf[k][ci[k]];
      low[k] = y * 0.75f + low[k] * 0.25f;  // damping: the tail darkens as it fades
      combBuf[k][ci[k]] = in + low[k] * 0.8f;
      ci[k] = (ci[k] + 1) % COMB[k];
      out += y;
    }
    for (int k = 0; k < 2; k++) {
      float y = passBuf[k][pi[k]];
      passBuf[k][pi[k]] = out + y * 0.5f;
      out = y - out * 0.5f;
      pi[k] = (pi[k] + 1) % PASS[k];
    }
    buf[i] = buf[i] + out * WET;
  }
}

}  // namespace

int render(Cue cue, int16_t *out, int max) {
  const Shape &shape = SHAPES[cue];
  int n = int((shape.length + TAIL_S) * RATE);
  if (n > max) n = max;
  if (n > MAX_SAMPLES) n = MAX_SAMPLES;
  // On the heap: on the device that is PSRAM, where 200 KB can go
  static float *buf = (float *)malloc(MAX_SAMPLES * sizeof(float));
  if (!buf) return 0;
  memset(buf, 0, n * sizeof *buf);
  // A sine table and running phases and envelopes: sinf of a growing angle
  // and expf every sample are far too slow on the ESP32
  constexpr int TABLE = 1024;
  static float sine[TABLE + 1];
  if (sine[TABLE / 4] == 0)
    for (int i = 0; i <= TABLE; i++) sine[i] = sinf(6.28318531f * i / TABLE);
  auto wave = [](float phase) {  // phase in turns, 0..1
    float at = phase * TABLE;
    int i = int(at);
    return sine[i] + (sine[i + 1] - sine[i]) * (at - i);
  };
  for (int k = 0; k < shape.count; k++) {
    const Note &note = shape.notes[k];
    int start = int(note.at * RATE);
    // The fundamental, the second voice six cents sharp, and the two partials
    const float freq[4] = {note.freq, note.freq * 1.0035f, note.freq * 2.f, note.freq * 3.01f};
    const float decay[4] = {note.decay, note.decay, note.decay * 0.5f, note.decay * 0.3f};
    const float level[4] = {1 / 1.45f, 0.45f / 1.45f, note.bright * 0.5f, note.bright * 0.25f};
    float phase[4] = {0, 0, 0, 0}, step[4], env[4], fall[4];
    for (int p = 0; p < 4; p++) step[p] = freq[p] / RATE, env[p] = level[p], fall[p] = expf(-1.f / (decay[p] * RATE));
    int attack = RATE * 6 / 1000;
    for (int i = start; i < n; i++) {
      if (env[0] < 0.0005f) break;
      float v = 0;
      for (int p = 0; p < 4; p++) {
        v += wave(phase[p]) * env[p];
        phase[p] += step[p];
        phase[p] -= phase[p] >= 1 ? 1 : 0;
        env[p] *= fall[p];
      }
      int t = i - start;
      buf[i] += v * (t < attack ? float(t) / attack : 1) * note.level;
    }
  }
  reverb(buf, n);
  // Fade the last 60 ms so nothing clicks
  int tail = RATE * 6 / 100;
  for (int i = 0; i < n; i++) {
    float s = buf[i] * 22000;
    if (i >= n - tail) s *= float(n - i) / tail;
    out[i] = int16_t(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
  }
  return n;
}

}  // namespace synth
