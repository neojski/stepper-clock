#include "animation.h"

// Pendulum period is 2s (omega = pi). Reduce the time modulo the period in
// integer math before converting to float: a large time as float loses
// precision (e.g. (float)millis() has 32ms steps after ~3 days of uptime)
// which made the pendulum stutter more and more the longer the clock ran.
// Using the same clock as the seconds programs keeps them in phase: the
// period divides both a minute and a day.
const uint32_t pendulumPeriodMs = 2000;

Motion pendulum() {
  float t = (float)(msOfDay % pendulumPeriodMs) / 1e3;
  return { 180 + 18 * cosf((float) pi * t), -18 * (float) pi * sinf((float) pi * t) };
}
