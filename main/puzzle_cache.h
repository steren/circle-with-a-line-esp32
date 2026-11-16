// Keeps a freshly generated puzzle ready for every level. A task on the second
// CPU core generates them in the background while the game is played, starting
// with the levels the player is most likely to open next. A level opened before
// its puzzle is ready gets a random one from the pre-computed bank instead.

#pragma once

#include "puzzle.h"

void puzzle_cache_start(void);

// Hands over a new puzzle for `level` and starts preparing the next one for it.
// Matches game_puzzle_source_t.
void puzzle_cache_take(int level, puzzle_t *out);
