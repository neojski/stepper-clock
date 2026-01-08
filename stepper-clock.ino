#include <AccelStepper.h>
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "credentials.h"
#include <ezTime.h>

Timezone timezone;

// README:
//
// Make sure to use wemos D1 mini such that the pin mappings are correct.
// See https://chewett.co.uk/blog/1066/pin-numbering-for-wemos-d1-mini-esp8266

// Notes:
// - with stepper motor it's a bit strange that I'm using PI. This can introduce errors
//   - maybe this isn't actually weird as we need trig operations for things like pendulum
//   - maybe what's weird is that we use both degrees and radians
// - wires going to the motor seem to be sensitive to good connections
// - I set ESP to 160MHz but that doesn't seem to make a difference
// - The potentiometer on TMC2208 is set to 1/4 turn from minimum

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
  return lroundf(rad / 2.0 / PI * stepsPerCycle());
}

float stepsToRad(long steps) {
  return (float)steps / stepsPerCycle() * 2.0 * PI;
}

void setMicrosteps0 (int x) {
  microsteps = x;
  motor.setMaxSpeed(stepsPerCycle());
  motor.setAcceleration(2 * stepsPerCycle()); 
  // with 3x acceleration the motor was missing steps (although I could only see it a couple of times a day), try 1x
  // Having small acceleration (1x) causes pendulum to not be smooth at the edges
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

void printWiFiStatus(int status) {
  switch (status) {
    case WL_IDLE_STATUS:
      Serial.println("Status: Idle (trying to connect)");
      break;
    case WL_NO_SSID_AVAIL:
      Serial.println("Status: SSID not available");
      break;
    case WL_SCAN_COMPLETED:
      Serial.println("Status: Scan completed");
      break;
    case WL_CONNECTED:
      Serial.println("Status: Connected to WiFi");
      break;
    case WL_CONNECT_FAILED:
      Serial.println("Status: Connection failed");
      break;
    case WL_CONNECTION_LOST:
      Serial.println("Status: Connection lost");
      break;
    case WL_DISCONNECTED:
      Serial.println("Status: Disconnected");
      break;
    default:
      Serial.print("Status: Unknown (code ");
      Serial.print(status);
      Serial.println(")");
      break;
  }
}
  

void setup() {
  Serial.begin(115200);
  Serial.println("starting up");
  motor.setPinsInverted(false, false, false); // rotate opposite direction

  pinMode(D1, OUTPUT);
  pinMode(D2, OUTPUT);

  setMicrosteps('3'); // use highest microstepping
  
  WiFi.begin(wifi_ssid, wifi_password);
  while (true) {
    int status = WiFi.status();
    printWiFiStatus(status);
    if (status != WL_CONNECTED) {
      delay(500);
    } else {
      break;
    }
  }

  timeClient.begin();
  if (!timeClient.update()) {
    Serial.println("NTP failed to update");
  }

  udp.begin(8888);

  timezone.setLocation("Europe/London");
  timeClient.setTimeOffset(-60 * timezone.getOffset(timeClient.getEpochTime()));
}


// returns a number in [-PI; PI]
float modPi(float x) {
  if (x >= 0) {
    return fmodf(x + PI, 2 * PI) - PI;
  } else {
    return -modPi(-x);
  }
}

float degToRad(float deg) {
  return deg * 2.0 * PI / 360.0;
}

float radToDeg(float rad) {
  return rad / 2.0 / PI * 360.0;
}

float getAngleDeg(float deg) {
  return degToRad(deg);
}

float getAngleSeconds(float sec) {
  return getAngleDeg(sec * 360 / 60);
}

float seconds() {
  int sec = getSeconds();
  return getAngleSeconds(sec);
}

float pendulum() {
  // TODO: Reduce cosf argument with fmodf to avoid long-run float phase jitter.
  float angle = 180 + 18 * cos((float)millis() / 1e3 * PI);
  return getAngleDeg(angle);
}

float smoothSeconds() {
  return getAngleSeconds(getSeconds());
}

float forwardAndBack() {
  int multiSec = (float)(4 * millis()) / 1000.0;
  int c = multiSec % 4;
  if (c == 0 || c == 2) {
    return getAngleSeconds(multiSec / 4);
  } else if (c == 1) {
    return getAngleSeconds(multiSec / 4 + 1);
  }
  return 0;
}

float hours() {
  float hour = timeClient.getHours() + (float) timeClient.getMinutes() / 60;
  return getAngleSeconds(hour * 60 / 12);
}

// NOTE: be careful with choice of programs to avoid "jumps" described in
// readyForNext
float (*programs[])() = {
  pendulum, seconds, hours, smoothSeconds
};
const int hoursProgram = 2;
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

int program = 1;
int getNextProgram() {
  return (program + 1) % (sizeof(programs) / sizeof(programs[0]));
}

int lastApiChange = -1e9; // millis
bool lastAPIChangeLongTimeAgo () {
  return millis() - lastApiChange > 60 * 1000; // reset after 1m
}

bool readyForNext(int lastChange) {
  if (lastAPIChangeLongTimeAgo()) {
    if (millis() - lastChange > 30 * 1000) {
      // Allow change every 30s. In practice it'll happen much less frequently
      long nextProgramTarget = runProgram(getNextProgram());

      // It's important we use motor.currentPosition and not target.  The former
      // is continuous (?) so, as long as we use some kind of "seconds" program, we
      // should always be able to eventually satisfy this inequality
      if (labs(nextProgramTarget - motor.currentPosition()) <= 1) {
        return true;
      }
    }
  }
  return false;
}

void nextProgramFromApi() {
  if (lastAPIChangeLongTimeAgo()) {
    program = hoursProgram;
  } else {
    program = getNextProgram();
  }
  lastApiChange = millis();
}

int lastChange = millis(); // millis
void maybeNextProgram() {
  if(readyForNext(lastChange)) {
    Serial.println("next program");
    lastChange = millis();
    program = getNextProgram();
  }
}

long wrapTargetSteps(long targetSteps) {
  long cycle = stepsPerCycle();
  if (cycle <= 0) {
    return targetSteps;
  }

  long current = motor.currentPosition();
  long diff = targetSteps - current;
  diff %= cycle;
  if (diff > cycle / 2) {
    diff -= cycle;
  } else if (diff < -cycle / 2) {
    diff += cycle;
  }

  return current + diff;
}

long runProgram(int program) {
  long target = radToSteps(programs[program]());
  return wrapTargetSteps(target);
}

void runMotor(long target) {
  long x = motor.distanceToGo();
  if (x != 0) {
    //Serial.print(motor.distanceToGo());
    //Serial.print(" ");
    //Serial.println(motor.speed());
    //Serial.println(motor.distanceToGo());
    // FIXME: pendulum has numbers far from 0. Maybe that's why it's lagging?
  }
  motor.moveTo(target);
  motor.run();
}

void normalizePositionIfStopped() {
  if (motor.distanceToGo() != 0) {
    return;
  }

  long cycle = stepsPerCycle();
  if (cycle <= 0) {
    return;
  }

  long pos = motor.currentPosition();
  long norm = pos % cycle;
  if (norm < 0) {
    norm += cycle;
  }

  if (norm != pos) {
    motor.setCurrentPosition(norm);
  }
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
  normalizePositionIfStopped();
  runApi();
  maybeNextProgram();
  long target = runProgram(program);
  runMotor(target);
  timeClient.update();
}
