// Puzzles get bigger, and each kind of clue is introduced on a small board
// before it is combined with the others. Rules: n = triangles, g = squares,
// s = stars, p = polyominoes, y = negators, b = hexagons, x = gaps (see generator.h).

#include "levels.h"

const puzzle_spec_t levels[] = {
    // Chapter 1: Lines, Gaps & Hexagons
    {2, 2, ""},       //  1. draw a line from start to exit
    {2, 2, "x"},      //  2. channel gaps (2x2)
    {2, 3, "x"},      //  3. channel gaps (2x3)
    {2, 2, "b"},      //  4. hexagons (2x2)
    {2, 3, "b"},      //  5. hexagons (2x3)
    {3, 3, "b"},      //  6. hexagons (3x3)
    {3, 3, "bx"},     //  7. hexagons + gaps (3x3)

    // Chapter 2: Rounded Squares (colour separation)
    {2, 2, "g"},      //  8. first squares (2x2)
    {2, 3, "g"},      //  9. squares (2x3)
    {3, 3, "g"},      // 10. squares (3x3)
    {3, 3, "gx"},     // 11. squares + gaps (3x3)
    {3, 3, "gb"},     // 12. squares + hexagons (3x3)
    {3, 4, "gb"},     // 13. squares + hexagons (3x4)
    {4, 4, "gbx"},    // 14. squares + hexagons + gaps (4x4)

    // Chapter 3: Triangles (edge counts 1-3)
    {2, 2, "n"},      // 15. first triangles (2x2)
    {2, 3, "n"},      // 16. triangles (2x3)
    {3, 3, "n"},      // 17. triangles (3x3)
    {3, 3, "nx"},     // 18. triangles + gaps (3x3)
    {3, 3, "nb"},     // 19. triangles + hexagons (3x3)
    {3, 3, "ng"},     // 20. triangles + squares (3x3)
    {3, 4, "ng"},     // 21. triangles + squares (3x4)
    {4, 4, "n"},      // 22. triangles (4x4)

    // Chapter 4: Stars (pair same-coloured symbols)
    {2, 2, "sb"},     // 23. first stars (2x2)
    {2, 3, "sx"},     // 24. stars + gaps (2x3)
    {3, 3, "sx"},     // 25. stars + gaps (3x3)
    {3, 3, "sb"},     // 26. stars + hexagons (3x3)
    {2, 3, "gs"},     // 27. stars paired with squares (2x3)
    {3, 3, "gs"},     // 28. stars + squares (3x3)
    {3, 3, "ns"},     // 29. stars + triangles (3x3)
    {4, 4, "gsb"},    // 30. stars + squares + hexagons (4x4)

    // Chapter 5: Polyominoes (area tiling)
    {2, 2, "p"},      // 31. first polyominoes (2x2)
    {2, 3, "p"},      // 32. polyominoes (2x3)
    {3, 3, "p"},      // 33. polyominoes (3x3)
    {3, 3, "px"},     // 34. polyominoes + gaps (3x3)
    {3, 3, "pb"},     // 35. polyominoes + hexagons (3x3)
    {3, 3, "gp"},     // 36. polyominoes + squares (3x3)
    {3, 3, "np"},     // 37. polyominoes + triangles (3x3)
    {4, 4, "px"},     // 38. polyominoes + gaps (4x4)

    // Chapter 6: Negators (rule elimination)
    {2, 2, "ny"},     // 39. first negator with triangles (2x2)
    {2, 3, "ny"},     // 40. negator + triangles (2x3)
    {3, 3, "ny"},     // 41. negator + triangles (3x3)
    {3, 3, "gy"},     // 42. negator + squares (3x3)
    {3, 3, "nsy"},    // 43. negator + triangles + stars (3x3)
    {3, 4, "nsy"},    // 44. negator + triangles + stars (3x4)

    // Chapter 7: Mastery & Combinations (4x4, 4x5 & 5x5)
    {4, 4, "gp"},     // 45. polyominoes + squares (4x4)
    {4, 4, "np"},     // 46. polyominoes + triangles (4x4)
    {4, 4, "ngbx"},   // 47. triangles + squares + hexagons + gaps (4x4)
    {4, 5, "n"},      // 48. triangles (4x5)
    {4, 5, "npbx"},   // 49. triangles + polyominoes + hexagons + gaps (4x5)
    {5, 5, "ngpsyx"}, // 50. all symbols combined (5x5)
};

const int level_count = sizeof(levels) / sizeof(levels[0]);
