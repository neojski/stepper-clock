

#include <AccelStepper.h>

// Define the stepper motor and the pins that is connected to
AccelStepper motor(1, 2, 5); // (Type of driver: with 2 pins, STEP, DIR)

void setup() {
  // Set maximum speed value for the motor
  motor.setMaxSpeed(5000);
  motor.setAcceleration(1000);
}

float targetAngle;
void setAngleRad(float rad) {
  targetAngle = rad * 400;
}

float degToRad(float deg) {
  return deg * 2.0 * PI / 360.0;
}

void setAngleDeg(float deg) {
  setAngleRad(degToRad(deg));
}

int getSeconds() {
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
  float angle = 90 * cos(millis() / 1000. * PI);
  setAngleDeg(angle);
}

void smoothSeconds() {
  float sec = millis() / 1000.;
  setAngleSeconds(sec);
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

void loop() {
  forwardAndBack();
  motor.moveTo(targetAngle);
  motor.run();
}