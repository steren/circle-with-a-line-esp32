// Circle with a Line puzzle rules. Portable C with no hardware or drawing code.
//
// A puzzle is a grid of square cells. The player draws one path along the grid
// lines from the start node to the exit node, never visiting a node twice. The
// exit is always on the outer border.
// The path is correct when it satisfies every clue on the board:
//
//   Beacon  (on a node or on a line)  the path must pass through it
//   Gap     (a blocked line)          the path cannot use that line
//   Count   (a digit in a cell)       exactly that many of the cell's four
//                                     sides are part of the path
//   Gem     (a colour in a cell)      the path splits the board into areas;
//                                     no area may hold gems of two colours
//
// Puzzles are written as text maps, one character per node, line and cell:
//
//   "+-+-E"     nodes: + plain, S start, E exit (border only), o beacon
//   "|1|a|"     lines: - or | open, space = gap, o = beacon
//   "S-+o+"     cells: space = empty, 0-3 = count, a/b/c = gem colour

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PUZZLE_MAX_COLS 6
#define PUZZLE_MAX_ROWS 6
#define PUZZLE_MAX_NODES ((PUZZLE_MAX_COLS + 1) * (PUZZLE_MAX_ROWS + 1))
#define PUZZLE_MAX_CELLS (PUZZLE_MAX_COLS * PUZZLE_MAX_ROWS)
#define PUZZLE_GEM_COLORS 3

typedef enum { CELL_EMPTY, CELL_COUNT, CELL_GEM } puzzle_cell_kind_t;
typedef enum { EDGE_OPEN, EDGE_GAP, EDGE_BEACON } puzzle_edge_t;
typedef enum { DIR_UP, DIR_RIGHT, DIR_DOWN, DIR_LEFT } puzzle_dir_t;
typedef enum { STEP_BLOCKED, STEP_FORWARD, STEP_BACK } puzzle_step_t;

typedef struct {
    uint8_t kind;   // puzzle_cell_kind_t
    uint8_t value;  // count (0-3) or gem colour index
} puzzle_cell_t;

typedef struct {
    int cols, rows;   // size in cells; nodes form a (rows + 1) x (cols + 1) lattice
    bool is_start[PUZZLE_MAX_NODES], is_exit[PUZZLE_MAX_NODES];
    puzzle_cell_t cells[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS];
    bool node_beacon[PUZZLE_MAX_NODES];
    uint8_t h_edges[PUZZLE_MAX_ROWS + 1][PUZZLE_MAX_COLS];  // [r][c]: node (r, c) to (r, c + 1)
    uint8_t v_edges[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS + 1];  // [r][c]: node (r, c) to (r + 1, c)
} puzzle_t;

typedef struct {
    uint8_t nodes[PUZZLE_MAX_NODES];
    int length;
} puzzle_path_t;

// Which clues a path breaks.
typedef struct {
    bool solved;
    bool bad_cell[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS];
    bool missed_node[PUZZLE_MAX_NODES];
    bool missed_h[PUZZLE_MAX_ROWS + 1][PUZZLE_MAX_COLS];
    bool missed_v[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS + 1];
} puzzle_result_t;

static inline int puzzle_node(const puzzle_t *p, int row, int col)
{
    return row * (p->cols + 1) + col;
}

static inline int puzzle_node_row(const puzzle_t *p, int node)
{
    return node / (p->cols + 1);
}

static inline int puzzle_node_col(const puzzle_t *p, int node)
{
    return node % (p->cols + 1);
}

static inline int puzzle_node_count(const puzzle_t *p)
{
    return (p->rows + 1) * (p->cols + 1);
}

// Reads a text map (rows separated by '\n'). Returns false if it is malformed.
bool puzzle_parse(puzzle_t *p, const char *map);

// The node next to `node` in direction `dir`, or -1 if there is no line that
// way (edge of the board, or a gap).
int puzzle_neighbor(const puzzle_t *p, int node, puzzle_dir_t dir);

// The start node.
int puzzle_first_start(const puzzle_t *p);

// Resets a path to just the node `start`.
void puzzle_path_start(puzzle_path_t *path, int start);

// The direction an exit's end cap points: straight out of the board.
puzzle_dir_t puzzle_exit_dir(const puzzle_t *p, int node);

static inline int puzzle_path_head(const puzzle_path_t *path)
{
    return path->nodes[path->length - 1];
}

// Moves the head of the path one node. Stepping onto the previous node undoes
// the last move. Steps onto other visited nodes, across gaps, or onwards from
// the exit are blocked.
puzzle_step_t puzzle_path_step(const puzzle_t *p, puzzle_path_t *path, puzzle_dir_t dir);

// Numbers the areas the path splits the board into. region[r][c] gets the
// area index of each cell; returns the number of areas.
int puzzle_regions(const puzzle_t *p, const puzzle_path_t *path,
                   uint8_t region[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS]);

// How many of a cell's four sides are part of the path.
int puzzle_cell_sides(const puzzle_t *p, const puzzle_path_t *path, int row, int col);

// True if the path ends on the exit and satisfies every clue. `result` may be
// NULL; otherwise it reports which clues are broken.
bool puzzle_check(const puzzle_t *p, const puzzle_path_t *path, puzzle_result_t *result);
