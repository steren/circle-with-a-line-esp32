#include "puzzle.h"

#include <string.h>

#define B(r, c) (1u << ((r) * 4 + (c)))

const puzzle_poly_t puzzle_polys[PUZZLE_POLY_SHAPES] = {
    {1, 1, 1, B(0, 0)},                                  // 0 ('d'): 1x1 monomino
    {2, 2, 1, B(0, 0) | B(0, 1)},                        // 1 ('e'): 1x2 horizontal domino
    {2, 1, 2, B(0, 0) | B(1, 0)},                        // 2 ('f'): 2x1 vertical domino
    {3, 3, 1, B(0, 0) | B(0, 1) | B(0, 2)},              // 3 ('g'): 1x3 horizontal tromino
    {3, 1, 3, B(0, 0) | B(1, 0) | B(2, 0)},              // 4 ('h'): 3x1 vertical tromino
    {3, 2, 2, B(0, 0) | B(1, 0) | B(1, 1)},              // 5 ('i'): L-tromino #. / ##
    {3, 2, 2, B(0, 1) | B(1, 0) | B(1, 1)},              // 6 ('j'): L-tromino .# / ##
    {3, 2, 2, B(0, 0) | B(0, 1) | B(1, 0)},              // 7 ('k'): L-tromino ## / #.
    {3, 2, 2, B(0, 0) | B(0, 1) | B(1, 1)},              // 8 ('l'): L-tromino ## / .#
    {4, 2, 2, B(0, 0) | B(0, 1) | B(1, 0) | B(1, 1)},    // 9 ('m'): 2x2 O-tetromino
    {4, 3, 2, B(0, 0) | B(0, 1) | B(0, 2) | B(1, 1)},    // 10 ('n'): T-tetromino ### / .#.
    {4, 3, 2, B(0, 1) | B(1, 0) | B(1, 1) | B(1, 2)},    // 11 ('o'): T-tetromino .#. / ###
    {4, 3, 2, B(0, 0) | B(1, 0) | B(1, 1) | B(1, 2)},    // 12 ('p'): L-tetromino #.. / ###
    {4, 3, 2, B(0, 2) | B(1, 0) | B(1, 1) | B(1, 2)},    // 13 ('q'): J-tetromino ..# / ###
    {4, 3, 2, B(0, 1) | B(0, 2) | B(1, 0) | B(1, 1)},    // 14 ('r'): S-tetromino .## / ##.
    {4, 3, 2, B(0, 0) | B(0, 1) | B(1, 1) | B(1, 2)},    // 15 ('s'): Z-tetromino ##. / .##
};

#undef B

// Which lines the path runs along, in the same layout as puzzle_t's edges.
typedef struct {
    bool h[PUZZLE_MAX_ROWS + 1][PUZZLE_MAX_COLS];
    bool v[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS + 1];
} used_edges_t;

bool puzzle_parse(puzzle_t *p, const char *map)
{
    memset(p, 0, sizeof(*p));

    const char *newline = strchr(map, '\n');
    const int width = newline ? (int)(newline - map) : (int)strlen(map);
    int height = 0;
    for (const char *line = map; *line;) {
        const char *end = strchr(line, '\n');
        const int len = end ? (int)(end - line) : (int)strlen(line);
        if (len != width) {
            return false;
        }
        height++;
        line = end ? end + 1 : line + len;
    }
    if (width < 3 || height < 3 || width % 2 == 0 || height % 2 == 0) {
        return false;
    }
    p->cols = (width - 1) / 2;
    p->rows = (height - 1) / 2;
    if (p->cols > PUZZLE_MAX_COLS || p->rows > PUZZLE_MAX_ROWS) {
        return false;
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const char ch = map[y * (width + 1) + x];
            const int row = y / 2, col = x / 2;
            if (y % 2 == 0 && x % 2 == 0) {  // node
                const int node = puzzle_node(p, row, col);
                if (ch == 'S') {
                    p->is_start[node] = true;
                } else if (ch == 'E') {
                    if (row != 0 && col != 0 && row != p->rows && col != p->cols) {
                        return false;  // exits must be on the border
                    }
                    p->is_exit[node] = true;
                } else if (ch == 'o') {
                    p->node_beacon[node] = true;
                } else if (ch != '+') {
                    return false;
                }
            } else if (y % 2 == 0 || x % 2 == 0) {  // line
                const bool horizontal = (y % 2 == 0);
                uint8_t *edge = horizontal ? &p->h_edges[row][col] : &p->v_edges[row][col];
                if (ch == (horizontal ? '-' : '|')) {
                    *edge = EDGE_OPEN;
                } else if (ch == ' ') {
                    *edge = EDGE_GAP;
                } else if (ch == 'o') {
                    *edge = EDGE_BEACON;
                } else {
                    return false;
                }
            } else {  // cell
                puzzle_cell_t *cell = &p->cells[row][col];
                if (ch >= '0' && ch <= '3') {
                    cell->kind = CELL_COUNT;
                    cell->value = ch - '0';
                } else if (ch >= 'a' && ch < 'a' + PUZZLE_GEM_COLORS) {
                    cell->kind = CELL_GEM;
                    cell->value = ch - 'a';
                } else if (ch >= 'A' && ch < 'A' + PUZZLE_GEM_COLORS) {
                    cell->kind = CELL_STAR;
                    cell->value = ch - 'A';
                } else if (ch >= 'd' && ch < 'd' + PUZZLE_POLY_SHAPES) {
                    cell->kind = CELL_POLY;
                    cell->value = ch - 'd';
                } else if (ch == 'Y') {
                    cell->kind = CELL_NEGATOR;
                    cell->value = 0;
                } else if (ch != ' ') {
                    return false;
                }
            }
        }
    }
    int starts = 0, exits = 0;
    for (int node = 0; node < puzzle_node_count(p); node++) {
        starts += p->is_start[node];
        exits += p->is_exit[node];
    }
    return starts > 0 && exits > 0;
}

int puzzle_first_start(const puzzle_t *p)
{
    for (int node = 0; node < puzzle_node_count(p); node++) {
        if (p->is_start[node]) {
            return node;
        }
    }
    return 0;
}

puzzle_dir_t puzzle_exit_dir(const puzzle_t *p, int node)
{
    const int row = puzzle_node_row(p, node), col = puzzle_node_col(p, node);
    // Corners point up or down; other border nodes straight out of their side.
    if (row == 0) return DIR_UP;
    if (row == p->rows) return DIR_DOWN;
    return col == 0 ? DIR_LEFT : DIR_RIGHT;
}

int puzzle_neighbor(const puzzle_t *p, int node, puzzle_dir_t dir)
{
    const int row = puzzle_node_row(p, node), col = puzzle_node_col(p, node);
    switch (dir) {
    case DIR_UP:
        return (row > 0 && p->v_edges[row - 1][col] != EDGE_GAP) ? puzzle_node(p, row - 1, col) : -1;
    case DIR_DOWN:
        return (row < p->rows && p->v_edges[row][col] != EDGE_GAP) ? puzzle_node(p, row + 1, col) : -1;
    case DIR_LEFT:
        return (col > 0 && p->h_edges[row][col - 1] != EDGE_GAP) ? puzzle_node(p, row, col - 1) : -1;
    case DIR_RIGHT:
        return (col < p->cols && p->h_edges[row][col] != EDGE_GAP) ? puzzle_node(p, row, col + 1) : -1;
    }
    return -1;
}

void puzzle_path_start(puzzle_path_t *path, int start)
{
    path->nodes[0] = start;
    path->length = 1;
}

puzzle_step_t puzzle_path_step(const puzzle_t *p, puzzle_path_t *path, puzzle_dir_t dir)
{
    const int next = puzzle_neighbor(p, puzzle_path_head(path), dir);
    if (next < 0) {
        return STEP_BLOCKED;
    }
    if (path->length >= 2 && path->nodes[path->length - 2] == next) {
        path->length--;
        return STEP_BACK;
    }
    if (p->is_exit[puzzle_path_head(path)]) {
        return STEP_BLOCKED;  // the line stops at the exit
    }
    for (int i = 0; i < path->length; i++) {
        if (path->nodes[i] == next) {
            return STEP_BLOCKED;
        }
    }
    path->nodes[path->length++] = next;
    return STEP_FORWARD;
}

static void find_used_edges(const puzzle_t *p, const puzzle_path_t *path, used_edges_t *used)
{
    memset(used, 0, sizeof(*used));
    for (int i = 1; i < path->length; i++) {
        const int a = path->nodes[i - 1], b = path->nodes[i];
        const int first = a < b ? a : b;  // the upper or left node of the pair
        const int row = puzzle_node_row(p, first), col = puzzle_node_col(p, first);
        if (a - b == 1 || b - a == 1) {
            used->h[row][col] = true;
        } else {
            used->v[row][col] = true;
        }
    }
}

static int find_regions(const puzzle_t *p, const used_edges_t *used,
                        uint8_t region[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS])
{
    enum { UNASSIGNED = 0xFF };
    memset(region, UNASSIGNED, PUZZLE_MAX_ROWS * PUZZLE_MAX_COLS);

    int count = 0;
    for (int r0 = 0; r0 < p->rows; r0++) {
        for (int c0 = 0; c0 < p->cols; c0++) {
            if (region[r0][c0] != UNASSIGNED) {
                continue;
            }
            // Flood fill: two neighbouring cells are in the same area unless
            // the path runs along the line between them.
            uint8_t stack[PUZZLE_MAX_CELLS][2];
            int depth = 0;
            region[r0][c0] = count;
            stack[depth][0] = r0;
            stack[depth++][1] = c0;
            while (depth > 0) {
                const int r = stack[--depth][0], c = stack[depth][1];
                const struct {
                    int r, c;
                    bool open;
                } next[] = {
                    {r - 1, c, r > 0 && !used->h[r][c]},
                    {r + 1, c, r < p->rows - 1 && !used->h[r + 1][c]},
                    {r, c - 1, c > 0 && !used->v[r][c]},
                    {r, c + 1, c < p->cols - 1 && !used->v[r][c + 1]},
                };
                for (int i = 0; i < 4; i++) {
                    if (next[i].open && region[next[i].r][next[i].c] == UNASSIGNED) {
                        region[next[i].r][next[i].c] = count;
                        stack[depth][0] = next[i].r;
                        stack[depth++][1] = next[i].c;
                    }
                }
            }
            count++;
        }
    }
    return count;
}

int puzzle_regions(const puzzle_t *p, const puzzle_path_t *path,
                   uint8_t region[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS])
{
    used_edges_t used;
    find_used_edges(p, path, &used);
    return find_regions(p, &used, region);
}

static int cell_sides(const used_edges_t *used, int row, int col)
{
    return used->h[row][col] + used->h[row + 1][col] + used->v[row][col] + used->v[row][col + 1];
}

int puzzle_cell_sides(const puzzle_t *p, const puzzle_path_t *path, int row, int col)
{
    used_edges_t used;
    find_used_edges(p, path, &used);
    return cell_sides(&used, row, col);
}

static uint64_t poly_board_mask(uint8_t shape_idx, int r0, int c0)
{
    const uint16_t m = puzzle_polys[shape_idx].mask;
    uint64_t out = 0;
    for (int r = 0; r < 4; r++) {
        const uint64_t row_bits = (m >> (r * 4)) & 0xF;
        out |= row_bits << ((r0 + r) * PUZZLE_MAX_COLS + c0);
    }
    return out;
}

static bool can_tile_area(const puzzle_t *p, uint64_t remaining, const uint8_t *shapes, int count)
{
    if (count == 0) {
        return remaining == 0;
    }
    const uint8_t s = shapes[0];
    const puzzle_poly_t *sh = &puzzle_polys[s];
    if (count == 1) {
        const int first_bit = __builtin_ctzll(remaining);
        const int r_first = first_bit / PUZZLE_MAX_COLS;
        const int c_first = first_bit % PUZZLE_MAX_COLS;
        const int shape_first_col = __builtin_ctz(sh->mask & 0xF);
        const int r0 = r_first;
        const int c0 = c_first - shape_first_col;
        if (r0 < 0 || c0 < 0 || r0 + sh->height > p->rows || c0 + sh->width > p->cols) {
            return false;
        }
        return poly_board_mask(s, r0, c0) == remaining;
    }
    for (int r0 = 0; r0 <= p->rows - sh->height; r0++) {
        for (int c0 = 0; c0 <= p->cols - sh->width; c0++) {
            const uint64_t pm = poly_board_mask(s, r0, c0);
            if ((remaining & pm) == pm && can_tile_area(p, remaining ^ pm, shapes + 1, count - 1)) {
                return true;
            }
        }
    }
    return false;
}

static bool eval_area_clues(const puzzle_t *p, uint64_t area_mask, int cell_count,
                            const uint8_t clues[][2], int clue_count,
                            const uint8_t sides[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS],
                            uint64_t cancel_mask, uint64_t *bad_mask_out)
{
    uint64_t bad = 0;
    uint8_t gem_colors = 0;
    uint8_t color_counts[PUZZLE_GEM_COLORS] = {0};
    uint8_t poly_shapes[PUZZLE_MAX_CELLS];
    uint8_t poly_indices[PUZZLE_MAX_CELLS];
    int poly_count = 0, poly_size_sum = 0;

    for (int i = 0; i < clue_count; i++) {
        if ((cancel_mask >> i) & 1) {
            continue;
        }
        const int r = clues[i][0], c = clues[i][1];
        const puzzle_cell_t *cell = &p->cells[r][c];
        switch (cell->kind) {
        case CELL_COUNT:
            if (sides[r][c] != cell->value) {
                bad |= 1ULL << i;
            }
            break;
        case CELL_GEM:
            gem_colors |= 1u << cell->value;
            color_counts[cell->value]++;
            break;
        case CELL_STAR:
            color_counts[cell->value]++;
            break;
        case CELL_POLY:
            poly_shapes[poly_count] = cell->value;
            poly_indices[poly_count] = i;
            poly_count++;
            poly_size_sum += puzzle_polys[cell->value].size;
            break;
        default:
            break;
        }
    }

    const bool gem_conflict = (gem_colors & (gem_colors - 1)) != 0;
    for (int i = 0; i < clue_count; i++) {
        if ((cancel_mask >> i) & 1) {
            continue;
        }
        const puzzle_cell_t *cell = &p->cells[clues[i][0]][clues[i][1]];
        if (cell->kind == CELL_GEM && gem_conflict) {
            bad |= 1ULL << i;
        } else if (cell->kind == CELL_STAR && color_counts[cell->value] != 2) {
            bad |= 1ULL << i;
        }
    }

    if (poly_count > 0) {
        if (poly_size_sum != cell_count || !can_tile_area(p, area_mask, poly_shapes, poly_count)) {
            for (int k = 0; k < poly_count; k++) {
                bad |= 1ULL << poly_indices[k];
            }
        }
    }

    if (bad_mask_out) {
        *bad_mask_out = bad;
    }
    return bad == 0;
}

static bool can_cancel_clues(const puzzle_t *p, uint64_t area_mask, int cell_count,
                             const uint8_t clues[][2], int clue_count,
                             const uint8_t sides[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS],
                             int start_idx, int left, uint64_t cancel_mask)
{
    if (left == 0) {
        return eval_area_clues(p, area_mask, cell_count, clues, clue_count, sides, cancel_mask, NULL);
    }
    for (int i = start_idx; i <= clue_count - left; i++) {
        if (can_cancel_clues(p, area_mask, cell_count, clues, clue_count, sides, i + 1, left - 1,
                             cancel_mask | (1ULL << i))) {
            return true;
        }
    }
    return false;
}

bool puzzle_check_area(const puzzle_t *p, const uint8_t area_cells[][2], int cell_count,
                       const uint8_t sides[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS],
                       bool bad_cell[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS])
{
    uint64_t area_mask = 0;
    uint8_t negators[PUZZLE_MAX_CELLS][2];
    uint8_t clues[PUZZLE_MAX_CELLS][2];
    int neg_count = 0, clue_count = 0;

    for (int i = 0; i < cell_count; i++) {
        const int r = area_cells[i][0], c = area_cells[i][1];
        area_mask |= 1ULL << (r * PUZZLE_MAX_COLS + c);
        const uint8_t kind = p->cells[r][c].kind;
        if (kind == CELL_NEGATOR) {
            negators[neg_count][0] = r;
            negators[neg_count++][1] = c;
        } else if (kind != CELL_EMPTY) {
            clues[clue_count][0] = r;
            clues[clue_count++][1] = c;
        }
    }

    if (neg_count == 0 && clue_count == 0) {
        return true;
    }

    uint64_t bad0 = 0;
    const bool ok0 = eval_area_clues(p, area_mask, cell_count, clues, clue_count, sides, 0, &bad0);
    if (neg_count == 0) {
        if (ok0) {
            return true;
        }
        if (bad_cell) {
            for (int i = 0; i < clue_count; i++) {
                if ((bad0 >> i) & 1) {
                    bad_cell[clues[i][0]][clues[i][1]] = true;
                }
            }
        }
        return false;
    }

    // Each negator must eliminate an error: if the area is already valid on its
    // own, or has fewer clues than negators, the negators fail.
    if (!ok0 && clue_count >= neg_count &&
        can_cancel_clues(p, area_mask, cell_count, clues, clue_count, sides, 0, neg_count, 0)) {
        return true;
    }

    if (bad_cell) {
        for (int i = 0; i < neg_count; i++) {
            bad_cell[negators[i][0]][negators[i][1]] = true;
        }
        for (int i = 0; i < clue_count; i++) {
            if ((bad0 >> i) & 1) {
                bad_cell[clues[i][0]][clues[i][1]] = true;
            }
        }
    }
    return false;
}

bool puzzle_check(const puzzle_t *p, const puzzle_path_t *path, puzzle_result_t *result)
{
    puzzle_result_t local;
    if (!result) {
        result = &local;
    }
    memset(result, 0, sizeof(*result));

    used_edges_t used;
    find_used_edges(p, path, &used);
    bool ok = path->length > 0 && p->is_start[path->nodes[0]] && p->is_exit[puzzle_path_head(path)];

    // Beacons
    bool visited[PUZZLE_MAX_NODES] = {false};
    for (int i = 0; i < path->length; i++) {
        visited[path->nodes[i]] = true;
    }
    for (int node = 0; node < puzzle_node_count(p); node++) {
        if (p->node_beacon[node] && !visited[node]) {
            result->missed_node[node] = true;
            ok = false;
        }
    }
    for (int r = 0; r <= p->rows; r++) {
        for (int c = 0; c < p->cols; c++) {
            if (p->h_edges[r][c] == EDGE_BEACON && !used.h[r][c]) {
                result->missed_h[r][c] = true;
                ok = false;
            }
        }
    }
    for (int r = 0; r < p->rows; r++) {
        for (int c = 0; c <= p->cols; c++) {
            if (p->v_edges[r][c] == EDGE_BEACON && !used.v[r][c]) {
                result->missed_v[r][c] = true;
                ok = false;
            }
        }
    }

    // Cell clues per area (counts, squares, stars, polyominoes, negators)
    uint8_t sides[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS];
    for (int r = 0; r < p->rows; r++) {
        for (int c = 0; c < p->cols; c++) {
            sides[r][c] = cell_sides(&used, r, c);
        }
    }
    uint8_t region[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS];
    const int region_count = find_regions(p, &used, region);
    for (int i = 0; i < region_count; i++) {
        uint8_t area_cells[PUZZLE_MAX_CELLS][2];
        int count = 0;
        for (int r = 0; r < p->rows; r++) {
            for (int c = 0; c < p->cols; c++) {
                if (region[r][c] == i) {
                    area_cells[count][0] = r;
                    area_cells[count++][1] = c;
                }
            }
        }
        if (!puzzle_check_area(p, area_cells, count, sides, result->bad_cell)) {
            ok = false;
        }
    }

    result->solved = ok;
    return ok;
}
