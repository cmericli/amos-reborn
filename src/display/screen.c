/*
 * screen.c — AMOS screen management and drawing operations
 *
 * Each screen has its own RGBA framebuffer, palette, and cursor state.
 * Drawing operations work on the current screen.
 */

#include "amos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── Default Amiga Palette (32 colors, classic AMOS) ─────────────── */

/* AMOS default palette: packed for GL_RGBA GL_UNSIGNED_BYTE (little-endian safe) */
const uint32_t amos_default_palette_32[32] = {
    AMOS_RGBA(0x00,0x00,0x00,0xFF), /* 0:  Black */
    AMOS_RGBA(0x00,0x00,0xAA,0xFF), /* 1:  Dark Blue */
    AMOS_RGBA(0x00,0xAA,0x00,0xFF), /* 2:  Dark Green */
    AMOS_RGBA(0x00,0xAA,0xAA,0xFF), /* 3:  Dark Cyan */
    AMOS_RGBA(0xAA,0x00,0x00,0xFF), /* 4:  Dark Red */
    AMOS_RGBA(0xAA,0x00,0xAA,0xFF), /* 5:  Dark Magenta */
    AMOS_RGBA(0xAA,0x55,0x00,0xFF), /* 6:  Brown */
    AMOS_RGBA(0xAA,0xAA,0xAA,0xFF), /* 7:  Light Gray */
    AMOS_RGBA(0x55,0x55,0x55,0xFF), /* 8:  Dark Gray */
    AMOS_RGBA(0x55,0x55,0xFF,0xFF), /* 9:  Blue */
    AMOS_RGBA(0x55,0xFF,0x55,0xFF), /* 10: Green */
    AMOS_RGBA(0x55,0xFF,0xFF,0xFF), /* 11: Cyan */
    AMOS_RGBA(0xFF,0x55,0x55,0xFF), /* 12: Red */
    AMOS_RGBA(0xFF,0x55,0xFF,0xFF), /* 13: Magenta */
    AMOS_RGBA(0xFF,0xFF,0x55,0xFF), /* 14: Yellow */
    AMOS_RGBA(0xFF,0xFF,0xFF,0xFF), /* 15: White */
    AMOS_RGBA(0x00,0x00,0x00,0xFF), /* 16-31: Additional colors */
    AMOS_RGBA(0x11,0x11,0x11,0xFF), AMOS_RGBA(0x22,0x22,0x22,0xFF),
    AMOS_RGBA(0x33,0x33,0x33,0xFF), AMOS_RGBA(0x44,0x44,0x44,0xFF),
    AMOS_RGBA(0x55,0x55,0x55,0xFF), AMOS_RGBA(0x66,0x66,0x66,0xFF),
    AMOS_RGBA(0x77,0x77,0x77,0xFF), AMOS_RGBA(0x88,0x88,0x88,0xFF),
    AMOS_RGBA(0x99,0x99,0x99,0xFF), AMOS_RGBA(0xAA,0xAA,0xAA,0xFF),
    AMOS_RGBA(0xBB,0xBB,0xBB,0xFF), AMOS_RGBA(0xCC,0xCC,0xCC,0xFF),
    AMOS_RGBA(0xDD,0xDD,0xDD,0xFF), AMOS_RGBA(0xEE,0xEE,0xEE,0xFF),
    AMOS_RGBA(0xFF,0xFF,0xFF,0xFF),
};

/* ── Screen Management ───────────────────────────────────────────── */

int amos_screen_open(amos_state_t *state, int id, int w, int h, int depth)
{
    if (id < 0 || id >= AMOS_MAX_SCREENS) return -1;

    amos_screen_t *scr = &state->screens[id];

    /* Close existing screen */
    if (scr->active) {
        free(scr->pixels);
        free(scr->back_buffer);
    }

    memset(scr, 0, sizeof(amos_screen_t));
    scr->id = id;
    scr->active = true;
    scr->width = w;
    scr->height = h;
    scr->depth = depth;
    scr->visible = true;
    scr->priority = id;
    scr->display_w = w;
    scr->display_h = h;
    scr->ink_color = 1;     /* Default pen: color 1 (usually white or blue) */
    scr->paper_color = 0;   /* Default paper: color 0 (black) */
    scr->text_pen = 1;
    scr->text_paper = 0;

    /* Allocate pixel buffer (RGBA) */
    scr->pixels = calloc(w * h, sizeof(uint32_t));

    /* Copy default palette */
    int num_colors = 1 << depth;
    if (num_colors > 256) num_colors = 256;
    for (int i = 0; i < num_colors && i < 32; i++) {
        scr->palette[i] = amos_default_palette_32[i];
    }

    /* Clear to color 0 */
    uint32_t bg = scr->palette[0];
    for (int i = 0; i < w * h; i++) {
        scr->pixels[i] = bg;
    }

    state->current_screen = id;
    return 0;
}

void amos_screen_close(amos_state_t *state, int id)
{
    if (id < 0 || id >= AMOS_MAX_SCREENS) return;
    amos_screen_t *scr = &state->screens[id];
    free(scr->pixels);
    free(scr->back_buffer);
    memset(scr, 0, sizeof(amos_screen_t));
}

/* ── Drawing State ───────────────────────────────────────────────── */

void amos_screen_ink(amos_state_t *state, int pen, int paper, int outline)
{
    amos_screen_t *scr = &state->screens[state->current_screen];
    if (!scr->active) return;
    scr->ink_color = pen;
    scr->text_pen = pen;
    if (paper >= 0) {
        scr->paper_color = paper;
        scr->text_paper = paper;
    }
    if (outline >= 0) scr->outline_color = outline;
}

void amos_screen_colour(amos_state_t *state, int index, uint32_t rgb)
{
    amos_screen_t *scr = &state->screens[state->current_screen];
    if (!scr->active || index < 0 || index >= 256) return;

    /* AMOS uses $RGB format (4 bits per channel): $F00 = red, $0F0 = green, $00F = blue */
    /* Convert $RGB to AMOS_RGBA packed format */
    int r4 = (rgb >> 8) & 0xF;
    int g4 = (rgb >> 4) & 0xF;
    int b4 = rgb & 0xF;
    scr->palette[index] = AMOS_RGBA(r4 * 17, g4 * 17, b4 * 17, 0xFF);
}

void amos_screen_palette(amos_state_t *state, uint32_t *colors, int count)
{
    for (int i = 0; i < count; i++) {
        amos_screen_colour(state, i, colors[i]);
    }
}

/* ── Clear ───────────────────────────────────────────────────────── */

void amos_screen_cls(amos_state_t *state, int color)
{
    amos_screen_t *scr = &state->screens[state->current_screen];
    if (!scr->active) return;

    uint32_t c = scr->palette[color % 256];
    for (int i = 0; i < scr->width * scr->height; i++) {
        scr->pixels[i] = c;
    }
    scr->cursor_x = 0;
    scr->cursor_y = 0;
}

/* ── Pixel Operations ────────────────────────────────────────────── */

static inline void put_pixel(amos_screen_t *scr, int x, int y, uint32_t color)
{
    if (x >= 0 && x < scr->width && y >= 0 && y < scr->height) {
        scr->pixels[y * scr->width + x] = color;
    }
}

void amos_screen_plot(amos_state_t *state, int x, int y, int color)
{
    amos_screen_t *scr = &state->screens[state->current_screen];
    if (!scr->active) return;
    put_pixel(scr, x, y, scr->palette[color % 256]);
}

/* ── Line Drawing (Bresenham) ────────────────────────────────────── */

void amos_screen_draw(amos_state_t *state, int x1, int y1, int x2, int y2, int color)
{
    amos_screen_t *scr = &state->screens[state->current_screen];
    if (!scr->active) return;

    uint32_t c = scr->palette[color % 256];
    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;

    while (1) {
        put_pixel(scr, x1, y1, c);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

/* ── Rectangle (outline) ────────────────────────────────────────── */

void amos_screen_box(amos_state_t *state, int x1, int y1, int x2, int y2, int color)
{
    amos_screen_draw(state, x1, y1, x2, y1, color);
    amos_screen_draw(state, x2, y1, x2, y2, color);
    amos_screen_draw(state, x2, y2, x1, y2, color);
    amos_screen_draw(state, x1, y2, x1, y1, color);
}

/* ── Filled Rectangle ───────────────────────────────────────────── */

void amos_screen_bar(amos_state_t *state, int x1, int y1, int x2, int y2, int color)
{
    amos_screen_t *scr = &state->screens[state->current_screen];
    if (!scr->active) return;

    uint32_t c = scr->palette[color % 256];

    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }

    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            put_pixel(scr, x, y, c);
        }
    }
}

/* ── Circle (Midpoint algorithm) ─────────────────────────────────── */

void amos_screen_circle(amos_state_t *state, int cx, int cy, int r, int color)
{
    amos_screen_t *scr = &state->screens[state->current_screen];
    if (!scr->active) return;

    uint32_t c = scr->palette[color % 256];
    int x = r, y = 0, err = 1 - r;

    while (x >= y) {
        put_pixel(scr, cx + x, cy + y, c);
        put_pixel(scr, cx + y, cy + x, c);
        put_pixel(scr, cx - y, cy + x, c);
        put_pixel(scr, cx - x, cy + y, c);
        put_pixel(scr, cx - x, cy - y, c);
        put_pixel(scr, cx - y, cy - x, c);
        put_pixel(scr, cx + y, cy - x, c);
        put_pixel(scr, cx + x, cy - y, c);

        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

/* ── Filled Circle ───────────────────────────────────────────────── */

void amos_screen_filled_circle(amos_state_t *state, int cx, int cy, int r, int color)
{
    amos_screen_t *scr = &state->screens[state->current_screen];
    if (!scr->active) return;

    uint32_t c = scr->palette[color % 256];
    int x = r, y = 0, err = 1 - r;

    while (x >= y) {
        for (int i = cx - x; i <= cx + x; i++) put_pixel(scr, i, cy + y, c);
        for (int i = cx - x; i <= cx + x; i++) put_pixel(scr, i, cy - y, c);
        for (int i = cx - y; i <= cx + y; i++) put_pixel(scr, i, cy + x, c);
        for (int i = cx - y; i <= cx + y; i++) put_pixel(scr, i, cy - x, c);

        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

/* ── Ellipse (outline, midpoint algorithm) ───────────────────────── */

void amos_screen_ellipse(amos_state_t *state, int cx, int cy, int rx, int ry, int color)
{
    amos_screen_t *scr = &state->screens[state->current_screen];
    if (!scr->active) return;

    uint32_t c = scr->palette[color % 256];
    long rx2 = (long)rx * rx, ry2 = (long)ry * ry;
    long two_rx2 = 2 * rx2, two_ry2 = 2 * ry2;
    long x = rx, y = 0;
    long px = 0, py = two_rx2 * y;

    /* Region 1 */
    long p1 = ry2 - rx2 * ry + rx2 / 4;
    px = 0; py = two_rx2 * ry; /* typo fix */
    x = rx; y = 0; px = 0; py = two_rx2 * y;
    while (px < py) {
        put_pixel(scr, cx + (int)x, cy + (int)y, c);
        put_pixel(scr, cx - (int)x, cy + (int)y, c);
        put_pixel(scr, cx + (int)x, cy - (int)y, c);
        put_pixel(scr, cx - (int)x, cy - (int)y, c);
        y++;
        px += two_ry2;
        if (p1 < 0) {
            p1 += ry2 + px;
        } else {
            x--;
            py -= two_rx2;
            p1 += ry2 + px - py;
        }
    }

    /* Region 2 */
    long p2 = ry2 * (x * 2 - 1) * (x * 2 - 1) / 4 + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
    while (x >= 0) {
        put_pixel(scr, cx + (int)x, cy + (int)y, c);
        put_pixel(scr, cx - (int)x, cy + (int)y, c);
        put_pixel(scr, cx + (int)x, cy - (int)y, c);
        put_pixel(scr, cx - (int)x, cy - (int)y, c);
        x--;
        py -= two_rx2;
        if (p2 > 0) {
            p2 += rx2 - py;
        } else {
            y++;
            px += two_ry2;
            p2 += rx2 - py + px;
        }
    }
}

/* ── Text Cursor & Print ─────────────────────────────────────────── */

void amos_screen_locate(amos_state_t *state, int x, int y)
{
    amos_screen_t *scr = &state->screens[state->current_screen];
    if (!scr->active) return;
    scr->cursor_x = x;
    scr->cursor_y = y;
}

/* Draw a single character using the Topaz 8x8 font */
static void draw_char(amos_screen_t *scr, int cx, int cy, unsigned char ch,
                      uint32_t fg, uint32_t bg)
{
    int px = cx * 8;
    int py = cy * 8;

    for (int row = 0; row < 8; row++) {
        uint8_t bits = topaz_font_8x8[ch][row];
        for (int col = 0; col < 8; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
            put_pixel(scr, px + col, py + row, color);
        }
    }
}

void amos_screen_print(amos_state_t *state, const char *text)
{
    amos_screen_t *scr = &state->screens[state->current_screen];
    if (!scr->active) return;

    uint32_t fg = scr->palette[scr->text_pen % 256];
    uint32_t bg = scr->palette[scr->text_paper % 256];

    int max_cols = scr->width / 8;
    int max_rows = scr->height / 8;

    for (const char *p = text; *p; p++) {
        if (*p == '\n') {
            scr->cursor_x = 0;
            scr->cursor_y++;
            if (scr->cursor_y >= max_rows) {
                /* Scroll up */
                int row_bytes = scr->width * 8 * sizeof(uint32_t);
                memmove(scr->pixels,
                        scr->pixels + scr->width * 8,
                        scr->width * (scr->height - 8) * sizeof(uint32_t));
                /* Clear bottom row */
                for (int i = scr->width * (scr->height - 8); i < scr->width * scr->height; i++) {
                    scr->pixels[i] = bg;
                }
                (void)row_bytes;
                scr->cursor_y = max_rows - 1;
            }
            continue;
        }
        if (*p == '\t') {
            scr->cursor_x = ((scr->cursor_x / 8) + 1) * 8;
            if (scr->cursor_x >= max_cols) {
                scr->cursor_x = 0;
                scr->cursor_y++;
            }
            continue;
        }

        if (scr->cursor_x < max_cols && scr->cursor_y < max_rows) {
            draw_char(scr, scr->cursor_x, scr->cursor_y, (unsigned char)*p, fg, bg);
        }
        scr->cursor_x++;
        if (scr->cursor_x >= max_cols) {
            scr->cursor_x = 0;
            scr->cursor_y++;
            if (scr->cursor_y >= max_rows) {
                /* Scroll */
                memmove(scr->pixels,
                        scr->pixels + scr->width * 8,
                        scr->width * (scr->height - 8) * sizeof(uint32_t));
                for (int i = scr->width * (scr->height - 8); i < scr->width * scr->height; i++) {
                    scr->pixels[i] = bg;
                }
                scr->cursor_y = max_rows - 1;
            }
        }
    }
}
