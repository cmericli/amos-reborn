/*
 * parser.c — AMOS BASIC statement parser
 *
 * Recursive descent parser that converts token streams into AST nodes.
 * Each statement type (Print, If, For, Let, etc.) has its own parse function.
 */

#include "amos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Helpers ─────────────────────────────────────────────────────── */

static amos_node_t *alloc_node(amos_node_type_t type, int line)
{
    amos_node_t *node = calloc(1, sizeof(amos_node_t));
    node->type = type;
    node->line = line;
    return node;
}

static void add_child(amos_node_t *parent, amos_node_t *child)
{
    if (child && parent->child_count < AST_MAX_CHILDREN) {
        parent->children[parent->child_count++] = child;
    }
}

static bool at_end(amos_token_t *tokens, int pos, int count)
{
    return pos >= count ||
           tokens[pos].type == TOK_EOF ||
           tokens[pos].type == TOK_NEWLINE ||
           tokens[pos].type == TOK_COLON;
}

static bool match(amos_token_t *tokens, int *pos, int count, amos_token_type_t type)
{
    if (*pos < count && tokens[*pos].type == type) {
        (*pos)++;
        return true;
    }
    return false;
}

static bool peek_is(amos_token_t *tokens, int pos, int count, amos_token_type_t type)
{
    return pos < count && tokens[pos].type == type;
}

/* ── Statement Parsers ───────────────────────────────────────────── */

/* PRINT expr[;|,] expr[;|,] ... */
static amos_node_t *parse_print(amos_token_t *tokens, int *pos, int count)
{
    amos_node_t *node = alloc_node(NODE_PRINT, tokens[*pos - 1].line);

    while (!at_end(tokens, *pos, count)) {
        /* Semicolon or comma as separator — semicolon = no newline, comma = tab */
        if (peek_is(tokens, *pos, count, TOK_SEMICOLON)) {
            /* Add a marker node for semicolon (suppress newline) */
            amos_node_t *sep = alloc_node(NODE_COMMAND, tokens[*pos].line);
            sep->token.type = TOK_SEMICOLON;
            add_child(node, sep);
            (*pos)++;
            continue;
        }
        if (peek_is(tokens, *pos, count, TOK_COMMA)) {
            amos_node_t *sep = alloc_node(NODE_COMMAND, tokens[*pos].line);
            sep->token.type = TOK_COMMA;
            add_child(node, sep);
            (*pos)++;
            continue;
        }

        amos_node_t *expr = amos_parse_expression(tokens, pos, count);
        if (!expr) break;
        add_child(node, expr);
    }

    return node;
}

/* LET var = expr (LET is optional) */
static amos_node_t *parse_let(amos_token_t *tokens, int *pos, int count, bool explicit_let)
{
    /* Variable name should be at *pos */
    if (*pos >= count || tokens[*pos].type != TOK_IDENTIFIER)
        return NULL;

    amos_node_t *node = alloc_node(NODE_LET, tokens[*pos].line);
    node->token = tokens[*pos];
    node->token.sval = strdup(tokens[*pos].sval);
    (*pos)++;

    /* Array assignment: var(idx) = expr */
    if (peek_is(tokens, *pos, count, TOK_LPAREN)) {
        (*pos)++;  /* skip ( */
        while (*pos < count && tokens[*pos].type != TOK_RPAREN) {
            amos_node_t *idx = amos_parse_expression(tokens, pos, count);
            if (idx) add_child(node, idx);
            if (peek_is(tokens, *pos, count, TOK_COMMA)) (*pos)++;
        }
        if (peek_is(tokens, *pos, count, TOK_RPAREN)) (*pos)++;
    }

    /* = */
    if (!match(tokens, pos, count, TOK_EQUAL)) {
        /* No assignment — might be a procedure call */
        node->type = NODE_PROC_CALL;
        /* Parse arguments */
        while (!at_end(tokens, *pos, count)) {
            amos_node_t *arg = amos_parse_expression(tokens, pos, count);
            if (arg) add_child(node, arg);
            if (!match(tokens, pos, count, TOK_COMMA)) break;
        }
        return node;
    }

    /* Expression */
    amos_node_t *expr = amos_parse_expression(tokens, pos, count);
    add_child(node, expr);

    return node;
}

/* IF expr THEN statements [ELSE statements] [END IF] */
static amos_node_t *parse_if(amos_token_t *tokens, int *pos, int count)
{
    amos_node_t *node = alloc_node(NODE_IF, tokens[*pos - 1].line);

    /* Condition */
    amos_node_t *cond = amos_parse_expression(tokens, pos, count);
    add_child(node, cond);

    /* THEN (optional in some AMOS variants) */
    match(tokens, pos, count, TOK_THEN);

    /* Then-branch: parse statements until Else/End If/newline */
    amos_node_t *then_body = alloc_node(NODE_LINE, tokens[*pos].line);
    while (!at_end(tokens, *pos, count) &&
           !peek_is(tokens, *pos, count, TOK_ELSE) &&
           !peek_is(tokens, *pos, count, TOK_END_IF)) {
        amos_node_t *stmt = amos_parse_line(tokens, pos, count);
        if (stmt) add_child(then_body, stmt);
        if (peek_is(tokens, *pos, count, TOK_COLON)) (*pos)++;
        else break;
    }
    add_child(node, then_body);

    /* ELSE branch */
    if (match(tokens, pos, count, TOK_ELSE)) {
        amos_node_t *else_body = alloc_node(NODE_LINE, tokens[*pos].line);
        while (!at_end(tokens, *pos, count) &&
               !peek_is(tokens, *pos, count, TOK_END_IF)) {
            amos_node_t *stmt = amos_parse_line(tokens, pos, count);
            if (stmt) add_child(else_body, stmt);
            if (peek_is(tokens, *pos, count, TOK_COLON)) (*pos)++;
            else break;
        }
        add_child(node, else_body);
    }

    /* END IF (optional for single-line If) */
    match(tokens, pos, count, TOK_END_IF);

    return node;
}

/* FOR var = start TO end [STEP step] */
static amos_node_t *parse_for(amos_token_t *tokens, int *pos, int count)
{
    amos_node_t *node = alloc_node(NODE_FOR, tokens[*pos - 1].line);

    /* Variable name */
    if (*pos < count && tokens[*pos].type == TOK_IDENTIFIER) {
        node->token = tokens[*pos];
        node->token.sval = strdup(tokens[*pos].sval);
        (*pos)++;
    }

    /* = */
    match(tokens, pos, count, TOK_EQUAL);

    /* Start value */
    add_child(node, amos_parse_expression(tokens, pos, count));

    /* TO */
    match(tokens, pos, count, TOK_TO);

    /* End value */
    add_child(node, amos_parse_expression(tokens, pos, count));

    /* STEP (optional) */
    if (match(tokens, pos, count, TOK_STEP)) {
        add_child(node, amos_parse_expression(tokens, pos, count));
    }

    return node;
}

/* WHILE expr ... WEND */
static amos_node_t *parse_while(amos_token_t *tokens, int *pos, int count)
{
    amos_node_t *node = alloc_node(NODE_WHILE, tokens[*pos - 1].line);
    add_child(node, amos_parse_expression(tokens, pos, count));
    return node;
}

/* REPEAT ... UNTIL expr */
static amos_node_t *parse_repeat(amos_token_t *tokens, int *pos, int count)
{
    return alloc_node(NODE_REPEAT, tokens[*pos - 1].line);
}

/* GOTO/GOSUB line_number_or_label */
static amos_node_t *parse_goto_gosub(amos_token_t *tokens, int *pos, int count, amos_node_type_t type)
{
    amos_node_t *node = alloc_node(type, tokens[*pos - 1].line);
    if (*pos < count) {
        amos_node_t *target = amos_parse_expression(tokens, pos, count);
        add_child(node, target);
    }
    return node;
}

/* DIM var(dims), ... */
static amos_node_t *parse_dim(amos_token_t *tokens, int *pos, int count)
{
    amos_node_t *node = alloc_node(NODE_DIM, tokens[*pos - 1].line);

    while (!at_end(tokens, *pos, count)) {
        if (*pos < count && tokens[*pos].type == TOK_IDENTIFIER) {
            amos_node_t *arr = alloc_node(NODE_ARRAY_ACCESS, tokens[*pos].line);
            arr->token = tokens[*pos];
            arr->token.sval = strdup(tokens[*pos].sval);
            (*pos)++;

            if (match(tokens, pos, count, TOK_LPAREN)) {
                while (*pos < count && tokens[*pos].type != TOK_RPAREN) {
                    amos_node_t *dim = amos_parse_expression(tokens, pos, count);
                    if (dim) add_child(arr, dim);
                    if (!match(tokens, pos, count, TOK_COMMA)) break;
                }
                match(tokens, pos, count, TOK_RPAREN);
            }
            add_child(node, arr);
        }
        if (!match(tokens, pos, count, TOK_COMMA)) break;
    }

    return node;
}

/* Generic command: KEYWORD [expr [, expr ...]] */
static amos_node_t *parse_command(amos_token_t *tokens, int *pos, int count, amos_token_type_t cmd_type)
{
    amos_node_t *node = alloc_node(NODE_COMMAND, tokens[*pos - 1].line);
    node->token.type = cmd_type;

    /* Parse arguments until end of statement */
    while (!at_end(tokens, *pos, count)) {
        /* "To" in commands like "Draw x1,y1 To x2,y2" */
        if (peek_is(tokens, *pos, count, TOK_TO)) {
            amos_node_t *sep = alloc_node(NODE_COMMAND, tokens[*pos].line);
            sep->token.type = TOK_TO;
            add_child(node, sep);
            (*pos)++;
            continue;
        }

        amos_node_t *arg = amos_parse_expression(tokens, pos, count);
        if (!arg) break;
        add_child(node, arg);

        if (!match(tokens, pos, count, TOK_COMMA)) {
            /* "To" acts as a separator in commands like Bar x1,y1 To x2,y2 */
            if (peek_is(tokens, *pos, count, TOK_TO)) {
                continue;  /* loop will handle the To token */
            }
            /* Check for implicit argument separation (some commands use spaces) */
            if (!at_end(tokens, *pos, count) &&
                (tokens[*pos].type == TOK_INTEGER ||
                 tokens[*pos].type == TOK_FLOAT ||
                 tokens[*pos].type == TOK_IDENTIFIER ||
                 tokens[*pos].type == TOK_LPAREN ||
                 tokens[*pos].type == TOK_MINUS ||
                 tokens[*pos].type == TOK_STRING)) {
                continue;  /* implicit comma */
            }
            break;
        }
    }

    return node;
}

/* ── Main Line Parser ────────────────────────────────────────────── */

amos_node_t *amos_parse_line(amos_token_t *tokens, int *pos, int count)
{
    if (*pos >= count) return NULL;

    amos_token_t *tok = &tokens[*pos];

    /* Skip line numbers */
    if (tok->type == TOK_LINE_NUMBER) {
        (*pos)++;
        if (*pos >= count) return NULL;
        tok = &tokens[*pos];
    }

    /* Skip empty lines */
    if (tok->type == TOK_NEWLINE || tok->type == TOK_EOF) {
        (*pos)++;
        return NULL;
    }

    /* Labels */
    if (tok->type == TOK_LABEL) {
        amos_node_t *node = alloc_node(NODE_LABEL, tok->line);
        node->token = *tok;
        node->token.sval = strdup(tok->sval);
        (*pos)++;
        return node;
    }

    /* Statement keywords */
    (*pos)++;
    switch (tok->type) {
        case TOK_PRINT: {
            /* Check for Print #channel — file I/O */
            if (peek_is(tokens, *pos, count, TOK_HASH)) {
                (*pos)++;  /* skip # */
                amos_node_t *n = alloc_node(NODE_COMMAND, tok->line);
                n->token.type = TOK_PRINT_FILE;
                /* channel number */
                amos_node_t *ch = amos_parse_expression(tokens, pos, count);
                add_child(n, ch);
                match(tokens, pos, count, TOK_COMMA);
                /* expression(s) to print */
                while (!at_end(tokens, *pos, count)) {
                    amos_node_t *expr = amos_parse_expression(tokens, pos, count);
                    if (!expr) break;
                    add_child(n, expr);
                    if (!match(tokens, pos, count, TOK_SEMICOLON)) {
                        match(tokens, pos, count, TOK_COMMA);
                    }
                }
                return n;
            }
            return parse_print(tokens, pos, count);
        }
        case TOK_LET:       return parse_let(tokens, pos, count, true);
        case TOK_IF:        return parse_if(tokens, pos, count);
        case TOK_FOR:       return parse_for(tokens, pos, count);
        case TOK_NEXT:      {
            amos_node_t *n = alloc_node(NODE_COMMAND, tok->line);
            n->token.type = TOK_NEXT;
            /* Optional variable name */
            if (!at_end(tokens, *pos, count) &&
                peek_is(tokens, *pos, count, TOK_IDENTIFIER)) {
                n->token.sval = strdup(tokens[*pos].sval);
                (*pos)++;
            }
            return n;
        }
        case TOK_WHILE:     return parse_while(tokens, pos, count);
        case TOK_WEND: {
            amos_node_t *n = alloc_node(NODE_COMMAND, tok->line);
            n->token.type = TOK_WEND;
            return n;
        }
        case TOK_DO: {
            amos_node_t *n = alloc_node(NODE_COMMAND, tok->line);
            n->token.type = TOK_DO;
            return n;
        }
        case TOK_LOOP: {
            amos_node_t *n = alloc_node(NODE_COMMAND, tok->line);
            n->token.type = TOK_LOOP;
            return n;
        }
        case TOK_REPEAT:    return parse_repeat(tokens, pos, count);
        case TOK_UNTIL: {
            amos_node_t *n = alloc_node(NODE_COMMAND, tok->line);
            n->token.type = TOK_UNTIL;
            add_child(n, amos_parse_expression(tokens, pos, count));
            return n;
        }
        case TOK_GOTO:      return parse_goto_gosub(tokens, pos, count, NODE_GOTO);
        case TOK_GOSUB:     return parse_goto_gosub(tokens, pos, count, NODE_GOSUB);
        case TOK_RETURN:    return alloc_node(NODE_RETURN, tok->line);
        case TOK_END:       return alloc_node(NODE_END, tok->line);
        case TOK_STOP:      return alloc_node(NODE_END, tok->line);
        case TOK_DIM:       return parse_dim(tokens, pos, count);
        case TOK_REM: {
            amos_node_t *n = alloc_node(NODE_REM, tok->line);
            n->token = *tok;
            if (tok->sval) n->token.sval = strdup(tok->sval);
            return n;
        }
        case TOK_DATA: {
            amos_node_t *n = alloc_node(NODE_DATA, tok->line);
            while (!at_end(tokens, *pos, count)) {
                amos_node_t *val = amos_parse_expression(tokens, pos, count);
                if (val) add_child(n, val);
                if (!match(tokens, pos, count, TOK_COMMA)) break;
            }
            return n;
        }
        case TOK_READ: {
            amos_node_t *n = alloc_node(NODE_READ, tok->line);
            while (!at_end(tokens, *pos, count)) {
                if (tokens[*pos].type == TOK_IDENTIFIER) {
                    amos_node_t *var = alloc_node(NODE_VARIABLE, tokens[*pos].line);
                    var->token = tokens[*pos];
                    var->token.sval = strdup(tokens[*pos].sval);
                    add_child(n, var);
                    (*pos)++;
                }
                if (!match(tokens, pos, count, TOK_COMMA)) break;
            }
            return n;
        }
        case TOK_RESTORE:   return alloc_node(NODE_RESTORE, tok->line);
        case TOK_SWAP: {
            /* Swap A,B — parse two variable names */
            amos_node_t *n = alloc_node(NODE_COMMAND, tok->line);
            n->token.type = TOK_SWAP;
            /* First variable */
            if (*pos < count && tokens[*pos].type == TOK_IDENTIFIER) {
                amos_node_t *var = alloc_node(NODE_VARIABLE, tokens[*pos].line);
                var->token = tokens[*pos];
                var->token.sval = strdup(tokens[*pos].sval);
                add_child(n, var);
                (*pos)++;
            }
            match(tokens, pos, count, TOK_COMMA);
            /* Second variable */
            if (*pos < count && tokens[*pos].type == TOK_IDENTIFIER) {
                amos_node_t *var = alloc_node(NODE_VARIABLE, tokens[*pos].line);
                var->token = tokens[*pos];
                var->token.sval = strdup(tokens[*pos].sval);
                add_child(n, var);
                (*pos)++;
            }
            return n;
        }

        /* Procedure definition (Pro only) */
        case TOK_PROCEDURE: {
            amos_node_t *n = alloc_node(NODE_PROCEDURE, tok->line);
            if (*pos < count && tokens[*pos].type == TOK_IDENTIFIER) {
                n->token.sval = strdup(tokens[*pos].sval);
                (*pos)++;
            }
            /* Parameters in brackets */
            if (match(tokens, pos, count, TOK_LPAREN)) {
                while (*pos < count && tokens[*pos].type != TOK_RPAREN) {
                    if (tokens[*pos].type == TOK_IDENTIFIER) {
                        amos_node_t *param = alloc_node(NODE_VARIABLE, tokens[*pos].line);
                        param->token.sval = strdup(tokens[*pos].sval);
                        add_child(n, param);
                        (*pos)++;
                    }
                    match(tokens, pos, count, TOK_COMMA);
                }
                match(tokens, pos, count, TOK_RPAREN);
            }
            return n;
        }
        case TOK_END_PROC: {
            amos_node_t *n = alloc_node(NODE_COMMAND, tok->line);
            n->token.type = TOK_END_PROC;
            return n;
        }
        case TOK_ELSE: {
            amos_node_t *n = alloc_node(NODE_COMMAND, tok->line);
            n->token.type = TOK_ELSE;
            return n;
        }
        case TOK_END_IF: {
            amos_node_t *n = alloc_node(NODE_COMMAND, tok->line);
            n->token.type = TOK_END_IF;
            return n;
        }

        /* File I/O commands */
        case TOK_INPUT: {
            /* Check for Input #channel — file I/O */
            if (peek_is(tokens, *pos, count, TOK_HASH)) {
                (*pos)++;  /* skip # */
                amos_node_t *n = alloc_node(NODE_COMMAND, tok->line);
                n->token.type = TOK_INPUT_FILE;
                /* channel number */
                amos_node_t *ch = amos_parse_expression(tokens, pos, count);
                add_child(n, ch);
                match(tokens, pos, count, TOK_COMMA);
                /* target variable(s) */
                while (!at_end(tokens, *pos, count)) {
                    if (tokens[*pos].type == TOK_IDENTIFIER) {
                        amos_node_t *var = alloc_node(NODE_VARIABLE, tokens[*pos].line);
                        var->token = tokens[*pos];
                        var->token.sval = strdup(tokens[*pos].sval);
                        add_child(n, var);
                        (*pos)++;
                    }
                    if (!match(tokens, pos, count, TOK_COMMA)) break;
                }
                return n;
            }
            return parse_command(tokens, pos, count, TOK_INPUT);
        }
        case TOK_LINE_INPUT_FILE: {
            /* Line Input #channel, var$ */
            match(tokens, pos, count, TOK_HASH);  /* optional # */
            amos_node_t *n = alloc_node(NODE_COMMAND, tok->line);
            n->token.type = TOK_LINE_INPUT_FILE;
            /* channel number */
            amos_node_t *ch = amos_parse_expression(tokens, pos, count);
            add_child(n, ch);
            match(tokens, pos, count, TOK_COMMA);
            /* target variable */
            if (*pos < count && tokens[*pos].type == TOK_IDENTIFIER) {
                amos_node_t *var = alloc_node(NODE_VARIABLE, tokens[*pos].line);
                var->token = tokens[*pos];
                var->token.sval = strdup(tokens[*pos].sval);
                add_child(n, var);
                (*pos)++;
            }
            return n;
        }
        case TOK_OPEN_IN: {
            /* Open In channel,"path" */
            amos_node_t *n = alloc_node(NODE_COMMAND, tok->line);
            n->token.type = TOK_OPEN_IN;
            amos_node_t *ch = amos_parse_expression(tokens, pos, count);
            add_child(n, ch);
            match(tokens, pos, count, TOK_COMMA);
            amos_node_t *path = amos_parse_expression(tokens, pos, count);
            add_child(n, path);
            return n;
        }
        case TOK_OPEN_OUT: {
            /* Open Out channel,"path" */
            amos_node_t *n = alloc_node(NODE_COMMAND, tok->line);
            n->token.type = TOK_OPEN_OUT;
            amos_node_t *ch = amos_parse_expression(tokens, pos, count);
            add_child(n, ch);
            match(tokens, pos, count, TOK_COMMA);
            amos_node_t *path = amos_parse_expression(tokens, pos, count);
            add_child(n, path);
            return n;
        }
        case TOK_APPEND: {
            /* Append channel,"path" */
            amos_node_t *n = alloc_node(NODE_COMMAND, tok->line);
            n->token.type = TOK_APPEND;
            amos_node_t *ch = amos_parse_expression(tokens, pos, count);
            add_child(n, ch);
            match(tokens, pos, count, TOK_COMMA);
            amos_node_t *path = amos_parse_expression(tokens, pos, count);
            add_child(n, path);
            return n;
        }
        case TOK_CLOSE: {
            /* Close channel (or Close #channel) */
            match(tokens, pos, count, TOK_HASH);  /* optional # */
            amos_node_t *n = alloc_node(NODE_COMMAND, tok->line);
            n->token.type = TOK_CLOSE;
            amos_node_t *ch = amos_parse_expression(tokens, pos, count);
            add_child(n, ch);
            return n;
        }
        case TOK_KILL:
        case TOK_RENAME:
        case TOK_MKDIR:
            return parse_command(tokens, pos, count, tok->type);

        /* All other commands: parse as generic command with arguments */
        case TOK_CLS:
        case TOK_HOME:
        case TOK_INK:
        case TOK_COLOUR:
        case TOK_PALETTE:
        case TOK_PLOT:
        case TOK_DRAW:
        case TOK_BOX:
        case TOK_BAR:
        case TOK_CIRCLE:
        case TOK_ELLIPSE:
        case TOK_PAINT:
        case TOK_POLYGON:
        case TOK_TEXT:
        case TOK_SET_FONT:
        case TOK_GR_WRITING:
        case TOK_LOCATE:
        case TOK_SCREEN_OPEN:
        case TOK_SCREEN_CLOSE:
        case TOK_SCREEN_DISPLAY:
        case TOK_SCREEN_OFFSET:
        case TOK_SCREEN:
        case TOK_SPRITE:
        case TOK_BOB:
        case TOK_SPRITE_OFF:
        case TOK_BOB_OFF:
        case TOK_ANIM:
        case TOK_BOOM:
        case TOK_SHOOT:
        case TOK_BELL:
        case TOK_SAM_PLAY:
        case TOK_VOLUME:
        case TOK_MUSIC:
        case TOK_TRACK_PLAY:
        case TOK_TRACK_STOP:
        case TOK_AMAL:
        case TOK_AMAL_ON:
        case TOK_AMAL_OFF:
        case TOK_SYNCHRO:
        case TOK_RESERVE:
        case TOK_ERASE:
        case TOK_LOAD:
        case TOK_SAVE:
        case TOK_RAINBOW:
        case TOK_COPPER:
        case TOK_SCREEN_COPY:
        case TOK_GET_BLOCK:
        case TOK_PUT_BLOCK:
        case TOK_DEL_BLOCK:
        case TOK_SCROLL:
        case TOK_DEF_SCROLL:
        case TOK_SCREEN_TO_FRONT:
        case TOK_SCREEN_TO_BACK:
        case TOK_SCREEN_HIDE:
        case TOK_SCREEN_SHOW:
        case TOK_SCREEN_SWAP:
        case TOK_DOUBLE_BUFFER:
        case TOK_AUTOBACK:
        case TOK_WAIT_VBL:
        case TOK_WAIT_KEY:
        case TOK_WAIT:
        case TOK_RANDOMIZE:
        case TOK_MODE:
        case TOK_SCREEN_MODE:
        case TOK_ON_ERROR_GOTO:
        case TOK_ON_ERROR_PROC:
        case TOK_RESUME:
        case TOK_RESUME_NEXT:
        case TOK_RESUME_LABEL:
        case TOK_TRAP:
        case TOK_ERROR:
        case TOK_POKE:
            return parse_command(tokens, pos, count, tok->type);

        /* Identifier — could be assignment or procedure call */
        case TOK_IDENTIFIER:
            (*pos)--;  /* back up to re-read identifier */
            return parse_let(tokens, pos, count, false);

        default:
            /* Unknown token — skip */
            return NULL;
    }
}

/* ── AST Cleanup ─────────────────────────────────────────────────── */

void amos_node_free(amos_node_t *node)
{
    if (!node) return;
    for (int i = 0; i < node->child_count; i++) {
        amos_node_free(node->children[i]);
    }
    if ((node->token.type == TOK_STRING || node->token.type == TOK_IDENTIFIER ||
         node->token.type == TOK_LABEL) && node->token.sval) {
        free(node->token.sval);
    }
    free(node);
}
