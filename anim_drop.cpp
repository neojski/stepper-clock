#include "animation.h"

// The Drop: the hand is let go at 3 o'clock like a pendulum held out level and
// swings through 6, each swing smaller, until it rests at 6 dropSec after
// dropOriginMs. The playlist starts it at :15.25, when the tick to 3 o'clock
// has finished, so it's at rest at 6 by :29.25 and the seconds hand can take
// over at :30.25 once its tick to 6 has finished.
//
// angle = 180 - dropAmplitude * envelope * cos(phase)
const int dropReleaseSecond = 15;
const float dropAmplitude = 180 - dropReleaseSecond * 6; // degrees from 6
extern const uint32_t dropStartMs = dropReleaseSecond * 1000 + 250;
const float dropSec = 29.25 - dropStartMs / 1e3;
// Big swings of a real pendulum take longer: start with a 3.5s period and speed
// up to 2.2s as the swings shrink. These peak at ~155 deg/s and ~300 deg/s^2.
// (Releasing from 12 the swings were too fast to look nice.)
const float dropOmegaStart = 2 * pi / 3.5; // rad/s
const float dropOmegaEnd = 2 * pi / 2.2;
const float dropOmegaTau = 15; // s
// The phase speed also ramps up from 0 so the hand tips over gently
const float dropRampTau = 0.4; // s
static uint32_t dropOriginMs = 0;

Motion drop() {
  float t = (int32_t) (nowMs() - dropOriginMs) / 1e3f;
  if (t <= 0) {
    return { 180 - dropAmplitude, 0 };
  }
  if (t >= dropSec) {
    return { 180, 0 };
  }

  // Envelope (1-u)^3 (1+3u): flat at the start so the hand starts at rest,
  // and reaching 0 with zero slope at the end.
  float u = t / dropSec;
  float envelope = (1 - u) * (1 - u) * (1 - u) * (1 + 3 * u);
  float envelopeSlope = -12 * u * (1 - u) * (1 - u) / dropSec; // per second

  // Phase speed (dropOmegaEnd - delta * e^(-t/omegaTau)) * (1 - e^(-t/rampTau)),
  // integrated in closed form so the phase is exact at any time.
  float delta = dropOmegaEnd - dropOmegaStart;
  float tau3 = dropRampTau * dropOmegaTau / (dropRampTau + dropOmegaTau);
  float e1 = expf(-t / dropRampTau);
  float e2 = expf(-t / dropOmegaTau);
  float e3 = expf(-t / tau3);
  float phaseSpeed = (dropOmegaEnd - delta * e2) * (1 - e1);
  float phase = dropOmegaEnd * (t - dropRampTau * (1 - e1))
    - delta * (dropOmegaTau * (1 - e2) - tau3 * (1 - e3));

  return {
    180 - dropAmplitude * envelope * cosf(phase),
    dropAmplitude * (envelope * phaseSpeed * sinf(phase) - envelopeSlope * cosf(phase))
  };
}

void dropStart(uint32_t startMs) {
  // Glide to the release point first if crossfading
  dropOriginMs = startMs;
}
