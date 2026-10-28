#pragma once

#include "gfx.h"
#include "model.h"

// The pieces of the dashboard's Halo (web/src/components/Halo.tsx, Orb.tsx):
// the mood orb with its three drifting colours, its glow, the scale round the
// rim and the track the satellites ride.
namespace halo {

// Where the home screen's orb sits: above centre, leaving room for two lines
constexpr float OX = 233, OY = 180, ORB_R = 86;
constexpr float R_TRACK = 200;  // the satellites' orbit, round the panel's centre
constexpr float R_TICK = 210;   // the scale grows outward from here

struct Palette {
  gfx::RGB m1, m2, m3;
};
Palette palette(model::Mood mood);
Palette mix(const Palette &a, const Palette &b, float t);

// How the orb's blobs have drifted: each runs its keyframes there and back
struct Motion {
  float phase[3];
  float spin;  // turns
};

bool init();

// Black, with the home orb's glow in --m1 at `strength`, dithered against
// RGB565 banding. Skips what an orb of radius `cover` at the home spot hides.
void background(gfx::RGB m1, float strength, float cover);
// A faint sky that turns once in twenty minutes, each star twinkling: depth
// for the black, and it never holds a pixel lit long enough to burn in
void stars(float seconds, float alpha);
// The orb as a glass sphere: the blobs inside, a soft highlight up and to the
// left, a lit rim and a little light bounced up from below
void orb(float cx, float cy, float R, const Palette &p, const Motion &m);
// 120 ticks, longer every 15 and 30 degrees, turned by `turn`, brighter where
// a slow light at `sweep` turns passes over them
void scale(float turn, gfx::RGB major, float alpha, float sweep);
// A tail fading out behind a satellite at `head` turns, `span` turns long
void tail(float r, float head, float span, uint16_t c, float alpha);
// A lit pearl: its glow, a dark seat that cuts the track, the body and a glint
void satellite(float x, float y, float radius, uint16_t c, float glow, bool hollow, float alpha);

}  // namespace halo
