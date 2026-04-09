/*
 * effects.c — AMOS built-in sound effects
 *
 * Boom, Shoot, Bell — procedurally synthesized to match the original AMOS.
 * These were generated algorithmically in the original, not from samples.
 */

#include "amos.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ── Sample generation buffers ───────────────────────────────────── */

#define EFFECT_MAX_LEN 8820  /* ~200ms at 44100Hz */

static int8_t boom_samples[EFFECT_MAX_LEN];
static int8_t shoot_samples[EFFECT_MAX_LEN];
static int8_t bell_samples[EFFECT_MAX_LEN];
static bool effects_initialized = false;

static void init_effects(void)
{
    if (effects_initialized) return;

    /* BOOM: descending noise burst with decay — like a bass drum/explosion */
    for (int i = 0; i < EFFECT_MAX_LEN; i++) {
        double t = (double)i / 44100.0;
        double env = exp(-t * 15.0);     /* fast decay */
        double freq = 80.0 - t * 200.0;  /* descending pitch */
        if (freq < 20.0) freq = 20.0;
        double noise = ((double)(rand() % 200 - 100)) / 100.0;
        double tone = sin(2.0 * M_PI * freq * t);
        boom_samples[i] = (int8_t)((tone * 0.7 + noise * 0.3) * env * 127.0);
    }

    /* SHOOT: white noise with high-frequency sweep down */
    for (int i = 0; i < EFFECT_MAX_LEN; i++) {
        double t = (double)i / 44100.0;
        double env = exp(-t * 20.0);
        double noise = ((double)(rand() % 200 - 100)) / 100.0;
        double freq = 2000.0 - t * 8000.0;
        if (freq < 100.0) freq = 100.0;
        double tone = sin(2.0 * M_PI * freq * t);
        shoot_samples[i] = (int8_t)((noise * 0.6 + tone * 0.4) * env * 127.0);
    }

    /* BELL: sine with harmonics and slow decay — classic 8-bit bell */
    for (int i = 0; i < EFFECT_MAX_LEN; i++) {
        double t = (double)i / 44100.0;
        double env = exp(-t * 3.0);     /* slow decay */
        double fundamental = 800.0;
        double sample = sin(2.0 * M_PI * fundamental * t) * 0.5 +
                        sin(2.0 * M_PI * fundamental * 2.0 * t) * 0.25 +
                        sin(2.0 * M_PI * fundamental * 3.0 * t) * 0.125 +
                        sin(2.0 * M_PI * fundamental * 4.76 * t) * 0.1;   /* inharmonic partial */
        bell_samples[i] = (int8_t)(sample * env * 100.0);
    }

    effects_initialized = true;
}

/* ── Play effect on a Paula channel ──────────────────────────────── */

static void play_effect(amos_state_t *state, const int8_t *samples, int length, int channel)
{
    init_effects();

    paula_channel_t *ch = &state->paula.channels[channel % 4];
    ch->sample_data = samples;
    ch->sample_length = length;
    ch->repeat_offset = 0;
    ch->repeat_length = 0;  /* no loop */
    ch->period = 160;       /* ~11kHz at PAL clock */
    ch->volume = 64;        /* max volume */
    ch->position = 0.0;
    ch->active = true;
}

void amos_boom(amos_state_t *state)
{
    play_effect(state, boom_samples, EFFECT_MAX_LEN, 0);
}

void amos_shoot(amos_state_t *state)
{
    play_effect(state, shoot_samples, EFFECT_MAX_LEN, 1);
}

void amos_bell(amos_state_t *state)
{
    play_effect(state, bell_samples, EFFECT_MAX_LEN, 2);
}

void amos_sam_play(amos_state_t *state, int channel, int sample_id)
{
    /* TODO: Play sample from bank 3 */
    (void)state; (void)channel; (void)sample_id;
}
