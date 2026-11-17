#include "generator.h"

#include <stdlib.h>
#include <string.h>

// ---- Solver: depth-first search over every path from start to exit ----
//
// After every move the search asks whether the line could still be finished
// correctly, and backs up straight away if not. Everything the rest of the
// line could ever touch lies in the nodes it can still reach, so:
//   - the exit, and every beacon not yet passed, must be reachable;
//   - a count must still be able to collect enough sides;
//   - an area the rest of the line cannot enter is final, and must not mix gems.

typedef struct {
    const puzzle_t *p;
    puzzle_path_t path;
    bool visited[PUZZLE_MAX_NODES];
    bool used_h[PUZZLE_MAX_ROWS + 1][PUZZLE_MAX_COLS];  // lines the path runs along
    bool used_v[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS + 1];
    uint8_t sides[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS];    // path lines around each cell so far
    bool has_counts, has_area_rules, has_beacons;
    int total_negators;
    long count, limit;
    long steps, budget;  // budget 0 = no limit
    bool out_of_budget;
    puzzle_path_t first;
} solver_t;

// Marks the line a-b as used or not, and updates the side counts of the cells
// next to it. Returns false if a count clue is now exceeded.
static bool set_line(solver_t *s, int a, int b, bool used)
{
    const puzzle_t *p = s->p;
    const int first = a < b ? a : b;
    const int row = puzzle_node_row(p, first), col = puzzle_node_col(p, first);
    const bool horizontal = (abs(a - b) == 1);
    if (horizontal) {
        s->used_h[row][col] = used;
    } else {
        s->used_v[row][col] = used;
    }
    const int cells[2][2] = {
        {horizontal ? row - 1 : row, horizontal ? col : col - 1},
        {row, col},
    };
    bool ok = true;
    for (int i = 0; i < 2; i++) {
        const int r = cells[i][0], c = cells[i][1];
        if (r < 0 || c < 0 || r >= p->rows || c >= p->cols) {
            continue;
        }
        s->sides[r][c] += used ? 1 : -1;
        if (!s->total_negators && p->cells[r][c].kind == CELL_COUNT && s->sides[r][c] > p->cells[r][c].value) {
            ok = false;
        }
    }
    return ok;
}

// Whether the rest of the line could still run along the line between the
// two nodes. `live` holds the nodes it can still reach, plus the head.
static bool could_use(const puzzle_t *p, const bool *live, int a, int b, puzzle_edge_t kind)
{
    (void)p;
    return kind != EDGE_GAP && live[a] && live[b];
}

static bool still_possible(solver_t *s)
{
    const puzzle_t *p = s->p;
    const int head = puzzle_path_head(&s->path);

    // Nodes the rest of the line can reach.
    bool live[PUZZLE_MAX_NODES] = {false};
    uint8_t queue[PUZZLE_MAX_NODES];
    int queued = 0, done = 0;
    live[head] = true;
    queue[queued++] = head;
    while (done < queued) {
        const int node = queue[done++];
        for (int dir = 0; dir < 4; dir++) {
            const int next = puzzle_neighbor(p, node, dir);
            if (next >= 0 && !s->visited[next] && !live[next]) {
                live[next] = true;
                queue[queued++] = next;
            }
        }
    }
    bool exit_reachable = false;
    for (int node = 0; node < puzzle_node_count(p) && !exit_reachable; node++) {
        exit_reachable = p->is_exit[node] && live[node];
    }
    if (!exit_reachable) {
        return false;
    }

    if (s->has_beacons) {
        for (int node = 0; node < puzzle_node_count(p); node++) {
            if (p->node_beacon[node] && !s->visited[node] && !live[node]) {
                return false;
            }
        }
        for (int r = 0; r <= p->rows; r++) {
            for (int c = 0; c < p->cols; c++) {
                if (p->h_edges[r][c] == EDGE_BEACON && !s->used_h[r][c] &&
                    !(live[puzzle_node(p, r, c)] && live[puzzle_node(p, r, c + 1)])) {
                    return false;
                }
            }
        }
        for (int r = 0; r < p->rows; r++) {
            for (int c = 0; c <= p->cols; c++) {
                if (p->v_edges[r][c] == EDGE_BEACON && !s->used_v[r][c] &&
                    !(live[puzzle_node(p, r, c)] && live[puzzle_node(p, r + 1, c)])) {
                    return false;
                }
            }
        }
    }

    if (!s->has_counts && !s->has_area_rules && !s->total_negators) {
        return true;
    }

    // For every cell: how many of its sides the rest of the line could still add.
    uint8_t open_sides[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS];
    int broken_counts = 0;
    for (int r = 0; r < p->rows; r++) {
        for (int c = 0; c < p->cols; c++) {
            const int tl = puzzle_node(p, r, c), tr = tl + 1, bl = puzzle_node(p, r + 1, c), br = bl + 1;
            open_sides[r][c] =
                (!s->used_h[r][c] && could_use(p, live, tl, tr, p->h_edges[r][c])) +
                (!s->used_h[r + 1][c] && could_use(p, live, bl, br, p->h_edges[r + 1][c])) +
                (!s->used_v[r][c] && could_use(p, live, tl, bl, p->v_edges[r][c])) +
                (!s->used_v[r][c + 1] && could_use(p, live, tr, br, p->v_edges[r][c + 1]));
            const puzzle_cell_t *cell = &p->cells[r][c];
            if (cell->kind == CELL_COUNT &&
                (s->sides[r][c] > cell->value || s->sides[r][c] + open_sides[r][c] < cell->value)) {
                if (++broken_counts > s->total_negators) {
                    return false;
                }
            }
        }
    }

    if (!s->has_area_rules && !s->total_negators) {
        return true;
    }

    // Areas the path has already closed off. One the rest of the line cannot
    // reach (none of its cells has a side the line could still use) is final.
    uint8_t region[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS];
    memset(region, 0xFF, sizeof(region));
    int count = 0;
    for (int r0 = 0; r0 < p->rows; r0++) {
        for (int c0 = 0; c0 < p->cols; c0++) {
            if (region[r0][c0] != 0xFF) {
                continue;
            }
            uint8_t stack[PUZZLE_MAX_CELLS][2];
            uint8_t area_cells[PUZZLE_MAX_CELLS][2];
            int depth = 0, area_len = 0, max_poly = 0, area_negators = 0;
            bool open = false;
            region[r0][c0] = count;
            stack[depth][0] = r0;
            stack[depth++][1] = c0;
            while (depth > 0) {
                const int r = stack[--depth][0], c = stack[depth][1];
                area_cells[area_len][0] = r;
                area_cells[area_len++][1] = c;
                if (open_sides[r][c]) {
                    open = true;
                }
                if (p->cells[r][c].kind == CELL_POLY) {
                    const int sz = puzzle_polys[p->cells[r][c].value].size;
                    if (sz > max_poly) max_poly = sz;
                } else if (p->cells[r][c].kind == CELL_NEGATOR) {
                    area_negators++;
                }
                const struct {
                    int r, c;
                    bool joined;
                } next[] = {
                    {r - 1, c, r > 0 && !s->used_h[r][c]},
                    {r + 1, c, r < p->rows - 1 && !s->used_h[r + 1][c]},
                    {r, c - 1, c > 0 && !s->used_v[r][c]},
                    {r, c + 1, c < p->cols - 1 && !s->used_v[r][c + 1]},
                };
                for (int i = 0; i < 4; i++) {
                    if (next[i].joined && region[next[i].r][next[i].c] == 0xFF) {
                        region[next[i].r][next[i].c] = count;
                        stack[depth][0] = next[i].r;
                        stack[depth++][1] = next[i].c;
                    }
                }
            }
            if (!open) {
                if (!puzzle_check_area(p, area_cells, area_len, s->sides, NULL)) {
                    return false;
                }
            } else if (area_negators == 0 && max_poly > area_len) {
                return false;
            }
            count++;
        }
    }
    return true;
}

static void search(solver_t *s)
{
    if (s->budget && s->steps >= s->budget) {
        s->out_of_budget = true;
        return;
    }
    s->steps++;
    const int head = puzzle_path_head(&s->path);
    if (s->p->is_exit[head]) {
        if (puzzle_check(s->p, &s->path, NULL)) {
            if (s->count == 0) {
                s->first = s->path;
            }
            s->count++;
        }
        return;  // the line stops at the exit
    }
    for (int dir = 0; dir < 4 && s->count < s->limit && !s->out_of_budget; dir++) {
        const int next = puzzle_neighbor(s->p, head, dir);
        if (next < 0 || s->visited[next]) {
            continue;
        }
        s->visited[next] = true;
        s->path.nodes[s->path.length++] = next;
        if (set_line(s, head, next, true) && still_possible(s)) {
            search(s);
        }
        set_line(s, head, next, false);
        s->path.length--;
        s->visited[next] = false;
    }
}

long puzzle_count_solutions(const puzzle_t *p, long limit, long budget, puzzle_path_t *first, long *steps)
{
    solver_t s = {.p = p, .limit = limit, .budget = budget};
    for (int r = 0; r < p->rows; r++) {
        for (int c = 0; c < p->cols; c++) {
            const uint8_t kind = p->cells[r][c].kind;
            s.has_counts |= kind == CELL_COUNT;
            s.has_area_rules |= (kind == CELL_GEM || kind == CELL_STAR || kind == CELL_POLY);
            s.total_negators += kind == CELL_NEGATOR;
        }
    }
    for (int node = 0; node < puzzle_node_count(p); node++) {
        s.has_beacons |= p->node_beacon[node];
    }
    for (int r = 0; r <= p->rows; r++) {
        for (int c = 0; c <= p->cols; c++) {
            s.has_beacons |= (c < p->cols && p->h_edges[r][c] == EDGE_BEACON) ||
                             (r < p->rows && p->v_edges[r][c] == EDGE_BEACON);
        }
    }

    for (int start = 0; start < puzzle_node_count(p) && s.count < limit && !s.out_of_budget; start++) {
        if (!p->is_start[start]) {
            continue;
        }
        puzzle_path_start(&s.path, start);
        s.visited[start] = true;
        search(&s);
        s.visited[start] = false;
    }
    if (steps) {
        *steps += s.steps;
    }
    if (first && s.count > 0) {
        *first = s.first;
    }
    if (s.out_of_budget && s.count < limit) {
        return -1;
    }
    return s.count;
}

// ---- Generator ----

// Search steps one uniqueness check may take; past that, the clue it is
// testing is kept just in case.
#define CHECK_BUDGET 20000
#define MAX_ATTEMPTS 60

// State of one generation. Kept per call, so puzzles can be generated in
// several tasks at once.
typedef struct {
    uint32_t rng;
    long steps, max_steps;  // max_steps 0 = no limit
} gen_t;

static int rnd(gen_t *ctx, int n)
{
    ctx->rng ^= ctx->rng << 13;
    ctx->rng ^= ctx->rng >> 17;
    ctx->rng ^= ctx->rng << 5;
    return (int)(ctx->rng % (uint32_t)n);
}

static bool out_of_steps(const gen_t *ctx)
{
    return ctx->max_steps && ctx->steps >= ctx->max_steps;
}

// Counts solutions up to `limit`, or returns -1 if that would take too long.
static long count_solutions(gen_t *ctx, const puzzle_t *p, long limit)
{
    long budget = CHECK_BUDGET;
    if (ctx->max_steps) {
        if (out_of_steps(ctx)) {
            return -1;
        }
        if (ctx->max_steps - ctx->steps < budget) {
            budget = ctx->max_steps - ctx->steps;
        }
    }
    return puzzle_count_solutions(p, limit, budget, NULL, &ctx->steps);
}

static bool random_walk(gen_t *ctx, const puzzle_t *p, puzzle_path_t *path, bool *visited)
{
    const int head = puzzle_path_head(path);
    if (p->is_exit[head]) {
        return true;
    }
    int dirs[4] = {0, 1, 2, 3};
    for (int i = 3; i > 0; i--) {
        const int j = rnd(ctx, i + 1), t = dirs[i];
        dirs[i] = dirs[j];
        dirs[j] = t;
    }
    for (int i = 0; i < 4; i++) {
        const int next = puzzle_neighbor(p, head, dirs[i]);
        if (next < 0 || visited[next]) {
            continue;
        }
        visited[next] = true;
        path->nodes[path->length++] = next;
        if (random_walk(ctx, p, path, visited)) {
            return true;
        }
        path->length--;
        visited[next] = false;
    }
    return false;
}

static int random_border_node(gen_t *ctx, const puzzle_t *p)
{
    for (;;) {
        const int row = rnd(ctx, p->rows + 1), col = rnd(ctx, p->cols + 1);
        if (row == 0 || col == 0 || row == p->rows || col == p->cols) {
            return puzzle_node(p, row, col);
        }
    }
}

typedef enum { CLUE_CELL, CLUE_CELL_PAIR, CLUE_NODE, CLUE_H_EDGE, CLUE_V_EDGE, CLUE_H_GAP, CLUE_V_GAP } clue_kind_t;

typedef struct {
    clue_kind_t kind;
    int row, col;    // or node index in `row` for CLUE_NODE
    int row2, col2;  // second cell for CLUE_CELL_PAIR
} clue_t;

static int distance(const puzzle_t *p, int a, int b)
{
    return abs(puzzle_node_row(p, a) - puzzle_node_row(p, b)) + abs(puzzle_node_col(p, a) - puzzle_node_col(p, b));
}

static uint64_t poly_at(uint8_t shape_idx, int r0, int c0)
{
    const uint16_t m = puzzle_polys[shape_idx].mask;
    uint64_t out = 0;
    for (int r = 0; r < 4; r++) {
        const uint64_t row_bits = (m >> (r * 4)) & 0xF;
        out |= row_bits << ((r0 + r) * PUZZLE_MAX_COLS + c0);
    }
    return out;
}

static bool match_single_poly(const puzzle_t *p, uint64_t area_mask, int area_len, uint8_t *out_shape)
{
    const int first_bit = __builtin_ctzll(area_mask);
    const int r_first = first_bit / PUZZLE_MAX_COLS;
    const int c_first = first_bit % PUZZLE_MAX_COLS;
    for (uint8_t s = 1; s < PUZZLE_POLY_SHAPES; s++) {
        const puzzle_poly_t *sh = &puzzle_polys[s];
        if (sh->size != area_len) {
            continue;
        }
        const int c0 = c_first - __builtin_ctz(sh->mask & 0xF);
        if (c0 >= 0 && r_first + sh->height <= p->rows && c0 + sh->width <= p->cols &&
            poly_at(s, r_first, c0) == area_mask) {
            *out_shape = s;
            return true;
        }
    }
    return false;
}

static bool match_two_polys(gen_t *ctx, const puzzle_t *p, uint64_t area_mask, int area_len,
                            uint8_t *out_s1, uint8_t *out_s2)
{
    const int first_bit = __builtin_ctzll(area_mask);
    const int r_first = first_bit / PUZZLE_MAX_COLS;
    const int c_first = first_bit % PUZZLE_MAX_COLS;
    uint8_t order[PUZZLE_POLY_SHAPES - 1];
    for (int i = 0; i < PUZZLE_POLY_SHAPES - 1; i++) {
        order[i] = i + 1;
    }
    for (int i = PUZZLE_POLY_SHAPES - 2; i > 0; i--) {
        const int j = rnd(ctx, i + 1);
        const uint8_t t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    for (int i = 0; i < PUZZLE_POLY_SHAPES - 1; i++) {
        const uint8_t s1 = order[i];
        const puzzle_poly_t *sh1 = &puzzle_polys[s1];
        if (sh1->size >= area_len) {
            continue;
        }
        const int c0 = c_first - __builtin_ctz(sh1->mask & 0xF);
        if (c0 < 0 || r_first + sh1->height > p->rows || c0 + sh1->width > p->cols) {
            continue;
        }
        const uint64_t pm1 = poly_at(s1, r_first, c0);
        if ((area_mask & pm1) == pm1) {
            uint8_t s2;
            if (match_single_poly(p, area_mask ^ pm1, area_len - sh1->size, &s2)) {
                *out_s1 = s1;
                *out_s2 = s2;
                return true;
            }
        }
    }
    return false;
}

// Picks a start and an exit on the border, well apart, and a random path
// between them that covers a good part of the board.
static void random_layout(gen_t *ctx, puzzle_t *p, int cols, int rows, puzzle_path_t *path)
{
    memset(p, 0, sizeof(*p));
    p->cols = cols;
    p->rows = rows;
    const int min_distance = cols > rows ? cols : rows;
    int start, exit;
    do {
        start = random_border_node(ctx, p);
        exit = random_border_node(ctx, p);
    } while (distance(p, start, exit) < min_distance);
    p->is_start[start] = true;
    p->is_exit[exit] = true;

    const int nodes = puzzle_node_count(p);
    do {
        bool visited[PUZZLE_MAX_NODES] = {false};
        puzzle_path_start(path, start);
        visited[start] = true;
        random_walk(ctx, p, path, visited);
    } while (path->length < nodes * 45 / 100 || path->length > nodes * 85 / 100);
}

// Builds one candidate puzzle around a random path. With `strict`, returns
// false unless the result has exactly one solution; otherwise always returns a
// solvable puzzle, keeping every clue a check could not prove unnecessary.
static bool generate_once(gen_t *ctx, puzzle_t *p, int cols, int rows, const char *rules, bool strict,
                          int *clue_count)
{
    const bool use_counts = strchr(rules, 'n'), use_gems = strchr(rules, 'g');
    const bool use_stars = strchr(rules, 's'), use_polys = strchr(rules, 'p');
    const bool use_negators = strchr(rules, 'y');
    const bool use_beacons = strchr(rules, 'b'), use_gaps = strchr(rules, 'x');

    // The intended solution. For area-based rules (squares, stars, polyominoes,
    // negators), re-roll the random path until it splits the board into at
    // least two areas with enough room for those symbols.
    puzzle_path_t path;
    uint8_t region[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS];
    int region_count = 0;
    const bool needs_two_star_areas = use_stars && !use_gems && !use_beacons;
    const int min_neg_cells = ((use_gems || use_stars) && !use_counts && !use_polys) ? 4 : 2;
    for (int layout_try = 0; layout_try < 40; layout_try++) {
        random_layout(ctx, p, cols, rows, &path);
        region_count = puzzle_regions(p, &path, region);
        if (!use_gems && !use_stars && !use_polys && !use_negators) {
            break;
        }
        int size2_areas = 0, max_area = 0;
        int cell_counts[PUZZLE_MAX_CELLS] = {0};
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                cell_counts[region[r][c]]++;
            }
        }
        for (int i = 0; i < region_count; i++) {
            size2_areas += cell_counts[i] >= 2;
            if (cell_counts[i] > max_area) max_area = cell_counts[i];
        }
        if (region_count >= 2 && (!needs_two_star_areas || size2_areas >= 2) &&
            (!use_negators || max_area >= min_neg_cells)) {
            break;
        }
    }
    if ((use_gems || use_stars || use_polys || use_negators) && region_count < 2 && strict) {
        return false;
    }

    bool on_path_h[PUZZLE_MAX_ROWS + 1][PUZZLE_MAX_COLS] = {{false}};
    bool on_path_v[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS + 1] = {{false}};
    for (int i = 1; i < path.length; i++) {
        const int a = path.nodes[i - 1], b = path.nodes[i], first = a < b ? a : b;
        if (abs(a - b) == 1) {
            on_path_h[puzzle_node_row(p, first)][puzzle_node_col(p, first)] = true;
        } else {
            on_path_v[puzzle_node_row(p, first)][puzzle_node_col(p, first)] = true;
        }
    }

    clue_t clues[PUZZLE_MAX_CELLS + 3 * PUZZLE_MAX_NODES];
    int n = 0, total_clues = 0;

    // Gaps go on lines the solution does not use. On a gap-only maze, start
    // with every unused line blocked and remove gaps that are not needed for a
    // unique solution.
    const bool gaps_only = use_gaps && !use_counts && !use_gems && !use_stars && !use_polys && !use_beacons;
    if (use_gaps) {
        for (int r = 0; r <= rows; r++) {
            for (int c = 0; c < cols; c++) {
                if (!on_path_h[r][c] && (gaps_only || rnd(ctx, 100) < 22)) {
                    p->h_edges[r][c] = EDGE_GAP;
                    if (gaps_only) {
                        clues[n++] = (clue_t){CLUE_H_GAP, r, c, 0, 0};
                        total_clues++;
                    }
                }
            }
        }
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c <= cols; c++) {
                if (!on_path_v[r][c] && (gaps_only || rnd(ctx, 100) < 22)) {
                    p->v_edges[r][c] = EDGE_GAP;
                    if (gaps_only) {
                        clues[n++] = (clue_t){CLUE_V_GAP, r, c, 0, 0};
                        total_clues++;
                    }
                }
            }
        }
    }

    uint8_t region_color[PUZZLE_MAX_CELLS];
    for (int i = 0; i < region_count; i++) {
        region_color[i] = (i < 2) ? i : rnd(ctx, region_count > 3 ? PUZZLE_GEM_COLORS : 2);
    }

    // Group and shuffle the cells of each area.
    uint8_t reg_cells[PUZZLE_MAX_CELLS][PUZZLE_MAX_CELLS][2];
    int reg_len[PUZZLE_MAX_CELLS] = {0};
    int reg_used[PUZZLE_MAX_CELLS] = {0};
    uint64_t reg_mask[PUZZLE_MAX_CELLS] = {0};
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            const int reg = region[r][c];
            reg_cells[reg][reg_len[reg]][0] = r;
            reg_cells[reg][reg_len[reg]++][1] = c;
            reg_mask[reg] |= 1ULL << (r * PUZZLE_MAX_COLS + c);
        }
    }
    for (int reg = 0; reg < region_count; reg++) {
        for (int i = reg_len[reg] - 1; i > 0; i--) {
            const int j = rnd(ctx, i + 1);
            const uint8_t tr = reg_cells[reg][i][0], tc = reg_cells[reg][i][1];
            reg_cells[reg][i][0] = reg_cells[reg][j][0];
            reg_cells[reg][i][1] = reg_cells[reg][j][1];
            reg_cells[reg][j][0] = tr;
            reg_cells[reg][j][1] = tc;
        }
    }

    // 1. Negator: pick one area and give it a negator plus one deliberate error
    // that the negator cancels. Both stay locked on the board.
    int neg_reg = -1;
    if (use_negators) {
        for (int tries = 0; tries < 2 * region_count && neg_reg < 0; tries++) {
            const int cand = rnd(ctx, region_count);
            if (reg_len[cand] >= min_neg_cells) {
                neg_reg = cand;
            }
        }
        if (neg_reg < 0) {
            for (int i = 0; i < region_count; i++) {
                if (reg_len[i] >= min_neg_cells) {
                    neg_reg = i;
                    break;
                }
            }
        }
        if (neg_reg < 0) {
            if (strict) return false;
        } else {
            const int rn = reg_cells[neg_reg][reg_used[neg_reg]][0];
            const int cn = reg_cells[neg_reg][reg_used[neg_reg]++][1];
            const int rb = reg_cells[neg_reg][reg_used[neg_reg]][0];
            const int cb = reg_cells[neg_reg][reg_used[neg_reg]++][1];
            p->cells[rn][cn] = (puzzle_cell_t){CELL_NEGATOR, 0};
            total_clues += 2;
            if (use_counts || (!use_gems && !use_stars && !use_polys)) {
                const int actual = puzzle_cell_sides(p, &path, rb, cb);
                const int wrong = actual == 1 ? 2 : actual == 2 ? 3 : 1;
                p->cells[rb][cb] = (puzzle_cell_t){CELL_COUNT, wrong};
            } else if (use_gems && reg_len[neg_reg] >= 3) {
                // Place squares of the area's true colour (0 or 1) and one square of
                // colour 2, so the colour-2 square conflicts with every area and must
                // be enclosed with the negator.
                for (int k = 0; k < 2 && reg_used[neg_reg] < reg_len[neg_reg]; k++) {
                    const int rt = reg_cells[neg_reg][reg_used[neg_reg]][0];
                    const int ct = reg_cells[neg_reg][reg_used[neg_reg]++][1];
                    p->cells[rt][ct] = (puzzle_cell_t){CELL_GEM, region_color[neg_reg] & 1};
                    total_clues++;
                }
                p->cells[rb][cb] = (puzzle_cell_t){CELL_GEM, 2};
            } else if (use_stars) {
                p->cells[rb][cb] = (puzzle_cell_t){CELL_STAR, 2};
            } else {
                p->cells[rb][cb] = (puzzle_cell_t){CELL_POLY, reg_len[neg_reg] == 2 ? 3 : 1};
            }
        }
    }

    // 2. Polyominoes: tile areas with 1 or 2 shapes from puzzle_polys.
    if (use_polys) {
        for (int reg = 0; reg < region_count; reg++) {
            if (reg == neg_reg) {
                continue;
            }
            const int rem = reg_len[reg] - reg_used[reg];
            uint8_t s1, s2;
            if (reg_len[reg] >= 1 && reg_len[reg] <= 4 && rem >= 1 &&
                match_single_poly(p, reg_mask[reg], reg_len[reg], &s1)) {
                const int r = reg_cells[reg][reg_used[reg]][0], c = reg_cells[reg][reg_used[reg]++][1];
                p->cells[r][c] = (puzzle_cell_t){CELL_POLY, s1};
                clues[n++] = (clue_t){CLUE_CELL, r, c, 0, 0};
                total_clues++;
            } else if (reg_len[reg] >= 3 && reg_len[reg] <= 8 && rem >= 2 &&
                       match_two_polys(ctx, p, reg_mask[reg], reg_len[reg], &s1, &s2)) {
                const int r1 = reg_cells[reg][reg_used[reg]][0], c1 = reg_cells[reg][reg_used[reg]++][1];
                const int r2 = reg_cells[reg][reg_used[reg]][0], c2 = reg_cells[reg][reg_used[reg]++][1];
                p->cells[r1][c1] = (puzzle_cell_t){CELL_POLY, s1};
                p->cells[r2][c2] = (puzzle_cell_t){CELL_POLY, s2};
                clues[n++] = (clue_t){CLUE_CELL_PAIR, r1, c1, r2, c2};
                total_clues += 2;
            }
        }
    }

    // 3. Stars: place pairs of same-colour stars (or a star paired with a square
    // of the same colour when squares are also enabled). Using the same star
    // colour across areas forces the path to separate those areas so no area
    // ends up with 4 stars of that colour.
    bool gem_locked[PUZZLE_MAX_CELLS] = {false};
    int star_col_used[PUZZLE_MAX_CELLS];
    for (int i = 0; i < region_count; i++) {
        star_col_used[i] = -1;
    }
    if (use_stars) {
        for (int reg = 0; reg < region_count; reg++) {
            const int rem = reg_len[reg] - reg_used[reg];
            if (rem < 2) {
                continue;
            }
            const int r1 = reg_cells[reg][reg_used[reg]][0], c1 = reg_cells[reg][reg_used[reg]++][1];
            const int r2 = reg_cells[reg][reg_used[reg]][0], c2 = reg_cells[reg][reg_used[reg]++][1];
            if (use_gems && reg != neg_reg && rnd(ctx, 100) < 55) {
                const uint8_t col = region_color[reg];
                p->cells[r1][c1] = (puzzle_cell_t){CELL_STAR, col};
                p->cells[r2][c2] = (puzzle_cell_t){CELL_GEM, col};
                gem_locked[reg] = true;
            } else {
                const uint8_t col = use_gems ? (region_color[reg] + 1) % 2 : 0;
                p->cells[r1][c1] = (puzzle_cell_t){CELL_STAR, col};
                p->cells[r2][c2] = (puzzle_cell_t){CELL_STAR, col};
                star_col_used[reg] = col;
            }
            clues[n++] = (clue_t){CLUE_CELL_PAIR, r1, c1, r2, c2};
            total_clues += 2;

            if (!use_gems && reg != neg_reg && reg_len[reg] - reg_used[reg] >= 2) {
                const int r3 = reg_cells[reg][reg_used[reg]][0], c3 = reg_cells[reg][reg_used[reg]++][1];
                const int r4 = reg_cells[reg][reg_used[reg]][0], c4 = reg_cells[reg][reg_used[reg]++][1];
                const uint8_t col2 = 1;
                p->cells[r3][c3] = (puzzle_cell_t){CELL_STAR, col2};
                p->cells[r4][c4] = (puzzle_cell_t){CELL_STAR, col2};
                clues[n++] = (clue_t){CLUE_CELL_PAIR, r3, c3, r4, c4};
                total_clues += 2;
            }
        }
    }

    // 4. Squares and triangles in the remaining cells.
    for (int reg = 0; reg < region_count; reg++) {
        while (reg_used[reg] < reg_len[reg]) {
            const int r = reg_cells[reg][reg_used[reg]][0];
            const int c = reg_cells[reg][reg_used[reg]++][1];
            if (reg == neg_reg && !use_counts && !use_gems) {
                continue;
            }
            const bool can_gem = use_gems && !gem_locked[reg] && region_color[reg] != star_col_used[reg];
            const bool pick_gem = can_gem && (!use_counts || rnd(ctx, 100) < 50);
            if (pick_gem) {
                p->cells[r][c] = (puzzle_cell_t){CELL_GEM, region_color[reg]};
            } else if (use_counts) {
                const int sides = puzzle_cell_sides(p, &path, r, c);
                if (sides > 0) {
                    p->cells[r][c] = (puzzle_cell_t){CELL_COUNT, sides};
                } else {
                    continue;
                }
            } else {
                continue;
            }
            clues[n++] = (clue_t){CLUE_CELL, r, c, 0, 0};
            total_clues++;
        }
    }

    // 5. Beacons go on the solution.
    if (use_beacons) {
        const int chance = (use_counts || use_gems || use_stars || use_polys) ? 12 : 45;
        for (int i = 1; i < path.length - 1; i++) {
            if (rnd(ctx, 100) < chance) {
                p->node_beacon[path.nodes[i]] = true;
                clues[n++] = (clue_t){CLUE_NODE, path.nodes[i], 0, 0, 0};
                total_clues++;
            }
        }
        for (int r = 0; r <= rows; r++) {
            for (int c = 0; c < cols; c++) {
                if (on_path_h[r][c] && rnd(ctx, 100) < chance) {
                    p->h_edges[r][c] = EDGE_BEACON;
                    clues[n++] = (clue_t){CLUE_H_EDGE, r, c, 0, 0};
                    total_clues++;
                }
            }
        }
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c <= cols; c++) {
                if (on_path_v[r][c] && rnd(ctx, 100) < chance) {
                    p->v_edges[r][c] = EDGE_BEACON;
                    clues[n++] = (clue_t){CLUE_V_EDGE, r, c, 0, 0};
                    total_clues++;
                }
            }
        }
    }

    *clue_count = total_clues;
    if (count_solutions(ctx, p, 2) != 1 && strict) {
        return false;
    }

    // Take clues away in random order for as long as the solution stays unique.
    for (int i = n - 1; i > 0; i--) {
        const int j = rnd(ctx, i + 1);
        const clue_t t = clues[i];
        clues[i] = clues[j];
        clues[j] = t;
    }
    for (int i = 0; i < n; i++) {
        const clue_t *clue = &clues[i];
        const puzzle_t saved = *p;
        int removed = 1;
        switch (clue->kind) {
        case CLUE_CELL:
            p->cells[clue->row][clue->col] = (puzzle_cell_t){CELL_EMPTY, 0};
            break;
        case CLUE_CELL_PAIR:
            p->cells[clue->row][clue->col] = (puzzle_cell_t){CELL_EMPTY, 0};
            p->cells[clue->row2][clue->col2] = (puzzle_cell_t){CELL_EMPTY, 0};
            removed = 2;
            break;
        case CLUE_NODE:
            p->node_beacon[clue->row] = false;
            break;
        case CLUE_H_EDGE:
        case CLUE_H_GAP:
            p->h_edges[clue->row][clue->col] = EDGE_OPEN;
            break;
        case CLUE_V_EDGE:
        case CLUE_V_GAP:
            p->v_edges[clue->row][clue->col] = EDGE_OPEN;
            break;
        }
        if (count_solutions(ctx, p, 2) == 1) {
            *clue_count -= removed;
        } else {
            *p = saved;
        }
    }

    if (strict) {
        int counts = 0, gems = 0, gem_colors = 0, stars = 0, polys = 0, negators = 0;
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                switch (p->cells[r][c].kind) {
                case CELL_COUNT:
                    counts++;
                    break;
                case CELL_GEM:
                    gems++;
                    gem_colors |= 1 << p->cells[r][c].value;
                    break;
                case CELL_STAR:
                    stars++;
                    break;
                case CELL_POLY:
                    polys++;
                    break;
                case CELL_NEGATOR:
                    negators++;
                    break;
                default:
                    break;
                }
            }
        }
        if (use_counts && counts == 0) return false;
        if (use_gems && (gems == 0 || (!use_stars && !(gem_colors & (gem_colors - 1))))) return false;
        if (use_stars && stars == 0) return false;
        if (use_polys && polys == 0) return false;
        if (use_negators && negators == 0) return false;
    }
    return true;
}

void puzzle_generate(puzzle_t *p, const puzzle_spec_t *spec, uint32_t *seed, long max_steps,
                     puzzle_gen_stats_t *stats)
{
    gen_t ctx = {.rng = *seed ? *seed : 1, .max_steps = max_steps};
    const bool any_rules = spec->rules && spec->rules[0];
    int attempts = 0, clues = 0;
    bool unique = false;

    if (!any_rules) {
        puzzle_path_t path;
        random_layout(&ctx, p, spec->cols, spec->rows, &path);
        attempts = 1;
    } else {
        while (attempts < MAX_ATTEMPTS && !unique && !out_of_steps(&ctx)) {
            attempts++;
            unique = generate_once(&ctx, p, spec->cols, spec->rows, spec->rules, true, &clues);
        }
        if (!unique) {
            // Settle for a solvable puzzle, removing what clues there is time for.
            generate_once(&ctx, p, spec->cols, spec->rows, spec->rules, false, &clues);
            attempts++;
            unique = count_solutions(&ctx, p, 2) == 1;
        }
    }

    *seed = ctx.rng;
    if (stats) {
        *stats = (puzzle_gen_stats_t){attempts, ctx.steps, clues, unique};
    }
}
