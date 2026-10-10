#include <stdlib.h>
#include "animation.h"

// Forward and back: the seconds hand ticks every 0.5s in short groups of 2-4
// ticks, mostly forward with some steps back, each followed by a pause. It
// stays within about 2s of the right time: a tick is more likely forward the
// further behind the hand is, and never takes it more than 2s off.
//
// Each 0.5s slot is a tick like seconds() then stillness.
const int fabMaxSlots = 5; // 2-4 ticks and a pause
int8_t fabSlots[fabMaxSlots]; // +1 forward, -1 back, 0 pause
int fabSlotCount = 0;
uint32_t fabGroupStartMs = 0; // msOfDay, on a half second
int fabHandSecond = 0; // where the hand is when the group starts, 0-59
const uint32_t slotMs = 500;

void forwardAndBackRestart(uint32_t start) {
  // At a whole second S with the hand at S-1, like seconds() before its tick
  fabGroupStartMs = (start + 999) / 1000 * 1000 % 86400000;
  fabHandSecond = (fabGroupStartMs / 1000 + 59) % 60;
  fabSlotCount = 0;
}

void forwardAndBackStart(uint32_t startMs) {
  // First group once faded in
  forwardAndBackRestart(msOfDay + (startMs - nowMs()));
}

// How far the hand is ahead of the time at the start of the group, in half
// seconds. The seconds hand alternates between 0 (before a tick) and 1.
int forwardAndBackLead() {
  int lead = (2 * fabHandSecond - (int) (fabGroupStartMs / slotMs % 120) + 2) % 120;
  if (lead < -60) {
    lead += 120;
  } else if (lead >= 60) {
    lead -= 120;
  }
  return lead;
}

void forwardAndBackNewGroup() {
  int lead = forwardAndBackLead();
  int ticks = randomInt(2, 5);
  fabSlotCount = 0;
  for (int i = 0; i < ticks; i++) {
    // A forward tick gains half a second on the time, a back tick loses 1.5s.
    // 83% forward balances that with the pauses when on time.
    float forwardChance = clampf(0.83 - 0.125 * (lead - 0.5), 0.05, 0.95);
    int8_t move = randomInt(0, 100) < forwardChance * 100 ? 1 : -1;
    if (abs(lead + (move > 0 ? 1 : -3)) > 4) {
      move = -move;
    }
    fabSlots[fabSlotCount++] = move;
    lead += move > 0 ? 1 : -3;
  }
  fabSlots[fabSlotCount++] = 0;
}

Motion forwardAndBack() {
  int32_t ms = msSince(fabGroupStartMs);
  if (ms < 0) {
    return seconds();
  }
  int32_t groupMs = fabSlotCount * slotMs;
  if (ms >= groupMs) {
    if (ms < groupMs + 1000) {
      for (int i = 0; i < fabSlotCount; i++) {
        fabHandSecond += fabSlots[i];
      }
      fabHandSecond = (fabHandSecond + 60) % 60;
      fabGroupStartMs = (fabGroupStartMs + groupMs) % 86400000;
    } else {
      // Fell far behind, e.g. the clocks changed: start again now
      forwardAndBackRestart(msOfDay - msOfDay % 1000);
    }
    forwardAndBackNewGroup();
    ms = msSince(fabGroupStartMs);
  }

  int slot = ms / slotMs;
  int second = fabHandSecond;
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
