// AMOLED panel driver (CO5300 / SH8601 over QSPI).
//
// The panel shows a framebuffer that is drawn into with the functions in
// gfx.h; display_flush() sends whatever changed since the last flush.

#pragma once

#include <stdint.h>

#include "esp_err.h"

// Allocates the framebuffer, hands it to gfx_init() and switches the panel on.
// column_offset: where the visible area starts in the controller's memory
// (16 for the CO5300 on the V2 board, 0 for the SH8601 on the original).
esp_err_t display_init(int column_offset);

void display_set_brightness(uint8_t level);

void display_set_power(bool on);

void display_flush(void);
