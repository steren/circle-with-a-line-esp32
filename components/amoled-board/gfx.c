#include "gfx.h"

#include <math.h>
#include <string.h>

#include "font5x7.h"

static uint16_t *s_framebuffer;
static int s_width, s_height;
static const uint8_t *s_shade_col, *s_shade_row;

// 4x4 ordered-dither thresholds, 0-15.
static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

void gfx_set_shading(const uint8_t *col, const uint8_t *row)
{
    s_shade_col = col;
    s_shade_row = row;
}

// Writes one row of a rectangle with lighting applied.
static void fill_row_shaded(uint16_t *p, int x, int x2, int y, uint16_t color)
{
    const uint32_t r8 = ((color >> 11) & 0x1F) * 255 / 31;
    const uint32_t g8 = ((color >> 5) & 0x3F) * 255 / 63;
    const uint32_t b8 = (color & 0x1F) * 255 / 31;
    const uint32_t row = s_shade_row[y];
    const uint8_t *bayer = BAYER[y & 3];
    for (; x <= x2; x++) {
        const uint32_t f = s_shade_col[x] * row;  // 0..65025
        const uint32_t d = bayer[x & 3];
        // Scale each channel, adding a dither offset of up to one output step.
        uint32_t r = ((r8 * f) >> 16) + (d >> 1);
        uint32_t g = ((g8 * f) >> 16) + (d >> 2);
        uint32_t b = ((b8 * f) >> 16) + (d >> 1);
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        *p++ = __builtin_bswap16((uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)));
    }
}

// Bounding box of the pixels changed since the last gfx_take_dirty() (inclusive).
static struct {
    int x1, y1, x2, y2;
} s_dirty;

static void mark_clean(void)
{
    s_dirty.x1 = s_width;
    s_dirty.y1 = s_height;
    s_dirty.x2 = -1;
    s_dirty.y2 = -1;
}

void gfx_init(uint16_t *framebuffer, int width, int height)
{
    s_framebuffer = framebuffer;
    s_width = width;
    s_height = height;
    mark_clean();
}

bool gfx_take_dirty(int *x1, int *y1, int *x2, int *y2)
{
    if (s_dirty.x2 < s_dirty.x1) {
        return false;
    }
    *x1 = s_dirty.x1;
    *y1 = s_dirty.y1;
    *x2 = s_dirty.x2;
    *y2 = s_dirty.y2;
    mark_clean();
    return true;
}

void gfx_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    int x2 = x + w - 1, y2 = y + h - 1;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 >= s_width) x2 = s_width - 1;
    if (y2 >= s_height) y2 = s_height - 1;
    if (x > x2 || y > y2) {
        return;
    }

    const uint16_t wire_color = __builtin_bswap16(color);
    for (int row = y; row <= y2; row++) {
        uint16_t *p = &s_framebuffer[row * s_width + x];
        if (s_shade_col) {
            fill_row_shaded(p, x, x2, row, color);
            continue;
        }
        for (int i = x2 - x + 1; i > 0; i--) {
            *p++ = wire_color;
        }
    }

    if (x < s_dirty.x1) s_dirty.x1 = x;
    if (y < s_dirty.y1) s_dirty.y1 = y;
    if (x2 > s_dirty.x2) s_dirty.x2 = x2;
    if (y2 > s_dirty.y2) s_dirty.y2 = y2;
}

void gfx_save(uint16_t *layer)
{
    memcpy(layer, s_framebuffer, (size_t)s_width * s_height * sizeof(uint16_t));
}

void gfx_restore(const uint16_t *layer, int x, int y, int w, int h)
{
    int x2 = x + w - 1, y2 = y + h - 1;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 >= s_width) x2 = s_width - 1;
    if (y2 >= s_height) y2 = s_height - 1;
    if (x > x2 || y > y2) {
        return;
    }
    for (int row = y; row <= y2; row++) {
        memcpy(&s_framebuffer[row * s_width + x], &layer[row * s_width + x], (size_t)(x2 - x + 1) * sizeof(uint16_t));
    }
    if (x < s_dirty.x1) s_dirty.x1 = x;
    if (y < s_dirty.y1) s_dirty.y1 = y;
    if (x2 > s_dirty.x2) s_dirty.x2 = x2;
    if (y2 > s_dirty.y2) s_dirty.y2 = y2;
}

void gfx_blend_pixel(int x, int y, uint16_t color, uint8_t alpha)
{
    if (alpha == 0 || x < 0 || y < 0 || x >= s_width || y >= s_height) {
        return;
    }
    if (alpha == 255) {
        gfx_fill_rect(x, y, 1, 1, color);
        return;
    }

    uint32_t fg_r = ((color >> 11) & 0x1F) * 255 / 31;
    uint32_t fg_g = ((color >> 5) & 0x3F) * 255 / 63;
    uint32_t fg_b = (color & 0x1F) * 255 / 31;
    if (s_shade_col) {
        const uint32_t f = s_shade_col[x] * (uint32_t)s_shade_row[y];
        const uint32_t d = BAYER[y & 3][x & 3];
        fg_r = ((fg_r * f) >> 16) + (d >> 1);
        fg_g = ((fg_g * f) >> 16) + (d >> 2);
        fg_b = ((fg_b * f) >> 16) + (d >> 1);
        if (fg_r > 255) fg_r = 255;
        if (fg_g > 255) fg_g = 255;
        if (fg_b > 255) fg_b = 255;
    }

    const uint16_t bg_c = __builtin_bswap16(s_framebuffer[y * s_width + x]);
    const uint32_t bg_r = ((bg_c >> 11) & 0x1F) * 255 / 31;
    const uint32_t bg_g = ((bg_c >> 5) & 0x3F) * 255 / 63;
    const uint32_t bg_b = (bg_c & 0x1F) * 255 / 31;

    const uint32_t inv = 255 - alpha;
    const uint32_t r = (fg_r * alpha + bg_r * inv + 127) / 255;
    const uint32_t g = (fg_g * alpha + bg_g * inv + 127) / 255;
    const uint32_t b = (fg_b * alpha + bg_b * inv + 127) / 255;
    s_framebuffer[y * s_width + x] = __builtin_bswap16((uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)));

    if (x < s_dirty.x1) s_dirty.x1 = x;
    if (y < s_dirty.y1) s_dirty.y1 = y;
    if (x > s_dirty.x2) s_dirty.x2 = x;
    if (y > s_dirty.y2) s_dirty.y2 = y;
}

void gfx_fill_round_rect(int x, int y, int w, int h, int radius, uint16_t color)
{
    const float r_arc = (float)radius + 0.25f;
    for (int row = 0; row < h; row++) {
        if (row >= radius && row < h - radius) {
            gfx_fill_rect(x, y + row, w, 1, color);
            continue;
        }
        const float dy = (row < radius) ? ((float)radius - 0.5f - (float)row)
                                        : ((float)row - (float)(h - radius) + 0.5f);
        float x_arc = (dy < r_arc) ? sqrtf(r_arc * r_arc - dy * dy) : 0.0f;
        if (x_arc > (float)radius) x_arc = (float)radius;
        const int solid_ext = (int)floorf(x_arc);
        const int inset = radius - solid_ext;
        gfx_fill_rect(x + inset, y + row, w - 2 * inset, 1, color);
        if (inset > 0) {
            const uint8_t alpha = (uint8_t)((x_arc - (float)solid_ext) * 255.0f + 0.5f);
            gfx_blend_pixel(x + inset - 1, y + row, color, alpha);
            gfx_blend_pixel(x + w - inset, y + row, color, alpha);
        }
    }
}

void gfx_fill_circle(int cx, int cy, int radius, uint16_t color)
{
    const float r_edge = (float)radius + 0.35f;
    for (int dy = -radius; dy <= radius; dy++) {
        const float fdy = (float)dy;
        float x_exact = (fabsf(fdy) < r_edge) ? sqrtf(r_edge * r_edge - fdy * fdy) : 0.0f;
        if (x_exact > (float)radius) x_exact = (float)radius;
        const int inner = (int)floorf(x_exact);
        gfx_fill_rect(cx - inner, cy + dy, 2 * inner + 1, 1, color);
        if (inner < radius) {
            const uint8_t alpha = (uint8_t)((x_exact - (float)inner) * 255.0f + 0.5f);
            gfx_blend_pixel(cx - inner - 1, cy + dy, color, alpha);
            gfx_blend_pixel(cx + inner + 1, cy + dy, color, alpha);
        }
    }
}

int gfx_text_width(const char *text, int scale)
{
    const int len = strlen(text);
    return len ? (len * FONT_CELL_WIDTH - 1) * scale : 0;
}

int gfx_text_height(int scale)
{
    return FONT_CELL_HEIGHT * scale;
}

void gfx_draw_text(int x, int y, const char *text, int scale, uint16_t color)
{
    for (; *text; text++, x += FONT_CELL_WIDTH * scale) {
        unsigned char c = *text;
        if (c < FONT_FIRST_CHAR || c > FONT_LAST_CHAR) {
            c = '?';
        }
        const uint8_t *glyph = font5x7[c - FONT_FIRST_CHAR];
        for (int col = 0; col < FONT_GLYPH_WIDTH; col++) {
            for (int row = 0; row < FONT_GLYPH_HEIGHT; row++) {
                if (glyph[col] & (1 << row)) {
                    gfx_fill_rect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
    }
}
