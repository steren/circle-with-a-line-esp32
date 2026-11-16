// Drawing into an RGB565 framebuffer. Portable C: no hardware access, so it
// also builds on a PC for tests and screenshots.

#pragma once

#include <stdbool.h>
#include <stdint.h>

// The framebuffer holds width * height pixels. Pixels are stored high byte
// first, which is the order the panel wants them on the wire.
void gfx_init(uint16_t *framebuffer, int width, int height);

void gfx_fill_rect(int x, int y, int w, int h, uint16_t color);
void gfx_fill_round_rect(int x, int y, int w, int h, int radius, uint16_t color);
void gfx_fill_circle(int cx, int cy, int radius, uint16_t color);

// Text uses a 5x7 font in a 6x8 cell, scaled up by an integer factor. Only the
// glyph pixels are drawn; the background is left as it is.
int gfx_text_width(const char *text, int scale);
int gfx_text_height(int scale);
void gfx_draw_text(int x, int y, const char *text, int scale, uint16_t color);

// Optional lighting over everything drawn afterwards, like a panel lit from
// behind: each colour is scaled by col[x] * row[y] / (255 * 255), then dithered
// so smooth falloffs don't show bands. The arrays must stay valid while set;
// NULL for both turns lighting off.
void gfx_set_shading(const uint8_t *col, const uint8_t *row);

// Copies the whole framebuffer into `layer` (width * height pixels), and later
// copies a rectangle of it back. Lets a static picture be kept, so only what
// moves over it has to be redrawn.
void gfx_save(uint16_t *layer);
void gfx_restore(const uint16_t *layer, int x, int y, int w, int h);

// Reports the bounding box (inclusive) of everything drawn since the last call
// and resets it. Returns false if nothing was drawn.
bool gfx_take_dirty(int *x1, int *y1, int *x2, int *y2);

// A colour from 8-bit red, green and blue. The macro form is a constant
// expression, for use in static tables.
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return RGB565(r, g, b);
}
