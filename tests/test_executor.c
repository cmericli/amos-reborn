/*
 * test_executor.c — Executor tests
 */

#include "amos.h"
#include <stdio.h>
#include <string.h>

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        errors++; \
    } \
} while(0)

int test_executor_all(void)
{
    int errors = 0;

    /* Test 1: Variable assignment */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state, "X=42");
        amos_run(state);
        amos_run_step(state);

        amos_var_t *var = amos_var_get(state, "X");
        ASSERT(var != NULL, "variable X not found");
        ASSERT(var->ival == 42, "X should be 42");
        amos_destroy(state);
    }

    /* Test 2: String variable */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state, "A$=\"Hello\"");
        amos_run(state);
        amos_run_step(state);

        amos_var_t *var = amos_var_get(state, "A$");
        ASSERT(var != NULL, "variable A$ not found");
        ASSERT(var->type == VAR_STRING, "A$ should be string");
        ASSERT(strcmp(var->sval.data, "Hello") == 0, "A$ should be Hello");
        amos_destroy(state);
    }

    /* Test 3: Arithmetic */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state, "X=10+20*3");
        amos_run(state);
        amos_run_step(state);

        amos_var_t *var = amos_var_get(state, "X");
        ASSERT(var != NULL, "variable X not found");
        ASSERT(var->ival == 70, "X should be 70 (10+20*3)");
        amos_destroy(state);
    }

    /* Test 4: For/Next loop */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state,
            "S=0\n"
            "For I=1 To 5\n"
            "S=S+I\n"
            "Next I\n"
        );
        amos_run(state);

        /* Execute enough steps for the loop */
        for (int i = 0; i < 100 && state->running; i++) {
            amos_run_step(state);
        }

        amos_var_t *var = amos_var_get(state, "S");
        ASSERT(var != NULL, "variable S not found");
        /* S should be 1+2+3+4+5 = 15 */
        /* Note: due to float conversion in executor, check approximately */
        double val = var->type == VAR_FLOAT ? var->fval : (double)var->ival;
        ASSERT(val >= 14.9 && val <= 15.1, "S should be 15 (sum 1..5)");
        amos_destroy(state);
    }

    /* Test 5: Multi-statement colon separator */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state, "A=1 : B=2 : C=A+B");
        amos_run(state);
        amos_run_step(state);

        amos_var_t *va = amos_var_get(state, "A");
        amos_var_t *vb = amos_var_get(state, "B");
        amos_var_t *vc = amos_var_get(state, "C");
        ASSERT(va != NULL && va->ival == 1, "A should be 1 (colon sep)");
        ASSERT(vb != NULL && vb->ival == 2, "B should be 2 (colon sep)");
        ASSERT(vc != NULL && vc->ival == 3, "C should be A+B=3 (colon sep)");
        amos_destroy(state);
    }

    /* Test 6: Multi-statement with Print */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state, "X=10 : Y=20 : Z=X*Y");
        amos_run(state);
        amos_run_step(state);

        amos_var_t *vz = amos_var_get(state, "Z");
        ASSERT(vz != NULL, "Z should exist");
        double val = vz->type == VAR_FLOAT ? vz->fval : (double)vz->ival;
        ASSERT(val >= 199.9 && val <= 200.1, "Z should be 200 (10*20)");
        amos_destroy(state);
    }

    /* Test 7: Select/Case/End Select */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state,
            "X=2\n"
            "Select X\n"
            "Case 1\n"
            "R=10\n"
            "Case 2\n"
            "R=20\n"
            "Case 3\n"
            "R=30\n"
            "End Select\n"
        );
        amos_run(state);
        for (int i = 0; i < 100 && state->running; i++)
            amos_run_step(state);

        amos_var_t *vr = amos_var_get(state, "R");
        ASSERT(vr != NULL, "R should exist (select/case)");
        ASSERT(vr->ival == 20, "R should be 20 (Case 2 matched)");
        amos_destroy(state);
    }

    /* Test 8: Select/Case with Default */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state,
            "X=99\n"
            "Select X\n"
            "Case 1\n"
            "R=10\n"
            "Case 2\n"
            "R=20\n"
            "Default\n"
            "R=999\n"
            "End Select\n"
        );
        amos_run(state);
        for (int i = 0; i < 100 && state->running; i++)
            amos_run_step(state);

        amos_var_t *vr = amos_var_get(state, "R");
        ASSERT(vr != NULL, "R should exist (select/default)");
        ASSERT(vr->ival == 999, "R should be 999 (Default matched)");
        amos_destroy(state);
    }

    /* Test 9: On X Goto */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state,
            "X=2\n"
            "On X Goto 10,20,30\n"
            "R=-1\n"
            "Goto 100\n"
            "10 R=10\n"
            "Goto 100\n"
            "20 R=20\n"
            "Goto 100\n"
            "30 R=30\n"
            "100 End\n"
        );
        amos_run(state);
        for (int i = 0; i < 100 && state->running; i++)
            amos_run_step(state);

        amos_var_t *vr = amos_var_get(state, "R");
        ASSERT(vr != NULL, "R should exist (On Goto)");
        ASSERT(vr->ival == 20, "R should be 20 (On 2 Goto goes to line 20)");
        amos_destroy(state);
    }

    /* Test 10: Reserve As Work / Length / Erase */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state,
            "Reserve As Work 5,1024\n"
            "L=Length(5)\n"
            "Erase 5\n"
            "L2=Length(5)\n"
        );
        amos_run(state);
        for (int i = 0; i < 100 && state->running; i++)
            amos_run_step(state);

        amos_var_t *vl = amos_var_get(state, "L");
        amos_var_t *vl2 = amos_var_get(state, "L2");
        ASSERT(vl != NULL && vl->ival == 1024, "L should be 1024 (bank length)");
        ASSERT(vl2 != NULL && vl2->ival == 0, "L2 should be 0 (erased bank)");
        amos_destroy(state);
    }

    /* Test 11: Gr Writing mode storage */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state, "Gr Writing 2");
        amos_run(state);
        amos_run_step(state);

        amos_screen_t *scr = &state->screens[state->current_screen];
        ASSERT(scr->writing_mode == 2, "Gr Writing should set mode to 2 (XOR)");
        amos_destroy(state);
    }

    /* Test 12: Set Font storage */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state, "Set Font 3");
        amos_run(state);
        amos_run_step(state);

        ASSERT(state->current_font == 3, "Set Font should store font number 3");
        amos_destroy(state);
    }

    /* Test 13: Clip command */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state, "Clip 10,20 To 100,200");
        amos_run(state);
        amos_run_step(state);

        ASSERT(state->clip_enabled == true, "Clip should enable clipping");
        ASSERT(state->clip_x1 == 10, "clip_x1 should be 10");
        ASSERT(state->clip_y1 == 20, "clip_y1 should be 20");
        ASSERT(state->clip_x2 == 100, "clip_x2 should be 100");
        ASSERT(state->clip_y2 == 200, "clip_y2 should be 200");
        amos_destroy(state);
    }

    /* Test 14: Screen Display */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state, "Screen Display 0,128,50,320,200");
        amos_run(state);
        amos_run_step(state);

        amos_screen_t *scr = &state->screens[0];
        ASSERT(scr->display_x == 128, "display_x should be 128");
        ASSERT(scr->display_y == 50, "display_y should be 50");
        ASSERT(scr->display_w == 320, "display_w should be 320");
        ASSERT(scr->display_h == 200, "display_h should be 200");
        amos_destroy(state);
    }

    /* Test 15: Set Line pattern */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state, "Set Line $AAAA");
        amos_run(state);
        amos_run_step(state);

        ASSERT(state->line_pattern == 0xAAAA, "line_pattern should be $AAAA");
        amos_destroy(state);
    }

    /* Test 16: Multiple colons with For loop */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state,
            "S=0 : T=0\n"
            "For I=1 To 3\n"
            "S=S+I : T=T+1\n"
            "Next I\n"
        );
        amos_run(state);
        for (int i = 0; i < 100 && state->running; i++)
            amos_run_step(state);

        amos_var_t *vs = amos_var_get(state, "S");
        amos_var_t *vt = amos_var_get(state, "T");
        double sval = vs ? (vs->type == VAR_FLOAT ? vs->fval : (double)vs->ival) : -1;
        double tval = vt ? (vt->type == VAR_FLOAT ? vt->fval : (double)vt->ival) : -1;
        ASSERT(sval >= 5.9 && sval <= 6.1, "S should be 6 (1+2+3 with colon in loop)");
        ASSERT(tval >= 2.9 && tval <= 3.1, "T should be 3 (3 iterations with colon)");
        amos_destroy(state);
    }

    /* Test 17: Case with multiple values */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state,
            "X=5\n"
            "Select X\n"
            "Case 1,2,3\n"
            "R=100\n"
            "Case 4,5,6\n"
            "R=200\n"
            "End Select\n"
        );
        amos_run(state);
        for (int i = 0; i < 100 && state->running; i++)
            amos_run_step(state);

        amos_var_t *vr = amos_var_get(state, "R");
        ASSERT(vr != NULL && vr->ival == 200, "R should be 200 (Case 4,5,6 matched X=5)");
        amos_destroy(state);
    }

    printf("  %d tests, %d errors\n", 17, errors);
    return errors;
}
