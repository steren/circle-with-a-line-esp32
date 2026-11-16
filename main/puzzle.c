#include "puzzle.h"

#include <string.h>

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

    // Counts
    for (int r = 0; r < p->rows; r++) {
        for (int c = 0; c < p->cols; c++) {
            const puzzle_cell_t *cell = &p->cells[r][c];
            if (cell->kind == CELL_COUNT && cell_sides(&used, r, c) != cell->value) {
                result->bad_cell[r][c] = true;
                ok = false;
            }
        }
    }

    // Gems: an area holding more than one colour is wrong.
    uint8_t region[PUZZLE_MAX_ROWS][PUZZLE_MAX_COLS];
    const int region_count = find_regions(p, &used, region);
    uint8_t colors[PUZZLE_MAX_CELLS] = {0};  // bit mask of gem colours per area
    for (int r = 0; r < p->rows; r++) {
        for (int c = 0; c < p->cols; c++) {
            if (p->cells[r][c].kind == CELL_GEM) {
                colors[region[r][c]] |= 1 << p->cells[r][c].value;
            }
        }
    }
    for (int i = 0; i < region_count; i++) {
        if (colors[i] & (colors[i] - 1)) {  // more than one bit set
            ok = false;
            for (int r = 0; r < p->rows; r++) {
                for (int c = 0; c < p->cols; c++) {
                    if (region[r][c] == i && p->cells[r][c].kind == CELL_GEM) {
                        result->bad_cell[r][c] = true;
                    }
                }
            }
        }
    }

    result->solved = ok;
    return ok;
}
