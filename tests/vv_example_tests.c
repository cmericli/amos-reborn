/*
 * vv_example_tests.c — Example V&V subsystem tests
 *
 * These demonstrate the framework by testing interpreter behaviors
 * against AMOS Basic specification requirements.
 */

#include "vv_framework.h"

/* ══════════════════════════════════════════════════════════════════
 *  REQ-INT: Interpreter core requirements
 * ══════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-INT-001: Integer variable assignment") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=42");
    VV_ASSERT_INT(s, "X", 42);
    vv_destroy(s);
}

VV_TEST("REQ-INT-002: Arithmetic precedence (multiply before add)") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=2+3*4");
    VV_ASSERT_INT(s, "X", 14);
    vv_destroy(s);
}

VV_TEST("REQ-INT-003: Parenthesized expression overrides precedence") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=(2+3)*4");
    VV_ASSERT_INT(s, "X", 20);
    vv_destroy(s);
}

VV_TEST("REQ-INT-004: Negative numbers via unary minus") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=-5\nY=10+X");
    VV_ASSERT_INT(s, "X", -5);
    VV_ASSERT_INT(s, "Y", 5);
    vv_destroy(s);
}

VV_TEST("REQ-INT-005: TRUE equals -1 (AMOS convention)") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=(1=1)");
    VV_ASSERT_INT(s, "X", -1);
    vv_destroy(s);
}

VV_TEST("REQ-INT-006: FALSE equals 0") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=(1=2)");
    VV_ASSERT_INT(s, "X", 0);
    vv_destroy(s);
}

VV_TEST("REQ-INT-007: For/Next loop accumulates correctly") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "S=0\n"
        "For I=1 To 5\n"
        "S=S+I\n"
        "Next I\n"
    );
    VV_ASSERT_FLOAT(s, "S", 15.0, 0.1);
    vv_destroy(s);
}

VV_TEST("REQ-INT-008: Colon separator executes multiple statements") {
    amos_state_t *s = vv_create();
    vv_run(s, "A=1 : B=2 : C=A+B");
    VV_ASSERT_INT(s, "A", 1);
    VV_ASSERT_INT(s, "B", 2);
    VV_ASSERT_INT(s, "C", 3);
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════
 *  REQ-STR: String function requirements
 * ══════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-STR-001: String variable assignment and retrieval") {
    amos_state_t *s = vv_create();
    vv_run(s, "A$=\"Hello\"");
    VV_ASSERT_STR(s, "A$", "Hello");
    vv_destroy(s);
}

VV_TEST("REQ-STR-002: String concatenation with + operator") {
    amos_state_t *s = vv_create();
    vv_run(s, "A$=\"Hello\"+\" \"+\"World\"");
    VV_ASSERT_STR(s, "A$", "Hello World");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════
 *  REQ-GFX: Graphics subsystem requirements
 * ══════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-GFX-001: Default screen dimensions are 320x256") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "W=Screen Width\n"
        "H=Screen Height\n"
    );
    VV_ASSERT_INT(s, "W", 320);
    VV_ASSERT_INT(s, "H", 256);
    vv_destroy(s);
}

VV_TEST("REQ-GFX-002: Gr Writing sets drawing mode") {
    amos_state_t *s = vv_create();
    vv_run(s, "Gr Writing 2");
    amos_screen_t *scr = &s->screens[s->current_screen];
    VV_ASSERT(scr->writing_mode == 2,
              "Gr Writing 2 should set XOR mode");
    vv_destroy(s);
}

VV_TEST("REQ-GFX-003: Set Line stores pattern mask") {
    amos_state_t *s = vv_create();
    vv_run(s, "Set Line $AAAA");
    VV_ASSERT(s->line_pattern == 0xAAAA,
              "Set Line $AAAA should store 0xAAAA");
    vv_destroy(s);
}

VV_TEST("REQ-GFX-004: Clip command sets clipping rectangle") {
    amos_state_t *s = vv_create();
    vv_run(s, "Clip 10,20 To 100,200");
    VV_ASSERT(s->clip_enabled == true, "clip should be enabled");
    VV_ASSERT(s->clip_x1 == 10, "clip_x1 should be 10");
    VV_ASSERT(s->clip_y1 == 20, "clip_y1 should be 20");
    VV_ASSERT(s->clip_x2 == 100, "clip_x2 should be 100");
    VV_ASSERT(s->clip_y2 == 200, "clip_y2 should be 200");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════
 *  REQ-BNK: Memory bank requirements
 * ══════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-BNK-001: Reserve As Work allocates bank with correct length") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "Reserve As Work 5,1024\n"
        "L=Length(5)\n"
    );
    VV_ASSERT_INT(s, "L", 1024);
    vv_destroy(s);
}

VV_TEST("REQ-BNK-002: Erase removes bank and Length returns 0") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "Reserve As Work 5,1024\n"
        "Erase 5\n"
        "L=Length(5)\n"
    );
    VV_ASSERT_INT(s, "L", 0);
    vv_destroy(s);
}
