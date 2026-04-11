/*
 * vv_framework.h — AMOS Reborn V&V Test Framework
 *
 * Three-level verification framework:
 *   Level 1: Unit tests (individual function tests)
 *   Level 2: Subsystem tests (interpreter snippet execution)
 *   Level 3: Golden tests (full program output comparison — design only)
 *
 * Each test is tagged with a requirement ID for traceability.
 * Format: REQ-<SUBSYSTEM>-<NNN>
 *   REQ-INT — Interpreter core (variables, expressions, control flow)
 *   REQ-GFX — Graphics subsystem (screens, drawing, palette)
 *   REQ-AUD — Audio subsystem (Paula, tracker, effects)
 *   REQ-IO  — File I/O
 *   REQ-STR — String functions
 *   REQ-SPR — Sprites and bobs
 *   REQ-AML — AMAL animation language
 *   REQ-BNK — Memory banks
 */

#ifndef VV_FRAMEWORK_H
#define VV_FRAMEWORK_H

#include "amos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── Configuration ──────────────────────────────────────────────── */

#define VV_MAX_TESTS       1024
#define VV_MAX_STEPS      10000   /* step limit per vv_run() call */
#define VV_FLOAT_TOLERANCE 1e-6   /* default float comparison tolerance */

/* ── Test registry entry ────────────────────────────────────────── */

typedef struct {
    const char *req_id;         /* e.g. "REQ-INT-005" */
    const char *description;    /* human-readable test name */
    const char *file;           /* source file */
    int         line;           /* source line */
    void      (*func)(void);   /* test body */
} vv_test_entry_t;

/* ── Global test state ──────────────────────────────────────────── */

typedef struct {
    /* Registry */
    vv_test_entry_t tests[VV_MAX_TESTS];
    int test_count;

    /* Current test tracking */
    const char *current_req;
    const char *current_desc;
    int         current_failed;  /* assertions failed in current test */

    /* Totals */
    int total_pass;
    int total_fail;
    int total_assertions;
    int failed_assertions;

    /* Filter */
    const char *filter_prefix;  /* NULL = run all, else match REQ prefix */
} vv_state_t;

extern vv_state_t vv_g;

/* ── Test registration ──────────────────────────────────────────── */

/*
 * VV_TEST("REQ-INT-005: TRUE equals -1") { ... }
 *
 * Expands to a static function + a constructor that registers it.
 * The req_id is extracted as everything before the first colon.
 * The description is the full string.
 */

#define VV_PASTE2(a, b) a##b
#define VV_PASTE(a, b) VV_PASTE2(a, b)

#define VV_TEST(desc_str)                                                    \
    static void VV_PASTE(vv_test_func_, __LINE__)(void);                     \
    __attribute__((constructor))                                              \
    static void VV_PASTE(vv_test_reg_, __LINE__)(void) {                     \
        vv_register_test(desc_str, __FILE__, __LINE__,                       \
                         VV_PASTE(vv_test_func_, __LINE__));                 \
    }                                                                        \
    static void VV_PASTE(vv_test_func_, __LINE__)(void)

/* ── Assertions ─────────────────────────────────────────────────── */

#define VV_ASSERT(cond, msg) do {                                            \
    vv_g.total_assertions++;                                                 \
    if (!(cond)) {                                                           \
        vv_g.current_failed++;                                               \
        vv_g.failed_assertions++;                                            \
        printf("    FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);           \
    }                                                                        \
} while(0)

/* Check an integer variable in the interpreter state */
#define VV_ASSERT_INT(state, varname, expected) do {                         \
    vv_g.total_assertions++;                                                 \
    amos_var_t *_v = amos_var_get(state, varname);                           \
    if (!_v) {                                                               \
        vv_g.current_failed++;                                               \
        vv_g.failed_assertions++;                                            \
        printf("    FAIL: variable '%s' not found (%s:%d)\n",                \
               varname, __FILE__, __LINE__);                                 \
    } else {                                                                 \
        int32_t _actual = (_v->type == VAR_FLOAT)                            \
            ? (int32_t)_v->fval : _v->ival;                                  \
        if (_actual != (expected)) {                                         \
            vv_g.current_failed++;                                           \
            vv_g.failed_assertions++;                                        \
            printf("    FAIL: %s = %d, expected %d (%s:%d)\n",               \
                   varname, _actual, (expected), __FILE__, __LINE__);        \
        }                                                                    \
    }                                                                        \
} while(0)

/* Check a float variable with tolerance */
#define VV_ASSERT_FLOAT(state, varname, expected, tolerance) do {            \
    vv_g.total_assertions++;                                                 \
    amos_var_t *_v = amos_var_get(state, varname);                           \
    if (!_v) {                                                               \
        vv_g.current_failed++;                                               \
        vv_g.failed_assertions++;                                            \
        printf("    FAIL: variable '%s' not found (%s:%d)\n",                \
               varname, __FILE__, __LINE__);                                 \
    } else {                                                                 \
        double _actual = (_v->type == VAR_FLOAT) ? _v->fval                  \
                       : (double)_v->ival;                                   \
        if (fabs(_actual - (expected)) > (tolerance)) {                      \
            vv_g.current_failed++;                                           \
            vv_g.failed_assertions++;                                        \
            printf("    FAIL: %s = %f, expected %f +/- %f (%s:%d)\n",        \
                   varname, _actual, (double)(expected),                     \
                   (double)(tolerance), __FILE__, __LINE__);                 \
        }                                                                    \
    }                                                                        \
} while(0)

/* Check a string variable */
#define VV_ASSERT_STR(state, varname, expected) do {                         \
    vv_g.total_assertions++;                                                 \
    amos_var_t *_v = amos_var_get(state, varname);                           \
    if (!_v) {                                                               \
        vv_g.current_failed++;                                               \
        vv_g.failed_assertions++;                                            \
        printf("    FAIL: variable '%s' not found (%s:%d)\n",                \
               varname, __FILE__, __LINE__);                                 \
    } else if (_v->type != VAR_STRING || !_v->sval.data) {                   \
        vv_g.current_failed++;                                               \
        vv_g.failed_assertions++;                                            \
        printf("    FAIL: %s is not a string (%s:%d)\n",                     \
               varname, __FILE__, __LINE__);                                 \
    } else if (strcmp(_v->sval.data, (expected)) != 0) {                     \
        vv_g.current_failed++;                                               \
        vv_g.failed_assertions++;                                            \
        printf("    FAIL: %s = \"%s\", expected \"%s\" (%s:%d)\n",           \
               varname, _v->sval.data, (expected), __FILE__, __LINE__);     \
    }                                                                        \
} while(0)

/* Check a screen pixel (RGBA as uint32_t) */
#define VV_ASSERT_SCREEN_PIXEL(state, screen_id, x, y, expected_rgba) do {   \
    vv_g.total_assertions++;                                                 \
    amos_screen_t *_scr = &(state)->screens[screen_id];                      \
    if (!_scr->active || !_scr->pixels) {                                    \
        vv_g.current_failed++;                                               \
        vv_g.failed_assertions++;                                            \
        printf("    FAIL: screen %d not active or no pixel buffer "          \
               "(%s:%d)\n", screen_id, __FILE__, __LINE__);                  \
    } else if ((x) < 0 || (x) >= _scr->width ||                             \
               (y) < 0 || (y) >= _scr->height) {                            \
        vv_g.current_failed++;                                               \
        vv_g.failed_assertions++;                                            \
        printf("    FAIL: pixel (%d,%d) out of bounds for screen %d "        \
               "(%dx%d) (%s:%d)\n", (x), (y), screen_id,                    \
               _scr->width, _scr->height, __FILE__, __LINE__);              \
    } else {                                                                 \
        uint32_t _px = _scr->pixels[(y) * _scr->width + (x)];               \
        if (_px != (expected_rgba)) {                                        \
            vv_g.current_failed++;                                           \
            vv_g.failed_assertions++;                                        \
            printf("    FAIL: pixel (%d,%d) on screen %d = 0x%08X, "         \
                   "expected 0x%08X (%s:%d)\n",                              \
                   (x), (y), screen_id, _px, (expected_rgba),               \
                   __FILE__, __LINE__);                                      \
        }                                                                    \
    }                                                                        \
} while(0)

/* Check a palette entry */
#define VV_ASSERT_PALETTE(state, screen_id, index, expected_rgba) do {       \
    vv_g.total_assertions++;                                                 \
    amos_screen_t *_scr = &(state)->screens[screen_id];                      \
    if (!_scr->active) {                                                     \
        vv_g.current_failed++;                                               \
        vv_g.failed_assertions++;                                            \
        printf("    FAIL: screen %d not active (%s:%d)\n",                   \
               screen_id, __FILE__, __LINE__);                               \
    } else if ((index) < 0 || (index) >= 256) {                             \
        vv_g.current_failed++;                                               \
        vv_g.failed_assertions++;                                            \
        printf("    FAIL: palette index %d out of range (%s:%d)\n",          \
               (index), __FILE__, __LINE__);                                 \
    } else {                                                                 \
        uint32_t _c = _scr->palette[index];                                  \
        if (_c != (expected_rgba)) {                                         \
            vv_g.current_failed++;                                           \
            vv_g.failed_assertions++;                                        \
            printf("    FAIL: palette[%d] on screen %d = 0x%08X, "           \
                   "expected 0x%08X (%s:%d)\n",                              \
                   (index), screen_id, _c, (expected_rgba),                  \
                   __FILE__, __LINE__);                                      \
        }                                                                    \
    }                                                                        \
} while(0)

/* ── Helpers (implemented in vv_framework.c) ────────────────────── */

/* Register a test — called by VV_TEST constructor */
void vv_register_test(const char *desc, const char *file, int line,
                      void (*func)(void));

/* Create a fresh interpreter state ready for snippet execution */
amos_state_t *vv_create(void);

/* Load and run an AMOS Basic snippet to completion (with step limit) */
void vv_run(amos_state_t *state, const char *program);

/* Destroy interpreter state */
void vv_destroy(amos_state_t *state);

/* Run all registered V&V tests. Returns number of failures. */
int vv_run_all(void);

/* Set filter prefix (e.g. "REQ-AUD"). NULL = run all. */
void vv_set_filter(const char *prefix);

/* Print traceability matrix to stdout */
void vv_print_traceability(void);

#endif /* VV_FRAMEWORK_H */
