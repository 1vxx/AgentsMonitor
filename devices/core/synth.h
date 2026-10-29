#pragma once

#include <stdint.h>

// The display's chimes, synthesised: soft bell tones, so the device needs no
// sound files and the review page plays the very samples the speaker will.
namespace synth {

enum Cue { ATTENTION, REMIND, DONE, ERROR, LEGEND, CUES };
constexpr int RATE = 24000;
constexpr int MAX_SAMPLES = RATE * 9 / 4;

// Renders the cue as mono 16-bit samples; returns how many
int render(Cue cue, int16_t *out, int max);

}  // namespace synth
