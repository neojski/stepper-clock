#include "animation.h"

// Wiper: the hand rests at the hour, sweeps over to the second like a
// windscreen wiper, stops there a moment and sweeps back.
//
// Each cycle takes 6s, starting at multiples of 6 seconds. It sweeps to one
// fixed mark, the second the seconds hand shows when it gets there: following
// the seconds as they move on wouldn't look like a wiper.
const uint32_t cycleMs = 6000;
// A sweep can be half a turn: in 1.5s that peaks at 180 deg/s and 480 deg/s^2
const float sweepSec = 1.5, atSecondSec = 0.5; // then at the hour for the rest
// It arrives 1.5s into the cycle, when the seconds hand has ticked once
const uint32_t secondsOn = 1;

Motion wiper() {
  Motion hour = hours();
  float t = msOfDay % cycleMs / 1e3f;

  float share, shareSlope; // of the way to the second, and per second
  if (t < sweepSec) {
    float u = t / sweepSec;
    share = smoothstep(u);
    shareSlope = smoothstepSlope(u) / sweepSec;
  } else if (t < sweepSec + atSecondSec) {
    share = 1;
    shareSlope = 0;
  } else if (t < 2 * sweepSec + atSecondSec) {
    float u = (t - sweepSec - atSecondSec) / sweepSec;
    share = 1 - smoothstep(u);
    shareSlope = -smoothstepSlope(u) / sweepSec;
  } else {
    return hour;
  }

  // The short way from the hour to the mark
  uint32_t mark = (msOfDay / cycleMs * (cycleMs / 1000) + secondsOn) % 60;
  float across = wrapDeg(mark * 6 - hour.deg);
  return { hour.deg + share * across, hour.degPerSec + shareSlope * across };
}
