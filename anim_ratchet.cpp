#include "animation.h"

// Ratchet: like a finger pushing the hand round against a ratchet. The hand
// creeps forward, slowing as the resistance builds, sticks at the tooth, then
// pops over it and jumps ahead, slipping back a touch as the pawl catches.
//
// Each cycle takes 2s and moves the hand 2 seconds on. Cycles start at even
// seconds + 0.25s, when the seconds hand is at rest after its tick, so the
// hand is where the seconds hand is at the start and end of every cycle.
const float pushSec = 1.25, pushDeg = 5;
const float stuckSec = 0.15;
const float popSec = 0.3, popDeg = 8;
const float slipSec = 0.1, slipDeg = 1; // then at rest until the 2s are up

Motion ratchet() {
  uint32_t ms = (msOfDay + 86400000 - 250) % 86400000;
  float from = ms / 2000 * 2 % 60 * 6;
  float t = ms % 2000 / 1e3f;

  if (t < pushSec) {
    // Fast at first then slower and slower: deg = pushDeg * u * (2 - u)
    float u = t / pushSec;
    return { from + pushDeg * u * (2 - u), pushDeg * 2 * (1 - u) / pushSec };
  }
  t -= pushSec;
  from += pushDeg;
  if (t < stuckSec) {
    return { from, 0 };
  }
  t -= stuckSec;
  if (t < popSec) {
    float u = t / popSec;
    return { from + popDeg * smoothstep(u), popDeg * smoothstepSlope(u) / popSec };
  }
  t -= popSec;
  from += popDeg;
  if (t < slipSec) {
    float u = t / slipSec;
    return { from - slipDeg * smoothstep(u), -slipDeg * smoothstepSlope(u) / slipSec };
  }
  return { from - slipDeg, 0 };
}
