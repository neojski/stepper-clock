#include <string.h>
#include "schedule.h"

// Programs play in order, each for at least durationSec. A program with
// startAtMs >= 0 waits for that ms of the minute (or of every startEveryMs)
// before starting, so it can begin where the hand already is. It then
// crossfades in over fadeMs.

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
  // Optional: startAtMs is within this many ms rather than within a minute
  uint32_t startEveryMs;
};

Entry playlist[] = {
  // Takes over from the pendulum at the end of its swing
  { "seconds", seconds, 30, anyTime, 0 },
  // Starts where the seconds hand is at rest, see drop()
  { "drop", drop, 15, (int32_t) dropStartMs, 0, dropStart },
  // Starts at rest at 6, where the drop ends and the seconds hand is at
  // :30.25. It's where the seconds hand is at the end of each of its 2s
  // cycles, see ratchet()
  { "ratchet", ratchet, 30, 250, 0, NULL, 2000 },
  { "hours", hours, 120, anyTime, fadeMs },
  // The glide from the hour ends in a sweep
  { "smoothSeconds", smoothSeconds, 10, anyTime, fadeMs },
  // Like the ratchet, with 4s cycles, see flick(). The sweeping hand is at
  // most a tick from where the flick starts, so a short crossfade is enough.
  { "flick", flick, 32, 250, 500, NULL, 4000 },
  { "seconds", seconds, 8, 250, 0, NULL, 4000 },
  // Waits for the seconds hand to be at rest, gets stuck every 10s and is the
  // seconds hand in between: 30s is 3 times, ending as the seconds hand
  { "stuck", stuck, 30, anyTime, 0, stuckStart },
  // Starts like the seconds hand at a whole second, before its tick
  { "forwardAndBack", forwardAndBack, 30, 0, 0, forwardAndBackStart, 1000 },
  // Until the pendulum can start
  { "seconds", seconds, 0, anyTime, fadeMs },
  // Starts at one end of its swing, at 198 degrees where the seconds hand is
  // at rest at :33.25, and 54s later, at :27.25, is at the other end, at 162
  // degrees where the seconds hand is then, see pendulum()
  { "pendulum", pendulum, 54, 33250, 0, pendulumStart },
};
const int playlistSize = sizeof(playlist) / sizeof(playlist[0]);

// What a crossfade starts from when there's no program to fade from: at boot,
// the hand at 12, and when interrupting a crossfade, the hand where it was,
// coasting to a stop from the speed it had (stopping dead would be too abrupt).
float heldDeg = 0;
float heldDegPerSec = 0;
uint32_t heldAtMs = 0; // millis
const float coastSec = 0.5;
Motion held() {
  float left = expf(-secondsSince(heldAtMs) / coastSec);
  return { heldDeg + heldDegPerSec * coastSec * (1 - left), heldDegPerSec * left };
}

int entry = 0;
Program fadeFrom = held;      // NULL once the crossfade is done
// Angle from the program we fade from to the one we fade to, NAN until set
float fadeDiff = NAN;
uint32_t entryFadeMs = fadeMs;
uint32_t entryStartMs = 0; // millis
// The entry wasn't started the way the playlist says (the API crossfades to it
// at any time), so it may not end in step with the clock.
bool offSchedule = false;
Motion lastMotion = { 0, 0 };

void startEntry(int next, uint32_t fade) {
  // Crossfade from the current program, or if we are mid-crossfade, from
  // where the hand is now.
  if (fadeFrom) {
    heldDeg = lastMotion.deg;
    heldDegPerSec = lastMotion.degPerSec;
    heldAtMs = nowMs();
    fadeFrom = held;
  } else {
    fadeFrom = playlist[entry].program;
  }
  entry = next;
  entryFadeMs = fade;
  entryStartMs = nowMs();
  offSchedule = fade != playlist[entry].fadeMs;
  fadeDiff = NAN;
  if (playlist[entry].onStart) {
    playlist[entry].onStart(entryStartMs + fade);
  }
  logLine("program %s", playlist[entry].name);
}

void playNext() {
  startEntry((entry + 1) % playlistSize, fadeMs);
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
  if (nowMs() - entryStartMs < playlist[entry].durationSec * 1000) {
    return;
  }
  int next = (entry + 1) % playlistSize;
  // Then the hand may not be where the next entry expects at its startAtMs:
  // crossfade to it now instead.
  if (offSchedule) {
    startEntry(next, fadeMs);
    return;
  }
  int32_t startAt = playlist[next].startAtMs;
  int32_t every = playlist[next].startEveryMs ? playlist[next].startEveryMs : 60000;
  int32_t ms = msOfDay % every;
  // Allow for being a little late, e.g. a slow loop()
  if (startAt == anyTime || (ms >= startAt && ms < startAt + every / 60)) {
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
