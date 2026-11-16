// The game's sound generator: a hum for drawing the line, and short plucked
// phrases for everything else. Portable C that only produces samples, so the
// PC tool can render exactly what the board plays.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "game.h"

#define SYNTH_SAMPLE_RATE 16000

// The hum fades in and out rather than switching abruptly.
void synth_hum_on(void);
void synth_hum_off(void);

// Starts the phrase for a game event, cutting off any phrase still playing.
void synth_phrase(game_sound_t sound);

// True once everything has faded out and there is nothing left to render.
bool synth_is_silent(void);

// Produces the next `count` samples (16-bit mono).
void synth_render(int16_t *samples, int count);
