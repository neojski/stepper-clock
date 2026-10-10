// What the clock logic needs from the hardware. Implemented for the clock in
// platform_arduino.cpp; the rest of the logic doesn't include Arduino headers
// so it can also be compiled for a simulator.
#pragma once

#include <stdint.h>

// Local time of day in ms, synced with NTP. Updated once per loop() so that
// all programs see the same time.
extern uint32_t msOfDay;

// Like millis() and micros()
uint32_t nowMs();
uint32_t nowUs();

// Random integer in [min, max)
int32_t randomInt(int32_t min, int32_t max);

// Move the motor one step, direction +1 (clockwise) or -1
void stepPulse(int direction);

// printf to the serial log, adding a newline
void logLine(const char *format, ...);
