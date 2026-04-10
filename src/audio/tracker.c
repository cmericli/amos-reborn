/*
 * tracker.c — MOD tracker playback (stub)
 *
 * This is a stub implementation. Full MOD playback requires libxmp.
 * Install with: brew install libxmp
 * Then rebuild with cmake to enable tracker support.
 *
 * TODO: When libxmp is available:
 *   - xmp_create_context() in tracker_init
 *   - xmp_load_module() in track_play
 *   - xmp_play_buffer() in tracker_mix (mix with Paula output)
 *   - xmp_end_player() / xmp_release_module() in track_stop
 *   - xmp_free_context() in tracker_shutdown
 */

#include "amos.h"
#include <stdio.h>

void amos_tracker_init(amos_state_t *state)
{
    (void)state;
    /* Stub: no tracker backend available */
}

void amos_tracker_shutdown(amos_state_t *state)
{
    (void)state;
}

void amos_track_play(amos_state_t *state, const char *filename)
{
    (void)state;
    fprintf(stderr, "Track Play \"%s\": tracker not available (install libxmp)\n",
            filename ? filename : "(null)");
}

void amos_track_stop(amos_state_t *state)
{
    (void)state;
    fprintf(stderr, "Track Stop: tracker not available (install libxmp)\n");
}

void amos_track_loop_on(amos_state_t *state)
{
    (void)state;
    fprintf(stderr, "Track Loop On: tracker not available (install libxmp)\n");
}

void amos_track_loop_off(amos_state_t *state)
{
    (void)state;
    fprintf(stderr, "Track Loop Off: tracker not available (install libxmp)\n");
}

void amos_tracker_mix(amos_state_t *state, int16_t *buffer, int frames)
{
    (void)state;
    (void)buffer;
    (void)frames;
    /* Stub: nothing to mix */
}
