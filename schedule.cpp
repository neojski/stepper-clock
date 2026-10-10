#include <string.h>
#include "schedule.h"

// Programs play in order, each for at least durationSec. A program with
// startAtMs >= 0 waits for that ms of the minute before starting, so it can
// begin where the hand already is. It then crossfades in over fadeMs.

const uint32_t fadeMs = 2000;
const int32_t anyTime = -1;

struct Entry {
  const char *name;
  Program program;
  uint32_t durationSec;
  int32_t startAtMs;
  uint32_t fadeMs;
  // Optional: called on start with millis when fully faded in
  void (*onStart)(uint32_t startMs);
};

Entry playlist[] = {
  { "seconds", seconds, 30, anyTime, fadeMs },
  // Starts where the seconds hand is at rest, see drop()
  { "drop", drop, 15, (int32_t) dropStartMs, 0, dropStart },
  // Starts at rest at 6, like the seconds hand at :31.00 before its tick
  { "forwardAndBack", forwardAndBack, 30, 31000, 0, forwardAndBackStart },
  { "hours", hours, 120, anyTime, fadeMs },
  { "smoothSeconds", smoothSeconds, 60, anyTime, fadeMs },
  // At :27 the pendulum is at rest at the end of its swing at 162 degrees,
  // where the seconds hands are too. Running for whole minutes it also ends
  // there.
  { "pendulum", pendulum, 60, 27000, fadeMs },
};
const int playlistSize = sizeof(playlist) / sizeof(playlist[0]);
const int hoursEntry = 3;

// Where the hand was when a crossfade had to start from a standstill (at boot,
// or when interrupting a crossfade).
float heldDeg = 0;
Motion held() {
  return { heldDeg, 0 };
}

int entry = 0;
Program fadeFrom = held;      // NULL once the crossfade is done
// Angle from the program we fade from to the one we fade to, NAN until set
float fadeDiff = NAN;
uint32_t entryFadeMs = fadeMs;
uint32_t entryStartMs = 0; // millis
Motion lastMotion = { 0, 0 };

void startEntry(int next, uint32_t fade) {
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
  entryStartMs = nowMs();
  fadeDiff = NAN;
  if (playlist[entry].onStart) {
    playlist[entry].onStart(entryStartMs + fade);
  }
  logLine("program %s", playlist[entry].name);
}

int32_t lastApiChange = -1e9; // millis
bool lastAPIChangeLongTimeAgo () {
  return nowMs() - lastApiChange > 60 * 1000; // reset after 1m
}

void nextProgramFromApi() {
  if (lastAPIChangeLongTimeAgo()) {
    startEntry(hoursEntry, fadeMs);
  } else {
    // Always crossfade: we're not starting at the entry's chosen time
    startEntry((entry + 1) % playlistSize, fadeMs);
  }
  lastApiChange = nowMs();
}

bool play(const char *name) {
  for (int i = 0; i < playlistSize; i++) {
    if (strcmp(name, playlist[i].name) == 0) {
      startEntry(i, fadeMs);
      return true;
    }
  }
  return false;
}

void maybeNextProgram() {
  if (!lastAPIChangeLongTimeAgo()) {
    return;
  }
  if (nowMs() - entryStartMs < playlist[entry].durationSec * 1000) {
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
  int32_t startAt = playlist[next].startAtMs;
  int32_t msOfMinute = msOfDay % 60000;
  if (startAt == anyTime || (msOfMinute >= startAt && msOfMinute < startAt + 1000)) {
    startEntry(next, playlist[next].fadeMs);
  }
}

Motion currentMotion() {
  Motion to = playlist[entry].program();
  if (!fadeFrom) {
    return to;
  }
  uint32_t elapsed = nowMs() - entryStartMs;
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

Motion scheduleStep() {
  maybeNextProgram();
  lastMotion = currentMotion();
  return lastMotion;
}
