/*
 * test_iff.c — Tests for IFF/ILBM image loader
 *
 * Creates minimal IFF/ILBM files in memory and verifies the loader
 * correctly parses them into screen pixels.
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

static void write_be32(uint8_t *p, uint32_t v) {
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

/* ── Build a minimal 8x8 uncompressed IFF/ILBM ─────────────────────── */

/*
 * Creates an 8x8 image with 4 bitplanes (16 colors), uncompressed.
 * Pixel colors are arranged so we can verify the planar-to-chunky conversion.
 *
 * Row 0: all color 0 (black)
 * Row 1: all color 1 (blue)
 * Row 2: all color 2 (green)
 * Row 3: all color 5 (magenta)
 * Row 4: all color 15 (white)
 * Row 5-7: all color 0
 *
 * With 4 planes and 8 pixels wide, each plane row is 1 byte (8 bits).
 * But IFF requires word alignment: 2 bytes per plane row.
 *
 * For all-same-color rows, the plane data is straightforward:
 *   color N: plane P bit = (N >> P) & 1, replicated 8 times = 0xFF or 0x00
 */
static uint8_t *build_test_iff_uncompressed(int *out_size)
{
    int width = 8, height = 8, planes = 4;
    int row_bytes = 2;  /* word-aligned: ceil(8/16)*2 = 2 */
    int body_size = row_bytes * planes * height;

    /* CMAP: 16 colors x 3 bytes */
    int cmap_size = 16 * 3;

    /* Total file size */
    int form_content = 4  /* "ILBM" */
        + 8 + 20          /* BMHD chunk */
        + 8 + cmap_size   /* CMAP chunk */
        + 8 + body_size;  /* BODY chunk */

    int file_size = 8 + form_content;  /* "FORM" + size + content */

    uint8_t *buf = calloc(file_size, 1);
    int pos = 0;

    /* FORM header */
    memcpy(buf + pos, "FORM", 4); pos += 4;
    write_be32(buf + pos, form_content); pos += 4;
    memcpy(buf + pos, "ILBM", 4); pos += 4;

    /* BMHD chunk */
    memcpy(buf + pos, "BMHD", 4); pos += 4;
    write_be32(buf + pos, 20); pos += 4;
    write_be16(buf + pos, width); pos += 2;       /* width */
    write_be16(buf + pos, height); pos += 2;      /* height */
    write_be16(buf + pos, 0); pos += 2;           /* x_origin */
    write_be16(buf + pos, 0); pos += 2;           /* y_origin */
    buf[pos++] = planes;                           /* num_planes */
    buf[pos++] = 0;                                /* masking (none) */
    buf[pos++] = 0;                                /* compression (none) */
    buf[pos++] = 0;                                /* pad */
    write_be16(buf + pos, 0); pos += 2;           /* transparent_color */
    buf[pos++] = 10;                               /* x_aspect */
    buf[pos++] = 11;                               /* y_aspect */
    write_be16(buf + pos, 320); pos += 2;         /* page_width */
    write_be16(buf + pos, 200); pos += 2;         /* page_height */

    /* CMAP chunk: define 16 colors */
    memcpy(buf + pos, "CMAP", 4); pos += 4;
    write_be32(buf + pos, cmap_size); pos += 4;

    /* Color 0: black (0x00, 0x00, 0x00) */
    buf[pos++] = 0x00; buf[pos++] = 0x00; buf[pos++] = 0x00;
    /* Color 1: blue (0x00, 0x00, 0xA0) */
    buf[pos++] = 0x00; buf[pos++] = 0x00; buf[pos++] = 0xA0;
    /* Color 2: green (0x00, 0xA0, 0x00) */
    buf[pos++] = 0x00; buf[pos++] = 0xA0; buf[pos++] = 0x00;
    /* Color 3: cyan */
    buf[pos++] = 0x00; buf[pos++] = 0xA0; buf[pos++] = 0xA0;
    /* Color 4: red */
    buf[pos++] = 0xA0; buf[pos++] = 0x00; buf[pos++] = 0x00;
    /* Color 5: magenta (0xA0, 0x00, 0xA0) */
    buf[pos++] = 0xA0; buf[pos++] = 0x00; buf[pos++] = 0xA0;
    /* Colors 6-14: gray shades */
    for (int i = 6; i < 15; i++) {
        uint8_t v = (uint8_t)(i * 0x10);
        buf[pos++] = v; buf[pos++] = v; buf[pos++] = v;
    }
    /* Color 15: white (0xF0, 0xF0, 0xF0) */
    buf[pos++] = 0xF0; buf[pos++] = 0xF0; buf[pos++] = 0xF0;

    /* BODY chunk */
    memcpy(buf + pos, "BODY", 4); pos += 4;
    write_be32(buf + pos, body_size); pos += 4;

    /* Row colors: 0, 1, 2, 5, 15, 0, 0, 0 */
    int row_colors[8] = {0, 1, 2, 5, 15, 0, 0, 0};

    for (int y = 0; y < height; y++) {
        int color = row_colors[y];
        for (int p = 0; p < planes; p++) {
            /* If bit p of color is set, all 8 pixels in this plane row are 1 */
            uint8_t plane_byte = ((color >> p) & 1) ? 0xFF : 0x00;
            buf[pos++] = plane_byte;
            buf[pos++] = 0x00;  /* padding to word boundary */
        }
    }

    *out_size = file_size;
    return buf;
}

/* ── Build a ByteRun1 compressed IFF/ILBM ──────────────────────────── */

static uint8_t *build_test_iff_compressed(int *out_size)
{
    int width = 8, height = 4, planes = 2;
    (void)0;  /* row_bytes = 2 (word-aligned) used implicitly */

    /* Uncompressed body: 4 rows x 2 planes x 2 bytes = 16 bytes
     * Row colors: 0, 1, 2, 3
     * Compress with ByteRun1: for each 2-byte plane row, use a repeat run.
     */

    /* Build compressed body */
    uint8_t compressed_body[128];
    int cpos = 0;

    int row_colors[4] = {0, 1, 2, 3};
    for (int y = 0; y < height; y++) {
        int color = row_colors[y];
        for (int p = 0; p < planes; p++) {
            uint8_t plane_byte = ((color >> p) & 1) ? 0xFF : 0x00;
            /* Repeat run: -1 means repeat next byte 2 times */
            compressed_body[cpos++] = (uint8_t)(int8_t)(-1);
            compressed_body[cpos++] = plane_byte;
        }
    }

    int compressed_size = cpos;

    /* CMAP: 4 colors */
    int cmap_size = 4 * 3;

    int form_content = 4 + 8 + 20 + 8 + cmap_size + 8 + compressed_size;
    int file_size = 8 + form_content;

    uint8_t *buf = calloc(file_size, 1);
    int pos = 0;

    /* FORM + ILBM */
    memcpy(buf + pos, "FORM", 4); pos += 4;
    write_be32(buf + pos, form_content); pos += 4;
    memcpy(buf + pos, "ILBM", 4); pos += 4;

    /* BMHD */
    memcpy(buf + pos, "BMHD", 4); pos += 4;
    write_be32(buf + pos, 20); pos += 4;
    write_be16(buf + pos, width); pos += 2;
    write_be16(buf + pos, height); pos += 2;
    write_be16(buf + pos, 0); pos += 2;
    write_be16(buf + pos, 0); pos += 2;
    buf[pos++] = planes;
    buf[pos++] = 0;    /* no masking */
    buf[pos++] = 1;    /* ByteRun1 compression */
    buf[pos++] = 0;
    write_be16(buf + pos, 0); pos += 2;
    buf[pos++] = 10; buf[pos++] = 11;
    write_be16(buf + pos, 320); pos += 2;
    write_be16(buf + pos, 200); pos += 2;

    /* CMAP: 4 colors */
    memcpy(buf + pos, "CMAP", 4); pos += 4;
    write_be32(buf + pos, cmap_size); pos += 4;
    buf[pos++] = 0x00; buf[pos++] = 0x00; buf[pos++] = 0x00; /* black */
    buf[pos++] = 0x00; buf[pos++] = 0x00; buf[pos++] = 0xA0; /* blue */
    buf[pos++] = 0x00; buf[pos++] = 0xA0; buf[pos++] = 0x00; /* green */
    buf[pos++] = 0x00; buf[pos++] = 0xA0; buf[pos++] = 0xA0; /* cyan */

    /* BODY */
    memcpy(buf + pos, "BODY", 4); pos += 4;
    write_be32(buf + pos, compressed_size); pos += 4;
    memcpy(buf + pos, compressed_body, compressed_size);
    pos += compressed_size;

    *out_size = pos;
    return buf;
}

/* ── Test: Uncompressed IFF loading ─────────────────────────────────── */

static int test_iff_uncompressed(void)
{
    int errors = 0;
    amos_state_t *state = amos_create();
    if (!state) { FAIL("amos_create"); return 1; }

    int size;
    uint8_t *data = build_test_iff_uncompressed(&size);

    TEST("Load uncompressed 8x8 IFF/ILBM");
    int result = amos_load_iff_from_memory(state, data, size, 0);
    if (result != 0) {
        FAIL(state->error_msg);
        errors++;
        free(data);
        amos_destroy(state);
        return errors;
    }
    PASS();

    amos_screen_t *scr = &state->screens[0];

    TEST("Screen dimensions 8x8");
    if (scr->width != 8 || scr->height != 8) {
        FAIL("wrong dimensions");
        errors++;
    } else {
        PASS();
    }

    /* Verify row 0: all black (color 0 = 0x00,0x00,0x00) */
    TEST("Row 0: all color 0 (black)");
    {
        uint32_t expected = AMOS_RGBA(0x00, 0x00, 0x00, 0xFF);
        bool ok = true;
        for (int x = 0; x < 8; x++) {
            if (scr->pixels[0 * 8 + x] != expected) { ok = false; break; }
        }
        if (ok) PASS(); else { FAIL("pixel mismatch"); errors++; }
    }

    /* Verify row 1: all blue (color 1 = 0x00,0x00,0xAA) */
    TEST("Row 1: all color 1 (blue)");
    {
        uint32_t expected = AMOS_RGBA(0x00, 0x00, 0xAA, 0xFF);
        bool ok = true;
        for (int x = 0; x < 8; x++) {
            if (scr->pixels[1 * 8 + x] != expected) { ok = false; break; }
        }
        if (ok) PASS(); else { FAIL("pixel mismatch"); errors++; }
    }

    /* Verify row 2: all green (color 2 = 0x00,0xAA,0x00) */
    TEST("Row 2: all color 2 (green)");
    {
        uint32_t expected = AMOS_RGBA(0x00, 0xAA, 0x00, 0xFF);
        bool ok = true;
        for (int x = 0; x < 8; x++) {
            if (scr->pixels[2 * 8 + x] != expected) { ok = false; break; }
        }
        if (ok) PASS(); else { FAIL("pixel mismatch"); errors++; }
    }

    /* Verify row 3: all magenta (color 5 = 0xAA,0x00,0xAA) */
    TEST("Row 3: all color 5 (magenta)");
    {
        uint32_t expected = AMOS_RGBA(0xAA, 0x00, 0xAA, 0xFF);
        bool ok = true;
        for (int x = 0; x < 8; x++) {
            if (scr->pixels[3 * 8 + x] != expected) { ok = false; break; }
        }
        if (ok) PASS(); else { FAIL("pixel mismatch"); errors++; }
    }

    /* Verify row 4: all white (color 15 = 0xFF,0xFF,0xFF) */
    TEST("Row 4: all color 15 (white)");
    {
        uint32_t expected = AMOS_RGBA(0xFF, 0xFF, 0xFF, 0xFF);
        bool ok = true;
        for (int x = 0; x < 8; x++) {
            if (scr->pixels[4 * 8 + x] != expected) { ok = false; break; }
        }
        if (ok) PASS(); else { FAIL("pixel mismatch"); errors++; }
    }

    /* Verify palette color 5 */
    TEST("Palette color 5 = magenta");
    {
        uint32_t expected = AMOS_RGBA(0xAA, 0x00, 0xAA, 0xFF);
        if (scr->palette[5] == expected) PASS();
        else { FAIL("palette mismatch"); errors++; }
    }

    free(data);
    amos_destroy(state);
    return errors;
}

/* ── Test: Compressed IFF loading ───────────────────────────────────── */

static int test_iff_compressed(void)
{
    int errors = 0;
    amos_state_t *state = amos_create();
    if (!state) { FAIL("amos_create"); return 1; }

    int size;
    uint8_t *data = build_test_iff_compressed(&size);

    TEST("Load ByteRun1 compressed 8x4 IFF/ILBM");
    int result = amos_load_iff_from_memory(state, data, size, 0);
    if (result != 0) {
        FAIL(state->error_msg);
        errors++;
        free(data);
        amos_destroy(state);
        return errors;
    }
    PASS();

    amos_screen_t *scr = &state->screens[0];

    TEST("Compressed: screen dimensions 8x4");
    if (scr->width != 8 || scr->height != 4) {
        FAIL("wrong dimensions");
        errors++;
    } else {
        PASS();
    }

    /* Row 0: color 0 (black) */
    TEST("Compressed: row 0 = black");
    {
        uint32_t expected = AMOS_RGBA(0x00, 0x00, 0x00, 0xFF);
        bool ok = true;
        for (int x = 0; x < 8; x++) {
            if (scr->pixels[0 * 8 + x] != expected) { ok = false; break; }
        }
        if (ok) PASS(); else { FAIL("pixel mismatch"); errors++; }
    }

    /* Row 1: color 1 (blue) */
    TEST("Compressed: row 1 = blue");
    {
        uint32_t expected = AMOS_RGBA(0x00, 0x00, 0xAA, 0xFF);
        bool ok = true;
        for (int x = 0; x < 8; x++) {
            if (scr->pixels[1 * 8 + x] != expected) { ok = false; break; }
        }
        if (ok) PASS(); else { FAIL("pixel mismatch"); errors++; }
    }

    /* Row 2: color 2 (green) */
    TEST("Compressed: row 2 = green");
    {
        uint32_t expected = AMOS_RGBA(0x00, 0xAA, 0x00, 0xFF);
        bool ok = true;
        for (int x = 0; x < 8; x++) {
            if (scr->pixels[2 * 8 + x] != expected) { ok = false; break; }
        }
        if (ok) PASS(); else { FAIL("pixel mismatch"); errors++; }
    }

    /* Row 3: color 3 (cyan) */
    TEST("Compressed: row 3 = cyan");
    {
        uint32_t expected = AMOS_RGBA(0x00, 0xAA, 0xAA, 0xFF);
        bool ok = true;
        for (int x = 0; x < 8; x++) {
            if (scr->pixels[3 * 8 + x] != expected) { ok = false; break; }
        }
        if (ok) PASS(); else { FAIL("pixel mismatch"); errors++; }
    }

    free(data);
    amos_destroy(state);
    return errors;
}

/* ── Test: Error handling ───────────────────────────────────────────── */

static int test_iff_errors(void)
{
    int errors = 0;
    amos_state_t *state = amos_create();

    TEST("Reject NULL data");
    if (amos_load_iff_from_memory(state, NULL, 0, 0) != -1) {
        FAIL("should reject"); errors++;
    } else PASS();

    TEST("Reject too-short data");
    {
        uint8_t tiny[4] = {0};
        if (amos_load_iff_from_memory(state, tiny, 4, 0) != -1) {
            FAIL("should reject"); errors++;
        } else PASS();
    }

    TEST("Reject non-FORM header");
    {
        uint8_t bad[12];
        memcpy(bad, "NOTF\x00\x00\x00\x04ILBM", 12);
        if (amos_load_iff_from_memory(state, bad, 12, 0) != -1) {
            FAIL("should reject"); errors++;
        } else PASS();
    }

    TEST("Reject non-ILBM type");
    {
        uint8_t bad[12] = "FORM\x00\x00\x00\x04ANIM";
        if (amos_load_iff_from_memory(state, bad, 12, 0) != -1) {
            FAIL("should reject"); errors++;
        } else PASS();
    }

    TEST("Reject file without BMHD");
    {
        /* FORM + ILBM + empty BODY */
        uint8_t no_bmhd[20];
        int p = 0;
        memcpy(no_bmhd + p, "FORM", 4); p += 4;
        no_bmhd[p++] = 0; no_bmhd[p++] = 0; no_bmhd[p++] = 0; no_bmhd[p++] = 12;
        memcpy(no_bmhd + p, "ILBM", 4); p += 4;
        memcpy(no_bmhd + p, "BODY", 4); p += 4;
        no_bmhd[p++] = 0; no_bmhd[p++] = 0; no_bmhd[p++] = 0; no_bmhd[p++] = 0;
        if (amos_load_iff_from_memory(state, no_bmhd, p, 0) != -1) {
            FAIL("should reject"); errors++;
        } else PASS();
    }

    TEST("Reject invalid screen ID");
    {
        int size;
        uint8_t *data = build_test_iff_uncompressed(&size);
        if (amos_load_iff_from_memory(state, data, size, 99) != -1) {
            FAIL("should reject"); errors++;
        } else PASS();
        free(data);
    }

    amos_destroy(state);
    return errors;
}

/* ── Test: Mixed pixel pattern ──────────────────────────────────────── */

static int test_iff_mixed_pixels(void)
{
    int errors = 0;
    amos_state_t *state = amos_create();

    /*
     * Build an 8x1 image with 2 planes where each pixel has a different color:
     * Pixel 0=0, 1=1, 2=2, 3=3, 4=0, 5=1, 6=2, 7=3
     *
     * Plane 0 (bit 0): 0,1,0,1,0,1,0,1 = 0b01010101 = 0x55
     * Plane 1 (bit 1): 0,0,1,1,0,0,1,1 = 0b00110011 = 0x33
     */
    int width = 8, height = 1, planes = 2;
    int row_bytes = 2;
    int body_size = row_bytes * planes * height;
    int cmap_size = 4 * 3;

    int form_content = 4 + 8 + 20 + 8 + cmap_size + 8 + body_size;
    int file_size = 8 + form_content;
    uint8_t *buf = calloc(file_size, 1);
    int pos = 0;

    memcpy(buf + pos, "FORM", 4); pos += 4;
    write_be32(buf + pos, form_content); pos += 4;
    memcpy(buf + pos, "ILBM", 4); pos += 4;

    /* BMHD */
    memcpy(buf + pos, "BMHD", 4); pos += 4;
    write_be32(buf + pos, 20); pos += 4;
    write_be16(buf + pos, width); pos += 2;
    write_be16(buf + pos, height); pos += 2;
    write_be16(buf + pos, 0); pos += 2;
    write_be16(buf + pos, 0); pos += 2;
    buf[pos++] = planes;
    buf[pos++] = 0; buf[pos++] = 0; buf[pos++] = 0;
    write_be16(buf + pos, 0); pos += 2;
    buf[pos++] = 10; buf[pos++] = 11;
    write_be16(buf + pos, 320); pos += 2;
    write_be16(buf + pos, 200); pos += 2;

    /* CMAP */
    memcpy(buf + pos, "CMAP", 4); pos += 4;
    write_be32(buf + pos, cmap_size); pos += 4;
    buf[pos++] = 0x00; buf[pos++] = 0x00; buf[pos++] = 0x00; /* 0: black */
    buf[pos++] = 0xF0; buf[pos++] = 0x00; buf[pos++] = 0x00; /* 1: red */
    buf[pos++] = 0x00; buf[pos++] = 0xF0; buf[pos++] = 0x00; /* 2: green */
    buf[pos++] = 0x00; buf[pos++] = 0x00; buf[pos++] = 0xF0; /* 3: blue */

    /* BODY */
    memcpy(buf + pos, "BODY", 4); pos += 4;
    write_be32(buf + pos, body_size); pos += 4;
    buf[pos++] = 0x55; buf[pos++] = 0x00;  /* plane 0 */
    buf[pos++] = 0x33; buf[pos++] = 0x00;  /* plane 1 */

    TEST("Mixed pixel pattern: planar-to-chunky accuracy");
    int result = amos_load_iff_from_memory(state, buf, file_size, 0);
    if (result != 0) {
        FAIL(state->error_msg);
        errors++;
        free(buf);
        amos_destroy(state);
        return errors;
    }

    amos_screen_t *scr = &state->screens[0];

    /* Expected colors: 0(black), 1(red), 2(green), 3(blue), 0, 1, 2, 3 */
    uint32_t expected[8] = {
        AMOS_RGBA(0x00, 0x00, 0x00, 0xFF),  /* 0: black */
        AMOS_RGBA(0xFF, 0x00, 0x00, 0xFF),  /* 1: red */
        AMOS_RGBA(0x00, 0xFF, 0x00, 0xFF),  /* 2: green */
        AMOS_RGBA(0x00, 0x00, 0xFF, 0xFF),  /* 3: blue */
        AMOS_RGBA(0x00, 0x00, 0x00, 0xFF),  /* 0: black */
        AMOS_RGBA(0xFF, 0x00, 0x00, 0xFF),  /* 1: red */
        AMOS_RGBA(0x00, 0xFF, 0x00, 0xFF),  /* 2: green */
        AMOS_RGBA(0x00, 0x00, 0xFF, 0xFF),  /* 3: blue */
    };

    bool ok = true;
    for (int x = 0; x < 8; x++) {
        if (scr->pixels[x] != expected[x]) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "pixel %d: got 0x%08X, expected 0x%08X",
                     x, scr->pixels[x], expected[x]);
            FAIL(msg);
            errors++;
            ok = false;
            break;
        }
    }
    if (ok) PASS();

    free(buf);
    amos_destroy(state);
    return errors;
}

/* ── Test runner ────────────────────────────────────────────────────── */

int test_iff_all(void)
{
    tests_passed = 0;
    tests_failed = 0;

    int errors = 0;
    errors += test_iff_uncompressed();
    errors += test_iff_compressed();
    errors += test_iff_errors();
    errors += test_iff_mixed_pixels();

    printf("  IFF tests: %d passed, %d failed\n",
           tests_passed, tests_failed);

    return errors;
}
