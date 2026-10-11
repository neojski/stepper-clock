#include "animation.h"

// Stuck: the seconds hand can't get past where it is. Each second it ticks
// into the obstacle, a bit harder each time, and bounces back. Then it breaks
// through and catches up with a burst of quick ticks. It carries on as the
// seconds hand for a few seconds and gets stuck again, every stuckEverySec.
const int attempts = 4;
const float attemptDeg[attempts] = { 3.5, 4, 4.8, 5.5 };
const float hitSec = 0.2, bounceSec = 0.25;
// One for each second missed and the 2 that pass meanwhile. It has to end
// while the seconds hand is at rest on the same second: with 0.3s per tick
// that's 6 ticks for 4 attempts (8 for 5).
const int catchUpTicks = 6;
const float catchUpEverySec = 0.3;
const int stuckEverySec = 10;

static uint32_t originMs = 0;
static float base = 0;

void stuckStart(uint32_t startMs) {
  originMs = nextSecondsRest(startMs, &base);
}

Motion stuck() {
  float sinceStart = secondsSince(originMs);
  if (sinceStart < 0) {
    return seconds();
  }
  // Each time it starts where the seconds hand is at rest
  int episode = sinceStart / stuckEverySec;
  float base = ::base + episode * stuckEverySec * 6;
  // The seconds hand would next tick 0.75s after that
  float t = sinceStart - episode * stuckEverySec - 0.75;
  if (t < 0) {
    return { base, 0 };
  }
  if (t < attempts) {
    int attempt = t;
    float deg = attemptDeg[attempt];
    t -= attempt;
    if (t < hitSec) {
      float u = t / hitSec;
      return { base + deg * smoothstep(u), deg * smoothstepSlope(u) / hitSec };
    }
    t -= hitSec;
    if (t < bounceSec) {
      float u = t / bounceSec;
      return { base + deg * (1 - smoothstep(u)), -deg * smoothstepSlope(u) / bounceSec };
    }
    return { base, 0 };
  }
  t -= attempts;
  int tick = t / catchUpEverySec;
  if (tick >= catchUpTicks) {
    // Caught up
    return seconds();
  }
  float u = (t - tick * catchUpEverySec) / tickSec;
  if (u >= 1) {
    return { base + (tick + 1) * 6, 0 };
  }
  return { base + (tick + smoothstep(u)) * 6, smoothstepSlope(u) * 6 / tickSec };
}
