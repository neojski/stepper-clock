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

AccelStepper motor(AccelStepper::DRIVER, D3, D4);

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
  return (float) (millis() % 60000) / 1e3;
}

int microsteps;

int stepsPerCycle () {
  return 200 * microsteps;
}

long radToSteps(float rad) {
  return rad / 2.0 / PI * stepsPerCycle();
}

float stepsToRad(long steps) {
  return (float)steps / stepsPerCycle() * 2.0 * PI;
}

void setMicrosteps0 (int x) {
  microsteps = x;
  motor.setMaxSpeed(stepsPerCycle());
  motor.setAcceleration(stepsPerCycle()); // with 3x acceleration the motor was missing steps (although I could only see it a couple of times a day), try 1x
}

void setMicrosteps(char kind) {
  switch (kind) {
    case '0':
      digitalWrite(D1, HIGH) ; // 1/2 steps
      digitalWrite(D2, LOW);
      setMicrosteps0(2);
      break;
    case '1':
      digitalWrite(D1, LOW) ; // 1/4 steps
      digitalWrite(D2, HIGH);
      setMicrosteps0(4);
      break;
    case '2':
      // the default microstepping of TMC2208
      digitalWrite(D1, LOW) ; // 1/8 steps
      digitalWrite(D2, LOW);
      setMicrosteps0(8);
      break;
    case '3':
      digitalWrite(D1, HIGH) ; // 1/16 steps
      digitalWrite(D2, HIGH);
      setMicrosteps0(16);
      break;
  }  
}

void setup() {
  Serial.begin(115200);
  Serial.println("starting up");
  motor.setPinsInverted(true, false, false); // rotate opposite direction

  pinMode(D1, OUTPUT);
  pinMode(D2, OUTPUT);

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
float setAngleRad(float rad) {
  float current = target;
  if (abs(rad - current) > 1.1 * PI) {  // use 1.1 to avoid wrapping multiple times
    // wrap around to go to the destination using shortest path
    return current + modPi(rad - current);
  } else {
    return rad;
  }
}

float degToRad(float deg) {
  return deg * 2.0 * PI / 360.0;
}

float radToDeg(float rad) {
  return rad / 2.0 / PI * 360.0;
}

float setAngleDeg(float deg) {
  return setAngleRad(degToRad(deg));
}

float setAngleSeconds(float sec) {
  return setAngleDeg(sec * 360 / 60);
}

float seconds() {
  int sec = getSeconds();
  return setAngleSeconds(sec);
}

float pendulum() {
  float angle = 180 + 18 * cos((float)millis() / 1e3 * PI);
  return setAngleDeg(angle);
}

float smoothSeconds() {
  return setAngleSeconds(getSeconds());
}

float forwardAndBack() {
  int multiSec = (float)(4 * millis()) / 1000.0;
  int c = multiSec % 4;
  if (c == 0 || c == 2) {
    return setAngleSeconds(multiSec / 4);
  } else if (c == 1) {
    return setAngleSeconds(multiSec / 4 + 1);
  }
  return 0;
}

float hours() {
  float hour = timeClient.getHours() + (float) timeClient.getMinutes() / 60;
  return setAngleSeconds(hour * 60 / 12);
}

float (*programs[])() = {
  pendulum, seconds, hours, smoothSeconds
};
void printProgram(int program) {
  switch(program) {
    case 0:
      Serial.print("pendulum ");
      break;
    case 1:
      Serial.print("seconds ");
      break;
    case 2:
      Serial.print("hours ");
      break;
    case 3:
      Serial.print("smoothSeconds");
      break;
  }
}
const int defaultPrograms = 4;

int program;
int getNextProgram() {
  return (program + 1) % (sizeof(programs) / sizeof(programs[0]));
}

float getCurrentPosition(){
  return stepsToRad(motor.currentPosition());
}

int lastApiChange = -1e9; // millis
bool readyForNext(int lastChange) {
  if (millis() - lastApiChange > 10 * 60 * 1000) { // reset after 10m
    if (millis() - lastChange > 30 * 1000) {
      // Allow change every 30s. In practice it'll happen much less frequently
      float nextProgramTarget = programs[getNextProgram()]();

      // It's important we use motor.currentPosition and not target.  The former
      // is continuous so, as long as we use some kind of "seconds" program, we
      // should always be able to eventually satisfy this inequality
      if (abs(nextProgramTarget - getCurrentPosition()) <= degToRad(1)) {
        return true;
      }
    }
  }
  return false;
}

void nextProgramFromApi() {
  lastApiChange = millis();
  program = getNextProgram();
}

int lastChange = -1e9; // millis
void maybeNextProgram() {
  if(readyForNext(lastChange)) {
    Serial.println("next program");
    lastChange = millis();
    program = getNextProgram();
  }
}

void runProgram() {
  target = programs[program]();
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
  motor.moveTo(radToSteps(target));
  motor.run();
}

void handleUdp(String s) {
  if (s.startsWith("microsteps")) {
    char kind = s[10];
    setMicrosteps(kind);
  } else {
    nextProgramFromApi();
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
  maybeNextProgram();
  runProgram(); // computes target
  runMotor();   // runs motor to target
  timeClient.update();
}
