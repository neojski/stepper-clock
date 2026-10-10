#include "animation.h"

Motion seconds() {
  uint32_t msOfMinute = msOfDay % 60000;
  float second = msOfMinute / 1000;
  float u = (msOfMinute % 1000) / 1e3 / tickSec;
  if (u >= 1) {
    return { second * 6, 0 };
  }
  return { (second - 1 + smoothstep(u)) * 6, smoothstepSlope(u) * 6 / tickSec };
}

Motion smoothSeconds() {
  return { getSeconds() * 6, 6 };
}

Motion hours() {
  // Use the full time of day so the hand advances one microstep at a time
  // (~every 13.5s at 1/16 microstepping) instead of a ~3mm jump once a minute.
  return { (float) (msOfDay / 3.6e6 * 30), 30.0f / 3600 };
}
