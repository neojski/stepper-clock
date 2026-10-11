// Simulator: runs the clock's animation, schedule and motor code on a computer
// against a fake clock, checks the motion against the motor's limits and
// records it for viewer.html. Use run.sh to build and run it.
//
// Options:
//   --start HH:MM:SS[.mmm]  time of day at boot (default: now)
//   --minutes N             how long to simulate (default 10)
//   --play NAME@SEC         run "play NAME" SEC seconds after boot (repeatable);
//                           NAME next is the "next" command
//   --park SEC              park the hand at 12 SEC seconds after boot, as an
//                           OTA update does, and end there
//   --loop-us N             time each loop() takes (default 300)
//   --loop-jitter-us N      plus up to this much at random (default 0)
//   --seed N                random seed, e.g. for forwardAndBack (default 1)
//   --out FILE              recording for the viewer (default: none)

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>
#include "../schedule.h"
#include "../motor.h"

// platform.h for the simulator

uint32_t msOfDay = 0;
static uint64_t simUs = 0;

uint32_t nowMs() {
  return (uint32_t) (simUs / 1000);
}

uint32_t nowUs() {
  return (uint32_t) simUs;
}

int32_t randomInt(int32_t min, int32_t max) {
  return min + rand() % (max - min);
}

void stepPulse(int) {
  // drive() keeps the position, nothing else to do
}

struct Event {
  double t; // seconds since boot
  std::string text;
};
static std::vector<Event> events;
// The playlist starts with seconds, crossfading from 12
static std::string currentProgram = "seconds";

void logLine(const char *format, ...) {
  char line[200];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  events.push_back({ simUs / 1e6, line });
  if (strncmp(line, "program ", 8) == 0) {
    currentProgram = line + 8;
  }
}

// The simulation

struct Play {
  double at;
  std::string name;
};

static uint32_t parseTimeOfDay(const char *s) {
  int h = 0, m = 0;
  double sec = 0;
  if (sscanf(s, "%d:%d:%lf", &h, &m, &sec) < 2) {
    fprintf(stderr, "bad time %s, expected HH:MM:SS\n", s);
    exit(1);
  }
  return (uint32_t) ((h * 3600 + m * 60) * 1000 + llround(sec * 1000)) % 86400000;
}

static std::string timeOfDay(uint32_t ms) {
  char s[32];
  snprintf(s, sizeof(s), "%02u:%02u:%02u.%03u", ms / 3600000, ms / 60000 % 60,
    ms / 1000 % 60, ms % 1000);
  return s;
}

static std::string jsString(const std::string &s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
    }
    out += c;
  }
  return out + "\"";
}

int main(int argc, char **argv) {
  time_t now = time(NULL);
  struct tm *local = localtime(&now);
  uint32_t startMsOfDay = (local->tm_hour * 3600 + local->tm_min * 60 + local->tm_sec) * 1000;
  double minutes = 10;
  double parkAt = -1;
  uint32_t loopUs = 300;
  uint32_t loopJitterUs = 0;
  unsigned seed = 1;
  const char *out = NULL;
  std::vector<Play> plays;

  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];
    const char *value = i + 1 < argc ? argv[i + 1] : NULL;
    if (!value) {
      fprintf(stderr, "missing value for %s\n", arg);
      return 1;
    }
    i++;
    if (!strcmp(arg, "--start")) {
      startMsOfDay = parseTimeOfDay(value);
    } else if (!strcmp(arg, "--minutes")) {
      minutes = atof(value);
    } else if (!strcmp(arg, "--play")) {
      const char *at = strchr(value, '@');
      if (!at) {
        fprintf(stderr, "bad --play %s, expected NAME@SECONDS\n", value);
        return 1;
      }
      plays.push_back({ atof(at + 1), std::string(value, at - value) });
    } else if (!strcmp(arg, "--park")) {
      parkAt = atof(value);
    } else if (!strcmp(arg, "--loop-us")) {
      loopUs = atoi(value);
    } else if (!strcmp(arg, "--loop-jitter-us")) {
      loopJitterUs = atoi(value);
    } else if (!strcmp(arg, "--seed")) {
      seed = atoi(value);
    } else if (!strcmp(arg, "--out")) {
      out = value;
    } else {
      fprintf(stderr, "unknown option %s\n", arg);
      return 1;
    }
  }
  srand(seed);

  // Like setup(). The hand starts at 12, as after a reboot.
  setMicrostepping(16);
  float cycle = stepsPerCycle();

  const uint32_t sampleMs = 10;
  std::vector<float> target, hand;
  std::vector<std::string> programAt; // program at each sample
  uint64_t endUs = (uint64_t) (minutes * 60e6);
  uint64_t nextSampleUs = 0;
  std::vector<bool> played(plays.size());

  for (simUs = 0; simUs < endUs; simUs += loopUs + (loopJitterUs ? rand() % loopJitterUs : 0)) {
    msOfDay = (startMsOfDay + nowMs()) % 86400000;
    for (size_t i = 0; i < plays.size(); i++) {
      if (!played[i] && simUs >= plays[i].at * 1e6) {
        played[i] = true;
        logLine("play %s (API)", plays[i].name.c_str());
        if (plays[i].name == "next") {
          playNext();
        } else if (!play(plays[i].name.c_str())) {
          logLine("unknown program %s", plays[i].name.c_str());
        }
      }
    }
    Motion m;
    if (parkAt >= 0 && simUs >= parkAt * 1e6) {
      // Like parkAtZero() in the sketch
      if (currentProgram != "park") {
        logLine("program park");
        currentProgram = "park";
      }
      m = { 0, 0 };
      if (park()) {
        logLine("parked at %.1f deg", position * 360 / cycle);
        endUs = simUs;
      } else if (simUs >= (parkAt + 5) * 1e6) {
        logLine("PARKING TIMED OUT at %.1f deg", position * 360 / cycle);
        endUs = simUs;
      }
    } else {
      m = scheduleStep();
      drive(m);
    }
    while (simUs >= nextSampleUs) {
      target.push_back(fmodf(fmodf(m.deg, 360) + 360, 360));
      hand.push_back(fmodf(position * 360 / cycle + 360, 360));
      programAt.push_back(currentProgram);
      nextSampleUs += sampleMs * 1000;
    }
  }

  // Checks. Speed from the hand's movement over 50ms, acceleration from the
  // change in that over 100ms: shorter windows mostly show single steps.
  size_t n = hand.size();
  std::vector<float> speed(n), accel(n);
  const int speedWindow = 5, accelWindow = 10;
  for (size_t i = speedWindow; i < n; i++) {
    speed[i] = wrapDeg(hand[i] - hand[i - speedWindow]) / (speedWindow * sampleMs / 1e3);
  }
  for (size_t i = speedWindow + accelWindow; i < n; i++) {
    accel[i] = (speed[i] - speed[i - accelWindow]) / (accelWindow * sampleMs / 1e3);
  }
  float speedLimit = maxTurnsPerSec * 360, accelLimit = maxTurnsPerSec2 * 360;
  float maxSpeed = 0, maxAccel = 0;
  for (size_t i = 0; i < n; i++) {
    maxSpeed = fmaxf(maxSpeed, fabsf(speed[i]));
    maxAccel = fmaxf(maxAccel, fabsf(accel[i]));
  }
  // Peaks and tracking error per program, after the first 3s (from 12 at boot)
  struct Stats {
    std::string program;
    float speed, accel, error;
  };
  std::vector<Stats> stats;
  float maxError = 0;
  for (size_t i = 3000 / sampleMs; i < n; i++) {
    float e = fabsf(wrapDeg(target[i] - hand[i]));
    maxError = fmaxf(maxError, e);
    auto it = stats.begin();
    while (it != stats.end() && it->program != programAt[i]) {
      it++;
    }
    if (it == stats.end()) {
      stats.push_back({ programAt[i], 0, 0, 0 });
      it = stats.end() - 1;
    }
    it->speed = fmaxf(it->speed, fabsf(speed[i]));
    it->accel = fmaxf(it->accel, fabsf(accel[i]));
    it->error = fmaxf(it->error, e);
  }

  printf("Simulated %.1f min from %s, loop %u us", minutes, timeOfDay(startMsOfDay).c_str(), loopUs);
  if (loopJitterUs) {
    printf(" + up to %u us", loopJitterUs);
  }
  printf("\n\n");
  for (const Event &e : events) {
    printf("  %8.2fs  %s  %s\n", e.t, timeOfDay((startMsOfDay + (uint32_t) (e.t * 1000)) % 86400000).c_str(), e.text.c_str());
  }
  bool withinLimits = maxSpeed <= speedLimit && maxAccel <= accelLimit;
  // The hand normally stays within a couple of steps of where it should be
  const float errorLimit = 2;
  bool tracking = maxError <= errorLimit;
  bool ok = withinLimits && tracking;
  printf("\nPeak speed %.0f deg/s (limit %.0f), peak acceleration %.0f deg/s^2 (limit %.0f): %s\n",
    maxSpeed, speedLimit, maxAccel, accelLimit, withinLimits ? "OK" : "OVER THE MOTOR'S LIMITS");
  printf("Tracking error %.2f deg max (limit %.0f, 1 microstep = %.4f deg): %s\n\n", maxError, errorLimit,
    360 / cycle, tracking ? "OK" : "THE HAND ISN'T FOLLOWING");
  printf("  %-16s %12s %14s %12s\n", "program", "speed deg/s", "accel deg/s^2", "error deg");
  for (const Stats &st : stats) {
    printf("  %-16s %12.0f %14.0f %12.2f\n", st.program.c_str(), st.speed, st.accel, st.error);
  }

  if (out) {
    FILE *f = fopen(out, "w");
    if (!f) {
      perror(out);
      return 1;
    }
    fprintf(f, "// Written by sim.cpp, loaded by viewer.html\nwindow.TRACE = {\n");
    fprintf(f, "startMsOfDay: %u, sampleMs: %u, loopUs: %u, loopJitterUs: %u,\n", startMsOfDay, sampleMs, loopUs, loopJitterUs);
    fprintf(f, "speedLimit: %.0f, accelLimit: %.0f, maxSpeed: %.1f, maxAccel: %.1f, maxError: %.3f, stepDeg: %.4f,\n",
      speedLimit, accelLimit, maxSpeed, maxAccel, maxError, 360 / cycle);
    const std::vector<float> *series[] = { &target, &hand, &speed, &accel };
    const char *names[] = { "target", "hand", "speed", "accel" };
    const char *formats[] = { "%.2f", "%.2f", "%.0f", "%.0f" };
    for (int s = 0; s < 4; s++) {
      fprintf(f, "%s: [", names[s]);
      for (size_t i = 0; i < n; i++) {
        fprintf(f, i ? "," : "");
        fprintf(f, formats[s], (*series[s])[i]);
      }
      fprintf(f, "],\n");
    }
    fprintf(f, "events: [");
    for (size_t i = 0; i < events.size(); i++) {
      fprintf(f, "%s[%.3f,%s]", i ? "," : "", events[i].t, jsString(events[i].text).c_str());
    }
    fprintf(f, "]\n};\n");
    fclose(f);
  }
  return ok ? 0 : 2;
}
