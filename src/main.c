/*
 * main.c — AMOS Reborn entry point
 *
 * Initializes the runtime, loads an AMOS program, and runs the main loop.
 * The main loop is structured as a per-frame tick for Emscripten compatibility.
 *
 * Usage: amos-reborn [program.txt]
 *        amos-reborn              (opens with default demo)
 */

#include "amos.h"
#include "editor/editor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/* ── Global State ────────────────────────────────────────────────── */

static amos_state_t *g_state = NULL;

/* Statements to execute per frame (controls interpreter speed) */
#define STATEMENTS_PER_FRAME 1000

/* ── Built-in Demo Program ───────────────────────────────────────── */

/* Built-in demo program — available via direct mode "Run Demo" or future menu */
static const char *demo_program __attribute__((unused)) =
    "Rem *** AMOS Reborn Demo ***\n"
    "Screen Open 0,320,256,5\n"
    "Palette $000,$FFF,$F00,$0F0,$00F,$FF0,$0FF,$F0F,$888,$F80,$08F,$0F8,$80F,$F08,$8F0,$F88\n"
    "Cls 0\n"
    "Rem Draw colorful bars\n"
    "For I=0 To 15\n"
    "  Ink I\n"
    "  Bar 0,I*16 To 319,I*16+15\n"
    "Next I\n"
    "Rem Draw some circles\n"
    "Ink 15\n"
    "Circle 160,128,100\n"
    "Circle 160,128,80\n"
    "Circle 160,128,60\n"
    "Circle 160,128,40\n"
    "Circle 160,128,20\n"
    "Rem Title text\n"
    "Ink 15,0\n"
    "Locate 10,1\n"
    "Print \"AMOS REBORN v0.1\"\n"
    "Locate 7,3\n"
    "Print \"Press Ctrl+T for CRT modes\"\n"
    "Locate 11,5\n"
    "Print \"ESC to quit\"\n"
    "Rem Sound effect\n"
    "Boom\n"
    "Rem Main loop — keep display alive\n"
    "Do\n"
    "  Wait Vbl\n"
    "Loop\n";

/* ── Frame Tick ──────────────────────────────────────────────────── */

void amos_frame_tick(amos_state_t *state)
{
    /* 1. Poll input */
    platform_poll_events(state);

    /* 2. Execute interpreter statements */
    if (state->running) {
        for (int i = 0; i < STATEMENTS_PER_FRAME && state->running; i++) {
            amos_run_step(state);

            /* Wait Vbl means "stop executing until next frame" */
            if (state->synchro) {
                state->synchro = false;
                break;
            }
        }
    }

    /* 3. Tick AMAL engine (one frame of animation per channel) */
    amos_amal_tick(state);

    /* 4. Update timer (50ths of a second, PAL style) */
    static uint32_t last_ticks = 0;
    uint32_t now = platform_get_ticks();
    if (last_ticks == 0) last_ticks = now;
    state->timer += (now - last_ticks) * 50 / 1000;
    last_ticks = now;

    /* 5. Present display */
    platform_present(state);
}

#ifdef __EMSCRIPTEN__
static void emscripten_frame(void)
{
    if (platform_should_quit()) {
        emscripten_cancel_main_loop();
        return;
    }
    if (amos_editor_is_active(g_state)) {
        amos_editor_tick(g_state);
    } else if (g_state->running) {
        amos_frame_tick(g_state);
    } else {
        amos_editor_init(g_state);
    }
}
#endif

/* ── Main ────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    fprintf(stderr, "AMOS Reborn v%s\n", AMOS_VERSION_STRING);
    fprintf(stderr, "A cross-platform reimplementation of AMOS Basic\n\n");

    /* Create AMOS state */
    g_state = amos_create();
    if (!g_state) {
        fprintf(stderr, "Failed to create AMOS state\n");
        return 1;
    }

    /* Determine mode: editor (no args) or run program (with file arg) */
    bool start_in_editor = (argc <= 1);

    /* Load program if specified */
    if (argc > 1) {
        fprintf(stderr, "Loading: %s\n", argv[1]);
        if (amos_load_file(g_state, argv[1]) < 0) {
            fprintf(stderr, "Error: %s\n", g_state->error_msg);
            amos_destroy(g_state);
            return 1;
        }
    }

    /* Initialize platform (SDL2 + OpenGL window) */
    int win_w, win_h;

    if (start_in_editor) {
        /* Editor uses 320x256, scale 3x for comfortable viewing */
        win_w = EDITOR_SCREEN_W * 3;
        win_h = EDITOR_SCREEN_H * 3;
    } else {
        int scale = (g_state->display_mode == AMOS_MODE_CLASSIC) ? 3 : 1;
        amos_screen_t *scr = &g_state->screens[0];
        win_w = scr->active ? scr->width * scale : 960;
        win_h = scr->active ? scr->height * scale : 768;
    }

    if (platform_init(g_state, win_w, win_h, "AMOS Reborn") < 0) {
        fprintf(stderr, "Platform initialization failed\n");
        amos_destroy(g_state);
        return 1;
    }

    /* Initialize audio */
    platform_audio_init(g_state);

    if (start_in_editor) {
        /* Start in editor mode */
        fprintf(stderr, "No program specified — opening AMOS 1.3 editor\n");
        amos_editor_init(g_state);
    } else {
        /* Start program directly */
        amos_run(g_state);
    }

    /* Main loop */
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(emscripten_frame, 0, 1);
#else
    while (!platform_should_quit()) {
        if (amos_editor_is_active(g_state)) {
            /* Editor mode */
            amos_editor_tick(g_state);
        } else if (g_state->running) {
            /* Run mode */
            amos_frame_tick(g_state);
        } else if (start_in_editor) {
            /* Program ended — show output briefly, then return to editor */
            /* Print "Press any key" and wait */
            amos_screen_print(g_state, "\n-- Program ended. Press any key --");
            platform_present(g_state);

            /* Wait for keypress */
            bool waiting = true;
            while (waiting && !platform_should_quit()) {
                SDL_Event ev;
                while (SDL_PollEvent(&ev)) {
                    if (ev.type == SDL_QUIT) {
                        platform_request_quit();
                        waiting = false;
                    }
                    if (ev.type == SDL_KEYDOWN || ev.type == SDL_MOUSEBUTTONDOWN) {
                        waiting = false;
                    }
                }
                SDL_Delay(16);
            }

            if (!platform_should_quit()) {
                amos_editor_init(g_state);
            }
        } else {
            /* Program ended, no editor — exit */
            break;
        }

        /* Simple frame rate limiting (~60fps) */
        SDL_Delay(16);
    }
#endif

    /* Cleanup */
    amos_editor_destroy(g_state);
    platform_shutdown();
    amos_destroy(g_state);

    fprintf(stderr, "\nAMOS Reborn terminated.\n");
    return 0;
}
