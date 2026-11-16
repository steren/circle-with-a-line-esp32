// Sound output: ES8311 codec over I2S, driving the speaker amplifier.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define AUDIO_SAMPLE_RATE 16000

// Call after board_init(). Sets up the codec and switches the amplifier on.
esp_err_t audio_init(void);

// Plays 16-bit mono samples at AUDIO_SAMPLE_RATE. Blocks until they have been
// handed to the output; silence is played whenever nothing is written.
void audio_write(const int16_t *samples, size_t count);
