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

// Hardware notes:
// - Use a Wemos D1 mini, so that the pin mappings are correct:
//   https://chewett.co.uk/blog/1066/pin-numbering-for-wemos-d1-mini-esp8266
// - The wires going to the motor are sensitive to good connections.
// - The potentiometer on the TMC2208 is set to 1/4 turn from minimum.
// - The CPU runs at 80MHz (see deploy.sh). 160MHz didn't seem to make a
//   difference.

// In platform_arduino.cpp
void setupMotorPins();

WiFiUDP udp;

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
  setupMotorPins();
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
  unsigned long ms = msOfDay;
  Serial.printf("local time %02lu:%02lu:%02lu.%03lu (%s)\n", ms / 3600000,
    ms / 60000 % 60, ms / 1000 % 60, ms % 1000,
    timezone.dateTime("H:i:s.v T").c_str());
}

// The motor has no position sensor: after a reboot wherever the hand is counts
// as 0 (12 o'clock). So before an OTA update reboots us, move the hand to 12.
void parkAtZero() {
  // It takes a couple of seconds at most. Don't hold up the update for ever
  // if something's wrong.
  unsigned long start = millis();
  while (!park() && millis() - start < 5000) {
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

// What "status" reports about the commands received
unsigned long commandCount = 0;
char lastCommand[24] = "";
unsigned long lastCommandMs = 0; // millis

// How the clock is doing, as one line of key=value for "status". The motor
// has no sensor, so hand is where the clock believes the hand is.
void writeStatus(char *out, size_t size) {
  unsigned long up = millis() / 1000;
  unsigned long ago = (millis() - lastCommandMs) / 1000;
  unsigned long ms = msOfDay;
  const char *ntp = timeStatus() == timeSet ? "set"
    : timeStatus() == timeNeedsSync ? "needs-sync" : "not-set";
  int n = snprintf(out, size,
    "program=%s time=%02lu:%02lu:%02lu uptime=%lud%02luh%02lum heap=%u rssi=%d ntp=%s "
    "hand=%.1f reset=\"%s\" commands=%lu last=\"%s\"",
    currentProgramName(), ms / 3600000, ms / 60000 % 60, ms / 1000 % 60,
    up / 86400, up / 3600 % 24, up / 60 % 60, ESP.getFreeHeap(), WiFi.RSSI(), ntp,
    position * 360.0 / stepsPerCycle(), ESP.getResetReason().c_str(), commandCount, lastCommand);
  if (commandCount > 0 && n > 0 && (size_t) n < size) {
    n += snprintf(out + n, size - n, " (%lus ago)", ago);
  }
  struct rst_info *reset = ESP.getResetInfoPtr();
  if (reset->reason == REASON_EXCEPTION_RST && n > 0 && (size_t) n < size) {
    snprintf(out + n, size - n, " exccause=%u epc1=0x%lx", (unsigned) reset->exccause,
      (unsigned long) reset->epc1);
  }
}

// Handle a command. If it has a reply, write it to reply.
void handleUdp(String s, char *reply, size_t replySize) {
  s.trim();
  reply[0] = 0;
  if (s.equals("status")) {
    // Not counted as the last command: that's what status is asked about
    writeStatus(reply, replySize);
    return;
  }
  commandCount++;
  lastCommandMs = millis();
  strncpy(lastCommand, s.c_str(), sizeof(lastCommand) - 1);
  lastCommand[sizeof(lastCommand) - 1] = 0;

  if (s.startsWith("play ")) {
    String name = s.substring(5);
    if (!play(name.c_str())) {
      Serial.println("unknown program " + name);
    }
  } else if (s.startsWith("microsteps")) {
    char kind = s[10];
    setMicrosteps(kind);
  } else if (s.equals("next")) {
    playNext();
  } else {
    Serial.println("unknown command " + s);
  }
}

void runApi() {
  if (udp.parsePacket()) {
    Serial.println("new packet");

    // Commands are short. Don't size this by UDP_TX_PACKET_MAX_SIZE: that's
    // 8192 bytes, twice the whole 4096 byte stack, so any packet overwrote
    // whatever lay below it and the clock died a little later. Anything longer
    // is cut off, which no command is.
    char packetBuffer[64];
    int packetSize = udp.read(packetBuffer, sizeof(packetBuffer) - 1);

    if (packetSize > 0) {
      // Null-terminate the received data
      packetBuffer[packetSize] = 0;

      // Print the received data
      Serial.print("Received packet: ");
      Serial.println(packetBuffer);

      char reply[240];
      handleUdp(packetBuffer, reply, sizeof(reply));
      if (reply[0]) {
        // Replying was once thought to crash the ESP; most likely it was the
        // overflowing buffer above
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write((const uint8_t *) reply, strlen(reply));
        udp.endPacket();
      }
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
