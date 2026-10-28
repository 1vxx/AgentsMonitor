#include "synth.h"

#include <math.h>
#include <string.h>

namespace synth {
namespace {

// One struck note: a fundamental with two quieter partials, each dying away
// at its own rate, after a 6 ms attack
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
  float length;  // seconds
};

// Waiting for you: a rising fourth, E5 then A5
const Note ATTENTION_NOTES[] = {{0, 659.25f, 0.55f, 0.45f, 0.35f}, {0.15f, 880.f, 0.6f, 0.6f, 0.35f}};
// Still waiting: the second note alone, softer
const Note REMIND_NOTES[] = {{0, 880.f, 0.35f, 0.45f, 0.25f}};
// A turn finished: one light high note, C6
const Note DONE_NOTES[] = {{0, 1046.5f, 0.3f, 0.3f, 0.2f}};
// A turn failed: a falling fourth, low and dark, A4 then E4
const Note ERROR_NOTES[] = {{0, 440.f, 0.5f, 0.35f, 0.1f}, {0.16f, 329.63f, 0.55f, 0.5f, 0.1f}};

const Shape SHAPES[CUES] = {
    {ATTENTION_NOTES, 2, 1.4f},
    {REMIND_NOTES, 1, 1.0f},
    {DONE_NOTES, 1, 0.8f},
    {ERROR_NOTES, 2, 1.2f},
};

}  // namespace

int render(Cue cue, int16_t *out, int max) {
  const Shape &shape = SHAPES[cue];
  int n = int(shape.length * RATE);
  if (n > max) n = max;
  memset(out, 0, n * sizeof *out);
  const float TAU = 6.28318531f;
  for (int k = 0; k < shape.count; k++) {
    const Note &note = shape.notes[k];
    int start = int(note.at * RATE);
    for (int i = start; i < n; i++) {
      float t = float(i - start) / RATE;
      float attack = t < 0.006f ? t / 0.006f : 1;
      float body = expf(-t / note.decay);
      float v = sinf(TAU * note.freq * t) * body;
      v += note.bright * 0.5f * sinf(TAU * note.freq * 2.f * t) * expf(-t / (note.decay * 0.5f));
      v += note.bright * 0.25f * sinf(TAU * note.freq * 3.01f * t) * expf(-t / (note.decay * 0.3f));
      float s = out[i] + v * attack * note.level * 22000;
      out[i] = int16_t(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
  }
  // Fade the last 30 ms so nothing clicks
  int tail = RATE * 3 / 100;
  for (int i = n - tail; i < n; i++)
    if (i >= 0) out[i] = int16_t(out[i] * float(n - i) / tail);
  return n;
}

}  // namespace synth
