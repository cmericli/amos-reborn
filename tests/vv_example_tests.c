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

VV_TEST("REQ-GFX-005: Default palette matches PI_DefEPa from 68K source") {
    /*
     * From +Interpreter_Config.s lines 86-89:
     *   dc.w $000,$A40,$FFF,$000,$F00,$0F0,$00F,$666
     *   dc.w $555,$333,$733,$373,$773,$337,$737,$377
     */
    amos_state_t *s = vv_create();
    amos_screen_t *scr = &s->screens[0];
    VV_ASSERT_PALETTE(s, 0, 0, AMOS_RGBA(0x00,0x00,0x00,0xFF));  /* $000 */
    VV_ASSERT_PALETTE(s, 0, 1, AMOS_RGBA(0xAA,0x44,0x00,0xFF));  /* $A40 */
    VV_ASSERT_PALETTE(s, 0, 2, AMOS_RGBA(0xFF,0xFF,0xFF,0xFF));  /* $FFF */
    VV_ASSERT_PALETTE(s, 0, 4, AMOS_RGBA(0xFF,0x00,0x00,0xFF));  /* $F00 */
    VV_ASSERT_PALETTE(s, 0, 5, AMOS_RGBA(0x00,0xFF,0x00,0xFF));  /* $0F0 */
    VV_ASSERT_PALETTE(s, 0, 6, AMOS_RGBA(0x00,0x00,0xFF,0xFF));  /* $00F */
    vv_destroy(s);
}

VV_TEST("REQ-GFX-006: Default pen=2 paper=1 from 68K Wo3a") {
    /*
     * From +W.s line 13656: Paper=1, Pen=2 (when depth > 1)
     * From +W.s line 13664: Paper=0, Pen=1 (when depth == 1)
     */
    amos_state_t *s = vv_create();
    amos_screen_t *scr = &s->screens[0];
    VV_ASSERT(scr->text_pen == 2, "default Pen should be 2 ($FFF white)");
    VV_ASSERT(scr->text_paper == 1, "default Paper should be 1 ($A40 brown)");
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
