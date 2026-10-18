#include "animation.h"

// Forward and back: R R L P, every 2 seconds, the same every time: tick
// forward, tick forward, tick back, pause. Every tick is sharp, 0.5s apart.
//
// It keeps time with no catching up: each forward tick moves the hand 1.5
// seconds (9 degrees) and the tick back a second, so the net is 1.5 + 1.5 - 1 =
// 2 seconds forward in the 2 seconds it takes. At the start of each phrase the
// hand is where the seconds hand is before its tick, so it can take over from
// it and give back to it at any even second.
const float forwardSeconds = 1.5; // how far each forward tick moves the hand
const float forwardSec = 0.35;    // how long it takes: 6 * 9 / 0.35^2 = 441 deg/s^2

Motion forwardAndBack() {
  uint32_t msOfMinute = msOfDay % 60000;
  float behind = (float) (msOfMinute / 2000 * 2) - 1; // where the hand is at the start of the phrase
  float t = msOfMinute % 2000 / 1e3f;

  float position = behind, speed = 0;
  auto move = [&](float at, float seconds, float duration) {
    float u = (t - at) / duration;
    if (u >= 1) {
      position += seconds;
    } else if (u > 0) {
      position += seconds * smoothstep(u);
      speed += seconds * smoothstepSlope(u) / duration;
    }
  };
  move(0.0, forwardSeconds, forwardSec); // R
  move(0.5, forwardSeconds, forwardSec); // R
  move(1.0, -1, tickSec);                // L
  // P: the rest of the 2 seconds
  return { position * 6, speed * 6 };
}
