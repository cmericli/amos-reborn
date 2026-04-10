/*
 * sprites.c — AMOS sprite and bob engine
 *
 * Sprites are overlaid on top of the screen output.
 * Bobs are drawn into the screen framebuffer (software blitted).
 *
 * In this Phase 1 implementation, both sprites and bobs use
 * procedurally generated images stored in Bank 1 (sprites)
 * and Bank 2 (icons/bobs). Real .abk bank loading comes later.
 */

#include "amos.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── Procedural Sprite Image Bank ────────────────────────────────── */

/* Each sprite image: 16x16 pixels, stored as uint32_t RGBA */
#define SPRITE_IMG_W 16
#define SPRITE_IMG_H 16
#define MAX_SPRITE_IMAGES 64

typedef struct {
    uint32_t pixels[SPRITE_IMG_W * SPRITE_IMG_H];
    int width, height;
    int hot_x, hot_y;   /* hotspot (center by default) */
} sprite_image_t;

static sprite_image_t g_sprite_images[MAX_SPRITE_IMAGES];
static int g_sprite_image_count = 0;
static bool g_sprites_initialized = false;

/* Generate a simple colored shape for a sprite image */
static void generate_sprite_image(int index, uint32_t color, int shape)
{
    if (index >= MAX_SPRITE_IMAGES) return;

    sprite_image_t *img = &g_sprite_images[index];
    img->width = SPRITE_IMG_W;
    img->height = SPRITE_IMG_H;
    img->hot_x = SPRITE_IMG_W / 2;
    img->hot_y = SPRITE_IMG_H / 2;

    memset(img->pixels, 0, sizeof(img->pixels));

    int cx = SPRITE_IMG_W / 2;
    int cy = SPRITE_IMG_H / 2;

    for (int y = 0; y < SPRITE_IMG_H; y++) {
        for (int x = 0; x < SPRITE_IMG_W; x++) {
            bool filled = false;
            switch (shape) {
                case 0: /* filled circle */
                    filled = ((x - cx) * (x - cx) + (y - cy) * (y - cy)) <= (cx * cx);
                    break;
                case 1: /* diamond */
                    filled = (abs(x - cx) + abs(y - cy)) <= cx;
                    break;
                case 2: /* square */
                    filled = (x >= 2 && x < SPRITE_IMG_W - 2 &&
                              y >= 2 && y < SPRITE_IMG_H - 2);
                    break;
                case 3: /* triangle */
                    filled = (y >= cy && abs(x - cx) <= (y - cy));
                    break;
                default: /* filled */
                    filled = true;
                    break;
            }
            if (filled) {
                img->pixels[y * SPRITE_IMG_W + x] = color;
            }
        }
    }
}

void amos_sprites_init(amos_state_t *state)
{
    if (g_sprites_initialized) return;

    /* Generate a set of default sprite images in various colors and shapes */
    uint32_t colors[] = {
        AMOS_RGBA(255, 255, 255, 255),  /* 1: white */
        AMOS_RGBA(255, 0, 0, 255),      /* 2: red */
        AMOS_RGBA(0, 255, 0, 255),      /* 3: green */
        AMOS_RGBA(0, 0, 255, 255),      /* 4: blue */
        AMOS_RGBA(255, 255, 0, 255),    /* 5: yellow */
        AMOS_RGBA(0, 255, 255, 255),    /* 6: cyan */
        AMOS_RGBA(255, 0, 255, 255),    /* 7: magenta */
        AMOS_RGBA(255, 128, 0, 255),    /* 8: orange */
        AMOS_RGBA(128, 255, 0, 255),    /* 9: lime */
        AMOS_RGBA(0, 128, 255, 255),    /* 10: sky blue */
        AMOS_RGBA(255, 128, 128, 255),  /* 11: pink */
        AMOS_RGBA(128, 255, 128, 255),  /* 12: light green */
        AMOS_RGBA(128, 128, 255, 255),  /* 13: light blue */
        AMOS_RGBA(255, 255, 128, 255),  /* 14: light yellow */
        AMOS_RGBA(200, 200, 200, 255),  /* 15: light gray */
        AMOS_RGBA(128, 128, 128, 255),  /* 16: gray */
    };

    /* Image 0 is empty (transparent) */
    memset(&g_sprite_images[0], 0, sizeof(sprite_image_t));
    g_sprite_images[0].width = SPRITE_IMG_W;
    g_sprite_images[0].height = SPRITE_IMG_H;
    g_sprite_image_count = 1;

    /* Generate images 1-16: circles in different colors */
    for (int i = 0; i < 16; i++) {
        generate_sprite_image(i + 1, colors[i], 0); /* circles */
        g_sprite_image_count++;
    }
    /* Generate images 17-20: different shapes in white */
    for (int s = 0; s < 4; s++) {
        generate_sprite_image(17 + s, colors[0], s);
        g_sprite_image_count++;
    }

    g_sprites_initialized = true;
}

/* ── Sprite/Bob Commands ─────────────────────────────────────────── */

void amos_sprite_set(amos_state_t *state, int id, int x, int y, int image)
{
    if (id < 0 || id >= AMOS_MAX_SPRITES) return;
    amos_sprites_init(state);

    amos_sprite_t *spr = &state->sprites[id];
    spr->active = true;
    spr->visible = true;
    spr->x = x;
    spr->y = y;
    if (image >= 0 && image < g_sprite_image_count) {
        spr->image = image;
        spr->hot_x = g_sprite_images[image].hot_x;
        spr->hot_y = g_sprite_images[image].hot_y;
    }
}

void amos_sprite_off(amos_state_t *state, int id)
{
    if (id < 0) {
        /* Sprite Off with no args = all off */
        for (int i = 0; i < AMOS_MAX_SPRITES; i++)
            state->sprites[i].active = false;
    } else if (id < AMOS_MAX_SPRITES) {
        state->sprites[id].active = false;
    }
}

void amos_bob_set(amos_state_t *state, int id, int x, int y, int image)
{
    if (id < 0 || id >= AMOS_MAX_BOBS) return;
    amos_sprites_init(state);

    amos_bob_t *bob = &state->bobs[id];
    bob->active = true;
    bob->visible = true;
    bob->x = x;
    bob->y = y;
    bob->screen_id = state->current_screen;
    if (image >= 0 && image < g_sprite_image_count) {
        bob->image = image;
        bob->hot_x = g_sprite_images[image].hot_x;
        bob->hot_y = g_sprite_images[image].hot_y;
    }
}

void amos_bob_off(amos_state_t *state, int id)
{
    if (id < 0) {
        for (int i = 0; i < AMOS_MAX_BOBS; i++)
            state->bobs[i].active = false;
    } else if (id < AMOS_MAX_BOBS) {
        state->bobs[id].active = false;
    }
}

/* ── Sprite/Bob Rendering ────────────────────────────────────────── */

/* Render a sprite image at (x,y) into the output buffer */
static void render_sprite_image(uint32_t *output, int out_w, int out_h,
                                int img_idx, int x, int y)
{
    if (img_idx <= 0 || img_idx >= g_sprite_image_count) return;

    sprite_image_t *img = &g_sprite_images[img_idx];
    int sx = x - img->hot_x;
    int sy = y - img->hot_y;

    for (int iy = 0; iy < img->height; iy++) {
        int dy = sy + iy;
        if (dy < 0 || dy >= out_h) continue;
        for (int ix = 0; ix < img->width; ix++) {
            int dx = sx + ix;
            if (dx < 0 || dx >= out_w) continue;
            uint32_t pixel = img->pixels[iy * img->width + ix];
            uint32_t alpha = (pixel >> 24) & 0xFF;
            if (alpha > 0) {
                output[dy * out_w + dx] = pixel;
            }
        }
    }
}

void amos_sprites_render(amos_state_t *state, uint32_t *output, int out_w, int out_h)
{
    amos_sprites_init(state);

    /* Draw bobs first (into screen buffer — they're part of the playfield) */
    for (int i = 0; i < AMOS_MAX_BOBS; i++) {
        amos_bob_t *bob = &state->bobs[i];
        if (!bob->active || !bob->visible) continue;
        render_sprite_image(output, out_w, out_h, bob->image, bob->x, bob->y);
    }

    /* Draw sprites on top (they're hardware overlays) */
    for (int i = 0; i < AMOS_MAX_SPRITES; i++) {
        amos_sprite_t *spr = &state->sprites[i];
        if (!spr->active || !spr->visible) continue;
        render_sprite_image(output, out_w, out_h, spr->image, spr->x, spr->y);
    }
}

/* ── Collision Detection ─────────────────────────────────────────── */

int amos_sprite_col(amos_state_t *state, int id)
{
    if (id < 0 || id >= AMOS_MAX_SPRITES) return 0;
    amos_sprite_t *a = &state->sprites[id];
    if (!a->active) return 0;

    /* Simple bounding box collision against all other sprites */
    int aw = SPRITE_IMG_W, ah = SPRITE_IMG_H;
    int ax1 = a->x - a->hot_x, ay1 = a->y - a->hot_y;
    int ax2 = ax1 + aw, ay2 = ay1 + ah;

    for (int i = 0; i < AMOS_MAX_SPRITES; i++) {
        if (i == id) continue;
        amos_sprite_t *b = &state->sprites[i];
        if (!b->active) continue;

        int bx1 = b->x - b->hot_x, by1 = b->y - b->hot_y;
        int bx2 = bx1 + aw, by2 = by1 + ah;

        if (ax1 < bx2 && ax2 > bx1 && ay1 < by2 && ay2 > by1)
            return i + 1;  /* AMOS returns 1-based collision target, 0 = none */
    }
    return 0;
}

int amos_bob_col(amos_state_t *state, int id)
{
    if (id < 0 || id >= AMOS_MAX_BOBS) return 0;
    amos_bob_t *a = &state->bobs[id];
    if (!a->active) return 0;

    int aw = SPRITE_IMG_W, ah = SPRITE_IMG_H;
    int ax1 = a->x - a->hot_x, ay1 = a->y - a->hot_y;
    int ax2 = ax1 + aw, ay2 = ay1 + ah;

    for (int i = 0; i < AMOS_MAX_BOBS; i++) {
        if (i == id) continue;
        amos_bob_t *b = &state->bobs[i];
        if (!b->active) continue;

        int bx1 = b->x - b->hot_x, by1 = b->y - b->hot_y;
        int bx2 = bx1 + aw, by2 = by1 + ah;

        if (ax1 < bx2 && ax2 > bx1 && ay1 < by2 && ay2 > by1)
            return i + 1;
    }
    return 0;
}
