// The game's sound: a synthesized hum while a line is being drawn, and short
// phrases for everything else, played through the board's speaker.

#pragma once

#include "game.h"

// Call after board_init(). If the audio hardware does not start, the game
// simply runs silent.
void sound_init(void);

// Responds to a game event. Matches game_sound_handler_t.
void sound_play(game_sound_t sound);
