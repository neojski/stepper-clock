#include "animation.h"

// Flick: the hand is drawn back as if against a spring, slowing as the spring
// tightens, then let go: it whips forward past the right second and wobbles
// back onto it.
//
// Each cycle takes 4s and ends 4 seconds on. Cycles start at multiples of 4
// seconds + 0.25s, when the seconds hand is at rest after its tick, so the
// hand is where the seconds hand is at the start and end of every cycle.
struct Stage {
  float sec; // how long it takes
  float deg; // where it ends, from the start of the cycle
};
const float pullSec = 1.6, pullDeg = -20, heldSec = 0.15;
const Stage wobble[] = {
  { 0.72, 34 }, // whip past
  { 0.4, 19 },
  { 0.3, 26 },
  { 0.2, 24 }, // 4 seconds on
};

Motion flick() {
  uint32_t ms = (msOfDay + 86400000 - 250) % 86400000;
  float start = ms / 4000 * 4 % 60 * 6;
  float t = ms % 4000 / 1e3f;

  if (t < pullSec) {
    // Fast at first then slower and slower: deg = pullDeg * u * (2 - u)
    float u = t / pullSec;
    return { start + pullDeg * u * (2 - u), pullDeg * 2 * (1 - u) / pullSec };
  }
  t -= pullSec;
  float from = pullDeg;
  if (t < heldSec) {
    return { start + from, 0 };
  }
  t -= heldSec;
  for (const Stage &stage : wobble) {
    if (t < stage.sec) {
      float u = t / stage.sec;
      return { start + from + (stage.deg - from) * smoothstep(u), (stage.deg - from) * smoothstepSlope(u) / stage.sec };
    }
    t -= stage.sec;
    from = stage.deg;
  }
  return { start + from, 0 };
}
