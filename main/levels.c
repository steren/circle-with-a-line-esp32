// Puzzles get bigger, and each kind of clue is introduced on a small board
// before it is combined with the others. Rules: n = counts, g = gems,
// b = beacons, x = gaps (see generator.h).

#include "levels.h"

const puzzle_spec_t levels[] = {
    {2, 2, ""},      // no clues: learn to draw a line
    {3, 3, "b"},     // beacons
    {3, 3, "bx"},    // gaps
    {2, 2, "n"},     // counts
    {3, 3, "n"},
    {4, 4, "n"},
    {2, 2, "g"},     // gems
    {3, 3, "g"},
    {4, 4, "gb"},
    {4, 4, "ng"},
    {4, 4, "ngbx"},
    {5, 5, "n"},
    {5, 5, "gbx"},
    {5, 5, "ngb"},
    {5, 5, "ngbx"},
};

const int level_count = sizeof(levels) / sizeof(levels[0]);
