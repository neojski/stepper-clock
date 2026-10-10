// Motor control: makes the hand follow a Motion
#pragma once

#include "animation.h"

// Motion has to stay within these or the motor can miss steps. With 3 turns/s^2
// it was missing steps (a couple of times a day); 1 turn/s^2 made the pendulum
// not smooth at the edges.
const float maxTurnsPerSec = 1;
const float maxTurnsPerSec2 = 2;

// The hand's position in steps from 12 o'clock
extern int32_t position;

int stepsPerCycle();

// Set the step size to 1/microsteps of a full step, keeping the hand's position
void setMicrostepping(int microsteps);

// Follow m: call every loop()
void drive(Motion m);
