/*
 * vv_framework.c — AMOS Reborn V&V Test Framework implementation
 *
 * Provides test registration, execution, filtering, and traceability.
 */

#include "vv_framework.h"
#include <string.h>

/* ── Global state ───────────────────────────────────────────────── */

vv_state_t vv_g = {0};

/* ── Registration ───────────────────────────────────────────────── */

void vv_register_test(const char *desc, const char *file, int line,
                      void (*func)(void))
{
    if (vv_g.test_count >= VV_MAX_TESTS) {
        fprintf(stderr, "vv_framework: too many tests (max %d)\n", VV_MAX_TESTS);
        return;
    }

    vv_test_entry_t *t = &vv_g.tests[vv_g.test_count++];
    t->description = desc;
    t->file = file;
    t->line = line;
    t->func = func;

    /* Extract requirement ID: everything before the first ':' */
    static char req_buf[VV_MAX_TESTS][32];
    int idx = vv_g.test_count - 1;
    const char *colon = strchr(desc, ':');
    if (colon) {
        int len = (int)(colon - desc);
        if (len > 31) len = 31;
        memcpy(req_buf[idx], desc, len);
        req_buf[idx][len] = '\0';
        t->req_id = req_buf[idx];
    } else {
        t->req_id = desc;  /* no colon — use full string */
    }
}

/* ── Interpreter helpers ────────────────────────────────────────── */

amos_state_t *vv_create(void)
{
    amos_state_t *state = amos_create();
    if (!state) {
        fprintf(stderr, "vv_create: amos_create() returned NULL\n");
        abort();
    }
    return state;
}

void vv_run(amos_state_t *state, const char *program)
{
    amos_load_text(state, program);
    amos_run(state);

    int steps = 0;
    while (state->running && steps < VV_MAX_STEPS) {
        amos_run_step(state);
        steps++;
    }

    if (steps >= VV_MAX_STEPS && state->running) {
        printf("    WARNING: step limit (%d) reached — program may not have "
               "finished\n", VV_MAX_STEPS);
    }
}

void vv_destroy(amos_state_t *state)
{
    if (state) {
        amos_destroy(state);
    }
}

/* ── Filter ─────────────────────────────────────────────────────── */

void vv_set_filter(const char *prefix)
{
    vv_g.filter_prefix = prefix;
}

/* ── Test runner ────────────────────────────────────────────────── */

static int vv_test_compare(const void *a, const void *b)
{
    const vv_test_entry_t *ta = (const vv_test_entry_t *)a;
    const vv_test_entry_t *tb = (const vv_test_entry_t *)b;
    return strcmp(ta->req_id, tb->req_id);
}

int vv_run_all(void)
{
    /* Sort tests by requirement ID for organized output */
    qsort(vv_g.tests, vv_g.test_count, sizeof(vv_test_entry_t),
          vv_test_compare);

    if (vv_g.filter_prefix) {
        printf("  Filter: %s*\n\n", vv_g.filter_prefix);
    }

    vv_g.total_pass = 0;
    vv_g.total_fail = 0;
    vv_g.total_assertions = 0;
    vv_g.failed_assertions = 0;

    int skipped = 0;

    for (int i = 0; i < vv_g.test_count; i++) {
        vv_test_entry_t *t = &vv_g.tests[i];

        /* Apply filter */
        if (vv_g.filter_prefix) {
            size_t plen = strlen(vv_g.filter_prefix);
            if (strncmp(t->req_id, vv_g.filter_prefix, plen) != 0) {
                skipped++;
                continue;
            }
        }

        /* Run the test */
        vv_g.current_req = t->req_id;
        vv_g.current_desc = t->description;
        vv_g.current_failed = 0;

        t->func();

        if (vv_g.current_failed == 0) {
            printf("  PASS  [%s] %s\n", t->req_id, t->description);
            vv_g.total_pass++;
        } else {
            printf("  FAIL  [%s] %s (%d assertion%s failed)\n",
                   t->req_id, t->description,
                   vv_g.current_failed,
                   vv_g.current_failed > 1 ? "s" : "");
            vv_g.total_fail++;
        }
    }

    printf("\n");
    printf("  V&V Results: %d passed, %d failed", vv_g.total_pass, vv_g.total_fail);
    if (skipped > 0) printf(", %d skipped (filtered)", skipped);
    printf("\n");
    printf("  Assertions: %d total, %d failed\n",
           vv_g.total_assertions, vv_g.failed_assertions);
    printf("\n");

    return vv_g.total_fail;
}

/* ── Traceability matrix ────────────────────────────────────────── */

void vv_print_traceability(void)
{
    /* Sort for organized output */
    qsort(vv_g.tests, vv_g.test_count, sizeof(vv_test_entry_t),
          vv_test_compare);

    printf("\n");
    printf("Traceability Matrix\n");
    printf("====================\n\n");
    printf("%-16s  %-48s  %s\n", "Requirement", "Description", "Location");
    printf("%-16s  %-48s  %s\n",
           "----------------", "------------------------------------------------",
           "--------------------");

    for (int i = 0; i < vv_g.test_count; i++) {
        vv_test_entry_t *t = &vv_g.tests[i];

        /* Truncate description for display */
        char desc_buf[49];
        strncpy(desc_buf, t->description, 48);
        desc_buf[48] = '\0';
        /* Trim at colon+space to show only the description part */
        const char *colon = strchr(t->description, ':');
        if (colon && colon[1] == ' ') {
            strncpy(desc_buf, colon + 2, 48);
            desc_buf[48] = '\0';
        }

        /* Extract just the filename from the full path */
        const char *fname = strrchr(t->file, '/');
        fname = fname ? fname + 1 : t->file;

        printf("%-16s  %-48s  %s:%d\n", t->req_id, desc_buf, fname, t->line);
    }

    printf("\n");
}

/* ── Integration point for test_main.c ──────────────────────────── */

int test_vv_all(void)
{
    return vv_run_all();
}
