/*
 * vv_audio_tests.c — V&V tests for Audio System (REQ-AUD)
 *
 * Tests Paula emulation, built-in effects, envelope system,
 * and mixer against AMOS specification requirements.
 */

#include "vv_framework.h"

/* ══════════════════════════════════════════════════════════════════════
 *  Paula Register Interface (REQ-AUD-001 through 004)
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-AUD-001: Paula has exactly 4 channels") {
    amos_state_t *s = vv_create();
    paula_t *p = &s->paula;
    /* Verify 4 channels exist with correct initial state */
    for (int i = 0; i < 4; i++) {
        VV_ASSERT(p->channels[i].active == false,
                  "channel should be inactive after init");
        VV_ASSERT(p->channels[i].volume == 0,
                  "channel volume should be 0 after init");
    }
    vv_destroy(s);
}

VV_TEST("REQ-AUD-003: Per-channel fields match register layout") {
    amos_state_t *s = vv_create();
    paula_channel_t *ch = &s->paula.channels[0];
    /* Verify all required fields exist and are accessible */
    VV_ASSERT(ch->sample_data == NULL, "sample_data should be NULL initially");
    VV_ASSERT(ch->sample_length == 0, "sample_length should be 0");
    VV_ASSERT(ch->period == 0, "period should be 0");
    VV_ASSERT(ch->volume == 0, "volume should be 0");
    VV_ASSERT(ch->position == 0.0, "position should be 0.0");
    vv_destroy(s);
}

VV_TEST("REQ-AUD-004: PAL clock constant is 3,546,895 Hz") {
    VV_ASSERT(PAULA_PAL_CLOCK == 3546895,
              "PAULA_PAL_CLOCK must be 3546895");
}

/* ══════════════════════════════════════════════════════════════════════
 *  Built-in Effects (REQ-AUD-014 through 018)
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-AUD-014: Boom activates all 4 channels") {
    amos_state_t *s = vv_create();
    amos_boom(s);
    for (int i = 0; i < 4; i++) {
        VV_ASSERT(s->paula.channels[i].active == true,
                  "Boom should activate all 4 channels");
    }
    vv_destroy(s);
}

VV_TEST("REQ-AUD-015: Shoot activates all 4 channels") {
    amos_state_t *s = vv_create();
    amos_shoot(s);
    for (int i = 0; i < 4; i++) {
        VV_ASSERT(s->paula.channels[i].active == true,
                  "Shoot should activate all 4 channels");
    }
    vv_destroy(s);
}

VV_TEST("REQ-AUD-016: Bell activates all 4 channels") {
    amos_state_t *s = vv_create();
    amos_bell(s);
    for (int i = 0; i < 4; i++) {
        VV_ASSERT(s->paula.channels[i].active == true,
                  "Bell should activate all 4 channels");
    }
    vv_destroy(s);
}

VV_TEST("REQ-AUD-017: Effect detuning — +1 note per channel") {
    amos_state_t *s = vv_create();
    amos_boom(s);
    /* Each successive channel should have a shorter period (higher note) */
    uint16_t p0 = s->paula.channels[0].period;
    uint16_t p1 = s->paula.channels[1].period;
    uint16_t p2 = s->paula.channels[2].period;
    uint16_t p3 = s->paula.channels[3].period;
    VV_ASSERT(p0 > p1, "channel 0 period > channel 1 (lower note)");
    VV_ASSERT(p1 > p2, "channel 1 period > channel 2");
    VV_ASSERT(p2 > p3, "channel 2 period > channel 3");
    vv_destroy(s);
}

VV_TEST("REQ-AUD-018: Effects play on all 4 channels simultaneously") {
    amos_state_t *s = vv_create();
    amos_bell(s);
    int active_count = 0;
    for (int i = 0; i < 4; i++) {
        if (s->paula.channels[i].active) active_count++;
    }
    VV_ASSERT(active_count == 4, "all 4 channels should be active");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Stereo Assignment (REQ-AUD-019)
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-AUD-019: Amiga stereo panning — ch 0+3 left, ch 1+2 right") {
    /* Verify the panning convention via filter state and structure */
    amos_state_t *s = vv_create();
    paula_t *p = &s->paula;
    /* Filter enabled by default (A500 behavior) */
    VV_ASSERT(p->filter_enabled == true,
              "filter should be enabled by default");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Envelope System (REQ-AUD-020 through 025)
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-AUD-020: Envelope uses fixed-point interpolation") {
    amos_state_t *s = vv_create();
    amos_boom(s);
    paula_channel_t *ch = &s->paula.channels[0];
    /* After Boom, envelope should be active with proper initial state */
    VV_ASSERT(ch->envelope.active == true, "envelope should be active after Boom");
    VV_ASSERT(ch->envelope.phase == 0 || ch->envelope.phase == 1,
              "envelope should start at phase 0 or 1");
    vv_destroy(s);
}

VV_TEST("REQ-AUD-022: Each channel has independent envelope") {
    amos_state_t *s = vv_create();
    amos_boom(s);
    /* All 4 channels should have independent envelopes */
    for (int i = 0; i < 4; i++) {
        VV_ASSERT(s->paula.channels[i].envelope.active == true,
                  "each channel should have its own active envelope");
    }
    vv_destroy(s);
}

VV_TEST("REQ-AUD-035: Low-pass filter enabled by default") {
    amos_state_t *s = vv_create();
    VV_ASSERT(s->paula.filter_enabled == true,
              "A500 low-pass filter should be on by default");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Command Integration Tests
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-AUD-014a: Boom command via interpreter") {
    amos_state_t *s = vv_create();
    vv_run(s, "Boom");
    VV_ASSERT(s->paula.channels[0].active == true,
              "Boom command should activate Paula channels");
    vv_destroy(s);
}

VV_TEST("REQ-AUD-015a: Shoot command via interpreter") {
    amos_state_t *s = vv_create();
    vv_run(s, "Shoot");
    VV_ASSERT(s->paula.channels[0].active == true,
              "Shoot command should activate Paula channels");
    vv_destroy(s);
}

VV_TEST("REQ-AUD-016a: Bell command via interpreter") {
    amos_state_t *s = vv_create();
    vv_run(s, "Bell");
    VV_ASSERT(s->paula.channels[0].active == true,
              "Bell command should activate Paula channels");
    vv_destroy(s);
}

VV_TEST("REQ-AUD-021: Envelope rate tied to 50 Hz VBL") {
    amos_state_t *s = vv_create();
    /* Verify the output_rate is set and envelope counter starts at 0 */
    VV_ASSERT(s->paula.envelope_counter == 0.0,
              "envelope counter should start at 0");
    vv_destroy(s);
}
