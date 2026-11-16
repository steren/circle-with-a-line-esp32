// The sequence of puzzles. Each entry says what kind of puzzle to make; the
// puzzle itself is generated afresh every time it is opened.

#pragma once

#include "generator.h"

// Solver steps the board may spend on one puzzle in the background. Past
// this, the generator settles for a puzzle with more clues.
#define LEVEL_GENERATION_STEPS 300000

extern const puzzle_spec_t levels[];
extern const int level_count;
