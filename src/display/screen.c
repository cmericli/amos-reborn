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

/* ── Default Amiga Palette (32 colors) ───────────────────────────── */

/*
 * Authentic AMOS Professional default palette from PI_DefEPa in
 * +Interpreter_Config.s (lines 86-89):
 *
 *   dc.w $000,$A40,$FFF,$000,$F00,$0F0,$00F,$666
 *   dc.w $555,$333,$733,$373,$773,$337,$737,$377
 *   dc.w 0,0,0,0,0,0,0,0
 *   dc.w 0,0,0,0,0,0,0,0
 *
 * Each $0RGB nibble maps to 8-bit via replication: $A → 0xAA (A*17).
 */
const uint32_t amos_default_palette_32[32] = {
    AMOS_RGBA(0x00,0x00,0x00,0xFF), /*  0: $000 Black         */
    AMOS_RGBA(0xAA,0x44,0x00,0xFF), /*  1: $A40 Brown/Orange   */
    AMOS_RGBA(0xFF,0xFF,0xFF,0xFF), /*  2: $FFF White          */
    AMOS_RGBA(0x00,0x00,0x00,0xFF), /*  3: $000 Black          */
    AMOS_RGBA(0xFF,0x00,0x00,0xFF), /*  4: $F00 Red            */
    AMOS_RGBA(0x00,0xFF,0x00,0xFF), /*  5: $0F0 Green          */
    AMOS_RGBA(0x00,0x00,0xFF,0xFF), /*  6: $00F Blue           */
    AMOS_RGBA(0x66,0x66,0x66,0xFF), /*  7: $666 Gray           */
    AMOS_RGBA(0x55,0x55,0x55,0xFF), /*  8: $555 Dark Gray      */
    AMOS_RGBA(0x33,0x33,0x33,0xFF), /*  9: $333 Darker Gray    */
    AMOS_RGBA(0x77,0x33,0x33,0xFF), /* 10: $733 Pinkish        */
    AMOS_RGBA(0x33,0x77,0x33,0xFF), /* 11: $373 Greenish       */
    AMOS_RGBA(0x77,0x77,0x33,0xFF), /* 12: $773 Yellowish      */
    AMOS_RGBA(0x33,0x33,0x77,0xFF), /* 13: $337 Blueish        */
    AMOS_RGBA(0x77,0x33,0x77,0xFF), /* 14: $737 Magenta-ish    */
    AMOS_RGBA(0x33,0x77,0x77,0xFF), /* 15: $377 Cyan-ish       */
    AMOS_RGBA(0x00,0x00,0x00,0xFF), /* 16-31: Black (unused)   */
    AMOS_RGBA(0x00,0x00,0x00,0xFF), AMOS_RGBA(0x00,0x00,0x00,0xFF),
    AMOS_RGBA(0x00,0x00,0x00,0xFF), AMOS_RGBA(0x00,0x00,0x00,0xFF),
    AMOS_RGBA(0x00,0x00,0x00,0xFF), AMOS_RGBA(0x00,0x00,0x00,0xFF),
    AMOS_RGBA(0x00,0x00,0x00,0xFF), AMOS_RGBA(0x00,0x00,0x00,0xFF),
    AMOS_RGBA(0x00,0x00,0x00,0xFF), AMOS_RGBA(0x00,0x00,0x00,0xFF),
    AMOS_RGBA(0x00,0x00,0x00,0xFF), AMOS_RGBA(0x00,0x00,0x00,0xFF),
    AMOS_RGBA(0x00,0x00,0x00,0xFF), AMOS_RGBA(0x00,0x00,0x00,0xFF),
    AMOS_RGBA(0x00,0x00,0x00,0xFF),
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
    /*
     * Default pen/paper from 68K source +W.s line 13656 (Wo3a):
     *   Paper=1 ($A40 brown), Pen=2 ($FFF white), Cursor=3
     * Exception: 1-bitplane screens use Paper=0, Pen=1
     */
    if (depth == 1) {
        scr->ink_color = 1;
        scr->paper_color = 0;
        scr->text_pen = 1;
        scr->text_paper = 0;
    } else {
        scr->ink_color = 2;
        scr->paper_color = 1;
        scr->text_pen = 2;
        scr->text_paper = 1;
    }

    /* Allocate pixel buffer (RGBA) */
    scr->pixels = calloc(w * h, sizeof(uint32_t));

    /* Copy default palette */
    int num_colors = 1 << depth;
    if (num_colors > 256) num_colors = 256;
    for (int i = 0; i < num_colors && i < 32; i++) {
        scr->palette[i] = amos_default_palette_32[i];
    }

    /* Clear to default paper color (color 1 = $A40 brown in AMOS) */
    uint32_t bg = scr->palette[1];
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

/* ── Screen Copy ────────────────────────────────────────────────── */

void amos_screen_copy(amos_state_t *state, int src_id, int x1, int y1, int x2, int y2,
                      int dst_id, int dx, int dy)
{
    if (src_id < 0 || src_id >= AMOS_MAX_SCREENS) return;
    if (dst_id < 0 || dst_id >= AMOS_MAX_SCREENS) return;
    amos_screen_t *src = &state->screens[src_id];
    amos_screen_t *dst = &state->screens[dst_id];
    if (!src->active || !dst->active) return;

    /* Normalize coordinates */
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }

    int w = x2 - x1 + 1;
    int h = y2 - y1 + 1;

    /* Allocate temp buffer to handle overlapping src/dst correctly */
    uint32_t *tmp = malloc(w * h * sizeof(uint32_t));
    if (!tmp) return;

    /* Copy source region to temp */
    for (int row = 0; row < h; row++) {
        int sy = y1 + row;
        for (int col = 0; col < w; col++) {
            int sx = x1 + col;
            if (sx >= 0 && sx < src->width && sy >= 0 && sy < src->height)
                tmp[row * w + col] = src->pixels[sy * src->width + sx];
            else
                tmp[row * w + col] = src->palette[0];
        }
    }

    /* Copy temp to destination */
    for (int row = 0; row < h; row++) {
        int ddy = dy + row;
        for (int col = 0; col < w; col++) {
            int ddx = dx + col;
            if (ddx >= 0 && ddx < dst->width && ddy >= 0 && ddy < dst->height)
                dst->pixels[ddy * dst->width + ddx] = tmp[row * w + col];
        }
    }

    free(tmp);
}

/* ── Get Block / Put Block / Del Block ──────────────────────────── */

void amos_get_block(amos_state_t *state, int id, int x, int y, int w, int h)
{
    if (id < 0 || id >= AMOS_MAX_BLOCKS) return;
    amos_screen_t *scr = &state->screens[state->current_screen];
    if (!scr->active) return;
    if (w <= 0 || h <= 0) return;

    amos_block_t *blk = &state->blocks[id];

    /* Free existing block data */
    if (blk->used && blk->pixels) {
        free(blk->pixels);
    }

    blk->width = w;
    blk->height = h;
    blk->pixels = malloc(w * h * sizeof(uint32_t));
    if (!blk->pixels) return;
    blk->used = true;

    for (int row = 0; row < h; row++) {
        int sy = y + row;
        for (int col = 0; col < w; col++) {
            int sx = x + col;
            if (sx >= 0 && sx < scr->width && sy >= 0 && sy < scr->height)
                blk->pixels[row * w + col] = scr->pixels[sy * scr->width + sx];
            else
                blk->pixels[row * w + col] = scr->palette[0];
        }
    }
}

void amos_put_block(amos_state_t *state, int id, int x, int y)
{
    if (id < 0 || id >= AMOS_MAX_BLOCKS) return;
    amos_block_t *blk = &state->blocks[id];
    if (!blk->used || !blk->pixels) return;

    amos_screen_t *scr = &state->screens[state->current_screen];
    if (!scr->active) return;

    for (int row = 0; row < blk->height; row++) {
        int dy = y + row;
        for (int col = 0; col < blk->width; col++) {
            int ddx = x + col;
            if (ddx >= 0 && ddx < scr->width && dy >= 0 && dy < scr->height)
                scr->pixels[dy * scr->width + ddx] = blk->pixels[row * blk->width + col];
        }
    }
}

void amos_del_block(amos_state_t *state, int id)
{
    if (id < 0 || id >= AMOS_MAX_BLOCKS) return;
    amos_block_t *blk = &state->blocks[id];
    if (blk->used && blk->pixels) {
        free(blk->pixels);
    }
    memset(blk, 0, sizeof(amos_block_t));
}

/* ── Scroll ─────────────────────────────────────────────────────── */

void amos_screen_scroll(amos_state_t *state, int dx, int dy)
{
    amos_screen_t *scr = &state->screens[state->current_screen];
    if (!scr->active) return;

    int w = scr->width;
    int h = scr->height;
    uint32_t bg = scr->palette[0];

    if (abs(dx) >= w || abs(dy) >= h) {
        /* Scrolled completely off -- clear */
        for (int i = 0; i < w * h; i++) scr->pixels[i] = bg;
        return;
    }

    /* Vertical scroll using memmove */
    if (dy > 0) {
        /* Scroll down: move rows up in memory, expose top */
        memmove(scr->pixels + dy * w, scr->pixels, (h - dy) * w * sizeof(uint32_t));
        for (int i = 0; i < dy * w; i++) scr->pixels[i] = bg;
    } else if (dy < 0) {
        /* Scroll up: move rows down in memory, expose bottom */
        int ady = -dy;
        memmove(scr->pixels, scr->pixels + ady * w, (h - ady) * w * sizeof(uint32_t));
        for (int i = (h - ady) * w; i < h * w; i++) scr->pixels[i] = bg;
    }

    /* Horizontal scroll */
    if (dx > 0) {
        /* Scroll right: expose left */
        for (int row = 0; row < h; row++) {
            uint32_t *line = scr->pixels + row * w;
            memmove(line + dx, line, (w - dx) * sizeof(uint32_t));
            for (int i = 0; i < dx; i++) line[i] = bg;
        }
    } else if (dx < 0) {
        /* Scroll left: expose right */
        int adx = -dx;
        for (int row = 0; row < h; row++) {
            uint32_t *line = scr->pixels + row * w;
            memmove(line, line + adx, (w - adx) * sizeof(uint32_t));
            for (int i = w - adx; i < w; i++) line[i] = bg;
        }
    }
}

/* ── Def Scroll / Scroll Zone ───────────────────────────────────── */

void amos_def_scroll(amos_state_t *state, int id, int x1, int y1, int x2, int y2)
{
    if (id < 0 || id >= AMOS_MAX_SCROLL_ZONES) return;
    amos_scroll_zone_t *z = &state->scroll_zones[id];
    z->defined = true;
    z->x1 = x1 < x2 ? x1 : x2;
    z->y1 = y1 < y2 ? y1 : y2;
    z->x2 = x1 > x2 ? x1 : x2;
    z->y2 = y1 > y2 ? y1 : y2;
}

void amos_scroll_zone(amos_state_t *state, int id, int dx, int dy)
{
    if (id < 0 || id >= AMOS_MAX_SCROLL_ZONES) return;
    amos_scroll_zone_t *z = &state->scroll_zones[id];
    if (!z->defined) return;

    amos_screen_t *scr = &state->screens[state->current_screen];
    if (!scr->active) return;

    int zw = z->x2 - z->x1 + 1;
    int zh = z->y2 - z->y1 + 1;
    uint32_t bg = scr->palette[0];

    if (abs(dx) >= zw || abs(dy) >= zh) {
        /* Clear the zone */
        for (int row = z->y1; row <= z->y2; row++) {
            for (int col = z->x1; col <= z->x2; col++) {
                if (col >= 0 && col < scr->width && row >= 0 && row < scr->height)
                    scr->pixels[row * scr->width + col] = bg;
            }
        }
        return;
    }

    /* Use temp buffer for zone scroll */
    uint32_t *tmp = malloc(zw * zh * sizeof(uint32_t));
    if (!tmp) return;

    /* Copy zone to temp */
    for (int row = 0; row < zh; row++) {
        for (int col = 0; col < zw; col++) {
            int sx = z->x1 + col, sy = z->y1 + row;
            if (sx >= 0 && sx < scr->width && sy >= 0 && sy < scr->height)
                tmp[row * zw + col] = scr->pixels[sy * scr->width + sx];
            else
                tmp[row * zw + col] = bg;
        }
    }

    /* Write back shifted, fill exposed with bg */
    for (int row = 0; row < zh; row++) {
        for (int col = 0; col < zw; col++) {
            int src_col = col - dx;
            int src_row = row - dy;
            uint32_t pixel;
            if (src_col >= 0 && src_col < zw && src_row >= 0 && src_row < zh)
                pixel = tmp[src_row * zw + src_col];
            else
                pixel = bg;
            int tx = z->x1 + col, ty = z->y1 + row;
            if (tx >= 0 && tx < scr->width && ty >= 0 && ty < scr->height)
                scr->pixels[ty * scr->width + tx] = pixel;
        }
    }

    free(tmp);
}

/* ── Screen To Front / Screen To Back ───────────────────────────── */

void amos_screen_to_front(amos_state_t *state, int id)
{
    if (id < 0 || id >= AMOS_MAX_SCREENS) return;
    if (!state->screens[id].active) return;
    int old_priority = state->screens[id].priority;
    for (int i = 0; i < AMOS_MAX_SCREENS; i++) {
        if (state->screens[i].active && state->screens[i].priority < old_priority)
            state->screens[i].priority++;
    }
    state->screens[id].priority = 0;
}

void amos_screen_to_back(amos_state_t *state, int id)
{
    if (id < 0 || id >= AMOS_MAX_SCREENS) return;
    if (!state->screens[id].active) return;
    int max_pri = 0;
    int old_priority = state->screens[id].priority;
    for (int i = 0; i < AMOS_MAX_SCREENS; i++) {
        if (state->screens[i].active && state->screens[i].priority > max_pri)
            max_pri = state->screens[i].priority;
    }
    for (int i = 0; i < AMOS_MAX_SCREENS; i++) {
        if (state->screens[i].active && state->screens[i].priority > old_priority)
            state->screens[i].priority--;
    }
    state->screens[id].priority = max_pri;
}

/* ── Screen Hide / Screen Show ──────────────────────────────────── */

void amos_screen_hide(amos_state_t *state, int id)
{
    if (id < 0 || id >= AMOS_MAX_SCREENS) return;
    state->screens[id].visible = false;
}

void amos_screen_show(amos_state_t *state, int id)
{
    if (id < 0 || id >= AMOS_MAX_SCREENS) return;
    state->screens[id].visible = true;
}

/* ── Paint (Iterative Scanline Flood Fill) ──────────────────────── */

void amos_screen_paint(amos_state_t *state, int x, int y, int color)
{
    amos_screen_t *scr = &state->screens[state->current_screen];
    if (!scr->active) return;
    if (x < 0 || x >= scr->width || y < 0 || y >= scr->height) return;

    uint32_t fill_color = scr->palette[color % 256];
    uint32_t target_color = scr->pixels[y * scr->width + x];

    if (fill_color == target_color) return;  /* Already the fill color */

    /* Heap-allocated stack for iterative scanline flood fill */
    typedef struct { int x, y; } point_t;
    int cap = 4096;
    int top = 0;
    point_t *stack = malloc(cap * sizeof(point_t));
    if (!stack) return;

    stack[top++] = (point_t){x, y};

    while (top > 0) {
        point_t p = stack[--top];

        /* Skip out-of-bounds or wrong color */
        if (p.x < 0 || p.x >= scr->width || p.y < 0 || p.y >= scr->height)
            continue;
        if (scr->pixels[p.y * scr->width + p.x] != target_color)
            continue;

        /* Scanline fill: find leftmost and rightmost extent */
        int lx = p.x, rx = p.x;
        while (lx > 0 && scr->pixels[p.y * scr->width + (lx - 1)] == target_color)
            lx--;
        while (rx < scr->width - 1 && scr->pixels[p.y * scr->width + (rx + 1)] == target_color)
            rx++;

        /* Fill the scanline */
        for (int i = lx; i <= rx; i++)
            scr->pixels[p.y * scr->width + i] = fill_color;

        /* Push spans above and below */
        for (int dir = -1; dir <= 1; dir += 2) {
            int ny = p.y + dir;
            if (ny < 0 || ny >= scr->height) continue;

            bool span_start = false;
            for (int i = lx; i <= rx; i++) {
                if (scr->pixels[ny * scr->width + i] == target_color) {
                    if (!span_start) {
                        if (top >= cap) {
                            cap *= 2;
                            stack = realloc(stack, cap * sizeof(point_t));
                            if (!stack) return;
                        }
                        stack[top++] = (point_t){i, ny};
                        span_start = true;
                    }
                } else {
                    span_start = false;
                }
            }
        }
    }

    free(stack);
}

/* ── Polygon (Filled, Scanline Algorithm) ───────────────────────── */

void amos_screen_polygon(amos_state_t *state, int *points, int npoints, int color)
{
    amos_screen_t *scr = &state->screens[state->current_screen];
    if (!scr->active || npoints < 3) return;

    uint32_t c = scr->palette[color % 256];

    /* Find Y bounds */
    int ymin = points[1], ymax = points[1];
    for (int i = 1; i < npoints; i++) {
        int py = points[i * 2 + 1];
        if (py < ymin) ymin = py;
        if (py > ymax) ymax = py;
    }

    if (ymin < 0) ymin = 0;
    if (ymax >= scr->height) ymax = scr->height - 1;

    /* Allocate intersections array */
    int *nodes = malloc(npoints * sizeof(int));
    if (!nodes) return;

    /* Scanline fill */
    for (int y = ymin; y <= ymax; y++) {
        int node_count = 0;

        /* Find intersections with all edges */
        int j = npoints - 1;
        for (int i = 0; i < npoints; i++) {
            int yi = points[i * 2 + 1];
            int yj = points[j * 2 + 1];
            if ((yi < y && yj >= y) || (yj < y && yi >= y)) {
                int xi = points[i * 2];
                int xj = points[j * 2];
                nodes[node_count++] = xi + (long)(y - yi) * (xj - xi) / (yj - yi);
            }
            j = i;
        }

        /* Sort intersections (insertion sort) */
        for (int i = 1; i < node_count; i++) {
            int tmp = nodes[i];
            int k = i - 1;
            while (k >= 0 && nodes[k] > tmp) {
                nodes[k + 1] = nodes[k];
                k--;
            }
            nodes[k + 1] = tmp;
        }

        /* Fill between pairs */
        for (int i = 0; i < node_count - 1; i += 2) {
            int fx1 = nodes[i];
            int fx2 = nodes[i + 1];
            if (fx1 < 0) fx1 = 0;
            if (fx2 >= scr->width) fx2 = scr->width - 1;
            for (int px = fx1; px <= fx2; px++) {
                put_pixel(scr, px, y, c);
            }
        }
    }

    free(nodes);
}
