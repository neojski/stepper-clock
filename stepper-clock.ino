#include <AccelStepper.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include "credentials.h"
#include <ezTime.h>
#include "platform.h"
#include "schedule.h"
#include "motor.h"

// This file has what's specific to the clock's hardware: WiFi, OTA updates,
// NTP time, the UDP API. The animations, schedule and motor control are in the
// other files, which don't use Arduino directly.

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

// Only used to park the hand before OTA updates. Otherwise drive() sends the
// step pulses itself. The pins are in platform_arduino.cpp.
extern const int stepPin;
extern const int dirPin;
AccelStepper motor(AccelStepper::DRIVER, stepPin, dirPin);

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

// Updates msOfDay from ezTime
int16_t utcOffsetMin = 0; // ezTime convention: UTC minus local time
time_t offsetCheckedAt = 0;

void readClock() {
  time_t utc = UTC.now();
  uint16_t ms = UTC.ms(LAST_READ); // same reading as utc above
  if (utc - offsetCheckedAt >= 60) {
    // getOffset parses the timezone rules so don't call it every loop
    utcOffsetMin = timezone.getOffset(utc, UTC_TIME);
    offsetCheckedAt = utc;
  }
  time_t local = utc - (time_t) utcOffsetMin * 60;
  msOfDay = (unsigned long) (local % 86400) * 1000 + ms;
}

void setMicrosteps(char kind) {
  switch (kind) {
    case '0':
      digitalWrite(D1, HIGH) ; // 1/2 steps
      digitalWrite(D2, LOW);
      setMicrostepping(2);
      break;
    case '1':
      digitalWrite(D1, LOW) ; // 1/4 steps
      digitalWrite(D2, HIGH);
      setMicrostepping(4);
      break;
    case '2':
      // the default microstepping of TMC2208
      digitalWrite(D1, LOW) ; // 1/8 steps
      digitalWrite(D2, LOW);
      setMicrostepping(8);
      break;
    case '3':
      digitalWrite(D1, HIGH) ; // 1/16 steps
      digitalWrite(D2, HIGH);
      setMicrostepping(16);
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
  
  WiFi.hostname("stepper-clock"); // name shown by the router (DHCP)
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

  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  setupOta();

  udp.begin(8888);

  // Europe/London. Set the rules directly rather than with setLocation() so we
  // don't depend on ezTime's timezone lookup server. Handles BST changes.
  timezone.setPosix("GMT0BST,M3.5.0/1,M10.5.0");
  // ezTime then resyncs every 30 minutes from events()
  if (!waitForSync(10)) {
    Serial.println("NTP failed to sync");
  }
  readClock();
  sprintf(buf, "local time %02lu:%02lu:%02lu.%03lu (%s)", msOfDay / 3600000,
    msOfDay / 60000 % 60, msOfDay / 1000 % 60, msOfDay % 1000,
    timezone.dateTime("H:i:s.v T").c_str());
  Serial.println(buf);
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

// The motor has no position sensor: after a reboot wherever the hand is counts
// as 0 (12 o'clock). So before an OTA update reboots us, move the hand to 12.
void parkAtZero() {
  // Hand AccelStepper our position, starting from a stop
  motor.setMaxSpeed(maxTurnsPerSec * stepsPerCycle());
  motor.setAcceleration(maxTurnsPerSec2 * stepsPerCycle());
  motor.setCurrentPosition(position);
  motor.moveTo(wrapTargetSteps(0));
  while (motor.distanceToGo() != 0) {
    motor.run();
    yield();
  }
}

void setupOta() {
  ArduinoOTA.setHostname("stepper-clock");
  ArduinoOTA.onStart([]() {
    Serial.println("OTA start, parking at 0");
    parkAtZero();
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("OTA done, rebooting");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA error %u\n", error);
  });
  ArduinoOTA.begin();
}

void handleUdp(String s) {
  s.trim();
  if (s.startsWith("play ")) {
    String name = s.substring(5);
    if (!play(name.c_str())) {
      Serial.println("unknown program " + name);
    }
  } else if (s.startsWith("microsteps")) {
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
  // First so that a bug further down in loop() can't block updates
  ArduinoOTA.handle();
  events(); // ezTime NTP resync
  readClock();
  runApi();
  drive(scheduleStep());
}
