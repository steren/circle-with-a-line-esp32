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
    bool has_counts, has_gems, has_beacons;
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
        if (p->cells[r][c].kind == CELL_COUNT && s->sides[r][c] > p->cells[r][c].value) {
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

    if (!s->has_counts && !s->has_gems) {
        return true;
    }

    // For every cell: how many of its sides the rest of the line could still add.
    uint8_t open_sides[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS];
    for (int r = 0; r < p->rows; r++) {
        for (int c = 0; c < p->cols; c++) {
            const int tl = puzzle_node(p, r, c), tr = tl + 1, bl = puzzle_node(p, r + 1, c), br = bl + 1;
            open_sides[r][c] =
                (!s->used_h[r][c] && could_use(p, live, tl, tr, p->h_edges[r][c])) +
                (!s->used_h[r + 1][c] && could_use(p, live, bl, br, p->h_edges[r + 1][c])) +
                (!s->used_v[r][c] && could_use(p, live, tl, bl, p->v_edges[r][c])) +
                (!s->used_v[r][c + 1] && could_use(p, live, tr, br, p->v_edges[r][c + 1]));
            const puzzle_cell_t *cell = &p->cells[r][c];
            if (cell->kind == CELL_COUNT && s->sides[r][c] + open_sides[r][c] < cell->value) {
                return false;
            }
        }
    }

    if (!s->has_gems) {
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
            int depth = 0, colors = 0;
            bool open = false;
            region[r0][c0] = count;
            stack[depth][0] = r0;
            stack[depth++][1] = c0;
            while (depth > 0) {
                const int r = stack[--depth][0], c = stack[depth][1];
                if (open_sides[r][c]) {
                    open = true;
                }
                if (p->cells[r][c].kind == CELL_GEM) {
                    colors |= 1 << p->cells[r][c].value;
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
            if (!open && (colors & (colors - 1))) {
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
            s.has_counts |= p->cells[r][c].kind == CELL_COUNT;
            s.has_gems |= p->cells[r][c].kind == CELL_GEM;
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

typedef enum { CLUE_CELL, CLUE_NODE, CLUE_H_EDGE, CLUE_V_EDGE } clue_kind_t;

typedef struct {
    clue_kind_t kind;
    int row, col;  // or node index in `row` for CLUE_NODE
} clue_t;

static int distance(const puzzle_t *p, int a, int b)
{
    return abs(puzzle_node_row(p, a) - puzzle_node_row(p, b)) + abs(puzzle_node_col(p, a) - puzzle_node_col(p, b));
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
    const bool use_beacons = strchr(rules, 'b'), use_gaps = strchr(rules, 'x');

    // The intended solution.
    puzzle_path_t path;
    random_layout(ctx, p, cols, rows, &path);

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
    int n = 0;

    // Gaps go on lines the solution does not use.
    if (use_gaps) {
        for (int r = 0; r <= rows; r++) {
            for (int c = 0; c < cols; c++) {
                if (!on_path_h[r][c] && rnd(ctx, 100) < 22) {
                    p->h_edges[r][c] = EDGE_GAP;
                }
            }
        }
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c <= cols; c++) {
                if (!on_path_v[r][c] && rnd(ctx, 100) < 22) {
                    p->v_edges[r][c] = EDGE_GAP;
                }
            }
        }
    }

    // Start with a clue in every cell, taken from the solution.
    uint8_t region[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS];
    const int region_count = puzzle_regions(p, &path, region);
    uint8_t region_color[PUZZLE_MAX_CELLS];
    for (int i = 0; i < region_count; i++) {
        region_color[i] = rnd(ctx, region_count > 3 ? PUZZLE_GEM_COLORS : 2);
    }
    if (use_gems && region_count < 2 && strict) {
        return false;
    }
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            const bool gem = use_gems && (!use_counts || rnd(ctx, 100) < 50);
            if (gem) {
                p->cells[r][c] = (puzzle_cell_t){CELL_GEM, region_color[region[r][c]]};
            } else if (use_counts) {
                p->cells[r][c] = (puzzle_cell_t){CELL_COUNT, puzzle_cell_sides(p, &path, r, c)};
            } else {
                continue;
            }
            clues[n++] = (clue_t){CLUE_CELL, r, c};
        }
    }

    // Beacons go on the solution.
    if (use_beacons) {
        const int chance = (use_counts || use_gems) ? 12 : 45;
        for (int i = 1; i < path.length - 1; i++) {
            if (rnd(ctx, 100) < chance) {
                p->node_beacon[path.nodes[i]] = true;
                clues[n++] = (clue_t){CLUE_NODE, path.nodes[i], 0};
            }
        }
        for (int r = 0; r <= rows; r++) {
            for (int c = 0; c < cols; c++) {
                if (on_path_h[r][c] && rnd(ctx, 100) < chance) {
                    p->h_edges[r][c] = EDGE_BEACON;
                    clues[n++] = (clue_t){CLUE_H_EDGE, r, c};
                }
            }
        }
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c <= cols; c++) {
                if (on_path_v[r][c] && rnd(ctx, 100) < chance) {
                    p->v_edges[r][c] = EDGE_BEACON;
                    clues[n++] = (clue_t){CLUE_V_EDGE, r, c};
                }
            }
        }
    }

    *clue_count = n;
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
        switch (clue->kind) {
        case CLUE_CELL:
            p->cells[clue->row][clue->col] = (puzzle_cell_t){CELL_EMPTY, 0};
            break;
        case CLUE_NODE:
            p->node_beacon[clue->row] = false;
            break;
        case CLUE_H_EDGE:
            p->h_edges[clue->row][clue->col] = EDGE_OPEN;
            break;
        case CLUE_V_EDGE:
            p->v_edges[clue->row][clue->col] = EDGE_OPEN;
            break;
        }
        if (count_solutions(ctx, p, 2) == 1) {
            (*clue_count)--;
        } else {
            *p = saved;
        }
    }

    // A gem puzzle needs at least two colours left to mean anything.
    if (use_gems) {
        int colors = 0;
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                if (p->cells[r][c].kind == CELL_GEM) {
                    colors |= 1 << p->cells[r][c].value;
                }
            }
        }
        if (!(colors & (colors - 1)) && strict) {
            return false;
        }
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
