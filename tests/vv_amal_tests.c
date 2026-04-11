/*
 * vv_amal_tests.c — V&V tests for AMAL Engine (REQ-AML)
 *
 * Tests AMAL compiler, bytecode execution, register operations,
 * and channel management against AMOS specification requirements.
 */

#include "vv_framework.h"

/* ══════════════════════════════════════════════════════════════════════
 *  AMAL Channel Structure (REQ-AML-001 through 010)
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-AML-001: AMAL supports 16 channels") {
    VV_ASSERT(AMOS_MAX_AMAL_CHANNELS == 16,
              "AMOS_MAX_AMAL_CHANNELS must be 16");
}

VV_TEST("REQ-AML-002: AMAL channels start inactive") {
    amos_state_t *s = vv_create();
    for (int i = 0; i < AMOS_MAX_AMAL_CHANNELS; i++) {
        VV_ASSERT(s->amal[i].active == false,
                  "AMAL channel should be inactive at init");
    }
    vv_destroy(s);
}

VV_TEST("REQ-AML-003: AMAL channel has 10 registers (R0-R9)") {
    amos_state_t *s = vv_create();
    amal_channel_t *ch = &s->amal[0];
    /* Verify 10 registers exist and start at 0 */
    for (int i = 0; i < 10; i++) {
        VV_ASSERT(ch->registers[i] == 0,
                  "AMAL register should start at 0");
    }
    vv_destroy(s);
}

VV_TEST("REQ-AML-004: AMAL channel has target type and ID") {
    amos_state_t *s = vv_create();
    amal_channel_t *ch = &s->amal[0];
    /* Target type 0=sprite, 1=bob */
    VV_ASSERT(ch->target_type == 0, "default target type should be 0 (sprite)");
    VV_ASSERT(ch->target_id == 0, "default target ID should be 0");
    vv_destroy(s);
}

VV_TEST("REQ-AML-005: AMAL channel has program counter") {
    amos_state_t *s = vv_create();
    amal_channel_t *ch = &s->amal[0];
    VV_ASSERT(ch->pc == 0, "AMAL PC should start at 0");
    vv_destroy(s);
}

VV_TEST("REQ-AML-006: AMAL channel can store program string") {
    amos_state_t *s = vv_create();
    amal_channel_t *ch = &s->amal[0];
    VV_ASSERT(ch->program == NULL, "program should be NULL initially");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  AMAL Compile & Execute (REQ-AML-010 through 030)
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-AML-010: AMAL compile stores program on channel") {
    amos_state_t *s = vv_create();
    int result = amos_amal_compile(s, 0, "M 0,0,100,100;");
    VV_ASSERT(result == 0, "AMAL compile should succeed");
    VV_ASSERT(s->amal[0].program != NULL, "channel should have program after compile");
    vv_destroy(s);
}

VV_TEST("REQ-AML-011: AMAL On activates channel") {
    amos_state_t *s = vv_create();
    amos_amal_compile(s, 0, "M 0,0,100,100;");
    amos_amal_on(s, 0);
    VV_ASSERT(s->amal[0].active == true, "AMAL On should activate channel");
    vv_destroy(s);
}

VV_TEST("REQ-AML-012: AMAL Off deactivates channel") {
    amos_state_t *s = vv_create();
    amos_amal_compile(s, 0, "M 0,0,100,100;");
    amos_amal_on(s, 0);
    amos_amal_off(s, 0);
    VV_ASSERT(s->amal[0].active == false, "AMAL Off should deactivate channel");
    vv_destroy(s);
}

VV_TEST("REQ-AML-013: AMAL Freeze pauses channel") {
    amos_state_t *s = vv_create();
    amos_amal_compile(s, 0, "M 0,0,100,100;");
    amos_amal_on(s, 0);
    amos_amal_freeze(s, 0);
    VV_ASSERT(s->amal[0].frozen == true, "AMAL Freeze should set frozen flag");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  AMAL via Interpreter Commands
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-AML-020: Amal command compiles and stores program") {
    amos_state_t *s = vv_create();
    vv_run(s, "Amal 0,\"M 0,0,100,100;\"");
    VV_ASSERT(s->amal[0].program != NULL,
              "Amal command should compile and store program");
    vv_destroy(s);
}

VV_TEST("REQ-AML-021: Amal On command activates via interpreter") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "Amal 0,\"M 0,0,100,100;\"\n"
        "Amal On\n"
    );
    VV_ASSERT(s->amal[0].active == true,
              "Amal On should activate compiled channel");
    vv_destroy(s);
}

VV_TEST("REQ-AML-022: Amal Off command deactivates via interpreter") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "Amal 0,\"M 0,0,100,100;\"\n"
        "Amal On\n"
        "Amal Off\n"
    );
    /* Amal Off without args deactivates all */
    VV_ASSERT(s->amal[0].active == false,
              "Amal Off should deactivate channel");
    vv_destroy(s);
}
