/*
 * test_bank_loader.c — Tests for AMOS sprite/icon bank (.abk) loader
 *
 * Creates minimal AmSp/AmIc banks in memory and verifies the loader
 * correctly parses sprite data, converts planar-to-chunky, and
 * applies the palette.
 */

#include "amos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { printf("  %-50s ", name); } while (0)

#define PASS() \
    do { printf("PASS\n"); tests_passed++; } while (0)

#define FAIL(msg) \
    do { printf("FAIL: %s\n", msg); tests_failed++; } while (0)

/* ── Big-endian write helpers ───────────────────────────────────────── */

static void write_be16(uint8_t *p, uint16_t v) {
    p[0] = (v >> 8) & 0xFF;
    p[1] = v & 0xFF;
}

/* ── Build a minimal AmSp bank with one 16x2 sprite, 2 bitplanes ──── */

/*
 * Creates a bank with 1 sprite:
 *   Width: 1 word (16 pixels)
 *   Height: 2 pixels
 *   Depth: 2 bitplanes
 *
 * Pixel data (interleaved planar):
 *   Row 0, plane 0: 0xFF 0x00 (pixels 0-7 have bit 0 set, 8-15 clear)
 *   Row 0, plane 1: 0x00 0xFF (pixels 0-7 have bit 1 clear, 8-15 set)
 *   Row 1, plane 0: 0xF0 0xF0 (alternating groups of 4)
 *   Row 1, plane 1: 0xF0 0xF0
 *
 * Expected colors:
 *   Row 0: pixels 0-7 = color 1, pixels 8-15 = color 2
 *   Row 1: pixels 0-3 = color 3, 4-7 = color 0, 8-11 = color 3, 12-15 = color 0
 *
 * Palette (after sprites):
 *   Color 0: $000 (black, transparent for sprites)
 *   Color 1: $F00 (red)
 *   Color 2: $0F0 (green)
 *   Color 3: $00F (blue)
 */
static uint8_t *build_test_bank_simple(int *out_size)
{
    int header_size = 4 + 2;  /* magic + count */
    int sprite_header = 10;   /* w,h,d,hx,hy */
    int image_data = 1 * 2 * 2 * 2;  /* width_words*2 * height * depth = 8 bytes */
    int palette_size = 64;    /* 32 colors x 2 bytes */

    int total = header_size + sprite_header + image_data + palette_size;
    uint8_t *buf = calloc(total, 1);
    int pos = 0;

    /* Magic: "AmSp" */
    buf[pos++] = 'A'; buf[pos++] = 'm'; buf[pos++] = 'S'; buf[pos++] = 'p';

    /* Number of sprites: 1 */
    write_be16(buf + pos, 1); pos += 2;

    /* Sprite header */
    write_be16(buf + pos, 1);  pos += 2;  /* width_words = 1 (16 pixels) */
    write_be16(buf + pos, 2);  pos += 2;  /* height = 2 */
    write_be16(buf + pos, 2);  pos += 2;  /* depth = 2 bitplanes */
    write_be16(buf + pos, 0);  pos += 2;  /* hotspot x = 0 */
    write_be16(buf + pos, 0);  pos += 2;  /* hotspot y = 0 */

    /* Image data (interleaved planar) */
    /* Row 0, plane 0 */
    buf[pos++] = 0xFF; buf[pos++] = 0x00;
    /* Row 0, plane 1 */
    buf[pos++] = 0x00; buf[pos++] = 0xFF;
    /* Row 1, plane 0 */
    buf[pos++] = 0xF0; buf[pos++] = 0xF0;
    /* Row 1, plane 1 */
    buf[pos++] = 0xF0; buf[pos++] = 0xF0;

    /* Palette: 32 colors x 2 bytes = 64 bytes */
    write_be16(buf + pos, 0x000); pos += 2;  /* color 0: black */
    write_be16(buf + pos, 0xF00); pos += 2;  /* color 1: red */
    write_be16(buf + pos, 0x0F0); pos += 2;  /* color 2: green */
    write_be16(buf + pos, 0x00F); pos += 2;  /* color 3: blue */
    /* Remaining colors: zeros (already calloc'd) */
    pos += 56;  /* 28 more colors x 2 bytes */

    *out_size = total;
    return buf;
}

/* ── Build a bank with an empty sprite ─────────────────────────────── */

static uint8_t *build_test_bank_empty_sprite(int *out_size)
{
    int total = 4 + 2 + 10 + 64;  /* magic + count + empty header + palette */
    uint8_t *buf = calloc(total, 1);
    int pos = 0;

    buf[pos++] = 'A'; buf[pos++] = 'm'; buf[pos++] = 'S'; buf[pos++] = 'p';
    write_be16(buf + pos, 1); pos += 2;

    /* Empty sprite: all zeros for 10 bytes */
    pos += 10;

    /* Palette */
    pos += 64;

    *out_size = total;
    return buf;
}

/* ── Build an icon bank ────────────────────────────────────────────── */

static uint8_t *build_test_icon_bank(int *out_size)
{
    int total = 4 + 2 + 10 + (1 * 2 * 1 * 1) + 64;  /* magic + count + header + data + palette */
    uint8_t *buf = calloc(total, 1);
    int pos = 0;

    /* Magic: "AmIc" */
    buf[pos++] = 'A'; buf[pos++] = 'm'; buf[pos++] = 'I'; buf[pos++] = 'c';
    write_be16(buf + pos, 1); pos += 2;

    /* 1 icon: 16x1, 1 bitplane */
    write_be16(buf + pos, 1);  pos += 2;  /* width_words */
    write_be16(buf + pos, 1);  pos += 2;  /* height */
    write_be16(buf + pos, 1);  pos += 2;  /* depth */
    write_be16(buf + pos, 8);  pos += 2;  /* hotspot x = 8 */
    write_be16(buf + pos, 0);  pos += 2;  /* hotspot y = 0 */

    /* 1 plane, 1 row, 2 bytes: all pixels set = color 1 */
    buf[pos++] = 0xFF; buf[pos++] = 0xFF;

    /* Palette */
    write_be16(buf + pos, 0x000); pos += 2;  /* color 0: black */
    write_be16(buf + pos, 0xFFF); pos += 2;  /* color 1: white */
    pos += 60;  /* remaining */

    *out_size = total;
    return buf;
}

/* ── Test: Simple sprite loading ───────────────────────────────────── */

static int test_bank_simple(void)
{
    int errors = 0;
    amos_state_t *state = amos_create();
    if (!state) { FAIL("amos_create"); return 1; }

    int size;
    uint8_t *data = build_test_bank_simple(&size);

    TEST("Load simple AmSp bank (1 sprite, 16x2, 2 planes)");
    int result = amos_load_sprite_bank_mem(state, data, size);
    if (result != 0) {
        FAIL(state->error_msg);
        errors++;
        free(data);
        amos_destroy(state);
        return errors;
    }
    PASS();

    /* Get sprite image */
    int w = 0, h = 0;
    uint32_t *pixels = amos_get_sprite_image(state, 1, &w, &h);

    TEST("Sprite dimensions: 16x2");
    if (w != 16 || h != 2) {
        char msg[64];
        snprintf(msg, sizeof(msg), "got %dx%d", w, h);
        FAIL(msg);
        errors++;
    } else {
        PASS();
    }

    TEST("Sprite pixel data not NULL");
    if (!pixels) {
        FAIL("NULL pixels");
        errors++;
        free(data);
        amos_destroy(state);
        return errors;
    }
    PASS();

    /* Row 0: pixels 0-7 should be color 1 (red), 8-15 should be color 2 (green) */
    /* Color 1: $F00 -> R=0xFF, G=0x00, B=0x00 -> AMOS_RGBA(0xFF,0x00,0x00,0xFF) */
    uint32_t red = AMOS_RGBA(0xFF, 0x00, 0x00, 0xFF);
    uint32_t green = AMOS_RGBA(0x00, 0xFF, 0x00, 0xFF);
    uint32_t blue = AMOS_RGBA(0x00, 0x00, 0xFF, 0xFF);
    uint32_t transparent = AMOS_RGBA(0x00, 0x00, 0x00, 0x00);

    TEST("Row 0, pixel 0: color 1 (red)");
    if (pixels[0] != red) {
        char msg[64];
        snprintf(msg, sizeof(msg), "got 0x%08X, expected 0x%08X", pixels[0], red);
        FAIL(msg);
        errors++;
    } else {
        PASS();
    }

    TEST("Row 0, pixel 8: color 2 (green)");
    if (pixels[8] != green) {
        char msg[64];
        snprintf(msg, sizeof(msg), "got 0x%08X, expected 0x%08X", pixels[8], green);
        FAIL(msg);
        errors++;
    } else {
        PASS();
    }

    TEST("Row 1, pixel 0: color 3 (blue)");
    if (pixels[16] != blue) {
        char msg[64];
        snprintf(msg, sizeof(msg), "got 0x%08X, expected 0x%08X", pixels[16], blue);
        FAIL(msg);
        errors++;
    } else {
        PASS();
    }

    TEST("Row 1, pixel 4: color 0 (transparent)");
    if (pixels[20] != transparent) {
        char msg[64];
        snprintf(msg, sizeof(msg), "got 0x%08X, expected 0x%08X", pixels[20], transparent);
        FAIL(msg);
        errors++;
    } else {
        PASS();
    }

    free(data);
    amos_destroy(state);
    return errors;
}

/* ── Test: Empty sprite ────────────────────────────────────────────── */

static int test_bank_empty_sprite(void)
{
    int errors = 0;
    amos_state_t *state = amos_create();
    if (!state) { FAIL("amos_create"); return 1; }

    int size;
    uint8_t *data = build_test_bank_empty_sprite(&size);

    TEST("Load bank with empty sprite");
    int result = amos_load_sprite_bank_mem(state, data, size);
    if (result != 0) {
        FAIL(state->error_msg);
        errors++;
    } else {
        PASS();
    }

    /* Empty sprite should still be retrievable */
    int w = 0, h = 0;
    uint32_t *pixels = amos_get_sprite_image(state, 1, &w, &h);

    TEST("Empty sprite has valid dimensions");
    if (w <= 0 || h <= 0) {
        FAIL("invalid dimensions for empty sprite");
        errors++;
    } else {
        PASS();
    }

    TEST("Empty sprite pixels not NULL");
    if (!pixels) {
        FAIL("NULL");
        errors++;
    } else {
        PASS();
    }

    free(data);
    amos_destroy(state);
    return errors;
}

/* ── Test: Icon bank (AmIc magic) ──────────────────────────────────── */

static int test_bank_icons(void)
{
    int errors = 0;
    amos_state_t *state = amos_create();
    if (!state) { FAIL("amos_create"); return 1; }

    int size;
    uint8_t *data = build_test_icon_bank(&size);

    TEST("Load AmIc icon bank");
    int result = amos_load_sprite_bank_mem(state, data, size);
    if (result != 0) {
        FAIL(state->error_msg);
        errors++;
    } else {
        PASS();
    }

    int w = 0, h = 0;
    uint32_t *pixels = amos_get_sprite_image(state, 1, &w, &h);

    TEST("Icon dimensions: 16x1");
    if (w != 16 || h != 1) {
        char msg[64];
        snprintf(msg, sizeof(msg), "got %dx%d", w, h);
        FAIL(msg);
        errors++;
    } else {
        PASS();
    }

    /* All 16 pixels should be color 1 (white) */
    TEST("Icon pixels all white");
    if (pixels) {
        uint32_t white = AMOS_RGBA(0xFF, 0xFF, 0xFF, 0xFF);
        bool ok = true;
        for (int x = 0; x < 16; x++) {
            if (pixels[x] != white) { ok = false; break; }
        }
        if (ok) PASS(); else { FAIL("pixel mismatch"); errors++; }
    } else {
        FAIL("NULL pixels");
        errors++;
    }

    free(data);
    amos_destroy(state);
    return errors;
}

/* ── Test: Error handling ──────────────────────────────────────────── */

static int test_bank_errors(void)
{
    int errors = 0;
    amos_state_t *state = amos_create();

    TEST("Reject NULL data");
    if (amos_load_sprite_bank_mem(state, NULL, 0) != -1) {
        FAIL("should reject"); errors++;
    } else PASS();

    TEST("Reject too-short data");
    {
        uint8_t tiny[4] = { 'A', 'm', 'S', 'p' };
        if (amos_load_sprite_bank_mem(state, tiny, 4) != -1) {
            FAIL("should reject"); errors++;
        } else PASS();
    }

    TEST("Reject invalid magic");
    {
        uint8_t bad[80];
        memset(bad, 0, sizeof(bad));
        memcpy(bad, "XXXX", 4);
        bad[4] = 0; bad[5] = 1;
        if (amos_load_sprite_bank_mem(state, bad, sizeof(bad)) != -1) {
            FAIL("should reject"); errors++;
        } else PASS();
    }

    TEST("Reject truncated sprite data");
    {
        /* Header says 1 sprite, 16x16, 4 planes, but data is too short */
        uint8_t trunc[20];
        memset(trunc, 0, sizeof(trunc));
        memcpy(trunc, "AmSp", 4);
        write_be16(trunc + 4, 1);   /* 1 sprite */
        write_be16(trunc + 6, 1);   /* width_words = 1 */
        write_be16(trunc + 8, 16);  /* height = 16 */
        write_be16(trunc + 10, 4);  /* depth = 4 */
        /* Only 20 bytes total, but would need 6 + 10 + 128 + 64 */
        if (amos_load_sprite_bank_mem(state, trunc, sizeof(trunc)) != -1) {
            FAIL("should reject"); errors++;
        } else PASS();
    }

    amos_destroy(state);
    return errors;
}

/* ── Test: Hotspot masking ─────────────────────────────────────────── */

static int test_bank_hotspot(void)
{
    int errors = 0;
    amos_state_t *state = amos_create();
    if (!state) { FAIL("amos_create"); return 1; }

    /* Build bank with hotspot flags in bits 14-15 */
    int total = 4 + 2 + 10 + (1 * 2 * 1 * 1) + 64;
    uint8_t *buf = calloc(total, 1);
    int pos = 0;

    memcpy(buf, "AmSp", 4); pos += 4;
    write_be16(buf + pos, 1); pos += 2;

    write_be16(buf + pos, 1);      pos += 2;  /* width_words */
    write_be16(buf + pos, 1);      pos += 2;  /* height */
    write_be16(buf + pos, 1);      pos += 2;  /* depth */
    write_be16(buf + pos, 0xC008); pos += 2;  /* hotspot x = 8 with flags in bits 14-15 */
    write_be16(buf + pos, 0x8004); pos += 2;  /* hotspot y = 4 with flag in bit 15 */

    buf[pos++] = 0xFF; buf[pos++] = 0xFF;  /* image data */

    /* Palette */
    write_be16(buf + pos, 0x000); pos += 2;
    write_be16(buf + pos, 0xFFF); pos += 2;
    /* remaining zeros */

    TEST("Hotspot masking (bits 14-15 stripped)");
    int result = amos_load_sprite_bank_mem(state, buf, total);
    if (result != 0) {
        FAIL(state->error_msg);
        errors++;
    } else {
        /* Set a sprite with this image and check hotspot */
        amos_sprite_set(state, 0, 100, 100, 1);
        if (state->sprites[0].hot_x == 8 && state->sprites[0].hot_y == 4) {
            PASS();
        } else {
            char msg[64];
            snprintf(msg, sizeof(msg), "hot=(%d,%d), expected (8,4)",
                     state->sprites[0].hot_x, state->sprites[0].hot_y);
            FAIL(msg);
            errors++;
        }
    }

    free(buf);
    amos_destroy(state);
    return errors;
}

/* ── Test runner ───────────────────────────────────────────────────── */

int test_bank_loader_all(void)
{
    tests_passed = 0;
    tests_failed = 0;

    int errors = 0;
    errors += test_bank_simple();
    errors += test_bank_empty_sprite();
    errors += test_bank_icons();
    errors += test_bank_errors();
    errors += test_bank_hotspot();

    printf("  Bank loader tests: %d passed, %d failed\n",
           tests_passed, tests_failed);

    return errors;
}
