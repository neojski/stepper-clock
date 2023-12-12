#include <AccelStepper.h>

AccelStepper motor(AccelStepper::DRIVER, 2, 5);

void setup() {
  motor.setMaxSpeed(5000);
  motor.setAcceleration(1000);
}

float targetAngle;
void setAngleRad(float rad) {
  targetAngle = rad * 200;
}

float degToRad(float deg) {
  return deg * 2.0 * PI / 360.0;
}

void setAngleDeg(float deg) {
  setAngleRad(degToRad(deg));
}

float getSeconds() {
  return (float)millis() / 1000.0;
}

void setAngleSeconds(float sec) {
  setAngleDeg(sec * 360 / 60);
}

void seconds (int dir) {
  int sec = dir * getSeconds();
  setAngleSeconds(sec);
}

void pendulum() {
  float angle = 90 * cos(getSeconds() * PI);
  setAngleDeg(angle);
}

void smoothSeconds() {
  setAngleSeconds(getSeconds());
}

void forwardAndBack() {
  int multiSec = (float) (4 * millis()) / 1000.0;
  int c = multiSec % 4;
  if (c == 0 || c == 2) {
    setAngleSeconds(multiSec / 4);
  } else if (c == 1) {
    setAngleSeconds(multiSec / 4 + 1);
  }
}

void hour() {
  float hour = getSeconds() / 3600;
  setAngleSeconds(hour);
}

const int maxPrograms = 6;
int getProgram() {
  return (int)(getSeconds() / 10) % maxPrograms;
}

void loop() {
  switch (getProgram()) {
    case 0:
      seconds(1);
      break;
    case 1:
      seconds(-1);
      break;
    case 2:
      pendulum();
      break;
    case 3:
      forwardAndBack();
      break;
    case 4:
      smoothSeconds();
      break;
    case 5:
      hour();
      break;
  }
  motor.moveTo(targetAngle);
  motor.run();
}