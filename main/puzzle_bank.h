// Puzzles generated ahead of time on a PC (`make game-bank`), as text maps in
// the format described in puzzle.h. The board falls back on them when a level
// is opened before a freshly generated puzzle is ready.

#pragma once

typedef struct {
    const char *const *maps;
    int count;
} puzzle_bank_t;

extern const puzzle_bank_t puzzle_bank[];  // one entry per level
extern const int puzzle_bank_levels;
