#include "animation.h"

// Pendulum: swings 18 degrees either side of 6, about every 2s.
//
// It takes over from the seconds hand on the beat. The playlist starts it at
// :33.25, when the seconds hand has ticked to 198 degrees, the far end of the
// pendulum's swing. It stays there until the next tick is due, at :34, and
// swings back instead. At :27.25 it's at the other end of its swing, at 162
// degrees, where the seconds hand is at rest until its tick at :28.
//
// So it makes 26.5 swings in 53.25s: 2.009s each.
const float amplitude = 18;
const float swings = 26.5;
const float swingingSec = 53.25;
const float omega = 2 * pi * swings / swingingSec; // rad/s
static uint32_t swingFromMs = 0; // millis

void pendulumStart(uint32_t startMs) {
  // The next tick that's at least half a second away. On schedule that's the
  // one 0.75s away, at :34, with room for the playlist starting us a bit late.
  uint32_t earliest = msOfDay + (startMs - nowMs()) + 500;
  swingFromMs = startMs + 500 + (1000 - earliest % 1000) % 1000;
}

Motion pendulum() {
  // From the swing starting. Not a large number, so fine as a float: (float)
  // millis() loses precision after days of uptime, which made an earlier
  // version stutter.
  float t = secondsSince(swingFromMs);
  if (t <= 0) {
    return { 180 + amplitude, 0 };
  }
  if (t >= swingingSec) {
    return { 180 - amplitude, 0 };
  }
  return { 180 + amplitude * cosf(omega * t), -amplitude * omega * sinf(omega * t) };
}
