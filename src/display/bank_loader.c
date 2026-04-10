/*
 * bank_loader.c — AMOS sprite/icon bank (.abk) loader
 *
 * Loads Amiga-format sprite and icon banks into AMOS state.
 * Handles planar-to-chunky conversion with $0RGB palette lookup.
 *
 * Bank format (standalone .abk):
 *   "AmSp" or "AmIc"  (4 bytes magic)
 *   Number of sprites  (2 bytes, big-endian)
 *   For each sprite:
 *     +0  2  Width in 16-pixel words
 *     +2  2  Height in pixels
 *     +4  2  Number of bitplanes (depth)
 *     +6  2  X hotspot
 *     +8  2  Y hotspot
 *     +10 N  Interleaved planar image data
 *   After all sprites: 64 bytes palette (32 x 2-byte $0RGB)
 */

#include "amos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Big-endian helpers ─────────────────────────────────────────────── */

static inline uint16_t BE16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint32_t BE32(const uint8_t *p) {
    return (uint32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

/* ── Magic identifiers ─────────────────────────────────────────────── */

#define MAGIC_AMSP 0x416D5370  /* "AmSp" */
#define MAGIC_AMIC 0x416D4963  /* "AmIc" */

/* ── Convert $0RGB to AMOS_RGBA ────────────────────────────────────── */

/*
 * Amiga $0RGB format: 0x0RGB where R,G,B are 4-bit nibbles.
 * Expand to 8-bit by replicating: 0xR -> 0xRR
 */
static uint32_t rgb4_to_rgba(uint16_t rgb4)
{
    uint8_t r = (rgb4 >> 8) & 0x0F;
    uint8_t g = (rgb4 >> 4) & 0x0F;
    uint8_t b = rgb4 & 0x0F;

    r = (r << 4) | r;
    g = (g << 4) | g;
    b = (b << 4) | b;

    return AMOS_RGBA(r, g, b, 0xFF);
}

/* ── Planar-to-chunky for sprite data ──────────────────────────────── */

/*
 * Convert interleaved planar sprite data to RGBA pixels.
 * Sprite planes are interleaved: for each scanline, depth planes
 * of width_words*2 bytes each appear consecutively.
 *
 * Color 0 is transparent (alpha=0).
 */
static uint32_t *planar_sprite_to_rgba(const uint8_t *plane_data,
                                        int width_words, int height,
                                        int depth, const uint32_t *palette)
{
    int pixel_width = width_words * 16;
    int row_plane_bytes = width_words * 2;
    int total_pixels = pixel_width * height;

    uint32_t *pixels = calloc(total_pixels, sizeof(uint32_t));
    if (!pixels) return NULL;

    for (int y = 0; y < height; y++) {
        /* Each scanline has depth planes of row_plane_bytes each */
        const uint8_t *scanline_base = plane_data +
            y * (row_plane_bytes * depth);

        for (int x = 0; x < pixel_width; x++) {
            int byte_idx = x / 8;
            int bit_idx = 7 - (x % 8);
            uint8_t color = 0;

            for (int p = 0; p < depth; p++) {
                const uint8_t *plane_row = scanline_base + p * row_plane_bytes;
                if (plane_row[byte_idx] & (1 << bit_idx))
                    color |= (1 << p);
            }

            if (color == 0) {
                /* Transparent */
                pixels[y * pixel_width + x] = AMOS_RGBA(0, 0, 0, 0);
            } else if (color < 32) {
                pixels[y * pixel_width + x] = palette[color];
            } else {
                pixels[y * pixel_width + x] = AMOS_RGBA(0, 0, 0, 0xFF);
            }
        }
    }

    return pixels;
}

/* ── Check if sprite entry is empty ────────────────────────────────── */

static bool is_empty_sprite(int width_words, int height, int depth)
{
    return (width_words == 0 || height == 0 || depth == 0);
}

/* ── Load sprite bank from memory ──────────────────────────────────── */

int amos_load_sprite_bank_mem(amos_state_t *state, const uint8_t *data, size_t length)
{
    if (!state || !data || length < 6) return -1;

    /* Check magic */
    uint32_t magic = BE32(data);
    if (magic != MAGIC_AMSP && magic != MAGIC_AMIC) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Bank: Invalid magic (not AmSp or AmIc)");
        return -1;
    }

    bool is_icons = (magic == MAGIC_AMIC);
    uint16_t num_sprites = BE16(data + 4);

    if (num_sprites == 0) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Bank: Empty bank (0 sprites)");
        return -1;
    }

    /* First pass: calculate total data size and find palette position */
    size_t pos = 6;
    size_t *sprite_offsets = calloc(num_sprites, sizeof(size_t));
    int *widths = calloc(num_sprites, sizeof(int));
    int *heights_arr = calloc(num_sprites, sizeof(int));
    int *depths = calloc(num_sprites, sizeof(int));
    int *hotspot_x = calloc(num_sprites, sizeof(int));
    int *hotspot_y = calloc(num_sprites, sizeof(int));

    if (!sprite_offsets || !widths || !heights_arr || !depths ||
        !hotspot_x || !hotspot_y) {
        free(sprite_offsets); free(widths); free(heights_arr);
        free(depths); free(hotspot_x); free(hotspot_y);
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Bank: Out of memory");
        return -1;
    }

    for (int i = 0; i < num_sprites; i++) {
        if (pos + 10 > length) {
            free(sprite_offsets); free(widths); free(heights_arr);
            free(depths); free(hotspot_x); free(hotspot_y);
            snprintf(state->error_msg, sizeof(state->error_msg),
                     "Bank: Truncated at sprite %d header", i);
            return -1;
        }

        int w_words = BE16(data + pos);
        int h = BE16(data + pos + 2);
        int d = BE16(data + pos + 4);
        int hx = (int16_t)BE16(data + pos + 6);
        int hy = (int16_t)BE16(data + pos + 8);

        /* Mask hotspot flags in bits 14-15 */
        hx &= 0x3FFF;
        hy &= 0x3FFF;

        widths[i] = w_words;
        heights_arr[i] = h;
        depths[i] = d;
        hotspot_x[i] = hx;
        hotspot_y[i] = hy;
        sprite_offsets[i] = pos + 10;

        if (is_empty_sprite(w_words, h, d)) {
            /* Empty sprite: header is 10 bytes of zeros, no image data */
            pos += 10;
        } else {
            int image_size = w_words * 2 * h * d;
            if (pos + 10 + (size_t)image_size > length) {
                free(sprite_offsets); free(widths); free(heights_arr);
                free(depths); free(hotspot_x); free(hotspot_y);
                snprintf(state->error_msg, sizeof(state->error_msg),
                         "Bank: Truncated image data at sprite %d", i);
                return -1;
            }
            pos += 10 + image_size;
        }
    }

    /* Parse palette (64 bytes = 32 colors x 2 bytes) */
    uint32_t palette[32];
    memset(palette, 0, sizeof(palette));

    if (pos + 64 <= length) {
        for (int i = 0; i < 32; i++) {
            uint16_t rgb4 = BE16(data + pos + i * 2);
            palette[i] = rgb4_to_rgba(rgb4);
        }
    }
    /* Color 0 palette entry can be anything but sprite color 0 is always transparent */

    /* Second pass: convert each sprite to RGBA */
    uint32_t **images = calloc(num_sprites, sizeof(uint32_t *));
    int *pixel_widths = calloc(num_sprites, sizeof(int));
    int *pixel_heights = calloc(num_sprites, sizeof(int));

    if (!images || !pixel_widths || !pixel_heights) {
        free(sprite_offsets); free(widths); free(heights_arr);
        free(depths); free(hotspot_x); free(hotspot_y);
        free(images); free(pixel_widths); free(pixel_heights);
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Bank: Out of memory for images");
        return -1;
    }

    for (int i = 0; i < num_sprites; i++) {
        if (is_empty_sprite(widths[i], heights_arr[i], depths[i])) {
            /* Empty sprite: single transparent pixel */
            images[i] = calloc(1, sizeof(uint32_t));
            pixel_widths[i] = 1;
            pixel_heights[i] = 1;
        } else {
            pixel_widths[i] = widths[i] * 16;
            pixel_heights[i] = heights_arr[i];
            images[i] = planar_sprite_to_rgba(data + sprite_offsets[i],
                                               widths[i], heights_arr[i],
                                               depths[i], palette);
        }

        if (!images[i]) {
            /* Clean up on failure */
            for (int j = 0; j < i; j++) free(images[j]);
            free(images); free(pixel_widths); free(pixel_heights);
            free(sprite_offsets); free(widths); free(heights_arr);
            free(depths); free(hotspot_x); free(hotspot_y);
            snprintf(state->error_msg, sizeof(state->error_msg),
                     "Bank: Failed to convert sprite %d", i);
            return -1;
        }
    }

    /* Load into sprite engine */
    amos_sprites_load_bank(state, images, pixel_widths, pixel_heights,
                           hotspot_x, hotspot_y, num_sprites);

    /* Store in bank slot */
    int bank_num = is_icons ? 1 : 0;  /* Bank 1 = sprites, Bank 2 = icons in AMOS (0-indexed) */
    if (bank_num < AMOS_MAX_BANKS) {
        amos_bank_t *bank = &state->banks[bank_num];
        if (bank->data) free(bank->data);
        bank->type = is_icons ? BANK_ICONS : BANK_SPRITES;
        bank->data = malloc(length);
        if (bank->data) {
            memcpy(bank->data, data, length);
            bank->size = (uint32_t)length;
        }
        strncpy(bank->name, is_icons ? "Icons" : "Sprites", sizeof(bank->name) - 1);
    }

    /* Clean up temporary arrays (images are now owned by sprite engine) */
    free(images);
    free(pixel_widths);
    free(pixel_heights);
    free(sprite_offsets);
    free(widths);
    free(heights_arr);
    free(depths);
    free(hotspot_x);
    free(hotspot_y);

    return 0;
}

/* ── Load sprite bank from file ────────────────────────────────────── */

int amos_load_sprite_bank(amos_state_t *state, const char *path)
{
    if (!state || !path) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Bank: Cannot open file '%s'", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 16 * 1024 * 1024) {
        fclose(f);
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Bank: Invalid file size %ld", file_size);
        return -1;
    }

    uint8_t *data = malloc(file_size);
    if (!data) {
        fclose(f);
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Bank: Out of memory");
        return -1;
    }

    size_t read_len = fread(data, 1, file_size, f);
    fclose(f);

    if ((long)read_len != file_size) {
        free(data);
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Bank: Short read (%zu of %ld bytes)", read_len, file_size);
        return -1;
    }

    int result = amos_load_sprite_bank_mem(state, data, (size_t)file_size);
    free(data);
    return result;
}

/* ── Load icon bank from file ──────────────────────────────────────── */

int amos_load_icon_bank(amos_state_t *state, const char *path)
{
    /* Icon banks use the same format as sprite banks, just with "AmIc" magic */
    return amos_load_sprite_bank(state, path);
}

/* ── Get sprite image as RGBA pixels ───────────────────────────────── */

uint32_t *amos_get_sprite_image(amos_state_t *state, int index,
                                 int *width, int *height)
{
    if (!state) return NULL;

    /* Delegate to the sprite engine which holds the loaded images */
    return amos_sprites_get_image(state, index, width, height);
}
