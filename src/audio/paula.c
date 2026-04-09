/*
 * paula.c — Amiga Paula chip emulation
 *
 * 4-channel DMA audio with period registers, per-channel volume (0-64),
 * and the characteristic Amiga stereo assignment (ch 0+3 left, 1+2 right).
 * Includes optional low-pass Butterworth filter (A500: 4.4kHz).
 */

#include "amos.h"
#include <string.h>
#include <math.h>

void amos_paula_init(paula_t *paula, int output_rate)
{
    memset(paula, 0, sizeof(paula_t));
    paula->output_rate = output_rate;
    paula->filter_enabled = true;   /* A500 filter on by default */
    paula->filter_state_l = 0.0;
    paula->filter_state_r = 0.0;
}

/* ── Low-pass filter (single-pole IIR, approximating Amiga RC filter) ── */

static double lowpass(double input, double *state, double cutoff, double sample_rate)
{
    double rc = 1.0 / (2.0 * M_PI * cutoff);
    double dt = 1.0 / sample_rate;
    double alpha = dt / (rc + dt);
    *state = *state + alpha * (input - *state);
    return *state;
}

/* ── Mix all 4 channels into stereo output ───────────────────────── */

void amos_paula_mix(paula_t *paula, int16_t *buffer, int frames)
{
    double step[4] = {0};

    /* Calculate playback step for each channel */
    for (int ch = 0; ch < 4; ch++) {
        paula_channel_t *c = &paula->channels[ch];
        if (c->active && c->period > 0 && c->sample_data && c->sample_length > 0) {
            double freq = (double)PAULA_PAL_CLOCK / (double)(c->period * 2);
            step[ch] = freq / (double)paula->output_rate;
        }
    }

    for (int i = 0; i < frames; i++) {
        double left = 0.0, right = 0.0;

        for (int ch = 0; ch < 4; ch++) {
            paula_channel_t *c = &paula->channels[ch];
            if (!c->active || c->period == 0 || !c->sample_data || c->sample_length == 0)
                continue;

            /* Sample interpolation (nearest neighbor for authenticity) */
            int pos = (int)c->position;
            if (pos >= (int)c->sample_length) {
                if (c->repeat_length > 0) {
                    c->position = c->repeat_offset;
                    pos = (int)c->position;
                } else {
                    c->active = false;
                    continue;
                }
            }

            double sample = (double)c->sample_data[pos] / 128.0;
            sample *= (double)c->volume / 64.0;

            /* Amiga stereo: channels 0,3 = left; channels 1,2 = right */
            if (ch == 0 || ch == 3) {
                left += sample;
            } else {
                right += sample;
            }

            c->position += step[ch];
        }

        /* Apply low-pass filter (A500: ~4.4kHz) */
        if (paula->filter_enabled) {
            left = lowpass(left, &paula->filter_state_l, 4400.0, paula->output_rate);
            right = lowpass(right, &paula->filter_state_r, 4400.0, paula->output_rate);
        }

        /* Clamp and convert to 16-bit */
        if (left > 1.0) left = 1.0;
        if (left < -1.0) left = -1.0;
        if (right > 1.0) right = 1.0;
        if (right < -1.0) right = -1.0;

        buffer[i * 2] = (int16_t)(left * 32767.0);
        buffer[i * 2 + 1] = (int16_t)(right * 32767.0);
    }
}
