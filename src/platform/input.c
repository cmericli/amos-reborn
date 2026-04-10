/*
 * input.c — AMOS Reborn input handling
 *
 * Keyboard, mouse, and joystick input functions that mirror the original
 * AMOS BASIC input commands. The actual SDL event processing happens in
 * platform_sdl2.c; this file reads the state that was deposited there.
 */

#include "amos.h"
#include <string.h>

/* ── Keyboard ───────────────────────────────────────────────────────── */

int amos_inkey(amos_state_t *state)
{
    int key = state->last_key;
    state->last_key = 0;  /* consumed on read */
    return key;
}

const char *amos_inkey_str(amos_state_t *state)
{
    static char buf[2];
    int key = amos_inkey(state);
    if (key > 0 && key < 128) {
        buf[0] = (char)key;
        buf[1] = '\0';
        return buf;
    }
    buf[0] = '\0';
    return buf;
}

int amos_key_state(amos_state_t *state, int scancode)
{
    if (scancode < 0 || scancode >= 512) return 0;
    return state->key_states[scancode] ? -1 : 0;
}

int amos_scancode(amos_state_t *state)
{
    return state->last_scancode;
}

int amos_scanshift(amos_state_t *state)
{
    return state->shift_state;
}

/* ── Mouse ──────────────────────────────────────────────────────────── */

int amos_x_mouse(amos_state_t *state)
{
    return state->mouse_x;
}

int amos_y_mouse(amos_state_t *state)
{
    return state->mouse_y;
}

int amos_mouse_key(amos_state_t *state)
{
    return state->mouse_buttons;
}

int amos_mouse_click(amos_state_t *state)
{
    int clicks = state->mouse_click;
    state->mouse_click = 0;  /* consumed on read */
    return clicks;
}

/* ── Joystick (keyboard-emulated) ───────────────────────────────────── */

/*
 * AMOS joystick bitmask:
 *   bit 0 = up
 *   bit 1 = down
 *   bit 2 = left
 *   bit 3 = right
 *   bit 4 = fire
 *
 * Port 1 maps arrow keys + Space/Left-Ctrl to fire.
 * Also supports WASD + Space as an alternative.
 */
int amos_joy(amos_state_t *state, int port)
{
    (void)port;  /* only port 1 supported for now */

    int joy = 0;

    /* Arrow keys */
    if (state->key_states[0x52]) joy |= 1;  /* SDL_SCANCODE_UP    = 0x52 (82) */
    if (state->key_states[0x51]) joy |= 2;  /* SDL_SCANCODE_DOWN  = 0x51 (81) */
    if (state->key_states[0x50]) joy |= 4;  /* SDL_SCANCODE_LEFT  = 0x50 (80) */
    if (state->key_states[0x4F]) joy |= 8;  /* SDL_SCANCODE_RIGHT = 0x4F (79) */

    /* WASD alternative */
    if (state->key_states[0x1A]) joy |= 1;  /* SDL_SCANCODE_W = 0x1A (26) */
    if (state->key_states[0x16]) joy |= 2;  /* SDL_SCANCODE_S = 0x16 (22) */
    if (state->key_states[0x04]) joy |= 4;  /* SDL_SCANCODE_A = 0x04 (4)  */
    if (state->key_states[0x07]) joy |= 8;  /* SDL_SCANCODE_D = 0x07 (7)  */

    /* Fire: Space or Left Ctrl */
    if (state->key_states[0x2C]) joy |= 16; /* SDL_SCANCODE_SPACE = 0x2C (44) */
    if (state->key_states[0xE0]) joy |= 16; /* SDL_SCANCODE_LCTRL = 0xE0 (224) */

    return joy;
}
