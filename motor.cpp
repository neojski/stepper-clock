#include "motor.h"

int32_t position = 0;
int microsteps = 0;

int stepsPerCycle() {
  return 200 * microsteps;
}

void setMicrostepping(int x) {
  if (microsteps) {
    // Same physical position in the new step size
    position = lround((double) position * x / microsteps);
  }
  microsteps = x;
}

// Run at the program's speed, plus a correction proportional to the position
// error so the hand never drifts. The commanded speed changes no faster than
// the acceleration limit, so even a big error can't jerk the motor.
//
// We send the step pulses ourselves rather than with AccelStepper's
// runSpeed(), which takes at most one step per loop() and drops the time a step
// was late by, so fast moves come out slower than asked. Instead keep track of
// the steps owed and send all that are due each loop.

const float correctionPerSec = 8; // fraction of the error corrected per second
float commandedSpeed = 0; // steps/s
float stepsOwed = 0;
// Don't catch up on more than this after loop() stalled (e.g. NTP sync): the
// correction makes up the rest smoothly.
const float maxStepsOwed = 8;
uint32_t lastDriveUs = 0;

void stepOnce(int direction) {
  stepPulse(direction);
  position += direction;
}

void drive(Motion m) {
  float cycle = stepsPerCycle();

  // Keep the step count small. The hand can go round forever.
  position %= (int32_t) cycle;

  float error = fmodf(m.deg / 360 * cycle - position, cycle);
  if (error > cycle / 2) {
    error -= cycle;
  } else if (error < -cycle / 2) {
    error += cycle;
  }
  // Within half a step is as close as we can get
  if (fabsf(error) <= 0.5) {
    error = 0;
  }

  float wanted = m.degPerSec / 360 * cycle + correctionPerSec * error;
  if (isnan(wanted)) {
    // A bug in a program. Don't let it into commandedSpeed, where it would
    // stay and stop the motor for good.
    wanted = 0;
  }

  uint32_t now = nowUs();
  float dt = (now - lastDriveUs) / 1e6;
  lastDriveUs = now;
  float maxChange = maxTurnsPerSec2 * cycle * dt;
  commandedSpeed = clampf(wanted, commandedSpeed - maxChange, commandedSpeed + maxChange);
  commandedSpeed = clampf(commandedSpeed, -maxTurnsPerSec * cycle, maxTurnsPerSec * cycle);

  stepsOwed = clampf(stepsOwed + commandedSpeed * dt, -maxStepsOwed, maxStepsOwed);
  while (stepsOwed >= 1) {
    stepOnce(1);
    stepsOwed -= 1;
  }
  while (stepsOwed <= -1) {
    stepOnce(-1);
    stepsOwed += 1;
  }
}
