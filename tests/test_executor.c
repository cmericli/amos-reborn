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

    printf("  %d tests, %d errors\n", 4, errors);
    return errors;
}
