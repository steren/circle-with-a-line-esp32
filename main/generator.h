// Puzzle generation and solving. Portable C, used by the game on the board
// and by the PC tool.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "puzzle.h"

// What kind of puzzle to make: its size, and which clues it may use.
// rules: any mix of 'n' (counts), 'g' (gems), 'b' (beacons), 'x' (gaps).
// No rules makes a clue-free board where any path from start to exit works.
typedef struct {
    uint8_t cols, rows;
    const char *rules;
} puzzle_spec_t;

typedef struct {
    int attempts;      // candidate puzzles built
    long solver_steps; // total work done checking them
    int clues;         // clues on the finished board
    bool unique;       // the finished puzzle has exactly one solution
} puzzle_gen_stats_t;

// Counts the solutions of `p`, stopping once `limit` are found. Gives up after
// `budget` search steps (0 = no limit) and then returns -1. Stores the first
// solution found in `first` if not NULL. Adds the steps taken to `*steps` if
// not NULL.
long puzzle_count_solutions(const puzzle_t *p, long limit, long budget, puzzle_path_t *first, long *steps);

// Makes a puzzle to `spec`, using and advancing the random state `*seed`
// (any non-zero value). It aims for exactly one solution, with as few clues
// as that allows. If that would take more than `max_steps` solver steps
// (0 = no limit), it settles for a puzzle that is still solvable but has more
// clues or several solutions. Safe to call from several tasks at once.
// `stats` may be NULL.
void puzzle_generate(puzzle_t *p, const puzzle_spec_t *spec, uint32_t *seed, long max_steps,
                     puzzle_gen_stats_t *stats);
