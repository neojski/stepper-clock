#include <AccelStepper.h>
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "credentials.h"

// https://chewett.co.uk/blog/1066/pin-numbering-for-wemos-d1-mini-esp8266/

AccelStepper motor(AccelStepper::DRIVER, 4 /* D2 */, 14 /* D5 */);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

float timeOffset;
float getSeconds() {
  float result = timeClient.getSeconds() + fmodf(millis(), 1);
  Serial.print("seconds ");
  Serial.println(result);
  return result;
}

void setup() {
  Serial.begin(115200);
  Serial.println("starting up");
  motor.setMaxSpeed(5000);
  motor.setAcceleration(1000);

  WiFi.begin(wifi_ssid, wifi_password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  timeClient.begin();
  if (!timeClient.update()) {
    Serial.println("NTP failed to update");
  }
}

// returns a number in [-PI; PI]
float modPi(float x) {
  if (x >= 0) {
    return fmodf(x + PI, 2 * PI) - PI;
  } else {
    return -modPi(-x);
  }
}

float target;
void setAngleRad(float rad) {
  float current = target;
  if (abs(rad - current) > 1.1 * PI) {  // use 1.1 to avoid wrapping multiple times
    // wrap around to go to the destination using shortest path
    Serial.println("wrap");
    Serial.println(current);
    Serial.println(rad);
    Serial.println(modPi(rad - current));
    target = current + modPi(rad - current);
  } else {
    target = rad;
  }
}

float degToRad(float deg) {
  return deg * 2.0 * PI / 360.0;
}

void setAngleDeg(float deg) {
  setAngleRad(degToRad(deg));
}

void setAngleSeconds(float sec) {
  setAngleDeg(sec * 360 / 60);
}

void seconds(int dir) {
  int sec = dir * getSeconds();
  setAngleSeconds(sec);
}

void pendulum() {
  float angle = 45 * cos(getSeconds() * PI);
  setAngleDeg(angle);
}

void smoothSeconds() {
  setAngleSeconds(getSeconds());
}

void forwardAndBack() {
  int multiSec = (float)(4 * millis()) / 1000.0;
  int c = multiSec % 4;
  if (c == 0 || c == 2) {
    setAngleSeconds(multiSec / 4);
  } else if (c == 1) {
    setAngleSeconds(multiSec / 4 + 1);
  }
}

void hour() {
  float hour = timeClient.getHours();
  setAngleSeconds(hour * 60 / 12);
}

const int maxPrograms = 6;
int getProgram() {
  return 0; // FIXME
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

  float frac = target / 2.0 / PI;
  long absolute = frac * 200 * 8.;  // TODO: I'm not really sure why this needs to be multiplied by 8
  motor.moveTo(absolute);
  motor.run();
  Serial.println("loop");

  timeClient.update();
  Serial.println(timeClient.getFormattedTime());
}