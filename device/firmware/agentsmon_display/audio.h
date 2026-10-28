#pragma once

// The speaker: the ES8311 codec over I2S at the synth's 24 kHz, and the
// NS4150B amplifier. Chimes are rendered and played on a task of their own.
namespace audio {

bool begin();
// Queues a synth::Cue; returns at once
void play(int cue);

}  // namespace audio
