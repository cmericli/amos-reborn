/*
 * platform_sdl2.c — SDL2 + OpenGL platform layer
 *
 * Window creation, input handling, audio callback, and frame presentation.
 * This is the bridge between AMOS Reborn and the host OS.
 */

#include "amos.h"
#include <SDL.h>

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <stdio.h>

/* ── State ───────────────────────────────────────────────────────── */

static SDL_Window *g_window = NULL;
static SDL_GLContext g_gl_context = NULL;
static SDL_AudioDeviceID g_audio_device = 0;
static amos_state_t *g_audio_state = NULL;  /* for audio callback */
static bool g_quit = false;

/* Compositor output buffer */
static uint32_t *g_compositor_buffer = NULL;
static int g_compositor_w = 0, g_compositor_h = 0;

/* ── Audio Callback ──────────────────────────────────────────────── */

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
    amos_state_t *state = (amos_state_t *)userdata;
    int frames = len / (2 * sizeof(int16_t));  /* stereo 16-bit */
    amos_paula_mix(&state->paula, (int16_t *)stream, frames);
}

/* ── Public API ──────────────────────────────────────────────────── */

int platform_init(amos_state_t *state, int width, int height, const char *title)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    /* Request OpenGL 3.3 Core */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    g_window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!g_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }

    g_gl_context = SDL_GL_CreateContext(g_window);
    if (!g_gl_context) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return -1;
    }

    SDL_GL_SetSwapInterval(1);  /* VSync */

    /* Get actual drawable size (may differ from window size on HiDPI) */
    int draw_w, draw_h;
    SDL_GL_GetDrawableSize(g_window, &draw_w, &draw_h);
    state->window_width = draw_w;
    state->window_height = draw_h;

    /* Initialize CRT shader pipeline */
    if (crt_init(state) < 0) {
        fprintf(stderr, "CRT shader init failed\n");
        return -1;
    }

    /* Allocate compositor buffer (matches the largest classic resolution) */
    g_compositor_w = 320;
    g_compositor_h = 256;
    g_compositor_buffer = calloc(g_compositor_w * g_compositor_h, sizeof(uint32_t));

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    fprintf(stderr, "[Platform] SDL2 + OpenGL 3.3 initialized (%dx%d drawable)\n",
            draw_w, draw_h);

    return 0;
}

void platform_audio_init(amos_state_t *state)
{
    SDL_AudioSpec want, have;
    SDL_memset(&want, 0, sizeof(want));
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_callback;
    want.userdata = state;

    g_audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (g_audio_device == 0) {
        fprintf(stderr, "SDL audio open failed: %s\n", SDL_GetError());
        return;
    }

    state->paula.output_rate = have.freq;
    g_audio_state = state;

    /* Start audio playback */
    SDL_PauseAudioDevice(g_audio_device, 0);

    fprintf(stderr, "[Platform] Audio initialized: %dHz, %d-sample buffer\n",
            have.freq, have.samples);
}

void platform_audio_shutdown(void)
{
    if (g_audio_device) {
        SDL_CloseAudioDevice(g_audio_device);
        g_audio_device = 0;
    }
}

void platform_shutdown(void)
{
    crt_shutdown();
    platform_audio_shutdown();

    free(g_compositor_buffer);
    g_compositor_buffer = NULL;

    if (g_gl_context) {
        SDL_GL_DeleteContext(g_gl_context);
        g_gl_context = NULL;
    }
    if (g_window) {
        SDL_DestroyWindow(g_window);
        g_window = NULL;
    }
    SDL_Quit();
}

bool platform_should_quit(void)
{
    return g_quit;
}

void platform_request_quit(void)
{
    g_quit = true;
}

void platform_poll_events(amos_state_t *state)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                g_quit = true;
                break;

            case SDL_KEYDOWN: {
                int sc = event.key.keysym.scancode;
                if (sc >= 0 && sc < 512)
                    state->key_states[sc] = true;
                state->last_scancode = sc;

                /* Store ASCII key code if printable */
                if (event.key.keysym.sym >= 0 && event.key.keysym.sym < 128)
                    state->last_key = (int)event.key.keysym.sym;

                /* Update modifier state bitmask */
                state->shift_state = 0;
                if (event.key.keysym.mod & KMOD_SHIFT) state->shift_state |= 1;
                if (event.key.keysym.mod & KMOD_CTRL)  state->shift_state |= 2;
                if (event.key.keysym.mod & KMOD_ALT)   state->shift_state |= 4;

                /* Ctrl+T: cycle CRT preset */
                if (event.key.keysym.sym == SDLK_t &&
                    (event.key.keysym.mod & KMOD_CTRL)) {
                    int next = (state->crt_preset + 1) % CRT_PRESET_COUNT;
                    crt_set_preset(state, (crt_preset_t)next);
                    fprintf(stderr, "[CRT] Preset: %s\n",
                            (const char *[]){"clean","vga","crt","amber","green","tv","commodore"}[next]);
                }
                /* ESC: stop program */
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    state->running = false;
                }
                break;
            }

            case SDL_KEYUP: {
                int sc = event.key.keysym.scancode;
                if (sc >= 0 && sc < 512)
                    state->key_states[sc] = false;

                /* Update modifier state on release too */
                state->shift_state = 0;
                if (event.key.keysym.mod & KMOD_SHIFT) state->shift_state |= 1;
                if (event.key.keysym.mod & KMOD_CTRL)  state->shift_state |= 2;
                if (event.key.keysym.mod & KMOD_ALT)   state->shift_state |= 4;
                break;
            }

            case SDL_MOUSEMOTION: {
                /* Scale window coordinates to AMOS screen resolution */
                amos_screen_t *scr = &state->screens[state->current_screen];
                int scr_w = scr->active ? scr->width : 320;
                int scr_h = scr->active ? scr->height : 256;
                int win_w, win_h;
                SDL_GetWindowSize(g_window, &win_w, &win_h);
                state->mouse_x = event.motion.x * scr_w / (win_w > 0 ? win_w : 1);
                state->mouse_y = event.motion.y * scr_h / (win_h > 0 ? win_h : 1);
                break;
            }

            case SDL_MOUSEBUTTONDOWN: {
                int bit = 0;
                if (event.button.button == SDL_BUTTON_LEFT)   bit = 1;
                if (event.button.button == SDL_BUTTON_RIGHT)  bit = 2;
                if (event.button.button == SDL_BUTTON_MIDDLE) bit = 4;
                state->mouse_buttons |= bit;
                state->mouse_click |= bit;
                break;
            }

            case SDL_MOUSEBUTTONUP: {
                int bit = 0;
                if (event.button.button == SDL_BUTTON_LEFT)   bit = 1;
                if (event.button.button == SDL_BUTTON_RIGHT)  bit = 2;
                if (event.button.button == SDL_BUTTON_MIDDLE) bit = 4;
                state->mouse_buttons &= ~bit;
                break;
            }

            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    int draw_w, draw_h;
                    SDL_GL_GetDrawableSize(g_window, &draw_w, &draw_h);
                    state->window_width = draw_w;
                    state->window_height = draw_h;
                }
                break;
        }
    }
}

void platform_present(amos_state_t *state)
{
    /* Get the primary screen dimensions for compositor */
    amos_screen_t *scr = &state->screens[state->current_screen];
    if (scr->active) {
        /* Resize compositor buffer if needed */
        if (scr->width != g_compositor_w || scr->height != g_compositor_h) {
            g_compositor_w = scr->width;
            g_compositor_h = scr->height;
            free(g_compositor_buffer);
            g_compositor_buffer = calloc(g_compositor_w * g_compositor_h, sizeof(uint32_t));
        }
    }

    /* Composite all active screens */
    compositor_render(state, g_compositor_buffer, g_compositor_w, g_compositor_h);

    /* Render through CRT shader pipeline */
    crt_render(state, g_compositor_buffer, g_compositor_w, g_compositor_h);

    /* Swap buffers */
    SDL_GL_SwapWindow(g_window);
}

uint32_t platform_get_ticks(void)
{
    return SDL_GetTicks();
}
