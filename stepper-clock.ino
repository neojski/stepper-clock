#include <AccelStepper.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include "credentials.h"
#include <ezTime.h>

Timezone timezone;

// The hand's angle (degrees clockwise from 12) and angular speed. Defined up
// here because the Arduino build declares functions using it near the top.
struct Motion {
  float deg;
  float degPerSec;
};

typedef Motion (*Program)();

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
// step pulses itself.
const int stepPin = D3;
const int dirPin = D4;
AccelStepper motor(AccelStepper::DRIVER, stepPin, dirPin);

// The hand's position in steps from 12 o'clock
long position = 0;

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

// Local time of day in ms, synced with NTP by ezTime. Read once per loop() so
// that all programs see the same time.
unsigned long msOfDay = 0;
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

float getSeconds() {
  return (float) (msOfDay % 60000) / 1e3;
}

int microsteps;

int stepsPerCycle () {
  return 200 * microsteps;
}

void setMicrosteps0 (int x) {
  if (microsteps) {
    // Same physical position in the new step size
    position = lround((double) position * x / microsteps);
  }
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

// Animations
//
// A program gives the hand's Motion for the current time. The scheduler below
// crossfades between programs so the hand never jumps, and drive() makes the
// motor follow.

// Smoothstep: eases 0 -> 1 as u goes 0 -> 1, with zero slope at both ends
float smoothstep(float u) {
  return u * u * (3 - 2 * u);
}

float smoothstepSlope(float u) {
  return 6 * u * (1 - u);
}

// Shortest signed difference between two angles, in (-180, 180]
float wrapDeg(float deg) {
  deg = fmodf(deg, 360);
  if (deg > 180) {
    deg -= 360;
  } else if (deg <= -180) {
    deg += 360;
  }
  return deg;
}

// Ticking seconds hand. Each tick is a smoothstep move over tickSec at the
// start of the second, short enough to look like a tick but within the
// motor's acceleration limit (peak 6 * 6deg / tickSec^2 = 576 deg/s^2).
const float tickSec = 0.25;
Motion seconds() {
  unsigned long msOfMinute = msOfDay % 60000;
  float second = msOfMinute / 1000;
  float u = (msOfMinute % 1000) / 1e3 / tickSec;
  if (u >= 1) {
    return { second * 6, 0 };
  }
  return { (second - 1 + smoothstep(u)) * 6, smoothstepSlope(u) * 6 / tickSec };
}

Motion smoothSeconds() {
  return { getSeconds() * 6, 6 };
}

// Pendulum period is 2s (omega = PI). Reduce the time modulo the period in
// integer math before converting to float: a large time as float loses
// precision (e.g. (float)millis() has 32ms steps after ~3 days of uptime)
// which made the pendulum stutter more and more the longer the clock ran.
// Using the same clock as the seconds programs keeps them in phase: the
// period divides both a minute and a day.
const unsigned long pendulumPeriodMs = 2000;
Motion pendulum() {
  float t = (float)(msOfDay % pendulumPeriodMs) / 1e3;
  return { 180 + 18 * cosf((float) PI * t), -18 * (float) PI * sinf((float) PI * t) };
}

Motion hours() {
  // Use the full time of day so the hand advances one microstep at a time
  // (~every 13.5s at 1/16 microstepping) instead of a ~3mm jump once a minute.
  return { (float) (msOfDay / 3.6e6 * 30), 30.0f / 3600 };
}

// The Drop: the hand falls from 12 like a released pendulum and swings
// through 6, each swing smaller, until it rests at 6 dropSec after
// dropOriginMs. The playlist starts it at :00.25, when the tick to 12 has
// finished, so it's at rest at 6 by :29.25 and the seconds hand can take over
// at :30.25 once its tick to 6 has finished.
//
// angle = 180 - 180 * envelope * cos(phase)
const unsigned long dropStartMs = 250; // ms of the minute
const float dropSec = 29;
// Big swings of a real pendulum take longer: start with a 6s period and speed
// up to 2.7s as the swings shrink. Faster swings exceed the motor's limits:
// these peak at ~220 deg/s and ~320 deg/s^2.
const float dropOmegaStart = PI / 3; // rad/s
const float dropOmegaEnd = 0.75 * PI;
const float dropOmegaTau = 15; // s
// The phase speed also ramps up from 0 so the hand tips over gently
const float dropRampTau = 0.4; // s
unsigned long dropOriginMs = 0; // millis

Motion drop() {
  float t = (long) (millis() - dropOriginMs) / 1e3f;
  if (t <= 0) {
    return { 0, 0 };
  }
  if (t >= dropSec) {
    return { 180, 0 };
  }

  // Envelope (1-u)^3 (1+3u): flat at the start so the hand starts at rest,
  // and reaching 0 with zero slope at the end.
  float u = t / dropSec;
  float envelope = (1 - u) * (1 - u) * (1 - u) * (1 + 3 * u);
  float envelopeSlope = -12 * u * (1 - u) * (1 - u) / dropSec; // per second

  // Phase speed (dropOmegaEnd - delta * e^(-t/omegaTau)) * (1 - e^(-t/rampTau)),
  // integrated in closed form so the phase is exact at any time.
  float delta = dropOmegaEnd - dropOmegaStart;
  float tau3 = dropRampTau * dropOmegaTau / (dropRampTau + dropOmegaTau);
  float e1 = expf(-t / dropRampTau);
  float e2 = expf(-t / dropOmegaTau);
  float e3 = expf(-t / tau3);
  float phaseSpeed = (dropOmegaEnd - delta * e2) * (1 - e1);
  float phase = dropOmegaEnd * (t - dropRampTau * (1 - e1))
    - delta * (dropOmegaTau * (1 - e2) - tau3 * (1 - e3));

  return {
    180 - 180 * envelope * cosf(phase),
    180 * (envelope * phaseSpeed * sinf(phase) - envelopeSlope * cosf(phase))
  };
}

// startMs: millis when the entry has fully faded in
void dropStart(unsigned long startMs) {
  // Glide to 12 first if crossfading
  dropOriginMs = startMs;
}

// msOfDay - then, allowing for midnight
long msSince(unsigned long then) {
  long diff = (long) msOfDay - (long) then;
  if (diff < -43200000) {
    diff += 86400000;
  } else if (diff > 43200000) {
    diff -= 86400000;
  }
  return diff;
}

// Forward and back: the seconds hand ticks every 0.5s in groups, mostly
// forward with some steps back. Each group is a random pattern of 2-5 ticks
// with 1 or 2 back, followed by extra forward ticks if the hand is behind or
// pauses if it's ahead, so the group ends on a whole second with the hand
// showing the right time. Between groups it's the ordinary seconds hand.
//
// Each 0.5s slot is a tick like seconds() then stillness. A group starts at a
// whole second S with the hand at S-1, like seconds() at the start of a tick.
// After n slots with net movement m the time is S + n/2 and the hand at
// S-1+m. Ending with a pause it must show the previous second:
// S-1+m+r = S + (n+r+p)/2 - 1 for r extra forward ticks and p >= 1 pauses,
// so r - p = n - 2m. With b back ticks that makes a group 4b + 2 slots long
// (or less with many forward ticks), so b is kept small.
const int fabMaxSlots = 10;
int8_t fabSlots[fabMaxSlots]; // +1 forward, -1 back, 0 pause
int fabSlotCount = 0;
unsigned long fabGroupStartMs = 0; // msOfDay, a whole second
const unsigned long slotMs = 500;

void forwardAndBackStart(unsigned long startMs) {
  // First group at the first whole second once faded in
  unsigned long start = msOfDay + (startMs - millis());
  fabGroupStartMs = (start + 999) / 1000 * 1000 % 86400000;
  fabSlotCount = 0;
}

void forwardAndBackNewGroup() {
  int n = random(2, 6);
  int backs = n > 2 && random(10) < 2 ? 2 : 1;
  for (int i = 0; i < n; i++) {
    fabSlots[i] = 1;
  }
  for (int placed = 0; placed < backs;) {
    int i = random(n);
    if (fabSlots[i] == 1) {
      fabSlots[i] = -1;
      placed++;
    }
  }
  fabSlotCount = n;
  int net = n - 2 * backs;
  int behind = n - 2 * net;
  int forward = behind >= 0 ? behind + 1 : 0;
  int pauses = behind >= 0 ? 1 : -behind;
  for (int i = 0; i < forward; i++) {
    fabSlots[fabSlotCount++] = 1;
  }
  for (int i = 0; i < pauses; i++) {
    fabSlots[fabSlotCount++] = 0;
  }
}

Motion forwardAndBack() {
  long ms = msSince(fabGroupStartMs);
  if (ms < 0) {
    return seconds();
  }
  long groupMs = fabSlotCount * slotMs;
  if (ms >= groupMs) {
    if (ms < groupMs + 1000) {
      fabGroupStartMs = (fabGroupStartMs + groupMs) % 86400000;
    } else {
      // Fell far behind, e.g. the clocks changed: start again now
      fabGroupStartMs = msOfDay - msOfDay % 1000;
    }
    forwardAndBackNewGroup();
    ms = msSince(fabGroupStartMs);
  }

  int slot = ms / slotMs;
  int second = (int) (fabGroupStartMs / 1000 % 60) - 1;
  for (int i = 0; i < slot; i++) {
    second += fabSlots[i];
  }
  int move = fabSlots[slot];
  float u = (ms % slotMs) / 1e3 / tickSec;
  if (u >= 1) {
    return { (float) (second + move) * 6, 0 };
  }
  return { (second + move * smoothstep(u)) * 6, move * smoothstepSlope(u) * 6 / tickSec };
}

// Only end in the last slot of a group, when the hand shows the right time
bool forwardAndBackCanEnd() {
  long ms = msSince(fabGroupStartMs);
  return ms < 0 || ms >= (long) ((fabSlotCount - 1) * slotMs);
}

// Where the hand was when a crossfade had to start from a standstill (at boot,
// or when interrupting a crossfade).
float heldDeg = 0;
Motion held() {
  return { heldDeg, 0 };
}

// Schedule
//
// Programs play in order, each for at least durationSec. A program with
// startAtMs >= 0 waits for that ms of the minute before starting, so it can
// begin where the hand already is. It then crossfades in over fadeMs.

const unsigned long fadeMs = 2000;
const long anyTime = -1;

struct Entry {
  const char *name;
  Program program;
  unsigned long durationSec;
  long startAtMs;
  unsigned long fadeMs;
  // Optional: called on start with millis when fully faded in
  void (*onStart)(unsigned long startMs);
  // Optional: whether the program can end now (once durationSec is up)
  bool (*canEnd)();
};

Entry playlist[] = {
  { "seconds", seconds, 30, anyTime, fadeMs },
  // Starts where the seconds hand is at rest, see drop()
  { "drop", drop, 30, dropStartMs, 0, dropStart },
  // Starts at rest at 6, like the seconds hand at :31.00 before its tick
  { "forwardAndBack", forwardAndBack, 30, 31000, 0, forwardAndBackStart, forwardAndBackCanEnd },
  { "hours", hours, 120, anyTime, fadeMs },
  { "smoothSeconds", smoothSeconds, 60, anyTime, fadeMs },
  // At :27 the pendulum is at rest at the end of its swing at 162 degrees,
  // where the seconds hands are too. Running for whole minutes it also ends
  // there.
  { "pendulum", pendulum, 60, 27000, fadeMs },
};
const int playlistSize = sizeof(playlist) / sizeof(playlist[0]);
const int hoursEntry = 3;

int entry = 0;
Program fadeFrom = held;      // NULL once the crossfade is done
// Angle from the program we fade from to the one we fade to, NAN until set
float fadeDiff = NAN;
unsigned long entryFadeMs = fadeMs;
unsigned long entryStartMs = 0; // millis
Motion lastMotion = { 0, 0 };

void startEntry(int next, unsigned long fade) {
  // Crossfade from the current program, or if we are mid-crossfade, from
  // where the hand is now.
  if (fadeFrom) {
    heldDeg = lastMotion.deg;
    fadeFrom = held;
  } else {
    fadeFrom = playlist[entry].program;
  }
  entry = next;
  entryFadeMs = fade;
  entryStartMs = millis();
  fadeDiff = NAN;
  if (playlist[entry].onStart) {
    playlist[entry].onStart(entryStartMs + fade);
  }
  Serial.printf("program %s\n", playlist[entry].name);
}

int lastApiChange = -1e9; // millis
bool lastAPIChangeLongTimeAgo () {
  return millis() - lastApiChange > 60 * 1000; // reset after 1m
}

void nextProgramFromApi() {
  if (lastAPIChangeLongTimeAgo()) {
    startEntry(hoursEntry, fadeMs);
  } else {
    // Always crossfade: we're not starting at the entry's chosen time
    startEntry((entry + 1) % playlistSize, fadeMs);
  }
  lastApiChange = millis();
}

void maybeNextProgram() {
  if (!lastAPIChangeLongTimeAgo()) {
    return;
  }
  if (millis() - entryStartMs < playlist[entry].durationSec * 1000) {
    return;
  }
  if (playlist[entry].canEnd && !playlist[entry].canEnd()) {
    return;
  }
  int next = (entry + 1) % playlistSize;
  // The drop runs on its own timer. If the API started it (with a crossfade)
  // it's not in step with the clock, so don't wait for the next entry's
  // startAtMs. Other programs follow the clock so they always line up.
  bool dropOffSchedule = playlist[entry].program == drop && dropOriginMs != entryStartMs;
  if (dropOffSchedule) {
    startEntry(next, fadeMs);
    return;
  }
  long startAt = playlist[next].startAtMs;
  long msOfMinute = msOfDay % 60000;
  if (startAt == anyTime || (msOfMinute >= startAt && msOfMinute < startAt + 1000)) {
    startEntry(next, playlist[next].fadeMs);
  }
}

Motion currentMotion() {
  Motion to = playlist[entry].program();
  if (!fadeFrom) {
    return to;
  }
  unsigned long elapsed = millis() - entryStartMs;
  if (elapsed >= entryFadeMs) {
    fadeFrom = NULL;
    return to;
  }
  // Blend from one program to the other: pos = from + s * (to - from) and its
  // derivative.
  Motion from = fadeFrom();
  float u = (float) elapsed / entryFadeMs;
  float s = smoothstep(u);
  // Go the shortest way round, but decide that once: if the programs are
  // about half a turn apart (e.g. pendulum around 6 to the drop at 12) the
  // shortest way flips back and forth, so follow the difference continuously.
  if (isnan(fadeDiff)) {
    fadeDiff = wrapDeg(to.deg - from.deg);
  }
  fadeDiff += wrapDeg(to.deg - from.deg - fadeDiff);
  float diff = fadeDiff;
  return {
    from.deg + s * diff,
    from.degPerSec + s * (to.degPerSec - from.degPerSec)
      + smoothstepSlope(u) / (entryFadeMs / 1e3f) * diff
  };
}

// Motor control
//
// Run at the program's speed, plus a correction proportional to the position
// error so the hand never drifts. The commanded speed changes no faster than
// the acceleration limit, so even a big error can't jerk the motor.
//
// We send the step pulses ourselves rather than with AccelStepper's
// runSpeed(), which takes at most one step per loop() and drops the time a step
// was late by, so fast moves come out slower than asked. Instead keep track of
// the steps owed and send all that are due each loop.

const float correctionPerSec = 8; // fraction of the error corrected per second
float commandedSpeed = 0; // steps/s
float stepsOwed = 0;
// Don't catch up on more than this after loop() stalled (e.g. NTP sync): the
// correction makes up the rest smoothly.
const float maxStepsOwed = 8;
unsigned long lastDriveUs = 0;

void stepOnce(int direction) {
  digitalWrite(dirPin, direction > 0 ? HIGH : LOW);
  digitalWrite(stepPin, HIGH);
  delayMicroseconds(1); // TMC2208 needs > 100ns
  digitalWrite(stepPin, LOW);
  position += direction;
}

void drive(Motion m) {
  float cycle = stepsPerCycle();

  // Keep the step count small. The hand can go round forever.
  position %= (long) cycle;

  float error = fmodf(m.deg / 360 * cycle - position, cycle);
  if (error > cycle / 2) {
    error -= cycle;
  } else if (error < -cycle / 2) {
    error += cycle;
  }
  // Within half a step is as close as we can get
  if (fabsf(error) <= 0.5) {
    error = 0;
  }

  float wanted = m.degPerSec / 360 * cycle + correctionPerSec * error;

  unsigned long now = micros();
  float dt = (now - lastDriveUs) / 1e6;
  lastDriveUs = now;
  float maxChange = 2 * cycle * dt; // see setMicrosteps0
  commandedSpeed = constrain(wanted, commandedSpeed - maxChange, commandedSpeed + maxChange);
  commandedSpeed = constrain(commandedSpeed, -cycle, cycle);

  stepsOwed = constrain(stepsOwed + commandedSpeed * dt, -maxStepsOwed, maxStepsOwed);
  while (stepsOwed >= 1) {
    stepOnce(1);
    stepsOwed -= 1;
  }
  while (stepsOwed <= -1) {
    stepOnce(-1);
    stepsOwed += 1;
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

// The motor has no position sensor: after a reboot wherever the hand is counts
// as 0 (12 o'clock). So before an OTA update reboots us, move the hand to 12.
void parkAtZero() {
  // Hand AccelStepper our position, starting from a stop
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

// "play <name>": crossfade to the first playlist entry with that name now and
// continue the playlist from there.
void play(String name) {
  for (int i = 0; i < playlistSize; i++) {
    if (name.equals(playlist[i].name)) {
      startEntry(i, fadeMs);
      return;
    }
  }
  Serial.println("unknown program " + name);
}

void handleUdp(String s) {
  s.trim();
  if (s.startsWith("play ")) {
    play(s.substring(5));
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
  maybeNextProgram();
  lastMotion = currentMotion();
  drive(lastMotion);
}
