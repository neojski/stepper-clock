#include <AccelStepper.h>
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "credentials.h"

// README:
//
// Make sure to use wemos D1 mini such that the pin mappings are correct.
// See https://chewett.co.uk/blog/1066/pin-numbering-for-wemos-d1-mini-esp8266

// Notes:
// - with stepper motor it's a bit strange that I'm using PI. This can introduce errors
// - wires going to the motor seem to be sensitive to good connections
// - I set ESP to 160MHz but that doesn't seem to make a difference

AccelStepper motor(AccelStepper::DRIVER, D2, D5);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

WiFiUDP udp;

// Inconvenient deduping debug:
// sprintf(buf, "program: %i", result); debug();
char buf[100];
String lastDebug = "";
void debug() {
  String x = String(buf);
  if (!x.equals(lastDebug)) {
    Serial.println(x);
    lastDebug = x;
  }
}

float getSeconds() {
  // TODO: use timeClient.getSeconds() sowehow?
  return (float) (micros() % 60000000) / 1e6;
}

int microsteps;

int stepsPerCycle () {
  return 200 * microsteps;
}

void setMicrosteps0 (int x) {
  microsteps = x;
  motor.setMaxSpeed(stepsPerCycle());
  motor.setAcceleration(stepsPerCycle()); // with 3x acceleration the motor was missing steps (although I could only see it a couple of times a day), try 1x
}

void setMicrosteps(char kind) {
  switch (kind) {
    case '0':
      digitalWrite(D3, HIGH) ; // 1/2 steps
      digitalWrite(D4, LOW);
      setMicrosteps0(2);
      break;
    case '1':
      digitalWrite(D3, LOW) ; // 1/4 steps
      digitalWrite(D4, HIGH);
      setMicrosteps0(4);
      break;
    case '2':
      // the default microstepping of TMC2208
      digitalWrite(D3, LOW) ; // 1/8 steps
      digitalWrite(D4, LOW);
      setMicrosteps0(8);
      break;
    case '3':
      digitalWrite(D3, HIGH) ; // 1/16 steps
      digitalWrite(D4, HIGH);
      setMicrosteps0(16);
      break;
  }  
}

void setup() {
  Serial.begin(115200);
  Serial.println("starting up");
  motor.setPinsInverted(true, false, false); // rotate opposite direction

  pinMode(D3, OUTPUT);
  pinMode(D4, OUTPUT);

  setMicrosteps('3'); // use highest microstepping
  
  WiFi.begin(wifi_ssid, wifi_password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  timeClient.begin();
  if (!timeClient.update()) {
    Serial.println("NTP failed to update");
  }

  udp.begin(8888);
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

void seconds() {
  int sec = getSeconds();
  setAngleSeconds(sec);
}

void pendulum() {
  float angle = 180 + 20 * cos((float)micros() / 1e6 * PI);
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

void hours() {
  float hour = timeClient.getHours() + (float) timeClient.getMinutes() / 60;
  setAngleSeconds(hour * 60 / 12);
}

void minutes() {
  float minutes = timeClient.getMinutes();
  setAngleSeconds(minutes);
}

void (*programs[])() = {
  pendulum, seconds, hours, minutes, smoothSeconds, forwardAndBack
};
const int defaultPrograms = 3;

int program;
int lastApiChange = -1e9; // millis
int getProgram() {
  if (millis() - lastApiChange > 10 * 60 * 1000) { // reset after 10m
    int result = (int)(millis() / 1000 / 60) % defaultPrograms; // cycle programs every minute
    return result;
  } else {
    return program;
  }
}

void nextProgram() {
  lastApiChange = millis();
  program = (program + 1) % (sizeof(programs) / sizeof(programs[0]));
}

void runProgram() {
  int program = getProgram();
  programs[program]();
}

void runMotor() {
  int x = motor.distanceToGo();
  if (x != 0) {
    Serial.print(motor.distanceToGo());
    Serial.print(" ");
    Serial.println(motor.speed());
    //Serial.println(motor.distanceToGo());
    // FIXME: pendulum has numbers far from 0. Maybe that's why it's lagging?
  }
  float frac = target / 2.0 / PI;
  long absolute = frac * stepsPerCycle();
  motor.moveTo(absolute);
  motor.run();
}

void setMicrostepping(char kind) {
  switch (kind) {
    case '0':
      digitalWrite(D3, HIGH) ; // 1/2 steps
      digitalWrite(D4, LOW);
      break;
    case '1':
      digitalWrite(D3, LOW) ; // 1/4 steps
      digitalWrite(D4, HIGH);
      break;
    case '2':
      digitalWrite(D3, LOW) ; // 1/8 steps
      digitalWrite(D4, LOW);
      break;
    case '3':
      digitalWrite(D3, HIGH) ; // 1/16 steps
      digitalWrite(D4, HIGH);
      break;
  }  
}

void handleUdp(String s) {
  if (s.startsWith("microsteps")) {
    char kind = s[10];
    setMicrosteps(kind);
  } else {
    nextProgram();
  }
}

void runApi() {
  if (udp.parsePacket()) {
    Serial.println("new packet");

    char packetBuffer[UDP_TX_PACKET_MAX_SIZE];
    int packetSize = udp.read(packetBuffer, sizeof(packetBuffer));

    if (packetSize > 0) {
      // Null-terminate the received data
      packetBuffer[packetSize] = 0;

      // Print the received data
      Serial.print("Received packet: ");
      Serial.println(packetBuffer);

      handleUdp(packetBuffer); // for now on any packet just rotate programs

      // TODO: looks like trying to send the message back crashes ESP
      //udp.beginPacket(udp.remoteIP(), udp.remotePort());
      //udp.print("Hello, client!");
      //udp.endPacket();
    }
  }
}

void loop() {
  runApi();
  runProgram(); // computes target
  runMotor();   // runs motor to target
  timeClient.update();
}