/*
 * test_main.c — AMOS Reborn test runner
 *
 * Simple test framework — each test function returns 0 on pass, 1 on fail.
 */

#include <stdio.h>
#include <stdlib.h>

/* Test declarations */
extern int test_tokenizer_all(void);
extern int test_parser_all(void);
extern int test_executor_all(void);
extern int test_expressions_all(void);

typedef struct {
    const char *name;
    int (*func)(void);
} test_suite_t;

static test_suite_t suites[] = {
    {"Tokenizer",   test_tokenizer_all},
    {"Parser",      test_parser_all},
    {"Executor",    test_executor_all},
    {"Expressions", test_expressions_all},
    {NULL, NULL}
};

int main(void)
{
    int total_pass = 0, total_fail = 0;

    printf("AMOS Reborn Test Suite\n");
    printf("======================\n\n");

    for (int i = 0; suites[i].name; i++) {
        printf("--- %s ---\n", suites[i].name);
        int result = suites[i].func();
        if (result == 0) {
            total_pass++;
            printf("  PASSED\n\n");
        } else {
            total_fail++;
            printf("  FAILED (%d errors)\n\n", result);
        }
    }

    printf("======================\n");
    printf("Results: %d passed, %d failed\n", total_pass, total_fail);

    return total_fail > 0 ? 1 : 0;
}
