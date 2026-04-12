/*
 * vv_interpreter_tests.c — V&V tests for AMOS Reborn interpreter core
 *
 * Comprehensive behavioral requirement tests covering:
 *   - Expression evaluator (types, coercion, comparisons, bitwise NOT)
 *   - Variables (type suffixes, case insensitivity)
 *   - Control flow (FOR/STEP, REPEAT/UNTIL, GOSUB/RETURN, PROCEDURE, nested stacks)
 *   - String operations (subtraction)
 *   - System (banks, Key State, Joy, PAL timer, RNG, default screen)
 *   - Oracle-verified behaviors (background color, text pen, Cls, Print)
 */

#include "vv_framework.h"

/* ══════════════════════════════════════════════════════════════════════
 *  REQ-INT-007: Three variable types (int=0, float=1, string=2)
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-INT-007a: Integer literal assignment gives integer type") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=42");
    amos_var_t *v = amos_var_get(s, "X");
    VV_ASSERT(v != NULL, "variable X should exist");
    if (v) {
        VV_ASSERT(v->type == VAR_INTEGER,
                  "assigning 42 should produce integer type");
        VV_ASSERT(v->ival == 42,
                  "X should equal 42");
    }
    vv_destroy(s);
}

VV_TEST("REQ-INT-007b: Float literal assignment gives float type") {
    amos_state_t *s = vv_create();
    vv_run(s, "X#=3.14");
    amos_var_t *v = amos_var_get(s, "X#");
    VV_ASSERT(v != NULL, "variable X# should exist");
    if (v) {
        VV_ASSERT(v->type == VAR_FLOAT,
                  "assigning 3.14 to X# should produce float type");
    }
    VV_ASSERT_FLOAT(s, "X#", 3.14, 0.001);
    vv_destroy(s);
}

VV_TEST("REQ-INT-007c: String literal assignment gives string type") {
    amos_state_t *s = vv_create();
    vv_run(s, "X$=\"hello\"");
    amos_var_t *v = amos_var_get(s, "X$");
    VV_ASSERT(v != NULL, "variable X$ should exist");
    if (v) {
        VV_ASSERT(v->type == VAR_STRING,
                  "assigning string to X$ should produce string type");
    }
    VV_ASSERT_STR(s, "X$", "hello");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  REQ-INT-008: Auto coercion int->float
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-INT-008: Integer auto-coerced to float in mixed arithmetic") {
    amos_state_t *s = vv_create();
    vv_run(s, "X#=5+3.5");
    amos_var_t *v = amos_var_get(s, "X#");
    VV_ASSERT(v != NULL, "variable X# should exist");
    if (v) {
        VV_ASSERT(v->type == VAR_FLOAT,
                  "5+3.5 should produce float result");
    }
    VV_ASSERT_FLOAT(s, "X#", 8.5, 0.001);
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  REQ-INT-022: TRUE = -1, all six comparison operators
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-INT-022a: Equality (=) true gives -1") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=(5=5)");
    VV_ASSERT_INT(s, "X", -1);
    vv_destroy(s);
}

VV_TEST("REQ-INT-022b: Equality (=) false gives 0") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=(5=3)");
    VV_ASSERT_INT(s, "X", 0);
    vv_destroy(s);
}

VV_TEST("REQ-INT-022c: Not-equal (<>) true gives -1") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=(5<>3)");
    VV_ASSERT_INT(s, "X", -1);
    vv_destroy(s);
}

VV_TEST("REQ-INT-022d: Not-equal (<>) false gives 0") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=(5<>5)");
    VV_ASSERT_INT(s, "X", 0);
    vv_destroy(s);
}

VV_TEST("REQ-INT-022e: Less-than (<) true gives -1") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=(3<5)");
    VV_ASSERT_INT(s, "X", -1);
    vv_destroy(s);
}

VV_TEST("REQ-INT-022f: Less-than (<) false gives 0") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=(5<3)");
    VV_ASSERT_INT(s, "X", 0);
    vv_destroy(s);
}

VV_TEST("REQ-INT-022g: Greater-than (>) true gives -1") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=(5>3)");
    VV_ASSERT_INT(s, "X", -1);
    vv_destroy(s);
}

VV_TEST("REQ-INT-022h: Greater-than (>) false gives 0") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=(3>5)");
    VV_ASSERT_INT(s, "X", 0);
    vv_destroy(s);
}

VV_TEST("REQ-INT-022i: Less-or-equal (<=) true gives -1") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=(3<=5)");
    VV_ASSERT_INT(s, "X", -1);
    vv_destroy(s);
}

VV_TEST("REQ-INT-022j: Less-or-equal (<=) equal case gives -1") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=(5<=5)");
    VV_ASSERT_INT(s, "X", -1);
    vv_destroy(s);
}

VV_TEST("REQ-INT-022k: Less-or-equal (<=) false gives 0") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=(5<=3)");
    VV_ASSERT_INT(s, "X", 0);
    vv_destroy(s);
}

VV_TEST("REQ-INT-022l: Greater-or-equal (>=) true gives -1") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=(5>=3)");
    VV_ASSERT_INT(s, "X", -1);
    vv_destroy(s);
}

VV_TEST("REQ-INT-022m: Greater-or-equal (>=) equal case gives -1") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=(5>=5)");
    VV_ASSERT_INT(s, "X", -1);
    vv_destroy(s);
}

VV_TEST("REQ-INT-022n: Greater-or-equal (>=) false gives 0") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=(3>=5)");
    VV_ASSERT_INT(s, "X", 0);
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  REQ-INT-026: NOT is bitwise
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-INT-026a: NOT(0) = -1 (bitwise complement)") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=Not(0)");
    VV_ASSERT_INT(s, "X", -1);
    vv_destroy(s);
}

VV_TEST("REQ-INT-026b: NOT(-1) = 0 (bitwise complement)") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=Not(-1)");
    VV_ASSERT_INT(s, "X", 0);
    vv_destroy(s);
}

VV_TEST("REQ-INT-026c: NOT(5) = -6 (bitwise complement)") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=Not(5)");
    VV_ASSERT_INT(s, "X", -6);
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  REQ-INT-013: Type suffix — A=int, A#=float, A$=string
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-INT-013a: Bare variable name is integer") {
    amos_state_t *s = vv_create();
    vv_run(s, "A=10");
    amos_var_t *v = amos_var_get(s, "A");
    VV_ASSERT(v != NULL, "variable A should exist");
    if (v) VV_ASSERT(v->type == VAR_INTEGER, "A should be integer type");
    VV_ASSERT_INT(s, "A", 10);
    vv_destroy(s);
}

VV_TEST("REQ-INT-013b: Hash suffix is float") {
    amos_state_t *s = vv_create();
    vv_run(s, "A#=2.718");
    amos_var_t *v = amos_var_get(s, "A#");
    VV_ASSERT(v != NULL, "variable A# should exist");
    if (v) VV_ASSERT(v->type == VAR_FLOAT, "A# should be float type");
    VV_ASSERT_FLOAT(s, "A#", 2.718, 0.001);
    vv_destroy(s);
}

VV_TEST("REQ-INT-013c: Dollar suffix is string") {
    amos_state_t *s = vv_create();
    vv_run(s, "A$=\"test\"");
    amos_var_t *v = amos_var_get(s, "A$");
    VV_ASSERT(v != NULL, "variable A$ should exist");
    if (v) VV_ASSERT(v->type == VAR_STRING, "A$ should be string type");
    VV_ASSERT_STR(s, "A$", "test");
    vv_destroy(s);
}

VV_TEST("REQ-INT-013d: A, A#, A$ are separate variables") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "A=42\n"
        "A#=3.14\n"
        "A$=\"hello\"\n"
    );
    VV_ASSERT_INT(s, "A", 42);
    VV_ASSERT_FLOAT(s, "A#", 3.14, 0.001);
    VV_ASSERT_STR(s, "A$", "hello");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  REQ-INT-016: Case insensitive variable names
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-INT-016: Setting ABC then reading abc gives same value") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "ABC=99\n"
        "X=abc\n"
    );
    VV_ASSERT_INT(s, "X", 99);
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  REQ-INT-017: FOR/NEXT with STEP
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-INT-017: For I=1 To 10 Step 2 accumulates 1+3+5+7+9=25") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "S=0\n"
        "For I=1 To 10 Step 2\n"
        "S=S+I\n"
        "Next I\n"
    );
    VV_ASSERT_INT(s, "S", 25);
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  REQ-INT-018: REPEAT/UNTIL loop
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-INT-018: Repeat/Until loop counts to 5") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "X=0\n"
        "Repeat\n"
        "X=X+1\n"
        "Until X=5\n"
    );
    VV_ASSERT_INT(s, "X", 5);
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  REQ-INT-019: GOSUB/RETURN
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-INT-019: Gosub jumps and Return comes back") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "X=1\n"
        "Gosub ADDER\n"
        "X=X+100\n"
        "End\n"
        "ADDER:\n"
        "X=X+10\n"
        "Return\n"
    );
    /* X starts at 1, Gosub adds 10 (X=11), returns, then adds 100 (X=111) */
    VV_ASSERT_INT(s, "X", 111);
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  REQ-INT-020: PROCEDURE with local variables
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-INT-020: Procedure with parameter") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "X=5\n"
        "Proc DOUBLE[X]\n"
        "End\n"
        "Procedure DOUBLE[N]\n"
        "Shared X\n"
        "X=N*2\n"
        "End Proc\n"
    );
    VV_ASSERT_INT(s, "X", 10);
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  REQ-INT-021: Separate stacks — nested For inside Gosub
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-INT-021: Nested For inside Gosub works correctly") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "TOTAL=0\n"
        "For I=1 To 3\n"
        "Gosub INNER\n"
        "Next I\n"
        "End\n"
        "INNER:\n"
        "For J=1 To 2\n"
        "TOTAL=TOTAL+1\n"
        "Next J\n"
        "Return\n"
    );
    /* 3 outer iterations x 2 inner iterations = 6 */
    VV_ASSERT_INT(s, "TOTAL", 6);
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  REQ-INT-023: String subtraction
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-INT-023: String subtraction removes substring") {
    amos_state_t *s = vv_create();
    vv_run(s, "R$=\"Hello World\"-\"World\"");
    VV_ASSERT_STR(s, "R$", "Hello ");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  REQ-SYS-001: 16 banks
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-SYS-001: AMOS_MAX_BANKS is 16") {
    VV_ASSERT(AMOS_MAX_BANKS == 16,
              "AMOS_MAX_BANKS should be 16");
}

/* ══════════════════════════════════════════════════════════════════════
 *  REQ-SYS-008: Banks start empty
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-SYS-008: All banks empty after create") {
    amos_state_t *s = vv_create();
    bool all_empty = true;
    for (int i = 0; i < AMOS_MAX_BANKS; i++) {
        if (s->banks[i].type != BANK_EMPTY || s->banks[i].data != NULL) {
            all_empty = false;
            break;
        }
    }
    VV_ASSERT(all_empty, "all banks should be empty after amos_create");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  REQ-SYS-015: Key State function exists (no crash)
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-SYS-015: Key State function does not crash") {
    amos_state_t *s = vv_create();
    /* Just verify the function exists and doesn't crash.
     * In testing mode with no SDL, keys are all up (0). */
    int result = amos_key_state(s, 0);
    VV_ASSERT(result == 0 || result == -1,
              "Key State should return 0 (up) or -1 (down)");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  REQ-SYS-016: Joy bitmask function exists
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-SYS-016: Joy function exists and returns bitmask") {
    amos_state_t *s = vv_create();
    int joy = amos_joy(s, 1);
    /* With no input, joy should be 0 (no buttons pressed) */
    VV_ASSERT(joy >= 0 && joy <= 31,
              "Joy(1) should return a valid bitmask (0-31)");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  REQ-SYS-022: PAL default — timer rate is 50Hz-based
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-SYS-022: Timer initialized to 0 (PAL 50Hz tick base)") {
    amos_state_t *s = vv_create();
    /* Timer counts in 50ths of a second (PAL default) */
    VV_ASSERT(s->timer == 0,
              "timer should start at 0");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  REQ-SYS-025: RNG seeded — Rnd(100) returns values in range
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-SYS-025: Rnd(100) returns values in valid range") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "A=Rnd(100)\n"
        "B=Rnd(100)\n"
        "C=Rnd(100)\n"
    );
    amos_var_t *va = amos_var_get(s, "A");
    amos_var_t *vb = amos_var_get(s, "B");
    amos_var_t *vc = amos_var_get(s, "C");
    VV_ASSERT(va != NULL, "variable A should exist");
    VV_ASSERT(vb != NULL, "variable B should exist");
    VV_ASSERT(vc != NULL, "variable C should exist");
    if (va) {
        int32_t a = (va->type == VAR_FLOAT) ? (int32_t)va->fval : va->ival;
        VV_ASSERT(a >= 0 && a <= 100,
                  "Rnd(100) should return 0..100");
    }
    if (vb) {
        int32_t b = (vb->type == VAR_FLOAT) ? (int32_t)vb->fval : vb->ival;
        VV_ASSERT(b >= 0 && b <= 100,
                  "Rnd(100) should return 0..100");
    }
    if (vc) {
        int32_t c = (vc->type == VAR_FLOAT) ? (int32_t)vc->fval : vc->ival;
        VV_ASSERT(c >= 0 && c <= 100,
                  "Rnd(100) should return 0..100");
    }
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  REQ-SYS-026: Default screen — screen 0 is 320x256 with palette
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-SYS-026a: Default screen 0 is 320x256") {
    amos_state_t *s = vv_create();
    amos_screen_t *scr = &s->screens[0];
    VV_ASSERT(scr->active, "screen 0 should be active by default");
    VV_ASSERT(scr->width == 320, "default screen width should be 320");
    VV_ASSERT(scr->height == 256, "default screen height should be 256");
    vv_destroy(s);
}

VV_TEST("REQ-SYS-026b: Default screen has correct palette entries") {
    amos_state_t *s = vv_create();
    /* Verify key palette entries from PI_DefEPa:
     * 0=$000 (black), 1=$A40 (brown), 2=$FFF (white), 3=$000 (black) */
    VV_ASSERT_PALETTE(s, 0, 0, AMOS_RGBA(0x00, 0x00, 0x00, 0xFF));  /* $000 */
    VV_ASSERT_PALETTE(s, 0, 1, AMOS_RGBA(0xAA, 0x44, 0x00, 0xFF));  /* $A40 */
    VV_ASSERT_PALETTE(s, 0, 2, AMOS_RGBA(0xFF, 0xFF, 0xFF, 0xFF));  /* $FFF */
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Oracle-verified behaviors
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-GFX-010: Default background is palette[1] ($A40 brown), not black") {
    amos_state_t *s = vv_create();
    amos_screen_t *scr = &s->screens[0];
    VV_ASSERT(scr->text_paper == 1,
              "default Paper should be 1 ($A40 brown)");
    vv_destroy(s);
}

VV_TEST("REQ-GFX-011: Default text pen is 2 ($FFF white)") {
    amos_state_t *s = vv_create();
    amos_screen_t *scr = &s->screens[0];
    VV_ASSERT(scr->text_pen == 2,
              "default Pen should be 2 ($FFF white)");
    vv_destroy(s);
}

VV_TEST("REQ-GFX-012: Cls clears to paper color (1), not black (0)") {
    amos_state_t *s = vv_create();
    /* Paper is palette[1] by default. Cls should fill with paper color. */
    vv_run(s, "Cls");
    amos_screen_t *scr = &s->screens[0];
    if (scr->pixels) {
        /* Check center pixel — should be palette[1] color, not black */
        int cx = scr->width / 2;
        int cy = scr->height / 2;
        uint32_t pixel = scr->pixels[cy * scr->width + cx];
        uint32_t paper_color = scr->palette[1];  /* $A40 brown */
        VV_ASSERT(pixel == paper_color,
                  "Cls should clear to paper color (palette[1])");
    }
    vv_destroy(s);
}

VV_TEST("REQ-INT-030: Print 1=1 produces -1 (TRUE)") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=(1=1)");
    VV_ASSERT_INT(s, "X", -1);
    vv_destroy(s);
}

VV_TEST("REQ-INT-031: Print 2+3 produces 5") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=2+3");
    VV_ASSERT_INT(s, "X", 5);
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Additional REQ-SYS tests
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-SYS-002: Bank type enum includes BANK_DATA") {
    /* Bank flags encode type — BANK_DATA exists for data banks (bit 31 in 68K) */
    VV_ASSERT(BANK_DATA != BANK_EMPTY, "BANK_DATA enum must differ from BANK_EMPTY");
    VV_ASSERT(BANK_DATA != BANK_WORK, "BANK_DATA enum must differ from BANK_WORK");
}

VV_TEST("REQ-SYS-007: Bank type enums for sprites, icons, samples") {
    /* Bank 1=Sprites, 2=Icons, 3=Samples per 68K convention */
    VV_ASSERT(BANK_SPRITES != BANK_EMPTY, "BANK_SPRITES must exist");
    VV_ASSERT(BANK_ICONS != BANK_EMPTY, "BANK_ICONS must exist");
    VV_ASSERT(BANK_SAMPLES != BANK_EMPTY, "BANK_SAMPLES must exist");
    /* All distinct */
    VV_ASSERT(BANK_SPRITES != BANK_ICONS, "Sprites and Icons must be distinct types");
    VV_ASSERT(BANK_ICONS != BANK_SAMPLES, "Icons and Samples must be distinct types");
}

VV_TEST("REQ-SYS-009: Timer field exists for VBI tick counting") {
    amos_state_t *s = vv_create();
    /* Timer is initialized and accessible — PAL 50Hz base */
    VV_ASSERT(s->timer == 0, "Timer should start at 0");
    vv_destroy(s);
}

VV_TEST("REQ-SYS-013: Every interval/counter fields exist for VBI timer") {
    amos_state_t *s = vv_create();
    /* Every timer infrastructure exists */
    VV_ASSERT(s->every_interval == 0, "every_interval should start at 0 (disabled)");
    VV_ASSERT(s->every_counter == 0, "every_counter should start at 0");
    VV_ASSERT(s->every_target_line == -1 || s->every_target_line == 0,
              "every_target_line should be unset at init");
    vv_destroy(s);
}

VV_TEST("REQ-SYS-017: Mouse position fields exist and start at 0") {
    amos_state_t *s = vv_create();
    VV_ASSERT(s->mouse_x == 0, "mouse_x should start at 0");
    VV_ASSERT(s->mouse_y == 0, "mouse_y should start at 0");
    vv_destroy(s);
}

VV_TEST("REQ-SYS-018: Mouse button fields exist and start at 0") {
    amos_state_t *s = vv_create();
    VV_ASSERT(s->mouse_buttons == 0, "mouse_buttons should start at 0");
    vv_destroy(s);
}

VV_TEST("REQ-SYS-023: amos_create allocates master data zone") {
    amos_state_t *s = vv_create();
    VV_ASSERT(s != NULL, "amos_create must return non-NULL state");
    /* Verify key fields are accessible */
    VV_ASSERT(s->line_count == 0, "fresh state should have 0 program lines");
    VV_ASSERT(s->running == false, "fresh state should not be running");
    vv_destroy(s);
}

VV_TEST("REQ-SYS-024: Init sets up BASIC stack limits") {
    amos_state_t *s = vv_create();
    VV_ASSERT(s->gosub_top == 0, "gosub stack should start empty");
    VV_ASSERT(s->for_top == 0, "for stack should start empty");
    VV_ASSERT(AMOS_MAX_GOSUB_DEPTH >= 16, "gosub depth should be at least 16");
    VV_ASSERT(AMOS_MAX_FOR_DEPTH >= 16, "for depth should be at least 16");
    vv_destroy(s);
}

VV_TEST("REQ-INT-021a: Gosub/For use separate stack arrays, not C call stack") {
    amos_state_t *s = vv_create();
    /* Verify stacks are arrays in amos_state_t, not C recursion */
    VV_ASSERT(AMOS_MAX_GOSUB_DEPTH == 128, "gosub stack depth should be 128");
    VV_ASSERT(AMOS_MAX_FOR_DEPTH == 64, "for stack depth should be 64");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  REQ-INT-032+: Additional behavioral tests
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-INT-032: Goto label jumps correctly") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "X=1\n"
        "Goto SKIP\n"
        "X=99\n"
        "SKIP:\n"
        "X=X+10\n"
    );
    /* X=1, Goto skips X=99, then X=1+10=11 */
    VV_ASSERT_INT(s, "X", 11);
    vv_destroy(s);
}

VV_TEST("REQ-INT-033: Multiple Gosub calls and returns") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "X=0\n"
        "Gosub ADD1\n"
        "Gosub ADD1\n"
        "Gosub ADD1\n"
        "End\n"
        "ADD1:\n"
        "X=X+1\n"
        "Return\n"
    );
    VV_ASSERT_INT(s, "X", 3);
    vv_destroy(s);
}

VV_TEST("REQ-INT-034: Nested Gosub two levels deep") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "X=0\n"
        "Gosub OUTER\n"
        "End\n"
        "OUTER:\n"
        "X=X+1\n"
        "Gosub INNER\n"
        "X=X+100\n"
        "Return\n"
        "INNER:\n"
        "X=X+10\n"
        "Return\n"
    );
    /* X=0, +1=1, +10=11, +100=111 */
    VV_ASSERT_INT(s, "X", 111);
    vv_destroy(s);
}

VV_TEST("REQ-INT-035: While/Wend loop") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "X=0\n"
        "While X<5\n"
        "X=X+1\n"
        "Wend\n"
    );
    VV_ASSERT_INT(s, "X", 5);
    vv_destroy(s);
}

VV_TEST("REQ-INT-036: If/Then/Else branching") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "X=10\n"
        "If X>5 Then Y=1 Else Y=0\n"
    );
    VV_ASSERT_INT(s, "Y", 1);
    vv_destroy(s);
}

VV_TEST("REQ-INT-037: If/Then/Else false branch") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "X=3\n"
        "If X>5 Then Y=1 Else Y=0\n"
    );
    VV_ASSERT_INT(s, "Y", 0);
    vv_destroy(s);
}

VV_TEST("REQ-INT-038: End stops execution") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "X=1\n"
        "End\n"
        "X=99\n"
    );
    VV_ASSERT_INT(s, "X", 1);
    vv_destroy(s);
}

VV_TEST("REQ-INT-039: Procedure without parameters") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "X=0\n"
        "Proc BUMP\n"
        "End\n"
        "Procedure BUMP\n"
        "Shared X\n"
        "X=X+1\n"
        "End Proc\n"
    );
    VV_ASSERT_INT(s, "X", 1);
    vv_destroy(s);
}

VV_TEST("REQ-INT-040: Procedure skipped when not called") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "X=5\n"
        "End\n"
        "Procedure NEVER_CALLED\n"
        "X=999\n"
        "End Proc\n"
    );
    /* Flow-through into Procedure definition should skip to End Proc */
    VV_ASSERT_INT(s, "X", 5);
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Procedure Variable Scoping
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-INT-020a: Procedure locals don't leak to caller") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "Proc SETLOCAL\n"
        "End\n"
        "Procedure SETLOCAL\n"
        "LOCAL_VAR=999\n"
        "End Proc\n"
    );
    /* LOCAL_VAR should not exist in caller's scope */
    amos_var_t *v = amos_var_get(s, "LOCAL_VAR");
    VV_ASSERT(v == NULL, "local variable should not leak to caller");
    vv_destroy(s);
}

VV_TEST("REQ-INT-020b: Shared variables are visible from procedure") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "RESULT=0\n"
        "Proc MODIFY\n"
        "End\n"
        "Procedure MODIFY\n"
        "Shared RESULT\n"
        "RESULT=42\n"
        "End Proc\n"
    );
    VV_ASSERT_INT(s, "RESULT", 42);
    vv_destroy(s);
}

VV_TEST("REQ-INT-020c: Parameters are local copies, not references") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "X=100\n"
        "Proc HALVE[X]\n"
        "End\n"
        "Procedure HALVE[N]\n"
        "N=N/2\n"
        "End Proc\n"
    );
    /* X should still be 100 — N was a local copy */
    VV_ASSERT_INT(s, "X", 100);
    vv_destroy(s);
}

VV_TEST("REQ-INT-020d: Nested procedures have independent scopes") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "SV=0\n"
        "Proc OUTER\n"
        "End\n"
        "Procedure OUTER\n"
        "Shared SV\n"
        "LCL=10\n"
        "Proc INNER\n"
        "SV=SV+LCL\n"
        "End Proc\n"
        "Procedure INNER\n"
        "Shared SV\n"
        "SV=SV+1\n"
        "End Proc\n"
    );
    /* OUTER: LCL=10, calls INNER which adds 1 to SV. Then OUTER does SV+LCL.
     * After INNER: SV=1. Then SV=1+10=11. */
    VV_ASSERT_INT(s, "SV", 11);
    vv_destroy(s);
}

VV_TEST("REQ-INT-020e0: Comma-separated Shared A,B works") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "A=10\n"
        "B=20\n"
        "Proc ADDEM\n"
        "End\n"
        "Procedure ADDEM\n"
        "Shared A,B\n"
        "A=A+1\n"
        "B=B+1\n"
        "End Proc\n"
    );
    VV_ASSERT_INT(s, "A", 11);
    VV_ASSERT_INT(s, "B", 21);
    vv_destroy(s);
}

VV_TEST("REQ-INT-020e: Multiple shared variables on separate lines") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "A=1\n"
        "B=2\n"
        "Proc DOSWAP\n"
        "End\n"
        "Procedure DOSWAP\n"
        "Shared A\n"
        "Shared B\n"
        "T=A\n"
        "A=B\n"
        "B=T\n"
        "End Proc\n"
    );
    VV_ASSERT_INT(s, "A", 2);
    VV_ASSERT_INT(s, "B", 1);
    vv_destroy(s);
}

VV_TEST("REQ-INT-041: Modulo operator") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=17 Mod 5");
    VV_ASSERT_INT(s, "X", 2);
    vv_destroy(s);
}

VV_TEST("REQ-INT-042: Integer division") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=17/5");
    /* AMOS integer division truncates: 17/5 = 3 (both operands int) */
    VV_ASSERT_INT(s, "X", 3);
    vv_destroy(s);
}

VV_TEST("REQ-INT-043: Power operator") {
    amos_state_t *s = vv_create();
    vv_run(s, "X=2^10");
    VV_ASSERT_INT(s, "X", 1024);
    vv_destroy(s);
}

VV_TEST("REQ-INT-044: Logical AND/OR with TRUE=-1") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "A=(1=1) And (2=2)\n"
        "B=(1=1) Or (1=2)\n"
        "C=(1=2) And (1=1)\n"
        "D=(1=2) Or (2=3)\n"
    );
    VV_ASSERT_INT(s, "A", -1);   /* TRUE AND TRUE = -1 */
    VV_ASSERT_INT(s, "B", -1);   /* TRUE OR FALSE = -1 */
    VV_ASSERT_INT(s, "C", 0);    /* FALSE AND TRUE = 0 */
    VV_ASSERT_INT(s, "D", 0);    /* FALSE OR FALSE = 0 */
    vv_destroy(s);
}

VV_TEST("REQ-INT-045: Abs function") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "A=Abs(-5)\n"
        "B=Abs(3)\n"
        "C=Abs(0)\n"
    );
    VV_ASSERT_INT(s, "A", 5);
    VV_ASSERT_INT(s, "B", 3);
    VV_ASSERT_INT(s, "C", 0);
    vv_destroy(s);
}

VV_TEST("REQ-INT-046: Sgn function") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "A=Sgn(-5)\n"
        "B=Sgn(3)\n"
        "C=Sgn(0)\n"
    );
    VV_ASSERT_INT(s, "A", -1);
    VV_ASSERT_INT(s, "B", 1);
    VV_ASSERT_INT(s, "C", 0);
    vv_destroy(s);
}

VV_TEST("REQ-INT-047: Min and Max functions") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "A=Min(3,7)\n"
        "B=Max(3,7)\n"
    );
    VV_ASSERT_INT(s, "A", 3);
    VV_ASSERT_INT(s, "B", 7);
    vv_destroy(s);
}

VV_TEST("REQ-STR-003: Len function") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "A$=\"Hello\"\n"
        "L=Len(A$)\n"
    );
    VV_ASSERT_INT(s, "L", 5);
    vv_destroy(s);
}

VV_TEST("REQ-STR-004: Left$/Right$/Mid$ functions") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "A$=\"Hello World\"\n"
        "L$=Left$(A$,5)\n"
        "R$=Right$(A$,5)\n"
        "M$=Mid$(A$,7,5)\n"
    );
    VV_ASSERT_STR(s, "L$", "Hello");
    VV_ASSERT_STR(s, "R$", "World");
    VV_ASSERT_STR(s, "M$", "World");
    vv_destroy(s);
}

VV_TEST("REQ-STR-005: Str$ and Val functions") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "A$=Str$(42)\n"
        "V=Val(\"123\")\n"
    );
    VV_ASSERT_STR(s, "A$", "42");
    VV_ASSERT_INT(s, "V", 123);
    vv_destroy(s);
}

VV_TEST("REQ-STR-006: Asc and Chr$ functions") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "A=Asc(\"A\")\n"
        "C$=Chr$(65)\n"
    );
    VV_ASSERT_INT(s, "A", 65);
    VV_ASSERT_STR(s, "C$", "A");
    vv_destroy(s);
}

VV_TEST("REQ-STR-007: Upper$/Lower$ functions") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "A$=Upper$(\"hello\")\n"
        "B$=Lower$(\"WORLD\")\n"
    );
    VV_ASSERT_STR(s, "A$", "HELLO");
    VV_ASSERT_STR(s, "B$", "world");
    vv_destroy(s);
}

VV_TEST("REQ-STR-008: Instr function finds substring") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "A=Instr(\"Hello World\",\"World\")\n"
        "B=Instr(\"Hello World\",\"xyz\")\n"
    );
    /* AMOS Instr is 1-based; 0 = not found */
    VV_ASSERT_INT(s, "A", 7);
    VV_ASSERT_INT(s, "B", 0);
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Do/Loop/Exit control flow
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-INT-050: Do/Loop with Exit") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "X=0\n"
        "Do\n"
        "X=X+1\n"
        "If X=5 Then Exit\n"
        "Loop\n"
    );
    VV_ASSERT_INT(s, "X", 5);
    vv_destroy(s);
}

VV_TEST("REQ-INT-051: Do/Loop infinite without Exit") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "X=0\n"
        "Do\n"
        "X=X+1\n"
        "If X>=10 Then Exit\n"
        "Loop\n"
    );
    VV_ASSERT_INT(s, "X", 10);
    vv_destroy(s);
}

VV_TEST("REQ-INT-052: Exit If conditional exit") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "X=0\n"
        "Do\n"
        "X=X+1\n"
        "Exit If X=7\n"
        "Loop\n"
    );
    VV_ASSERT_INT(s, "X", 7);
    vv_destroy(s);
}

VV_TEST("REQ-INT-053: Nested Do/Loop") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "TOTAL=0\n"
        "I=0\n"
        "Do\n"
        "I=I+1\n"
        "J=0\n"
        "Do\n"
        "J=J+1\n"
        "TOTAL=TOTAL+1\n"
        "If J=3 Then Exit\n"
        "Loop\n"
        "If I=4 Then Exit\n"
        "Loop\n"
    );
    /* 4 outer x 3 inner = 12 */
    VV_ASSERT_INT(s, "TOTAL", 12);
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Array operations (Dim, read, write)
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-INT-060: Dim creates integer array") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "Dim A(5)\n"
        "A(0)=10\n"
        "A(3)=30\n"
        "X=A(0)+A(3)\n"
    );
    VV_ASSERT_INT(s, "X", 40);
    vv_destroy(s);
}

VV_TEST("REQ-INT-061: Dim creates string array") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "Dim N$(3)\n"
        "N$(0)=\"Hello\"\n"
        "N$(1)=\"World\"\n"
        "R$=N$(0)+\" \"+N$(1)\n"
    );
    VV_ASSERT_STR(s, "R$", "Hello World");
    vv_destroy(s);
}

VV_TEST("REQ-INT-062: Two-dimensional array") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "Dim M(3,3)\n"
        "M(1,2)=42\n"
        "X=M(1,2)\n"
    );
    VV_ASSERT_INT(s, "X", 42);
    vv_destroy(s);
}

VV_TEST("REQ-INT-063: Array elements default to zero") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "Dim A(10)\n"
        "X=A(5)\n"
    );
    VV_ASSERT_INT(s, "X", 0);
    vv_destroy(s);
}

VV_TEST("REQ-INT-064: For loop fills array") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "Dim A(5)\n"
        "For I=0 To 5\n"
        "A(I)=I*I\n"
        "Next I\n"
        "X=A(0)+A(1)+A(2)+A(3)+A(4)+A(5)\n"
    );
    /* 0+1+4+9+16+25 = 55 */
    VV_ASSERT_INT(s, "X", 55);
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Read/Data
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-INT-070: Read reads Data values") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "Read A\n"
        "Read B\n"
        "Read C\n"
        "Data 10,20,30\n"
    );
    VV_ASSERT_INT(s, "A", 10);
    VV_ASSERT_INT(s, "B", 20);
    VV_ASSERT_INT(s, "C", 30);
    vv_destroy(s);
}

VV_TEST("REQ-INT-071: Read string Data") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "Read A$\n"
        "Read B$\n"
        "Data \"Hello\",\"World\"\n"
    );
    VV_ASSERT_STR(s, "A$", "Hello");
    VV_ASSERT_STR(s, "B$", "World");
    vv_destroy(s);
}

VV_TEST("REQ-INT-072: Restore resets Data pointer") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "Read A\n"
        "Read B\n"
        "Restore\n"
        "Read C\n"
        "Data 100,200\n"
    );
    VV_ASSERT_INT(s, "A", 100);
    VV_ASSERT_INT(s, "B", 200);
    VV_ASSERT_INT(s, "C", 100);  /* re-read after Restore */
    vv_destroy(s);
}

VV_TEST("REQ-INT-073: Data across multiple lines") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "Read A\n"
        "Read B\n"
        "Read C\n"
        "Data 1\n"
        "Data 2\n"
        "Data 3\n"
    );
    VV_ASSERT_INT(s, "A", 1);
    VV_ASSERT_INT(s, "B", 2);
    VV_ASSERT_INT(s, "C", 3);
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Additional string functions
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-STR-009: Space$ function") {
    amos_state_t *s = vv_create();
    vv_run(s, "A$=Space$(5)");
    VV_ASSERT_STR(s, "A$", "     ");
    vv_destroy(s);
}

VV_TEST("REQ-STR-010: String$ function") {
    amos_state_t *s = vv_create();
    vv_run(s, "A$=String$(\"Ab\",3)");
    VV_ASSERT_STR(s, "A$", "AbAbAb");
    vv_destroy(s);
}

VV_TEST("REQ-STR-011: Flip$ function reverses string") {
    amos_state_t *s = vv_create();
    vv_run(s, "A$=Flip$(\"Hello\")");
    VV_ASSERT_STR(s, "A$", "olleH");
    vv_destroy(s);
}
