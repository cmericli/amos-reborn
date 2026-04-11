/*
 * oracle_capture.c -- Headless AMOS screen capture to PNG
 *
 * Runs an AMOS program headlessly (no SDL/OpenGL) and dumps the
 * composited framebuffer to a PNG file for visual regression testing.
 *
 * Usage:
 *   oracle-capture program.txt --screenshot output.png [--timeout 5000]
 *
 * Exit codes:
 *   0 = success
 *   1 = error (bad args, load failure, no active screen, etc.)
 */

#include "amos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* ── Timeout handling ─────────────────────────────────────────────── */

static volatile int timed_out = 0;

static void alarm_handler(int sig)
{
    (void)sig;
    timed_out = 1;
}

/* ── Usage ────────────────────────────────────────────────────────── */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <program> --screenshot <output.png> [--timeout <steps>]\n"
        "\n"
        "  <program>       AMOS source file (.txt or .AMOS binary)\n"
        "  --screenshot    Path to write the PNG screenshot\n"
        "  --timeout       Max execution steps (default: 10000)\n"
        "\n", prog);
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    const char *program_path = NULL;
    const char *screenshot_path = NULL;
    int max_steps = 10000;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot_path = argv[++i];
        } else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            max_steps = atoi(argv[++i]);
            if (max_steps <= 0) max_steps = 10000;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            program_path = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (!program_path) {
        fprintf(stderr, "Error: no program file specified\n");
        usage(argv[0]);
        return 1;
    }

    if (!screenshot_path) {
        fprintf(stderr, "Error: --screenshot <path> is required\n");
        usage(argv[0]);
        return 1;
    }

    /* Create interpreter state */
    amos_state_t *state = amos_create();
    if (!state) {
        fprintf(stderr, "Error: amos_create() failed\n");
        return 1;
    }

    /* Load program */
    fprintf(stderr, "Loading: %s\n", program_path);
    int result = amos_load_file(state, program_path);
    if (result < 0) {
        fprintf(stderr, "Load error: %s\n", state->error_msg);
        amos_destroy(state);
        return 1;
    }
    fprintf(stderr, "Loaded %d lines\n", state->line_count);

    /* Execute with timeout */
    fprintf(stderr, "Executing (max %d steps)...\n", max_steps);

    /* Wall-clock safety: 30 second alarm */
    signal(SIGALRM, alarm_handler);
    alarm(30);

    amos_run(state);

    int steps = 0;
    while (state->running && steps < max_steps && !timed_out) {
        amos_run_step(state);
        steps++;

        if (state->error_code) {
            fprintf(stderr, "  Runtime error at line %d: [%d] %s\n",
                    state->current_line + 1,
                    state->error_code, state->error_msg);
            /* Clear error and continue -- some programs recover */
            state->error_code = 0;
            state->error_msg[0] = '\0';
        }
    }

    alarm(0);

    if (timed_out) {
        fprintf(stderr, "  (wall-clock timeout after 30s)\n");
    } else if (steps >= max_steps && state->running) {
        fprintf(stderr, "  (step limit %d reached)\n", max_steps);
    }
    fprintf(stderr, "Executed %d steps\n", steps);

    /* Find the active screen to capture */
    int screen_id = state->current_screen;
    amos_screen_t *scr = NULL;

    /* Try current screen first, then scan for any active screen */
    if (screen_id >= 0 && screen_id < AMOS_MAX_SCREENS &&
        state->screens[screen_id].active &&
        state->screens[screen_id].pixels) {
        scr = &state->screens[screen_id];
    } else {
        for (int i = 0; i < AMOS_MAX_SCREENS; i++) {
            if (state->screens[i].active && state->screens[i].pixels) {
                scr = &state->screens[i];
                screen_id = i;
                break;
            }
        }
    }

    if (!scr) {
        fprintf(stderr, "Error: no active screen with pixel data\n");
        amos_destroy(state);
        return 1;
    }

    fprintf(stderr, "Capturing screen %d (%dx%d)\n",
            screen_id, scr->width, scr->height);

    /* Option A: Dump the single screen directly */
    /* Option B: Composite all screens via compositor_render */
    /* We do both: composite into an output buffer at screen dimensions */

    int out_w = scr->width;
    int out_h = scr->height;
    uint32_t *output = calloc(out_w * out_h, sizeof(uint32_t));
    if (!output) {
        fprintf(stderr, "Error: failed to allocate output buffer\n");
        amos_destroy(state);
        return 1;
    }

    compositor_render(state, output, out_w, out_h);

    /* Write PNG (RGBA, 4 bytes per pixel) */
    int ok = stbi_write_png(screenshot_path,
                            out_w, out_h,
                            4,                      /* RGBA components */
                            output,
                            out_w * 4);             /* stride */

    if (!ok) {
        fprintf(stderr, "Error: failed to write PNG to %s\n", screenshot_path);
        free(output);
        amos_destroy(state);
        return 1;
    }

    fprintf(stderr, "Screenshot saved: %s (%dx%d)\n",
            screenshot_path, out_w, out_h);

    /* Print text output to stdout for text-mode comparison.
     * The screen's text cursor position tells us how much was printed.
     * We extract readable text by scanning the screen pixels against
     * the font, but a simpler approach: just re-read the screen buffer
     * row by row looking for non-background pixels as a signal that
     * text was rendered. For actual text extraction we'd need OCR.
     *
     * Instead, print a summary of screen state for test harness use. */
    printf("screen_id=%d\n", screen_id);
    printf("width=%d\n", scr->width);
    printf("height=%d\n", scr->height);
    printf("cursor_x=%d\n", scr->cursor_x);
    printf("cursor_y=%d\n", scr->cursor_y);
    printf("steps=%d\n", steps);

    free(output);
    amos_destroy(state);
    return 0;
}
