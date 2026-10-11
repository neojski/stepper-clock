// Schedule: which program plays when, and crossfading between them
#pragma once

#include "animation.h"

// Switch programs if it's time and return where the hand should be now
Motion scheduleStep();

// Crossfade to the first playlist entry with that name now and continue the
// playlist from there. Returns false if there's no such entry.
bool play(const char *name);

// The same, to the next entry
void playNext();
