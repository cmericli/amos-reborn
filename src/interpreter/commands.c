/*
 * commands.c — AMOS command table and program lifecycle
 *
 * Program loading, line management, and the run loop.
 */

#include "amos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── State Lifecycle ─────────────────────────────────────────────── */

amos_state_t *amos_create(void)
{
    amos_state_t *state = calloc(1, sizeof(amos_state_t));
    state->dialect = AMOS_DIALECT_PRO;
    state->display_mode = AMOS_MODE_CLASSIC;
    state->crt_preset = CRT_PRESET_CRT;

    /* Initialize Paula */
    amos_paula_init(&state->paula, 44100);

    /* Initialize default screen */
    amos_screen_open(state, 0, 320, 256, 5);

    /* Seed RNG */
    srand((unsigned)time(NULL));

    /* Error handling defaults */
    state->on_error_line = -1;
    state->on_error_proc[0] = '\0';
    state->trap_mode = false;
    state->last_error = 0;
    state->last_error_line = 0;
    state->resume_line = -1;

    return state;
}

void amos_destroy(amos_state_t *state)
{
    if (!state) return;

    /* Free program lines */
    for (int i = 0; i < state->line_count; i++) {
        free(state->lines[i].text);
        amos_node_free(state->lines[i].ast);
    }

    /* Free string variables */
    for (int i = 0; i < state->var_count; i++) {
        if (state->variables[i].type == VAR_STRING && state->variables[i].sval.data) {
            free(state->variables[i].sval.data);
        }
    }

    /* Free screens */
    for (int i = 0; i < AMOS_MAX_SCREENS; i++) {
        free(state->screens[i].pixels);
        free(state->screens[i].back_buffer);
    }

    /* Free banks */
    for (int i = 0; i < AMOS_MAX_BANKS; i++) {
        free(state->banks[i].data);
    }

    /* Free AMAL programs */
    for (int i = 0; i < AMOS_MAX_AMAL_CHANNELS; i++) {
        free(state->amal[i].program);
    }

    /* Close file channels */
    amos_file_close_all(state);

    free(state);
}

void amos_reset(amos_state_t *state)
{
    state->current_line = 0;
    state->current_pos = 0;
    state->running = false;
    state->gosub_top = 0;
    state->for_top = 0;
    state->data_line = 0;
    state->data_pos = 0;
    state->error_code = 0;
    state->error_msg[0] = '\0';

    /* Clear variables */
    for (int i = 0; i < state->var_count; i++) {
        if (state->variables[i].type == VAR_STRING && state->variables[i].sval.data) {
            free(state->variables[i].sval.data);
        }
    }
    state->var_count = 0;
}

/* ── Program Loading ─────────────────────────────────────────────── */

int amos_load_text(amos_state_t *state, const char *source)
{
    /* Clear existing program */
    for (int i = 0; i < state->line_count; i++) {
        free(state->lines[i].text);
        amos_node_free(state->lines[i].ast);
    }
    state->line_count = 0;

    /* Split into lines */
    const char *p = source;
    while (*p && state->line_count < AMOS_MAX_PROGRAM_LINES) {
        /* Find end of line */
        const char *eol = p;
        while (*eol && *eol != '\n') eol++;

        /* Skip empty lines */
        if (eol > p) {
            int len = (int)(eol - p);
            amos_program_line_t *line = &state->lines[state->line_count];
            line->text = strndup(p, len);
            line->ast = NULL;

            /* Check for line number */
            if (len > 0 && line->text[0] >= '0' && line->text[0] <= '9') {
                char *end;
                line->number = (int)strtol(line->text, &end, 10);
            } else {
                line->number = 0;
            }

            state->line_count++;
        }

        p = *eol ? eol + 1 : eol;
    }

    return state->line_count;
}

int amos_load_file(amos_state_t *state, const char *path)
{
    /* Auto-detect .AMOS tokenized files by reading the first 4 bytes */
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Cannot open file: %s", path);
        state->error_code = 2;
        return -1;
    }

    char magic[5] = {0};
    size_t magic_read = fread(magic, 1, 4, f);
    fclose(f);

    /* Check for "AMOS" magic header */
    if (magic_read == 4 && memcmp(magic, "AMOS", 4) == 0) {
        return amos_load_amos_file(state, path);
    }

    /* Fall through to plain text loader */
    f = fopen(path, "r");
    if (!f) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Cannot open file: %s", path);
        state->error_code = 2;
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *source = malloc(size + 1);
    size_t read = fread(source, 1, size, f);
    source[read] = '\0';
    fclose(f);

    int result = amos_load_text(state, source);
    free(source);
    return result;
}

/* ── Execution ───────────────────────────────────────────────────── */

void amos_run(amos_state_t *state)
{
    state->running = true;
    state->current_line = 0;
    state->current_pos = 0;
}

void amos_run_step(amos_state_t *state)
{
    if (!state->running) return;
    if (state->current_line >= state->line_count) {
        state->running = false;
        return;
    }

    int line_before = state->current_line;
    amos_execute_line(state, state->current_line);

    /* Only advance if execute didn't jump (GOTO/GOSUB/FOR loop) */
    if (state->current_line == line_before) {
        state->current_line++;
    }
}

void amos_stop(amos_state_t *state)
{
    state->running = false;
}
