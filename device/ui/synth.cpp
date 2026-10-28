#include "synth.h"

#include <math.h>
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
  static float buf[MAX_SAMPLES];
  memset(buf, 0, n * sizeof *buf);
  const float TAU = 6.28318531f;
  for (int k = 0; k < shape.count; k++) {
    const Note &note = shape.notes[k];
    int start = int(note.at * RATE);
    float f2 = note.freq * 1.0035f;  // the second voice, six cents sharp
    for (int i = start; i < n; i++) {
      float t = float(i - start) / RATE;
      float body = expf(-t / note.decay);
      if (body < 0.0005f) break;
      float attack = t < 0.006f ? t / 0.006f : 1;
      float v = (sinf(TAU * note.freq * t) + 0.45f * sinf(TAU * f2 * t)) / 1.45f * body;
      v += note.bright * 0.5f * sinf(TAU * note.freq * 2.f * t) * expf(-t / (note.decay * 0.5f));
      v += note.bright * 0.25f * sinf(TAU * note.freq * 3.01f * t) * expf(-t / (note.decay * 0.3f));
      buf[i] += v * attack * note.level;
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
