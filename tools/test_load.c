/*
 * test_load.c — Headless .AMOS file loader/tester
 *
 * Loads an .AMOS file, detokenizes it, and tries to parse/execute
 * the first N lines to find issues.
 *
 * Usage: test_load <file.AMOS> [max_lines]
 */

#include "amos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static volatile int timed_out = 0;
static void alarm_handler(int sig) {
    (void)sig;
    timed_out = 1;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.AMOS> [max_lines] [--no-exec]\n", argv[0]);
        return 1;
    }

    int max_lines = 50;
    int skip_exec = 0;
    int dump_only = 0;
    for (int a = 2; a < argc; a++) {
        if (strcmp(argv[a], "--no-exec") == 0) skip_exec = 1;
        else if (strcmp(argv[a], "--dump") == 0) dump_only = 1;
        else if (argv[a][0] != '-') max_lines = atoi(argv[a]);
    }

    amos_state_t *state = amos_create();
    if (!state) {
        fprintf(stderr, "Failed to create state\n");
        return 1;
    }

    fprintf(stderr, "Loading: %s\n", argv[1]);
    int result = amos_load_file(state, argv[1]);
    if (result < 0) {
        fprintf(stderr, "Load error: %s\n", state->error_msg);
        amos_destroy(state);
        return 1;
    }

    fprintf(stderr, "Loaded %d lines\n\n", state->line_count);

    /* Print first N lines of detokenized source */
    int show = state->line_count < max_lines ? state->line_count : max_lines;
    fprintf(stderr, "=== Detokenized source (first %d lines) ===\n", show);
    for (int i = 0; i < show; i++) {
        fprintf(stderr, "%4d: %s\n", i + 1, state->lines[i].text ? state->lines[i].text : "(null)");
    }
    fprintf(stderr, "=== End source ===\n\n");

    if (dump_only) goto done;

    /* Try to parse each line */
    fprintf(stderr, "=== Parsing lines ===\n");
    int parse_ok = 0, parse_fail = 0;
    for (int i = 0; i < show; i++) {
        if (!state->lines[i].text) continue;

        /* Skip empty / whitespace-only lines */
        const char *p = state->lines[i].text;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') continue;

        amos_token_list_t *tokens = amos_tokenize(state->lines[i].text);
        if (!tokens || tokens->count == 0) {
            fprintf(stderr, "  Line %d: TOKENIZE FAILED: %s\n", i + 1, state->lines[i].text);
            parse_fail++;
            if (tokens) amos_token_list_free(tokens);
            continue;
        }

        int pos = 0;
        amos_node_t *ast = amos_parse_line(tokens->tokens, &pos, tokens->count);
        if (!ast) {
            fprintf(stderr, "  Line %d: PARSE FAILED: %s\n", i + 1, state->lines[i].text);
            fprintf(stderr, "           First token: type=%d", tokens->tokens[0].type);
            if (tokens->tokens[0].sval)
                fprintf(stderr, " sval='%s'", tokens->tokens[0].sval);
            fprintf(stderr, "\n");
            parse_fail++;
        } else {
            parse_ok++;
            amos_node_free(ast);
        }

        amos_token_list_free(tokens);
    }
    fprintf(stderr, "\nParsing: %d OK, %d FAILED (of %d lines)\n\n", parse_ok, parse_fail, show);

    if (skip_exec) goto done;

    /* Try headless execution of first N lines (limited steps to avoid infinite loops) */
    fprintf(stderr, "=== Executing (headless, max 500 steps, 3s timeout) ===\n");
    signal(SIGALRM, alarm_handler);
    alarm(3);  /* 3 second timeout */
    timed_out = 0;
    state->running = true;
    state->current_line = 0;
    int executed = 0;
    int max_steps = 500;
    while (state->running && state->current_line < state->line_count && executed < max_steps && !timed_out) {
        int before = state->current_line;
        amos_execute_line(state, state->current_line);

        if (state->error_code) {
            fprintf(stderr, "  Line %d: RUNTIME ERROR %d: %s\n",
                    state->current_line + 1, state->error_code, state->error_msg);
            state->error_code = 0;
            state->error_msg[0] = '\0';
        }

        if (state->current_line == before) {
            state->current_line++;
        }
        executed++;
    }
    alarm(0);  /* cancel timer */
    if (timed_out) {
        fprintf(stderr, "  (execution timed out after 3s)\n");
    } else if (executed >= max_steps) {
        fprintf(stderr, "  (execution limit reached — %d steps)\n", executed);
    }
    fprintf(stderr, "Executed %d steps\n", executed);

done:
    amos_destroy(state);
    return parse_fail > 0 ? 1 : 0;
}
