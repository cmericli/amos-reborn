/*
 * effects.c — AMOS built-in sound effects (Boom, Shoot, Bell)
 *
 * Exact reproduction of the original AMOS algorithm:
 * - Wave 0 (noise): 256 bytes via LFSR with multiplier 0x3171, plus sub-octaves
 * - Wave 1 (square): 127/-127 for 128/128 bytes, plus sub-octaves
 * - Envelope system: (duration_in_VBLs, target_volume) pairs, 50Hz linear interp
 * - Each effect plays on all 4 Paula channels with +1 note detuning per channel
 */

#include "amos.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ── AMOS Waveform Tables ──────────────────────────────────────────── */

/*
 * Each wave has sub-octave versions: 256, 128, 64, 32, 16, 8, 4, 2 bytes.
 * Total storage per wave = 256+128+64+32+16+8+4+2 = 510 bytes.
 * We store them contiguously; sub-octave N starts at offset (256 - (256 >> N))
 * for the first, but it's easier to just have a flat buffer and index by length.
 */

#define WAVE_TOTAL_SIZE 510  /* 256+128+64+32+16+8+4+2 */
#define NUM_SUB_OCTAVES 8    /* lengths: 256, 128, 64, 32, 16, 8, 4, 2 */

static int8_t wave_noise[WAVE_TOTAL_SIZE];   /* Wave 0 */
static int8_t wave_square[WAVE_TOTAL_SIZE];  /* Wave 1 */
static bool waves_initialized = false;

/* Offsets into wave buffer for each sub-octave length */
static int wave_offset[NUM_SUB_OCTAVES];
static int wave_length[NUM_SUB_OCTAVES]; /* 256, 128, 64, ... 2 */

static void init_wave_tables(void)
{
    if (waves_initialized) return;

    /* Compute sub-octave offsets */
    int offset = 0;
    for (int i = 0; i < NUM_SUB_OCTAVES; i++) {
        int len = 256 >> i;
        wave_offset[i] = offset;
        wave_length[i] = len;
        offset += len;
    }

    /* Wave 0: Noise via LFSR — seed * 0x3171, take high byte */
    uint16_t seed = 1;  /* non-zero seed */
    for (int i = 0; i < 256; i++) {
        seed = (uint16_t)(seed * 0x3171);
        wave_noise[wave_offset[0] + i] = (int8_t)(seed >> 8);
    }

    /* Generate sub-octaves by averaging adjacent pairs */
    for (int oct = 1; oct < NUM_SUB_OCTAVES; oct++) {
        int prev_off = wave_offset[oct - 1];
        int prev_len = wave_length[oct - 1];
        int cur_off = wave_offset[oct];
        for (int i = 0; i < wave_length[oct]; i++) {
            int a = wave_noise[prev_off + (i * 2) % prev_len];
            int b = wave_noise[prev_off + (i * 2 + 1) % prev_len];
            wave_noise[cur_off + i] = (int8_t)((a + b) / 2);
        }
    }

    /* Wave 1: Square — 127 for first half, -127 for second half */
    for (int i = 0; i < 128; i++)
        wave_square[wave_offset[0] + i] = 127;
    for (int i = 128; i < 256; i++)
        wave_square[wave_offset[0] + i] = -127;

    /* Generate sub-octaves for square */
    for (int oct = 1; oct < NUM_SUB_OCTAVES; oct++) {
        int prev_off = wave_offset[oct - 1];
        int prev_len = wave_length[oct - 1];
        int cur_off = wave_offset[oct];
        for (int i = 0; i < wave_length[oct]; i++) {
            int a = wave_square[prev_off + (i * 2) % prev_len];
            int b = wave_square[prev_off + (i * 2 + 1) % prev_len];
            wave_square[cur_off + i] = (int8_t)((a + b) / 2);
        }
    }

    waves_initialized = true;
}

/* ── Note-to-period conversion ─────────────────────────────────────── */

/*
 * AMOS note-to-frequency: standard equal temperament.
 * Note 0 = C-0. Note 36 = C-2. Note 60 = C-4. Note 70 = Bb-4.
 *
 * For a given note, determine the octave and choose the appropriate
 * sub-octave waveform length. Lower octaves use longer waveforms (256),
 * higher octaves use shorter ones.
 *
 * Period = PAULA_PAL_CLOCK / (wave_length_for_octave * frequency_hz)
 */

static double note_to_freq(int note)
{
    /* A4 = note 69, 440 Hz */
    return 440.0 * pow(2.0, (note - 69) / 12.0);
}

/* Get the sub-octave index for a given MIDI-style note */
static int note_to_sub_octave(int note)
{
    /* Octave 0-1: use 256 samples (sub-octave 0)
     * Octave 2: 128 (sub-octave 1)
     * Octave 3: 64 (sub-octave 2)
     * etc.
     */
    int octave = note / 12;
    if (octave <= 1) return 0;
    int idx = octave - 1;
    if (idx >= NUM_SUB_OCTAVES) idx = NUM_SUB_OCTAVES - 1;
    return idx;
}

static uint16_t note_to_period(int note)
{
    double freq = note_to_freq(note);
    int sub = note_to_sub_octave(note);
    int wlen = wave_length[sub];
    double period = (double)PAULA_PAL_CLOCK / ((double)wlen * freq);
    if (period < 124) period = 124;   /* hardware minimum */
    if (period > 65535) period = 65535;
    return (uint16_t)period;
}

/* ── Envelope system ───────────────────────────────────────────────── */

void amos_envelope_init(envelope_t *env, const int *definition, int def_length)
{
    memset(env, 0, sizeof(envelope_t));
    env->active = true;
    env->phase = 0;
    env->current_vol = 0.0;

    /* Copy definition */
    if (def_length > 32) def_length = 32;
    memcpy(env->definition, definition, def_length * sizeof(int));
    env->def_length = def_length;

    /* Start first segment */
    if (def_length >= 2 && definition[0] > 0) {
        int dur = definition[0];
        double target = (double)definition[1];
        env->target_vol = target;
        env->frame_counter = dur;
        env->delta = (target - env->current_vol) / (double)dur;
    }
}

void amos_envelope_tick(envelope_t *env)
{
    if (!env->active) return;

    /* Advance volume */
    env->current_vol += env->delta;
    env->frame_counter--;

    if (env->frame_counter <= 0) {
        /* Snap to target */
        env->current_vol = env->target_vol;

        /* Move to next segment */
        env->phase += 2;
        if (env->phase >= env->def_length) {
            env->active = false;
            return;
        }

        int dur = env->definition[env->phase];
        int target = env->definition[env->phase + 1];

        if (dur == 0) {
            /* Terminator: 0,0 means end */
            env->active = false;
            return;
        }

        env->target_vol = (double)target;
        env->frame_counter = dur;
        env->delta = (env->target_vol - env->current_vol) / (double)dur;
    }
}

/* ── Play effect on all 4 channels ─────────────────────────────────── */

static void play_effect(amos_state_t *state, const int8_t *wave_data,
                        int base_note, const int *envelope_def, int env_len)
{
    init_wave_tables();

    for (int ch = 0; ch < 4; ch++) {
        int note = base_note + ch;  /* +1 note per channel for detuned spread */
        int sub = note_to_sub_octave(note);
        uint16_t period = note_to_period(note);

        paula_channel_t *c = &state->paula.channels[ch];
        c->sample_data = wave_data + wave_offset[sub];
        c->sample_length = (uint32_t)wave_length[sub];
        c->repeat_offset = 0;
        c->repeat_length = (uint32_t)wave_length[sub];  /* loop the waveform */
        c->period = period;
        c->volume = 0;  /* envelope will set volume */
        c->position = 0.0;
        c->active = true;

        /* Initialize envelope for this channel */
        amos_envelope_init(&c->envelope, envelope_def, env_len);
    }
}

/* ── Public API ────────────────────────────────────────────────────── */

/*
 * Boom: Note 36 (C2), noise wave, envelope: 1,64 → 10,50 → 50,0
 */
void amos_boom(amos_state_t *state)
{
    static const int env[] = { 1, 64, 10, 50, 50, 0, 0, 0 };
    play_effect(state, wave_noise, 36, env, 8);
}

/*
 * Shoot: Note 60 (C4), noise wave, envelope: 1,64 → 10,0
 */
void amos_shoot(amos_state_t *state)
{
    static const int env[] = { 1, 64, 10, 0, 0, 0 };
    play_effect(state, wave_noise, 60, env, 6);
}

/*
 * Bell: Note 70 (Bb4), square wave, envelope: 1,64 → 4,40 → 25,0
 */
void amos_bell(amos_state_t *state)
{
    static const int env[] = { 1, 64, 4, 40, 25, 0, 0, 0 };
    play_effect(state, wave_square, 70, env, 8);
}

void amos_sam_play(amos_state_t *state, int channel, int sample_id)
{
    /* TODO: Play sample from bank 3 */
    (void)state; (void)channel; (void)sample_id;
}
