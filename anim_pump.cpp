#include "animation.h"

// Pump: from :45.25 the hand pumps its way up the left side of the dial from 9
// o'clock to nearly 12, against gravity, slipping back a little every 2
// seconds. Each cycle it heaves up, slowing as gravity works against it, holds
// for a moment, and slips back, speeding up like a fall and caught with a
// jolt: 12 degrees forward in all, as far as the seconds hand goes in 2s, so at
// the end of each cycle it's at rest where the seconds hand is.
//
// How far it slips, and how hard gravity pulls, is how steep the slope is,
// which is how far the hand is from the top: most at 9 o'clock, less and less
// up to 12. So the slips fade away and it never goes past 12. It stops at
// :59.25, 6 degrees short of the top, and the playlist carries it smoothly over
// it with the sweeping seconds hand.
namespace {

const float slipAtNine = 7;   // degrees slipped where the slope is steepest
const float gravityAtNine = 55; // deg/s^2: speeds a slip up there
const float heaveAccel = 450; // deg/s^2: how quickly a heave gets going
const float catchAccel = 450; // deg/s^2: how hard a slip is caught
const float cycleSec = 2, heaveSec = 0.8, holdSec = 0.1;
const float cycleDeg = 12;    // what the seconds hand covers in a cycle
const float climbSec = 14;    // :45.25 to :59.25

// A heave of distance degrees over heaveSec: up to speed as fast as it can then
// slowing steadily to rest, t seconds in
Motion heave(float t, float distance) {
  float topSpeed = 2 * distance / heaveSec;
  float speedUpSec = topSpeed / heaveAccel;
  if (t < speedUpSec) {
    return { heaveAccel * t * t / 2, heaveAccel * t };
  }
  float left = heaveSec - t;
  float slowing = topSpeed / (heaveSec - speedUpSec);
  return { distance - slowing * left * left / 2, slowing * left };
}

// A slip of distance degrees downhill: speeds up under gravity, then is caught
float slipSpeed(float distance, float gravity) {
  return sqrtf(2 * distance / (1 / gravity + 1 / catchAccel));
}

float slipSec(float distance, float gravity) {
  return slipSpeed(distance, gravity) * (1 / gravity + 1 / catchAccel);
}

Motion slip(float t, float distance, float gravity) {
  float v = slipSpeed(distance, gravity);
  float fallSec = v / gravity;
  if (t < fallSec) {
    return { gravity * t * t / 2, gravity * t };
  }
  float s = t - fallSec;
  if (s < v / catchAccel) {
    return { v * fallSec / 2 + v * s - catchAccel * s * s / 2, v - catchAccel * s };
  }
  return { distance, 0 };
}

}  // namespace

Motion pump() {
  float t = ((msOfDay % 60000) + 60000 - 45250) % 60000 / 1e3f; // since :45.25
  if (t >= climbSec) {
    return seconds();
  }
  int cycle = t / cycleSec;
  float tau = t - cycle * cycleSec;
  float base = 270 + cycle * cycleDeg;

  // 1 at 9 o'clock, where the slope is steepest, down to 0 at 12
  float steepness = sinf((base - 180) * (float) pi / 180);
  float slipDeg = slipAtNine * steepness;
  float gravity = gravityAtNine * steepness;
  float heaveDeg = cycleDeg + slipDeg;

  Motion m;
  if (tau < heaveSec) {
    m = heave(tau, heaveDeg);
  } else if (tau < heaveSec + holdSec) {
    m = { heaveDeg, 0 };
  } else if (tau < heaveSec + holdSec + slipSec(slipDeg, gravity)) {
    Motion s = slip(tau - heaveSec - holdSec, slipDeg, gravity);
    m = { heaveDeg - s.deg, -s.degPerSec };
  } else {
    m = { cycleDeg, 0 };
  }
  return { base + m.deg, m.degPerSec };
}
