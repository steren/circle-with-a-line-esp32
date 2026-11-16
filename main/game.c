#include "game.h"

#include <stdlib.h>

#include "generator.h"
#include "gfx.h"
#include "levels.h"
#include "puzzle.h"

// ---- Layout: the board fills the screen, with nothing else on it ----
#define BOARD_CENTER_X (GAME_SCREEN_WIDTH / 2)
#define BOARD_CENTER_Y (GAME_SCREEN_HEIGHT / 2)
#define BOARD_MAX_SIZE 296  // leaves room at the sides for exit caps
#define BOARD_MARGIN 40  // room around the lattice for start circles and exit caps
#define MAX_CELL_SIZE 104
#define TILE_CORNER 5

// ---- Timing ----
#define RELEASE_POLLS 3  // updates without a touch before the finger counts as lifted
#define SOLVED_PAUSE_MS 1500
#define FAILED_PAUSE_MS 1000

// ---- Colours ----
// Each puzzle glows in one flat, saturated colour, like a sheet of lit acrylic
// or an electroluminescent panel: a field, slightly lighter tiles, channels a
// deep shade of the same hue, and a pale line with a soft halo.
typedef struct {
    uint8_t field[3], tile[3], track[3], path[3];
} theme_rgb_t;

static const theme_rgb_t THEMES[] = {
    {{18, 118, 122}, {30, 148, 148}, {8, 56, 62}, {228, 255, 248}},     // teal
    {{36, 72, 176}, {54, 98, 206}, {16, 30, 90}, {228, 238, 255}},      // cobalt
    {{104, 50, 164}, {130, 74, 194}, {48, 20, 86}, {244, 230, 255}},    // violet
    {{200, 112, 24}, {226, 142, 42}, {100, 50, 8}, {255, 247, 224}},    // amber
    {{58, 138, 62}, {82, 168, 80}, {24, 66, 28}, {238, 255, 228}},      // moss
    {{176, 40, 92}, {204, 62, 116}, {84, 14, 42}, {255, 230, 240}},     // raspberry
};
#define THEME_COUNT ((int)(sizeof(THEMES) / sizeof(THEMES[0])))

static struct {
    uint16_t field, tile, track, path, glow, count, exit, beacon;
} T;

#define COLOR_BACKGROUND T.field
#define COLOR_TRACK T.track                 // the channels the line runs in
#define COLOR_TILE T.tile                   // the cells between them
#define COLOR_PATH T.path
#define COLOR_SOLVED rgb565(150, 255, 170)
#define COLOR_FAILED rgb565(255, 86, 72)
#define COLOR_BEACON T.beacon
#define COLOR_COUNT T.count

// Soft darkening towards the edges of the screen (255 = full brightness).
#define VIGNETTE_EDGE 0.52f   // brightness right at an edge; corners get this squared
#define VIGNETTE_WIDTH 96     // pixels over which it fades in
static uint8_t s_shade_col[GAME_SCREEN_WIDTH], s_shade_row[GAME_SCREEN_HEIGHT];

static uint16_t mix(const uint8_t a[3], const uint8_t b[3], int a_percent)
{
    const int k = a_percent, j = 100 - a_percent;
    return rgb565((a[0] * k + b[0] * j) / 100, (a[1] * k + b[1] * j) / 100, (a[2] * k + b[2] * j) / 100);
}

static void set_theme(int level)
{
    const theme_rgb_t *t = &THEMES[level % THEME_COUNT];
    T.field = rgb565(t->field[0], t->field[1], t->field[2]);
    T.tile = rgb565(t->tile[0], t->tile[1], t->tile[2]);
    T.track = rgb565(t->track[0], t->track[1], t->track[2]);
    T.path = rgb565(t->path[0], t->path[1], t->path[2]);
    T.glow = mix(t->path, t->track, 38);     // halo: the line's light spread into the channel
    T.count = mix(t->path, t->tile, 80);     // markings: pale, a little tinted
    T.beacon = mix(t->path, t->field, 62);
}

static void build_vignette(uint8_t *shade, int length)
{
    for (int i = 0; i < length; i++) {
        const int from_edge = i < length - 1 - i ? i : length - 1 - i;
        float t = from_edge >= VIGNETTE_WIDTH ? 1.0f : (float)from_edge / VIGNETTE_WIDTH;
        t = t * t * (3 - 2 * t);  // smoothstep
        shade[i] = (uint8_t)(255.0f * (VIGNETTE_EDGE + (1.0f - VIGNETTE_EDGE) * t) + 0.5f);
    }
}

static const uint16_t GEM_COLORS[PUZZLE_GEM_COLORS] = {
    RGB565(255, 92, 180),   // pink
    RGB565(92, 214, 255),   // sky blue
    RGB565(214, 240, 70),   // lime
};

typedef enum { STATE_PLAYING, STATE_SOLVED, STATE_FAILED } state_t;

static struct {
    uint32_t seed;  // random state for generating puzzles
    int level;
    puzzle_t puzzle;
    puzzle_path_t path;
    puzzle_result_t result;  // valid in STATE_FAILED
    state_t state;
    uint32_t state_since_ms;
    uint32_t now_ms;

    bool finger_down;  // debounced touch state
    int released_polls;
    bool tracing;      // the finger is dragging the tip of the line

    // The line ends at a tip that slides along the channels: tip_offset pixels
    // out from the last node of the path, in direction tip_dir.
    puzzle_dir_t tip_dir;
    int tip_offset;

    int cell_size, origin_x, origin_y;  // origin = screen position of node (0, 0)

    // The board without the line, so moving the line only redraws what it
    // covers. NULL if there was no memory for it (then everything is redrawn).
    uint16_t *layer;
    int line_x1, line_y1, line_x2, line_y2;  // area the line was last drawn in
    int track_width, path_width;        // both odd, so they centre exactly on a node
    game_sound_handler_t sound_handler;
    game_puzzle_source_t puzzle_source;
} g;

static void play(game_sound_t sound)
{
    if (g.sound_handler) {
        g.sound_handler(sound);
    }
}

// ---- Geometry ----

static int node_x(int node)
{
    return g.origin_x + puzzle_node_col(&g.puzzle, node) * g.cell_size;
}

static int node_y(int node)
{
    return g.origin_y + puzzle_node_row(&g.puzzle, node) * g.cell_size;
}

void game_node_position(int node, int *x, int *y)
{
    *x = node_x(node);
    *y = node_y(node);
}

// Unit steps for DIR_UP, DIR_RIGHT, DIR_DOWN, DIR_LEFT.
static const int DIR_DX[4] = {0, 1, 0, -1};
static const int DIR_DY[4] = {-1, 0, 1, 0};

// Start nodes are enlarged solid circles; exits have a short rounded cap
// sticking out past the border.
static int start_radius(void)
{
    return g.track_width * 6 / 5;
}

static int cap_length(void)
{
    return g.track_width * 3 / 2;
}

// Where the line ends inside an exit's cap.
static void cap_end(int exit, int *x, int *y)
{
    const puzzle_dir_t dir = puzzle_exit_dir(&g.puzzle, exit);
    *x = node_x(exit) + DIR_DX[dir] * cap_length();
    *y = node_y(exit) + DIR_DY[dir] * cap_length();
}

// True once an answer has been submitted: the line then runs into the cap.
static bool line_in_cap(void)
{
    return g.state != STATE_PLAYING && g.puzzle.is_exit[puzzle_path_head(&g.path)];
}

static bool is_near(int x, int y, int node)
{
    const int reach = g.cell_size * 6 / 10 > 30 ? g.cell_size * 6 / 10 : 30;
    return abs(x - node_x(node)) <= reach && abs(y - node_y(node)) <= reach;
}

// How many pixels the tip may travel from node `from` in direction `dir`:
// a whole cell if the next node can be reached, less if something is in the way.
static int travel_limit(int from, puzzle_dir_t dir)
{
    const puzzle_t *p = &g.puzzle;
    const int row = puzzle_node_row(p, from) + DIR_DY[dir], col = puzzle_node_col(p, from) + DIR_DX[dir];
    if (row < 0 || col < 0 || row > p->rows || col > p->cols) {
        return 0;
    }
    const int next = puzzle_neighbor(p, from, dir);
    if (next < 0) {
        // A gap: stop at the plug.
        const int room = g.cell_size * 3 / 10 - g.track_width / 2 - g.path_width / 2 - 3;
        return room > 0 ? room : 0;
    }
    for (int i = 0; i < g.path.length; i++) {
        if (g.path.nodes[i] == next) {
            return g.cell_size - g.track_width - 2;  // stop short of the line itself
        }
    }
    return g.cell_size;
}

// ---- Drawing ----

static void draw_centered_text(const char *text, int center_x, int center_y, int scale, uint16_t color)
{
    // The font cell has one blank row at the bottom; ignore it when centring.
    const int height = gfx_text_height(scale) - scale;
    gfx_draw_text(center_x - gfx_text_width(text, scale) / 2, center_y - height / 2, text, scale, color);
}

// A horizontal or vertical line between two points, with square ends.
static void draw_line(int x1, int y1, int x2, int y2, int thickness, uint16_t color)
{
    const int left = x1 < x2 ? x1 : x2, top = y1 < y2 ? y1 : y2;
    const int half = thickness / 2;
    gfx_fill_rect(left - half, top - half, abs(x2 - x1) + thickness, abs(y2 - y1) + thickness, color);
}

// A straight piece of line from a node to a point, ending in a round tip
// instead of a square one.
static void draw_rounded_stub(int from_x, int from_y, int to_x, int to_y, int width, uint16_t color)
{
    const int half = width / 2;
    const int dx = to_x > from_x ? 1 : to_x < from_x ? -1 : 0, dy = to_y > from_y ? 1 : to_y < from_y ? -1 : 0;
    draw_line(from_x, from_y, to_x - dx * half, to_y - dy * half, width, color);
    gfx_fill_circle(to_x, to_y, half, color);
}


static void draw_diamond(int cx, int cy, int half, uint16_t color)
{
    for (int dy = -half; dy <= half; dy++) {
        const int w = half - abs(dy);
        gfx_fill_rect(cx - w, cy + dy, 2 * w + 1, 1, color);
    }
}

static int beacon_radius(void)
{
    return g.path_width / 2;  // small enough for the line to cover a beacon it passes
}

static void draw_grid_edge(int x1, int y1, int x2, int y2, puzzle_edge_t kind)
{
    draw_line(x1, y1, x2, y2, g.track_width, COLOR_TRACK);
    if (kind == EDGE_GAP) {
        // Plug the middle of the channel with tile.
        const int plug_x = (x2 - x1) * 3 / 10, plug_y = (y2 - y1) * 3 / 10;
        draw_line(x1 + plug_x, y1 + plug_y, x2 - plug_x, y2 - plug_y, g.track_width, COLOR_TILE);
    } else if (kind == EDGE_BEACON) {
        gfx_fill_circle((x1 + x2) / 2, (y1 + y2) / 2, beacon_radius(), COLOR_BEACON);
    }
}

static void draw_missed_beacon(int x, int y)
{
    gfx_fill_circle(x, y, beacon_radius() + 3, COLOR_FAILED);
    gfx_fill_circle(x, y, beacon_radius(), COLOR_BEACON);
}

// The start marker and the line, over the board.
static void draw_line_layer(void)
{
    const bool failed = (g.state == STATE_FAILED);
    const uint16_t path_color =
        g.state == STATE_SOLVED ? COLOR_SOLVED : failed ? COLOR_FAILED : COLOR_PATH;

    const bool started = g.tracing || g.path.length > 1;
    const int start = g.path.nodes[0];

    // The path: a soft halo first, then the line itself on top.
    if (started) {
        const int head = puzzle_path_head(&g.path);
        const int tip_x = node_x(head) + DIR_DX[g.tip_dir] * g.tip_offset;
        const int tip_y = node_y(head) + DIR_DY[g.tip_dir] * g.tip_offset;
        const uint16_t halo = g.state == STATE_PLAYING ? T.glow : COLOR_TRACK;
        for (int pass = 0; pass < 2; pass++) {
            const int width = pass == 0 ? g.path_width + 6 : g.path_width;
            const uint16_t color = pass == 0 ? halo : path_color;
            for (int i = 0; i < g.path.length; i++) {
                const int node = g.path.nodes[i];
                if (i > 0) {
                    const int prev = g.path.nodes[i - 1];
                    draw_line(node_x(prev), node_y(prev), node_x(node), node_y(node), width, color);
                }
                gfx_fill_circle(node_x(node), node_y(node), width / 2, color);
            }
            if (line_in_cap()) {
                int end_x, end_y;
                cap_end(head, &end_x, &end_y);
                draw_rounded_stub(node_x(head), node_y(head), end_x, end_y, width, color);
            } else {
                draw_line(node_x(head), node_y(head), tip_x, tip_y, width, color);
                gfx_fill_circle(tip_x, tip_y, width / 2 + 3, color);
            }
            // The start circle fills with light, leaving a rim of the channel.
            gfx_fill_circle(node_x(start), node_y(start), start_radius() - (pass == 0 ? 1 : 4), color);
        }
    }

    // Remember the area drawn in, to clear it next time.
    const int start_pad = start_radius() + 2, line_pad = g.path_width / 2 + 10;
    const int pad = start_pad > line_pad ? start_pad : line_pad;
    int x1 = node_x(start), x2 = x1, y1 = node_y(start), y2 = y1;
    for (int i = 0; i < g.path.length; i++) {
        const int x = node_x(g.path.nodes[i]), y = node_y(g.path.nodes[i]);
        if (x < x1) x1 = x;
        if (x > x2) x2 = x;
        if (y < y1) y1 = y;
        if (y > y2) y2 = y;
    }
    const int head = puzzle_path_head(&g.path);
    const int tip_x = node_x(head) + DIR_DX[g.tip_dir] * g.tip_offset;
    const int tip_y = node_y(head) + DIR_DY[g.tip_dir] * g.tip_offset;
    if (tip_x < x1) x1 = tip_x;
    if (tip_x > x2) x2 = tip_x;
    if (tip_y < y1) y1 = tip_y;
    if (tip_y > y2) y2 = tip_y;
    if (line_in_cap()) {
        int end_x, end_y;
        cap_end(head, &end_x, &end_y);
        if (end_x < x1) x1 = end_x;
        if (end_x > x2) x2 = end_x;
        if (end_y < y1) y1 = end_y;
        if (end_y > y2) y2 = end_y;
    }
    g.line_x1 = x1 - pad;
    g.line_y1 = y1 - pad;
    g.line_x2 = x2 + pad;
    g.line_y2 = y2 + pad;
}

static void draw_board(void)
{
    const puzzle_t *p = &g.puzzle;
    const bool failed = (g.state == STATE_FAILED);

    gfx_fill_rect(0, BOARD_CENTER_Y - BOARD_MAX_SIZE / 2 - BOARD_MARGIN, GAME_SCREEN_WIDTH,
                  BOARD_MAX_SIZE + 2 * BOARD_MARGIN, COLOR_BACKGROUND);

    // Tiles, then the channels between them
    const int tile_inset = g.track_width / 2 + 1, tile_size = g.cell_size - g.track_width;
    for (int r = 0; r < p->rows; r++) {
        for (int c = 0; c < p->cols; c++) {
            gfx_fill_round_rect(g.origin_x + c * g.cell_size + tile_inset, g.origin_y + r * g.cell_size + tile_inset,
                                tile_size, tile_size, TILE_CORNER, COLOR_TILE);
        }
    }
    for (int r = 0; r <= p->rows; r++) {
        for (int c = 0; c < p->cols; c++) {
            const int a = puzzle_node(p, r, c), b = puzzle_node(p, r, c + 1);
            draw_grid_edge(node_x(a), node_y(a), node_x(b), node_y(b), p->h_edges[r][c]);
        }
    }
    for (int r = 0; r < p->rows; r++) {
        for (int c = 0; c <= p->cols; c++) {
            const int a = puzzle_node(p, r, c), b = puzzle_node(p, r + 1, c);
            draw_grid_edge(node_x(a), node_y(a), node_x(b), node_y(b), p->v_edges[r][c]);
        }
    }
    for (int node = 0; node < puzzle_node_count(p); node++) {
        if (p->node_beacon[node]) {
            gfx_fill_circle(node_x(node), node_y(node), beacon_radius(), COLOR_BEACON);
        }
    }

    // Clues on the tiles
    for (int r = 0; r < p->rows; r++) {
        for (int c = 0; c < p->cols; c++) {
            const puzzle_cell_t *cell = &p->cells[r][c];
            const int cx = g.origin_x + c * g.cell_size + g.cell_size / 2;
            const int cy = g.origin_y + r * g.cell_size + g.cell_size / 2;
            const bool bad = failed && g.result.bad_cell[r][c];
            if (cell->kind == CELL_COUNT) {
                const char digit[] = {'0' + cell->value, '\0'};
                draw_centered_text(digit, cx, cy, tile_size / 15, bad ? COLOR_FAILED : COLOR_COUNT);
            } else if (cell->kind == CELL_GEM) {
                const int half = tile_size / 4;
                if (bad) {
                    draw_diamond(cx, cy, half + 5, COLOR_FAILED);
                }
                draw_diamond(cx, cy, half + 3, COLOR_TRACK);  // a dark rim keeps every colour readable
                draw_diamond(cx, cy, half, GEM_COLORS[cell->value]);
            }
        }
    }

    // Exits: a short rounded cap past the border. Starts: an enlarged solid circle.
    for (int node = 0; node < puzzle_node_count(p); node++) {
        if (p->is_exit[node]) {
            int end_x, end_y;
            cap_end(node, &end_x, &end_y);
            draw_rounded_stub(node_x(node), node_y(node), end_x, end_y, g.track_width, COLOR_TRACK);
        }
        if (p->is_start[node]) {
            gfx_fill_circle(node_x(node), node_y(node), start_radius(), COLOR_TRACK);
        }
    }

    if (g.layer) {
        gfx_save(g.layer);
    }
    draw_line_layer();

    // Beacons the path missed
    if (failed) {
        for (int node = 0; node < puzzle_node_count(p); node++) {
            if (g.result.missed_node[node]) {
                draw_missed_beacon(node_x(node), node_y(node));
            }
        }
        for (int r = 0; r <= p->rows; r++) {
            for (int c = 0; c < p->cols; c++) {
                if (g.result.missed_h[r][c]) {
                    const int a = puzzle_node(p, r, c);
                    draw_missed_beacon(node_x(a) + g.cell_size / 2, node_y(a));
                }
            }
        }
        for (int r = 0; r < p->rows; r++) {
            for (int c = 0; c <= p->cols; c++) {
                if (g.result.missed_v[r][c]) {
                    const int a = puzzle_node(p, r, c);
                    draw_missed_beacon(node_x(a), node_y(a) + g.cell_size / 2);
                }
            }
        }
    }
}

// Redraws just the line, after it moved while being drawn.
static void redraw_line(void)
{
    if (!g.layer) {
        draw_board();
        return;
    }
    gfx_restore(g.layer, g.line_x1, g.line_y1, g.line_x2 - g.line_x1 + 1, g.line_y2 - g.line_y1 + 1);
    draw_line_layer();
}

// ---- Game logic ----

static void set_state(state_t state)
{
    g.state = state;
    g.state_since_ms = g.now_ms;
    draw_board();
}

static void clear_line(void)
{
    puzzle_path_start(&g.path, puzzle_first_start(&g.puzzle));
    g.tip_offset = 0;
    g.tracing = false;
    set_state(STATE_PLAYING);
}

void game_load_level(int index)
{
    if (index < 0 || index >= level_count) {
        return;
    }
    g.level = index;
    if (g.puzzle_source) {
        g.puzzle_source(index, &g.puzzle);
    } else {
        puzzle_generate(&g.puzzle, &levels[index], &g.seed, 0, NULL);
    }

    const int by_width = BOARD_MAX_SIZE / g.puzzle.cols, by_height = BOARD_MAX_SIZE / g.puzzle.rows;
    g.cell_size = by_width < by_height ? by_width : by_height;
    if (g.cell_size > MAX_CELL_SIZE) {
        g.cell_size = MAX_CELL_SIZE;
    }
    g.origin_x = BOARD_CENTER_X - g.puzzle.cols * g.cell_size / 2;
    g.origin_y = BOARD_CENTER_Y - g.puzzle.rows * g.cell_size / 2;
    g.track_width = (g.cell_size / 5) | 1;
    g.path_width = g.track_width - 4;

    set_theme(index);
    gfx_fill_rect(0, 0, GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT, COLOR_BACKGROUND);
    clear_line();
}

void game_init(uint32_t seed)
{
    g.seed = seed ? seed : 1;
    build_vignette(s_shade_col, GAME_SCREEN_WIDTH);
    build_vignette(s_shade_row, GAME_SCREEN_HEIGHT);
    gfx_set_shading(s_shade_col, s_shade_row);
    if (!g.layer) {
        g.layer = malloc(sizeof(uint16_t) * GAME_SCREEN_WIDTH * GAME_SCREEN_HEIGHT);
    }
    g.finger_down = false;
    g.released_polls = RELEASE_POLLS;
    game_load_level(0);
}

void game_set_sound_handler(game_sound_handler_t handler)
{
    g.sound_handler = handler;
}

void game_set_puzzle_source(game_puzzle_source_t source)
{
    g.puzzle_source = source;
}

int game_level(void)
{
    return g.level;
}

bool game_is_solved(void)
{
    return g.state == STATE_SOLVED;
}

const puzzle_t *game_puzzle(void)
{
    return &g.puzzle;
}

void game_next_level(void)
{
    if (g.level < level_count - 1) {
        game_load_level(g.level + 1);
        play(GAME_SOUND_LEVEL);
    }
}

void game_previous_level(void)
{
    if (g.level > 0) {
        game_load_level(g.level - 1);
        play(GAME_SOUND_LEVEL);
    }
}

void game_reset_line(void)
{
    clear_line();
    play(GAME_SOUND_RESET);
}

// Slides the tip of the line along the channels towards the finger, adding
// nodes to the path as it passes them and removing them as it backs up.
static void follow_finger(int x, int y)
{
    enum { DEAD_ZONE = 3 };  // pixels the finger must move before the tip leaves a node
    const puzzle_t *p = &g.puzzle;
    const int cell = g.cell_size;

    for (int i = 0; i < 4 * PUZZLE_MAX_NODES; i++) {
        const int head = puzzle_path_head(&g.path);
        const int dx = x - node_x(head), dy = y - node_y(head);

        if (g.tip_offset > 0) {
            // In a channel: the tip is the finger's position measured along it.
            const puzzle_dir_t dir = g.tip_dir;
            const int along = dx * DIR_DX[dir] + dy * DIR_DY[dir];
            const int limit = travel_limit(head, dir);
            int target = along;

            // A finger that has left the channel sideways is cutting a corner.
            // Jump to the nearer end, if the line can turn that way from there.
            const int across = dir == DIR_UP || dir == DIR_DOWN ? dx : dy;
            if (abs(across) > cell / 2) {
                const puzzle_dir_t turn =
                    dir == DIR_UP || dir == DIR_DOWN ? (dx > 0 ? DIR_RIGHT : DIR_LEFT) : (dy > 0 ? DIR_DOWN : DIR_UP);
                if (2 * along >= cell) {
                    if (limit == cell && travel_limit(puzzle_neighbor(p, head, dir), turn) > 0) {
                        target = cell;
                    }
                } else if (travel_limit(head, turn) > 0) {
                    target = 0;
                }
            }

            if (target >= cell && limit == cell) {
                puzzle_path_step(p, &g.path, dir);  // reached the next node
                g.tip_offset = 0;
                continue;
            }
            if (target <= 0) {
                g.tip_offset = 0;  // back at the node; it may now go another way
                continue;
            }
            g.tip_offset = target < limit ? target : limit - (limit == cell);
            return;
        }

        // On a node: head off along whichever axis the finger is further away on.
        const puzzle_dir_t horizontal = dx > 0 ? DIR_RIGHT : DIR_LEFT, vertical = dy > 0 ? DIR_DOWN : DIR_UP;
        const bool prefer_horizontal = abs(dx) >= abs(dy);
        const puzzle_dir_t choices[2] = {prefer_horizontal ? horizontal : vertical,
                                         prefer_horizontal ? vertical : horizontal};
        const int distances[2] = {prefer_horizontal ? abs(dx) : abs(dy), prefer_horizontal ? abs(dy) : abs(dx)};
        bool moving = false;
        for (int k = 0; k < 2 && !moving && distances[k] > DEAD_ZONE; k++) {
            const puzzle_dir_t dir = choices[k];
            const int next = puzzle_neighbor(p, head, dir);
            if (g.path.length >= 2 && next == g.path.nodes[g.path.length - 2]) {
                // Backing up along the line: the tip is now in the channel it came from.
                puzzle_path_step(p, &g.path, dir);
                g.tip_dir = (puzzle_dir_t)((dir + 2) % 4);
                g.tip_offset = cell - 1;
                moving = true;
            } else if (travel_limit(head, dir) > 0) {
                g.tip_dir = dir;
                g.tip_offset = 1;
                moving = true;
            }
        }
        if (!moving) {
            return;
        }
        // The next pass places the tip exactly.
    }
}

static void on_touch_begin(int x, int y)
{
    if (g.state != STATE_PLAYING) {
        return;
    }
    // Touching the start circle begins a fresh path.
    const int start = puzzle_first_start(&g.puzzle);
    if (is_near(x, y, start)) {
        puzzle_path_start(&g.path, start);
        g.tip_offset = 0;
        g.tracing = true;
        draw_board();
        play(GAME_SOUND_TRACE);
    } else if (g.path.length > 1 && is_near(x, y, puzzle_path_head(&g.path))) {
        // Touching the head of an unfinished path picks it up again.
        g.tracing = true;
        play(GAME_SOUND_TRACE);
    }
}

static void on_touch_end(void)
{
    if (!g.tracing) {
        return;
    }
    g.tracing = false;

    // Settle the tip on a node: the next one if it is more than halfway there.
    if (2 * g.tip_offset >= g.cell_size && travel_limit(puzzle_path_head(&g.path), g.tip_dir) == g.cell_size) {
        puzzle_path_step(&g.puzzle, &g.path, g.tip_dir);
    }
    g.tip_offset = 0;

    if (!g.puzzle.is_exit[puzzle_path_head(&g.path)]) {
        draw_board();  // leave the unfinished path in place
        play(GAME_SOUND_LIFT);
        return;
    }
    const bool solved = puzzle_check(&g.puzzle, &g.path, &g.result);
    set_state(solved ? STATE_SOLVED : STATE_FAILED);
    play(solved ? GAME_SOUND_SOLVED : GAME_SOUND_FAILED);
}

void game_update(const game_touch_t *touch, uint32_t now_ms)
{
    g.now_ms = now_ms;

    if (touch->pressed) {
        g.released_polls = 0;
        if (!g.finger_down) {
            g.finger_down = true;
            on_touch_begin(touch->x, touch->y);
        }
        if (g.tracing) {
            const int old_length = g.path.length, old_offset = g.tip_offset;
            const puzzle_dir_t old_dir = g.tip_dir;
            follow_finger(touch->x, touch->y);
            if (g.path.length != old_length || g.tip_offset != old_offset ||
                (g.tip_offset > 0 && g.tip_dir != old_dir)) {
                redraw_line();
            }
        }
    } else if (g.finger_down && ++g.released_polls >= RELEASE_POLLS) {
        g.finger_down = false;
        on_touch_end();
    }

    const uint32_t elapsed = now_ms - g.state_since_ms;
    if (g.state == STATE_SOLVED && elapsed >= SOLVED_PAUSE_MS && g.level < level_count - 1) {
        game_load_level(g.level + 1);
    } else if (g.state == STATE_FAILED && elapsed >= FAILED_PAUSE_MS) {
        clear_line();
    }
}
