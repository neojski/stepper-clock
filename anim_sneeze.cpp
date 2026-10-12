#include "animation.h"

// Sneeze: ah... ah... AH... the hand leans back in three growing gasps, then
// CHOO: it lurches forward past the right second and shudders back onto it.
//
// Each gasp is a quick intake that slows as it peaks, like the flick's pull
// back, and between them the hand sags forward a little. (Plain eased steps
// back looked like slow backward ticks.)
//
// Each cycle takes 6s and ends 6 seconds on. Like the flick, cycles start at
// multiples of 6 seconds + 0.25s, when the seconds hand is at rest after its
// tick, so the hand is where the seconds hand is at the start and end.
struct Stage {
  float sec; // how long it takes to get there
  float deg; // where, from the start of the cycle
  float holdSec; // and how long it stays
  bool gasp; // speeds up as fast as it can then slows gently, rather than eased
};
const float gaspAccel = 600; // deg/s^2
const Stage stages[] = {
  { 0.4, -8, 0, true },    // ah
  { 0.5, -4, 0 },          // sags
  { 0.5, -17, 0, true },   // ah
  { 0.5, -11, 0 },
  { 0.6, -30, 0.45, true }, // AH, held
  { 0.9, 50, 0 },          // CHOO, past the 36 degrees that 6 seconds is
  { 0.45, 30, 0 },
  { 0.35, 40, 0 },
  { 0.25, 34, 0 },
  { 0.2, 36, 0 },
};

Motion sneeze() {
  uint32_t ms = (msOfDay + 86400000 - 250) % 86400000;
  float start = ms / 6000 * 6 % 60 * 6;
  float t = ms % 6000 / 1e3f;

  float from = 0;
  for (const Stage &stage : stages) {
    float distance = stage.deg - from;
    if (t < stage.sec && stage.gasp) {
      // Constant acceleration up to the top speed, then constant slowing to
      // rest: the top speed is twice the average
      float direction = distance < 0 ? -1 : 1;
      float topSpeed = 2 * fabsf(distance) / stage.sec;
      float speedUpSec = topSpeed / gaspAccel;
      if (t < speedUpSec) {
        return { start + from + direction * gaspAccel * t * t / 2, direction * gaspAccel * t };
      }
      float left = stage.sec - t;
      float slowing = topSpeed / (stage.sec - speedUpSec);
      return { start + stage.deg - direction * slowing * left * left / 2, direction * slowing * left };
    }
    if (t < stage.sec) {
      float u = t / stage.sec;
      return { start + from + distance * smoothstep(u), distance * smoothstepSlope(u) / stage.sec };
    }
    t -= stage.sec;
    from = stage.deg;
    if (t < stage.holdSec) {
      return { start + from, 0 };
    }
    t -= stage.holdSec;
  }
  return { start + from, 0 };
}
