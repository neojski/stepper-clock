#include <Arduino.h>
#include <stdarg.h>
#include "platform.h"

const int stepPin = D3;
const int dirPin = D4;

void setupMotorPins() {
  pinMode(stepPin, OUTPUT);
  pinMode(dirPin, OUTPUT);
  digitalWrite(stepPin, LOW);
}

uint32_t msOfDay = 0;

uint32_t nowMs() {
  return millis();
}

uint32_t nowUs() {
  return micros();
}

int32_t randomInt(int32_t min, int32_t max) {
  return random(min, max);
}

void stepPulse(int direction) {
  digitalWrite(dirPin, direction > 0 ? HIGH : LOW);
  digitalWrite(stepPin, HIGH);
  delayMicroseconds(1); // TMC2208 needs > 100ns
  digitalWrite(stepPin, LOW);
}

void logLine(const char *format, ...) {
  char line[128];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  Serial.println(line);
}
