/*
 * iff_loader.c — IFF/ILBM image loader for AMOS Reborn
 *
 * Loads Amiga-format IFF/ILBM images into AMOS screens.
 * Handles BMHD, CMAP, and BODY chunks with ByteRun1 decompression
 * and planar-to-chunky pixel conversion.
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

/* ── IFF chunk IDs ──────────────────────────────────────────────────── */

#define ID_FORM 0x464F524D  /* "FORM" */
#define ID_ILBM 0x494C424D  /* "ILBM" */
#define ID_BMHD 0x424D4844  /* "BMHD" */
#define ID_CMAP 0x434D4150  /* "CMAP" */
#define ID_BODY 0x424F4459  /* "BODY" */

/* ── BMHD structure ─────────────────────────────────────────────────── */

typedef struct {
    uint16_t width;
    uint16_t height;
    int16_t  x_origin;
    int16_t  y_origin;
    uint8_t  num_planes;
    uint8_t  masking;       /* 0=none, 1=has mask, 2=transparent color, 3=lasso */
    uint8_t  compression;   /* 0=none, 1=ByteRun1 */
    uint8_t  pad;
    uint16_t transparent_color;
    uint8_t  x_aspect;
    uint8_t  y_aspect;
    uint16_t page_width;
    uint16_t page_height;
} iff_bmhd_t;

/* ── ByteRun1 (PackBits) decompression ──────────────────────────────── */

/*
 * Decompress ByteRun1 data.
 * Returns number of bytes written to output, or -1 on error.
 */
static int byterun1_decompress(const uint8_t *src, int src_len,
                               uint8_t *dst, int dst_len)
{
    int si = 0, di = 0;

    while (si < src_len && di < dst_len) {
        int8_t n = (int8_t)src[si++];

        if (n >= 0) {
            /* Literal run: copy next n+1 bytes */
            int count = n + 1;
            if (si + count > src_len || di + count > dst_len)
                return -1;
            memcpy(dst + di, src + si, count);
            si += count;
            di += count;
        } else if (n != -128) {
            /* Repeat run: repeat next byte (-n+1) times */
            int count = -n + 1;
            if (si >= src_len || di + count > dst_len)
                return -1;
            uint8_t val = src[si++];
            memset(dst + di, val, count);
            di += count;
        }
        /* n == -128: no-op, skip */
    }

    return di;
}

/* ── Planar to chunky conversion ────────────────────────────────────── */

/*
 * Convert interleaved bitplane data to chunky (indexed) pixel format.
 * planes_data: row-interleaved bitplane data (plane0 row, plane1 row, ...)
 * chunky: output buffer (1 byte per pixel)
 * width, height: image dimensions
 * num_planes: number of bitplanes
 * has_mask: if true, there's an extra mask plane after the image planes
 */
static void planar_to_chunky(const uint8_t *planes_data, uint8_t *chunky,
                              int width, int height, int num_planes, bool has_mask)
{
    int row_bytes = ((width + 15) / 16) * 2;  /* bytes per plane row, word-aligned */
    int total_planes = num_planes + (has_mask ? 1 : 0);
    int src_row_size = row_bytes * total_planes;  /* all planes for one row */

    for (int y = 0; y < height; y++) {
        const uint8_t *row_base = planes_data + y * src_row_size;

        for (int x = 0; x < width; x++) {
            int byte_idx = x / 8;
            int bit_idx  = 7 - (x % 8);
            uint8_t color = 0;

            for (int p = 0; p < num_planes; p++) {
                const uint8_t *plane_row = row_base + p * row_bytes;
                if (plane_row[byte_idx] & (1 << bit_idx))
                    color |= (1 << p);
            }

            chunky[y * width + x] = color;
        }
    }
}

/* ── Parse BMHD chunk ───────────────────────────────────────────────── */

static int parse_bmhd(const uint8_t *data, int len, iff_bmhd_t *bmhd)
{
    if (len < 20) return -1;

    bmhd->width             = BE16(data + 0);
    bmhd->height            = BE16(data + 2);
    bmhd->x_origin          = (int16_t)BE16(data + 4);
    bmhd->y_origin          = (int16_t)BE16(data + 6);
    bmhd->num_planes        = data[8];
    bmhd->masking           = data[9];
    bmhd->compression       = data[10];
    bmhd->pad               = data[11];
    bmhd->transparent_color = BE16(data + 12);
    bmhd->x_aspect          = data[14];
    bmhd->y_aspect          = data[15];
    bmhd->page_width        = BE16(data + 16);
    bmhd->page_height       = BE16(data + 18);

    return 0;
}

/* ── Parse CMAP into palette ────────────────────────────────────────── */

/*
 * Convert IFF CMAP RGB triplets to AMOS RGBA palette entries.
 * Each byte's high nibble is the significant value (Amiga OCS 4-bit color).
 * We expand to full 8-bit by replicating the nibble: 0xA -> 0xAA.
 */
static int parse_cmap(const uint8_t *data, int len, uint32_t *palette, int max_colors)
{
    int num_colors = len / 3;
    if (num_colors > max_colors) num_colors = max_colors;

    for (int i = 0; i < num_colors; i++) {
        uint8_t r = data[i * 3 + 0];
        uint8_t g = data[i * 3 + 1];
        uint8_t b = data[i * 3 + 2];

        /* Amiga OCS colors: high nibble is significant.
         * Expand to 8-bit by replicating: 0xA0 -> 0xAA */
        r = (r & 0xF0) | ((r & 0xF0) >> 4);
        g = (g & 0xF0) | ((g & 0xF0) >> 4);
        b = (b & 0xF0) | ((b & 0xF0) >> 4);

        palette[i] = AMOS_RGBA(r, g, b, 0xFF);
    }

    return num_colors;
}

/* ── Internal: load IFF from memory buffer ──────────────────────────── */

int amos_load_iff_from_memory(amos_state_t *state, const uint8_t *data,
                               size_t size, int screen_id)
{
    if (!state || !data || size < 12) return -1;

    /* Verify FORM header */
    uint32_t form_id = BE32(data);
    if (form_id != ID_FORM) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "IFF: Not a FORM file");
        return -1;
    }

    uint32_t form_size = BE32(data + 4);
    (void)form_size;  /* We use 'size' for bounds checking */

    /* Verify ILBM type */
    uint32_t ilbm_id = BE32(data + 8);
    if (ilbm_id != ID_ILBM) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "IFF: Not an ILBM file (type: %c%c%c%c)",
                 (char)(ilbm_id >> 24), (char)(ilbm_id >> 16),
                 (char)(ilbm_id >> 8), (char)ilbm_id);
        return -1;
    }

    /* Parse chunks */
    iff_bmhd_t bmhd;
    memset(&bmhd, 0, sizeof(bmhd));
    bool have_bmhd = false;

    uint32_t palette[256];
    int palette_count = 0;

    const uint8_t *body_data = NULL;
    uint32_t body_size = 0;

    size_t pos = 12;  /* Skip "FORM" + size + "ILBM" */

    while (pos + 8 <= size) {
        uint32_t chunk_id   = BE32(data + pos);
        uint32_t chunk_size = BE32(data + pos + 4);
        const uint8_t *chunk_data = data + pos + 8;

        /* Bounds check */
        if (pos + 8 + chunk_size > size) break;

        switch (chunk_id) {
            case ID_BMHD:
                if (parse_bmhd(chunk_data, chunk_size, &bmhd) == 0)
                    have_bmhd = true;
                break;

            case ID_CMAP:
                palette_count = parse_cmap(chunk_data, chunk_size, palette, 256);
                break;

            case ID_BODY:
                body_data = chunk_data;
                body_size = chunk_size;
                break;

            default:
                /* Skip unknown chunks (CAMG, CCRT, AMSC, etc.) */
                break;
        }

        /* Advance to next chunk (pad to even boundary) */
        pos += 8 + chunk_size;
        if (pos & 1) pos++;
    }

    /* Validate we got what we need */
    if (!have_bmhd) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "IFF: Missing BMHD chunk");
        return -1;
    }

    if (!body_data) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "IFF: Missing BODY chunk");
        return -1;
    }

    if (bmhd.width == 0 || bmhd.height == 0 || bmhd.num_planes == 0) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "IFF: Invalid dimensions %dx%d, %d planes",
                 bmhd.width, bmhd.height, bmhd.num_planes);
        return -1;
    }

    /* Open/resize the target screen */
    if (screen_id < 0 || screen_id >= AMOS_MAX_SCREENS) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "IFF: Invalid screen ID %d", screen_id);
        return -1;
    }

    if (amos_screen_open(state, screen_id, bmhd.width, bmhd.height,
                         bmhd.num_planes) != 0) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "IFF: Failed to open screen %d (%dx%d)",
                 screen_id, bmhd.width, bmhd.height);
        return -1;
    }

    amos_screen_t *scr = &state->screens[screen_id];

    /* Apply palette */
    if (palette_count > 0) {
        for (int i = 0; i < palette_count && i < 256; i++) {
            scr->palette[i] = palette[i];
        }
    }

    /* Decode BODY data */
    bool has_mask = (bmhd.masking == 1);
    int row_bytes = ((bmhd.width + 15) / 16) * 2;
    int total_planes = bmhd.num_planes + (has_mask ? 1 : 0);
    int decompressed_size = row_bytes * total_planes * bmhd.height;

    uint8_t *plane_data = malloc(decompressed_size);
    if (!plane_data) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "IFF: Out of memory for plane data (%d bytes)", decompressed_size);
        return -1;
    }

    if (bmhd.compression == 0) {
        /* Uncompressed: copy directly */
        int copy_len = decompressed_size;
        if ((uint32_t)copy_len > body_size)
            copy_len = (int)body_size;
        memcpy(plane_data, body_data, copy_len);
    } else if (bmhd.compression == 1) {
        /* ByteRun1 compressed */
        int result = byterun1_decompress(body_data, (int)body_size,
                                         plane_data, decompressed_size);
        if (result < 0) {
            free(plane_data);
            snprintf(state->error_msg, sizeof(state->error_msg),
                     "IFF: ByteRun1 decompression failed");
            return -1;
        }
    } else {
        free(plane_data);
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "IFF: Unknown compression type %d", bmhd.compression);
        return -1;
    }

    /* Convert planar to chunky */
    int pixel_count = bmhd.width * bmhd.height;
    uint8_t *chunky = calloc(pixel_count, 1);
    if (!chunky) {
        free(plane_data);
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "IFF: Out of memory for chunky buffer");
        return -1;
    }

    planar_to_chunky(plane_data, chunky, bmhd.width, bmhd.height,
                     bmhd.num_planes, has_mask);

    /* Write pixels to screen using palette lookup */
    int max_color = (1 << bmhd.num_planes) - 1;
    for (int i = 0; i < pixel_count; i++) {
        int idx = chunky[i];
        if (idx > max_color) idx = max_color;

        if (palette_count > 0 && idx < palette_count) {
            scr->pixels[i] = palette[idx];
        } else if (idx < 32) {
            scr->pixels[i] = scr->palette[idx];
        } else {
            scr->pixels[i] = AMOS_RGBA(0, 0, 0, 0xFF);
        }
    }

    free(chunky);
    free(plane_data);

    return 0;
}

/* ── Public API: load from file ─────────────────────────────────────── */

int amos_load_iff(amos_state_t *state, const char *path)
{
    return amos_load_iff_to_screen(state, path, state->current_screen);
}

int amos_load_iff_to_screen(amos_state_t *state, const char *path, int screen_id)
{
    if (!state || !path) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "IFF: Cannot open file '%s'", path);
        return -1;
    }

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 16 * 1024 * 1024) {
        fclose(f);
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "IFF: Invalid file size %ld", file_size);
        return -1;
    }

    uint8_t *data = malloc(file_size);
    if (!data) {
        fclose(f);
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "IFF: Out of memory for file data");
        return -1;
    }

    size_t read_len = fread(data, 1, file_size, f);
    fclose(f);

    if ((long)read_len != file_size) {
        free(data);
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "IFF: Short read (%zu of %ld bytes)", read_len, file_size);
        return -1;
    }

    int result = amos_load_iff_from_memory(state, data, (size_t)file_size, screen_id);
    free(data);

    return result;
}
