// Circle with a Line: the game itself. Draws with gfx.h and is driven by touch input
// and button presses, so it runs unchanged on the board and in the PC test tool.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "puzzle.h"

#define GAME_SCREEN_WIDTH 368
#define GAME_SCREEN_HEIGHT 448

typedef struct {
    bool pressed;
    int x, y;
} game_touch_t;

// Moments the game wants a sound for.
typedef enum {
    GAME_SOUND_TRACE,   // the finger started drawing: hum until the next sound
    GAME_SOUND_LIFT,    // the finger let go of an unfinished line
    GAME_SOUND_SOLVED,
    GAME_SOUND_FAILED,
    GAME_SOUND_LEVEL,   // another puzzle was selected
} game_sound_t;

typedef void (*game_sound_handler_t)(game_sound_t sound);

// gfx_init() must have been called with a GAME_SCREEN_WIDTH x GAME_SCREEN_HEIGHT framebuffer.
// `seed` (any value) decides the puzzles; every puzzle is generated afresh
// each time it is opened.
void game_init(uint32_t seed);

// Optional: called whenever the game wants a sound played.
void game_set_sound_handler(game_sound_handler_t handler);

// Optional: supplies a freshly generated puzzle for a level each time one is
// opened (for example, one prepared in the background). Without it, the game
// generates the puzzle itself on the spot. Set it before game_init().
typedef void (*game_puzzle_source_t)(int level, puzzle_t *out);
void game_set_puzzle_source(game_puzzle_source_t source);

// Call regularly (every 10 ms or so) with the current touch state and time.
void game_update(const game_touch_t *touch, uint32_t now_ms);

// Button actions.
void game_next_level(void);
void game_previous_level(void);

void game_load_level(int index);
int game_level(void);
uint32_t game_puzzle_id(void);
int game_screen_fade(void);
bool game_is_solved(void);

// The puzzle on screen.
const puzzle_t *game_puzzle(void);

// Screen position of a puzzle node in the current level.
void game_node_position(int node, int *x, int *y);
