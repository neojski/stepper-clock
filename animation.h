// Animations
//
// A program gives the hand's Motion for the current time. The scheduler
// crossfades between programs so the hand never jumps, and drive() makes the
// motor follow.
#pragma once

#include <math.h>
#include <stdint.h>
#include "platform.h"

// The hand's angle (degrees clockwise from 12) and angular speed
struct Motion {
  float deg;
  float degPerSec;
};

typedef Motion (*Program)();

const double pi = 3.14159265358979323846;

// Smoothstep: eases 0 -> 1 as u goes 0 -> 1, with zero slope at both ends
inline float smoothstep(float u) {
  return u * u * (3 - 2 * u);
}

inline float smoothstepSlope(float u) {
  return 6 * u * (1 - u);
}

// Shortest signed difference between two angles, in (-180, 180]
inline float wrapDeg(float deg) {
  deg = fmodf(deg, 360);
  if (deg > 180) {
    deg -= 360;
  } else if (deg <= -180) {
    deg += 360;
  }
  return deg;
}

inline float clampf(float x, float low, float high) {
  return x < low ? low : x > high ? high : x;
}

inline float getSeconds() {
  return (float) (msOfDay % 60000) / 1e3;
}

// msOfDay - then, allowing for midnight
inline int32_t msSince(uint32_t then) {
  int32_t diff = (int32_t) msOfDay - (int32_t) then;
  if (diff < -43200000) {
    diff += 86400000;
  } else if (diff > 43200000) {
    diff -= 86400000;
  }
  return diff;
}

// Ticking seconds hand. Each tick is a smoothstep move over tickSec at the
// start of the second, short enough to look like a tick but within the
// motor's acceleration limit (peak 6 * 6deg / tickSec^2 = 576 deg/s^2).
const float tickSec = 0.25;

// anim_seconds.cpp
Motion seconds();
Motion smoothSeconds();
Motion hours();

// anim_pendulum.cpp
Motion pendulum();

// anim_drop.cpp
Motion drop();
// startMs: millis when the playlist entry has fully faded in
void dropStart(uint32_t startMs);
// ms of the minute the playlist starts the drop at
extern const uint32_t dropStartMs;
// millis the drop started at
extern uint32_t dropOriginMs;

// anim_forward_and_back.cpp
Motion forwardAndBack();
void forwardAndBackStart(uint32_t startMs);
