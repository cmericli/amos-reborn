/*
 * mixer.c — Audio mixer bridge
 *
 * Called by the SDL2 audio callback to fill the output buffer
 * using the Paula emulator.
 */

#include "amos.h"

/* The SDL audio callback calls this to get mixed audio */
void amos_audio_mix(amos_state_t *state, int16_t *buffer, int frames)
{
    amos_paula_mix(&state->paula, buffer, frames);
}
