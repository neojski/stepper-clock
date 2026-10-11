#include "animation.h"

// Bounce: the hand is let go at 3 o'clock like the drop, but 6 o'clock is a
// floor: it falls, hits it and bounces back up the same side, each bounce
// lower, like a dropped ball, until it rests at 6.
//
// The motor can't reverse instantly, so each impact is spread out: the hand
// sinks a little past 6 as it slows, like a ball squashing, and is pushed back
// out more gently, which is what makes it leave slower than it arrived.
const float releaseDeg = 90; // above the floor
const float gravity = 60; // deg/s^2
const float restitution = 0.72; // speed after an impact / speed before
// Impacts are as sharp as the acceleration allows, up to a limit
const float impactAccel = 600; // deg/s^2
const float maxImpactOmega = 20; // rad/s
const float stopBelowSpeed = 6; // deg/s: the last impact just stops

static uint32_t originMs = 0;

void bounceStart(uint32_t startMs) {
  originMs = startMs;
}

Motion bounce() {
  float t = secondsSince(originMs);
  if (t <= 0) {
    return { 180 - releaseDeg, 0 };
  }
  // height is above the floor, speed downward
  float fallSec = sqrtf(2 * releaseDeg / gravity);
  if (t < fallSec) {
    return { 180 - (releaseDeg - gravity * t * t / 2), gravity * t };
  }
  t -= fallSec;
  float speed = gravity * fallSec;
  while (true) {
    // Slowing: a quarter of a sine wave down to speed / omega below the floor
    float omega = fminf(impactAccel / speed, maxImpactOmega);
    float sink = speed / omega;
    float slowSec = (float) pi / (2 * omega);
    if (t < slowSec) {
      return { 180 + sink * sinf(omega * t), speed * cosf(omega * t) };
    }
    t -= slowSec;
    if (speed < stopBelowSpeed) {
      // Ease back up to the floor and stay
      float settleSec = 0.4;
      if (t >= settleSec) {
        return { 180, 0 };
      }
      float u = t / settleSec;
      return { 180 + sink * (1 - smoothstep(u)), -sink * smoothstepSlope(u) / settleSec };
    }
    // Pushed back out, slower: back to the floor at restitution * speed
    float outOmega = restitution * omega;
    float outSec = (float) pi / (2 * outOmega);
    if (t < outSec) {
      return { 180 + sink * cosf(outOmega * t), -sink * outOmega * sinf(outOmega * t) };
    }
    t -= outSec;
    speed *= restitution;
    // Up and down again
    float flightSec = 2 * speed / gravity;
    if (t < flightSec) {
      return { 180 - (speed * t - gravity * t * t / 2), -(speed - gravity * t) };
    }
    t -= flightSec;
  }
}
