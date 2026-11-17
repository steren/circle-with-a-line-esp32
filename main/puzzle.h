// Circle with a Line puzzle rules. Portable C with no hardware or drawing code.
//
// A puzzle is a grid of square cells. The player draws one path along the grid
// lines from the start node to the exit node, never visiting a node twice. The
// exit is always on the outer border.
// The path is correct when it satisfies every clue on the board:
//
//   Beacon    (on a node or on a line)  the path must pass through it
//   Gap       (a blocked line)          the path cannot use that line
//   Count     (1-3 triangles in a cell) that many of the cell's four sides are
//                                       part of the path
//   Gem       (a rounded square)        the path splits the board into areas;
//                                       no area may hold squares of two colours
//   Star      (an 8-pointed star)       its area must hold exactly two symbols
//                                       (stars or squares) of its colour
//   Polyomino (a Tetris shape)          the area must be tiled by the shapes
//                                       inside it, without rotation
//   Negator   (a 3-pronged Y)           cancels one broken symbol in its area
//
// Puzzles are written as text maps, one character per node, line and cell:
//
//   "+-+-E"     nodes: + plain, S start, E exit (border only), o beacon
//   "|1|a|"     lines: - or | open, space = gap, o = beacon
//   "S-+o+"     cells: space = empty, 0-3 = count, a-c = square, A-C = star,
//                      d-s = polyomino shape, Y = negator

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PUZZLE_MAX_COLS 6
#define PUZZLE_MAX_ROWS 6
#define PUZZLE_MAX_NODES ((PUZZLE_MAX_COLS + 1) * (PUZZLE_MAX_ROWS + 1))
#define PUZZLE_MAX_CELLS (PUZZLE_MAX_COLS * PUZZLE_MAX_ROWS)
#define PUZZLE_GEM_COLORS 3
#define PUZZLE_POLY_SHAPES 16

typedef enum {
    CELL_EMPTY,
    CELL_COUNT,
    CELL_GEM,
    CELL_STAR,
    CELL_POLY,
    CELL_NEGATOR,
} puzzle_cell_kind_t;
typedef enum { EDGE_OPEN, EDGE_GAP, EDGE_BEACON } puzzle_edge_t;
typedef enum { DIR_UP, DIR_RIGHT, DIR_DOWN, DIR_LEFT } puzzle_dir_t;
typedef enum { STEP_BLOCKED, STEP_FORWARD, STEP_BACK } puzzle_step_t;

typedef struct {
    uint8_t size;          // number of blocks (1-4)
    uint8_t width, height; // bounding box dimensions (1-3)
    uint16_t mask;         // 4x4 bitmask: bit (r * 4 + c) is set if block (r, c) is present
} puzzle_poly_t;

extern const puzzle_poly_t puzzle_polys[PUZZLE_POLY_SHAPES];

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

// Checks whether a single area (whose cells are listed in `area_cells[0..cell_count-1]`)
// satisfies all cell clues inside it, given the number of path sides around each cell.
// If `bad_cell` is not NULL and the area fails, marks the offending cells in `bad_cell`.
bool puzzle_check_area(const puzzle_t *p, const uint8_t area_cells[][2], int cell_count,
                       const uint8_t sides[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS],
                       bool bad_cell[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS]);

// True if the path ends on the exit and satisfies every clue. `result` may be
// NULL; otherwise it reports which clues are broken.
bool puzzle_check(const puzzle_t *p, const puzzle_path_t *path, puzzle_result_t *result);
