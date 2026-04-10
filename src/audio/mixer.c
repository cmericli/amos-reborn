/*
 * mixer.c — Audio mixer bridge
 *
 * Called by the SDL2 audio callback to fill the output buffer.
 * Mixes Paula emulator output with tracker (MOD) output.
 */

#include "amos.h"
#include <string.h>

/* The SDL audio callback calls this to get mixed audio */
void amos_audio_mix(amos_state_t *state, int16_t *buffer, int frames)
{
    /* Paula channels (effects, samples) */
    amos_paula_mix(&state->paula, buffer, frames);

    /* Mix in tracker output (additive) */
    /* tracker_mix adds to existing buffer content */
    amos_tracker_mix(state, buffer, frames);
}
