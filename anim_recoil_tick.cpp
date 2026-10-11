#include "animation.h"

// Recoil tick: a seconds hand that twitches back a little before each tick,
// like the hand of an old pendulum clock, whose escapement pushes it back
// before letting it go.
//
// The recoil starts before the second so the hand arrives 0.25s after it,
// like seconds(), and is at rest in the same places from then until the next
// recoil.
const float recoilDeg = 1.5;
const uint32_t recoilMs = 120, forwardMs = 280;
const uint32_t leadMs = recoilMs + forwardMs - 250; // starts this long before the second

Motion recoilTick() {
  uint32_t ms = msOfDay % 60000 + leadMs;
  float from = ((float) (ms / 1000) - 1) * 6; // where it rests before this tick
  uint32_t t = ms % 1000;
  if (t < recoilMs) {
    float u = (float) t / recoilMs;
    return { from - recoilDeg * smoothstep(u), -recoilDeg * smoothstepSlope(u) / (recoilMs / 1e3f) };
  }
  t -= recoilMs;
  if (t < forwardMs) {
    float u = (float) t / forwardMs;
    float deg = 6 + recoilDeg;
    return { from - recoilDeg + deg * smoothstep(u), deg * smoothstepSlope(u) / (forwardMs / 1e3f) };
  }
  return { from + 6, 0 };
}
