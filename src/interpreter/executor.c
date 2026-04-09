/*
 * executor.c — AMOS BASIC AST executor
 *
 * Walks the AST and dispatches commands. Manages control flow
 * (If/Then/Else, For/Next, While/Wend, Gosub/Return).
 */

#include "amos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* ── Expression Evaluation ───────────────────────────────────────── */

typedef struct {
    amos_var_type_t type;
    union {
        int32_t ival;
        double  fval;
        char   *sval;   /* owned — caller must free if type == VAR_STRING */
    };
} eval_result_t;

static eval_result_t eval_node(amos_state_t *state, amos_node_t *node);

static eval_result_t make_int(int32_t v)
{
    return (eval_result_t){.type = VAR_INTEGER, .ival = v};
}

static eval_result_t make_float(double v)
{
    return (eval_result_t){.type = VAR_FLOAT, .fval = v};
}

static eval_result_t make_string(const char *s)
{
    return (eval_result_t){.type = VAR_STRING, .sval = strdup(s ? s : "")};
}

static double to_number(eval_result_t r)
{
    if (r.type == VAR_FLOAT) return r.fval;
    if (r.type == VAR_INTEGER) return (double)r.ival;
    return 0.0;
}

static int32_t to_int(eval_result_t r)
{
    if (r.type == VAR_INTEGER) return r.ival;
    if (r.type == VAR_FLOAT) return (int32_t)r.fval;
    return 0;
}

static void free_result(eval_result_t *r)
{
    if (r->type == VAR_STRING && r->sval) {
        free(r->sval);
        r->sval = NULL;
    }
}

/* Evaluate built-in functions */
static eval_result_t eval_function(amos_state_t *state, amos_node_t *node)
{
    const char *name = node->token.sval;
    int argc = node->child_count;

    /* Evaluate arguments */
    eval_result_t args[8] = {0};
    for (int i = 0; i < argc && i < 8; i++) {
        args[i] = eval_node(state, node->children[i]);
    }

    eval_result_t result = make_int(0);

    if (strcasecmp(name, "Rnd") == 0) {
        int32_t max = argc > 0 ? to_int(args[0]) : 1;
        result = make_int(max > 0 ? rand() % max : 0);
    }
    else if (strcasecmp(name, "Abs") == 0) {
        double v = to_number(args[0]);
        result = (args[0].type == VAR_FLOAT) ? make_float(fabs(v)) : make_int(abs(to_int(args[0])));
    }
    else if (strcasecmp(name, "Sgn") == 0) {
        double v = to_number(args[0]);
        result = make_int(v > 0 ? 1 : (v < 0 ? -1 : 0));
    }
    else if (strcasecmp(name, "Int") == 0) {
        result = make_int((int32_t)floor(to_number(args[0])));
    }
    else if (strcasecmp(name, "Sin") == 0) {
        result = make_float(sin(to_number(args[0])));
    }
    else if (strcasecmp(name, "Cos") == 0) {
        result = make_float(cos(to_number(args[0])));
    }
    else if (strcasecmp(name, "Tan") == 0) {
        result = make_float(tan(to_number(args[0])));
    }
    else if (strcasecmp(name, "Sqr") == 0) {
        result = make_float(sqrt(to_number(args[0])));
    }
    else if (strcasecmp(name, "Log") == 0) {
        result = make_float(log(to_number(args[0])));
    }
    else if (strcasecmp(name, "Exp") == 0) {
        result = make_float(exp(to_number(args[0])));
    }
    else if (strcasecmp(name, "Min") == 0) {
        double a = to_number(args[0]), b = to_number(args[1]);
        result = make_int((int32_t)(a < b ? a : b));
    }
    else if (strcasecmp(name, "Max") == 0) {
        double a = to_number(args[0]), b = to_number(args[1]);
        result = make_int((int32_t)(a > b ? a : b));
    }
    else if (strcasecmp(name, "Len") == 0) {
        result = make_int(args[0].type == VAR_STRING ? (int32_t)strlen(args[0].sval) : 0);
    }
    else if (strcasecmp(name, "Str$") == 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", to_int(args[0]));
        result = make_string(buf);
    }
    else if (strcasecmp(name, "Val") == 0) {
        result = make_int(args[0].type == VAR_STRING ? (int32_t)atoi(args[0].sval) : 0);
    }
    else if (strcasecmp(name, "Chr$") == 0) {
        char buf[2] = {(char)to_int(args[0]), 0};
        result = make_string(buf);
    }
    else if (strcasecmp(name, "Asc") == 0) {
        result = make_int(args[0].type == VAR_STRING && args[0].sval[0] ?
                          (int32_t)(unsigned char)args[0].sval[0] : 0);
    }
    else if (strcasecmp(name, "Mid$") == 0) {
        if (args[0].type == VAR_STRING) {
            int start = to_int(args[1]) - 1; /* AMOS is 1-based */
            int len = argc > 2 ? to_int(args[2]) : (int)strlen(args[0].sval) - start;
            int slen = (int)strlen(args[0].sval);
            if (start < 0) start = 0;
            if (start >= slen) { result = make_string(""); }
            else {
                if (start + len > slen) len = slen - start;
                char *buf = malloc(len + 1);
                memcpy(buf, args[0].sval + start, len);
                buf[len] = '\0';
                result = make_string(buf);
                free(buf);
            }
        } else {
            result = make_string("");
        }
    }
    else if (strcasecmp(name, "Left$") == 0) {
        if (args[0].type == VAR_STRING) {
            int len = to_int(args[1]);
            int slen = (int)strlen(args[0].sval);
            if (len > slen) len = slen;
            char *buf = malloc(len + 1);
            memcpy(buf, args[0].sval, len);
            buf[len] = '\0';
            result = make_string(buf);
            free(buf);
        } else {
            result = make_string("");
        }
    }
    else if (strcasecmp(name, "Right$") == 0) {
        if (args[0].type == VAR_STRING) {
            int len = to_int(args[1]);
            int slen = (int)strlen(args[0].sval);
            if (len > slen) len = slen;
            result = make_string(args[0].sval + slen - len);
        } else {
            result = make_string("");
        }
    }
    else if (strcasecmp(name, "Instr") == 0) {
        if (args[0].type == VAR_STRING && args[1].type == VAR_STRING) {
            char *found = strstr(args[0].sval, args[1].sval);
            result = make_int(found ? (int32_t)(found - args[0].sval + 1) : 0);
        } else {
            result = make_int(0);
        }
    }
    else if (strcasecmp(name, "Timer") == 0) {
        result = make_int((int32_t)state->timer);
    }
    else if (strcasecmp(name, "Screen Width") == 0) {
        amos_screen_t *scr = &state->screens[state->current_screen];
        result = make_int(scr->active ? scr->width : 0);
    }
    else if (strcasecmp(name, "Screen Height") == 0) {
        amos_screen_t *scr = &state->screens[state->current_screen];
        result = make_int(scr->active ? scr->height : 0);
    }

    /* Clean up argument strings */
    for (int i = 0; i < argc && i < 8; i++) {
        free_result(&args[i]);
    }

    return result;
}

static eval_result_t eval_node(amos_state_t *state, amos_node_t *node)
{
    if (!node) return make_int(0);

    switch (node->type) {
        case NODE_INT_LITERAL:
            return make_int(node->token.ival);

        case NODE_FLOAT_LITERAL:
            return make_float(node->token.fval);

        case NODE_STRING_LITERAL:
            return make_string(node->token.sval);

        case NODE_VARIABLE: {
            amos_var_t *var = amos_var_get(state, node->token.sval);
            if (!var) return make_int(0);
            switch (var->type) {
                case VAR_INTEGER: return make_int(var->ival);
                case VAR_FLOAT:   return make_float(var->fval);
                case VAR_STRING:  return make_string(var->sval.data ? var->sval.data : "");
            }
            return make_int(0);
        }

        case NODE_FUNCTION_CALL:
            return eval_function(state, node);

        case NODE_UNARY_OP: {
            eval_result_t operand = eval_node(state, node->children[0]);
            if (node->token.type == TOK_MINUS) {
                if (operand.type == VAR_FLOAT) return make_float(-operand.fval);
                return make_int(-operand.ival);
            }
            if (node->token.type == TOK_NOT) {
                return make_int(~to_int(operand));
            }
            free_result(&operand);
            return make_int(0);
        }

        case NODE_BINARY_OP: {
            eval_result_t left = eval_node(state, node->children[0]);
            eval_result_t right = eval_node(state, node->children[1]);
            eval_result_t result;

            /* String concatenation */
            if (node->token.type == TOK_PLUS &&
                (left.type == VAR_STRING || right.type == VAR_STRING)) {
                const char *ls = left.type == VAR_STRING ? left.sval : "";
                const char *rs = right.type == VAR_STRING ? right.sval : "";
                char *buf = malloc(strlen(ls) + strlen(rs) + 1);
                sprintf(buf, "%s%s", ls, rs);
                result = make_string(buf);
                free(buf);
                free_result(&left);
                free_result(&right);
                return result;
            }

            /* String comparison */
            if (left.type == VAR_STRING && right.type == VAR_STRING) {
                int cmp = strcmp(left.sval, right.sval);
                free_result(&left);
                free_result(&right);
                switch (node->token.type) {
                    case TOK_EQUAL:         return make_int(cmp == 0 ? -1 : 0);
                    case TOK_NOT_EQUAL:     return make_int(cmp != 0 ? -1 : 0);
                    case TOK_LESS:          return make_int(cmp < 0 ? -1 : 0);
                    case TOK_GREATER:       return make_int(cmp > 0 ? -1 : 0);
                    case TOK_LESS_EQUAL:    return make_int(cmp <= 0 ? -1 : 0);
                    case TOK_GREATER_EQUAL: return make_int(cmp >= 0 ? -1 : 0);
                    default: return make_int(0);
                }
            }

            /* Numeric operations */
            bool use_float = (left.type == VAR_FLOAT || right.type == VAR_FLOAT);
            double lv = to_number(left);
            double rv = to_number(right);
            free_result(&left);
            free_result(&right);

            switch (node->token.type) {
                case TOK_PLUS:      result = use_float ? make_float(lv + rv) : make_int((int32_t)(lv + rv)); break;
                case TOK_MINUS:     result = use_float ? make_float(lv - rv) : make_int((int32_t)(lv - rv)); break;
                case TOK_MULTIPLY:  result = use_float ? make_float(lv * rv) : make_int((int32_t)(lv * rv)); break;
                case TOK_DIVIDE:    result = (rv != 0) ? (use_float ? make_float(lv / rv) : make_int((int32_t)(lv / rv))) : make_int(0); break;
                case TOK_MOD:       result = make_int(rv != 0 ? (int32_t)lv % (int32_t)rv : 0); break;
                case TOK_POWER:     result = make_float(pow(lv, rv)); break;
                case TOK_EQUAL:         result = make_int(lv == rv ? -1 : 0); break;
                case TOK_NOT_EQUAL:     result = make_int(lv != rv ? -1 : 0); break;
                case TOK_LESS:          result = make_int(lv < rv ? -1 : 0); break;
                case TOK_GREATER:       result = make_int(lv > rv ? -1 : 0); break;
                case TOK_LESS_EQUAL:    result = make_int(lv <= rv ? -1 : 0); break;
                case TOK_GREATER_EQUAL: result = make_int(lv >= rv ? -1 : 0); break;
                case TOK_AND:       result = make_int((int32_t)lv & (int32_t)rv); break;
                case TOK_OR:        result = make_int((int32_t)lv | (int32_t)rv); break;
                case TOK_XOR:       result = make_int((int32_t)lv ^ (int32_t)rv); break;
                default:            result = make_int(0); break;
            }
            return result;
        }

        default:
            return make_int(0);
    }
}

/* ── Find line by number ─────────────────────────────────────────── */

static int find_line_index(amos_state_t *state, int line_number)
{
    for (int i = 0; i < state->line_count; i++) {
        if (state->lines[i].number == line_number)
            return i;
    }
    return -1;
}

/* Find label by name */
static int find_label(amos_state_t *state, const char *name)
{
    for (int i = 0; i < state->line_count; i++) {
        if (state->lines[i].ast && state->lines[i].ast->type == NODE_LABEL &&
            state->lines[i].ast->token.sval &&
            strcasecmp(state->lines[i].ast->token.sval, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* ── Statement Execution ─────────────────────────────────────────── */

void amos_execute_node(amos_state_t *state, amos_node_t *node)
{
    if (!node || !state->running) return;

    switch (node->type) {
        case NODE_REM:
            /* Comment — do nothing */
            break;

        case NODE_LABEL:
            /* Label definition — do nothing at execution time */
            break;

        case NODE_END:
            state->running = false;
            break;

        case NODE_PRINT: {
            bool suppress_newline = false;
            for (int i = 0; i < node->child_count; i++) {
                amos_node_t *child = node->children[i];
                suppress_newline = false;

                if (child->type == NODE_COMMAND && child->token.type == TOK_SEMICOLON) {
                    suppress_newline = true;
                    continue;
                }
                if (child->type == NODE_COMMAND && child->token.type == TOK_COMMA) {
                    amos_screen_print(state, "\t");
                    suppress_newline = true;
                    continue;
                }

                eval_result_t r = eval_node(state, child);
                char buf[256];
                switch (r.type) {
                    case VAR_INTEGER:
                        snprintf(buf, sizeof(buf), "%d", r.ival);
                        amos_screen_print(state, buf);
                        break;
                    case VAR_FLOAT:
                        snprintf(buf, sizeof(buf), "%g", r.fval);
                        amos_screen_print(state, buf);
                        break;
                    case VAR_STRING:
                        amos_screen_print(state, r.sval ? r.sval : "");
                        break;
                }
                free_result(&r);

                /* Check if next child is semicolon */
                if (i + 1 < node->child_count &&
                    node->children[i + 1]->type == NODE_COMMAND &&
                    (node->children[i + 1]->token.type == TOK_SEMICOLON ||
                     node->children[i + 1]->token.type == TOK_COMMA)) {
                    suppress_newline = true;
                }
            }
            if (!suppress_newline) {
                amos_screen_print(state, "\n");
            }
            break;
        }

        case NODE_LET: {
            const char *name = node->token.sval;
            if (!name) break;

            /* Last child is the value expression */
            if (node->child_count > 0) {
                eval_result_t val = eval_node(state, node->children[node->child_count - 1]);
                switch (val.type) {
                    case VAR_INTEGER: amos_var_set_int(state, name, val.ival); break;
                    case VAR_FLOAT:   amos_var_set_float(state, name, val.fval); break;
                    case VAR_STRING:  amos_var_set_string(state, name, val.sval ? val.sval : ""); break;
                }
                free_result(&val);
            }
            break;
        }

        case NODE_IF: {
            if (node->child_count < 2) break;
            eval_result_t cond = eval_node(state, node->children[0]);
            bool is_true = to_int(cond) != 0;
            free_result(&cond);

            if (is_true) {
                /* Execute then-branch */
                amos_node_t *then_body = node->children[1];
                for (int i = 0; i < then_body->child_count && state->running; i++) {
                    amos_execute_node(state, then_body->children[i]);
                }
            } else if (node->child_count > 2) {
                /* Execute else-branch */
                amos_node_t *else_body = node->children[2];
                for (int i = 0; i < else_body->child_count && state->running; i++) {
                    amos_execute_node(state, else_body->children[i]);
                }
            }
            break;
        }

        case NODE_FOR: {
            const char *var_name = node->token.sval;
            if (!var_name || node->child_count < 2) break;

            eval_result_t start_val = eval_node(state, node->children[0]);
            eval_result_t end_val = eval_node(state, node->children[1]);
            double step = 1.0;
            if (node->child_count > 2) {
                eval_result_t step_val = eval_node(state, node->children[2]);
                step = to_number(step_val);
                free_result(&step_val);
            }

            /* Set loop variable */
            amos_var_set_float(state, var_name, to_number(start_val));

            /* Push FOR entry — loop_line points to the line AFTER the For */
            if (state->for_top < AMOS_MAX_FOR_DEPTH) {
                for_entry_t *fe = &state->for_stack[state->for_top++];
                strncpy(fe->var_name, var_name, sizeof(fe->var_name) - 1);
                fe->limit = to_number(end_val);
                fe->step = step;
                fe->loop_line = state->current_line + 1;
                fe->loop_pos = 0;
            }

            free_result(&start_val);
            free_result(&end_val);
            break;
        }

        case NODE_COMMAND: {
            switch (node->token.type) {
                case TOK_NEXT: {
                    if (state->for_top <= 0) break;
                    for_entry_t *fe = &state->for_stack[state->for_top - 1];

                    /* Increment */
                    amos_var_t *var = amos_var_get(state, fe->var_name);
                    if (!var) break;

                    double val = (var->type == VAR_FLOAT ? var->fval : (double)var->ival) + fe->step;
                    amos_var_set_float(state, fe->var_name, val);

                    /* Check completion */
                    bool done = (fe->step > 0 && val > fe->limit) ||
                                (fe->step < 0 && val < fe->limit);

                    if (done) {
                        state->for_top--;
                    } else {
                        /* Loop back */
                        state->current_line = fe->loop_line;
                        state->current_pos = fe->loop_pos;
                    }
                    break;
                }

                case TOK_WEND: {
                    /* Find matching While — walk backwards */
                    for (int i = state->current_line - 1; i >= 0; i--) {
                        amos_node_t *ln = state->lines[i].ast;
                        if (ln && ln->type == NODE_WHILE) {
                            state->current_line = i;
                            return;
                        }
                    }
                    break;
                }

                case TOK_DO: {
                    /* Push loop point onto gosub stack */
                    if (state->gosub_top < AMOS_MAX_GOSUB_DEPTH) {
                        gosub_entry_t *ge = &state->gosub_stack[state->gosub_top++];
                        ge->return_line = state->current_line + 1;
                        ge->return_pos = 0;
                    }
                    break;
                }

                case TOK_LOOP: {
                    /* Jump back to Do */
                    if (state->gosub_top > 0) {
                        gosub_entry_t *ge = &state->gosub_stack[state->gosub_top - 1];
                        state->current_line = ge->return_line;
                        state->current_pos = ge->return_pos;
                    }
                    break;
                }

                case TOK_UNTIL: {
                    if (node->child_count > 0) {
                        eval_result_t cond = eval_node(state, node->children[0]);
                        bool is_true = to_int(cond) != 0;
                        free_result(&cond);
                        if (!is_true) {
                            /* Loop back — find Repeat */
                        }
                    }
                    break;
                }

                case TOK_CLS: {
                    int color = 0;
                    if (node->child_count > 0) {
                        eval_result_t r = eval_node(state, node->children[0]);
                        color = to_int(r);
                        free_result(&r);
                    }
                    amos_screen_cls(state, color);
                    break;
                }

                case TOK_INK: {
                    int pen = 0, paper = -1, outline = -1;
                    if (node->child_count > 0) {
                        eval_result_t r = eval_node(state, node->children[0]);
                        pen = to_int(r); free_result(&r);
                    }
                    if (node->child_count > 1) {
                        eval_result_t r = eval_node(state, node->children[1]);
                        paper = to_int(r); free_result(&r);
                    }
                    if (node->child_count > 2) {
                        eval_result_t r = eval_node(state, node->children[2]);
                        outline = to_int(r); free_result(&r);
                    }
                    amos_screen_ink(state, pen, paper, outline);
                    break;
                }

                case TOK_COLOUR: {
                    if (node->child_count >= 2) {
                        eval_result_t idx = eval_node(state, node->children[0]);
                        eval_result_t rgb = eval_node(state, node->children[1]);
                        amos_screen_colour(state, to_int(idx), (uint32_t)to_int(rgb));
                        free_result(&idx);
                        free_result(&rgb);
                    }
                    break;
                }

                case TOK_PALETTE: {
                    /* Palette $RGB,$RGB,... — set multiple colors */
                    uint32_t colors[32] = {0};
                    int count = 0;
                    for (int i = 0; i < node->child_count && count < 32; i++) {
                        eval_result_t r = eval_node(state, node->children[i]);
                        colors[count++] = (uint32_t)to_int(r);
                        free_result(&r);
                    }
                    amos_screen_palette(state, colors, count);
                    break;
                }

                case TOK_PLOT: {
                    if (node->child_count >= 2) {
                        eval_result_t x = eval_node(state, node->children[0]);
                        eval_result_t y = eval_node(state, node->children[1]);
                        int color = -1;
                        if (node->child_count > 2) {
                            eval_result_t c = eval_node(state, node->children[2]);
                            color = to_int(c); free_result(&c);
                        }
                        amos_screen_plot(state, to_int(x), to_int(y),
                                         color >= 0 ? color : state->screens[state->current_screen].ink_color);
                        free_result(&x); free_result(&y);
                    }
                    break;
                }

                case TOK_DRAW: {
                    /* Draw x1,y1 To x2,y2 */
                    int coords[4] = {0};
                    int ci = 0;
                    for (int i = 0; i < node->child_count && ci < 4; i++) {
                        if (node->children[i]->type == NODE_COMMAND &&
                            node->children[i]->token.type == TOK_TO)
                            continue;
                        eval_result_t r = eval_node(state, node->children[i]);
                        coords[ci++] = to_int(r);
                        free_result(&r);
                    }
                    if (ci >= 4) {
                        amos_screen_draw(state, coords[0], coords[1], coords[2], coords[3],
                                         state->screens[state->current_screen].ink_color);
                    }
                    break;
                }

                case TOK_BOX: {
                    int coords[4] = {0};
                    int ci = 0;
                    for (int i = 0; i < node->child_count && ci < 4; i++) {
                        if (node->children[i]->type == NODE_COMMAND &&
                            node->children[i]->token.type == TOK_TO)
                            continue;
                        eval_result_t r = eval_node(state, node->children[i]);
                        coords[ci++] = to_int(r);
                        free_result(&r);
                    }
                    if (ci >= 4) {
                        amos_screen_box(state, coords[0], coords[1], coords[2], coords[3],
                                        state->screens[state->current_screen].ink_color);
                    }
                    break;
                }

                case TOK_BAR: {
                    int coords[4] = {0};
                    int ci = 0;
                    for (int i = 0; i < node->child_count && ci < 4; i++) {
                        if (node->children[i]->type == NODE_COMMAND &&
                            node->children[i]->token.type == TOK_TO)
                            continue;
                        eval_result_t r = eval_node(state, node->children[i]);
                        coords[ci++] = to_int(r);
                        free_result(&r);
                    }
                    if (ci >= 4) {
                        amos_screen_bar(state, coords[0], coords[1], coords[2], coords[3],
                                        state->screens[state->current_screen].ink_color);
                    }
                    break;
                }

                case TOK_CIRCLE: {
                    if (node->child_count >= 3) {
                        eval_result_t x = eval_node(state, node->children[0]);
                        eval_result_t y = eval_node(state, node->children[1]);
                        eval_result_t r = eval_node(state, node->children[2]);
                        amos_screen_circle(state, to_int(x), to_int(y), to_int(r),
                                           state->screens[state->current_screen].ink_color);
                        free_result(&x); free_result(&y); free_result(&r);
                    }
                    break;
                }

                case TOK_LOCATE: {
                    if (node->child_count >= 2) {
                        eval_result_t x = eval_node(state, node->children[0]);
                        eval_result_t y = eval_node(state, node->children[1]);
                        amos_screen_locate(state, to_int(x), to_int(y));
                        free_result(&x); free_result(&y);
                    }
                    break;
                }

                case TOK_SCREEN_OPEN: {
                    /* Screen Open id, w, h, depth, mode */
                    int args[5] = {0, 320, 256, 5, 0};
                    for (int i = 0; i < node->child_count && i < 5; i++) {
                        eval_result_t r = eval_node(state, node->children[i]);
                        args[i] = to_int(r);
                        free_result(&r);
                    }
                    amos_screen_open(state, args[0], args[1], args[2], args[3]);
                    break;
                }

                case TOK_SCREEN_CLOSE: {
                    if (node->child_count > 0) {
                        eval_result_t id = eval_node(state, node->children[0]);
                        amos_screen_close(state, to_int(id));
                        free_result(&id);
                    }
                    break;
                }

                case TOK_SCREEN: {
                    if (node->child_count > 0) {
                        eval_result_t id = eval_node(state, node->children[0]);
                        int sid = to_int(id);
                        if (sid >= 0 && sid < AMOS_MAX_SCREENS && state->screens[sid].active) {
                            state->current_screen = sid;
                        }
                        free_result(&id);
                    }
                    break;
                }

                case TOK_WAIT_VBL:
                    /* Handled by the frame tick system */
                    state->synchro = true;
                    break;

                case TOK_WAIT: {
                    /* Wait N — delay N/50ths of a second */
                    if (node->child_count > 0) {
                        eval_result_t r = eval_node(state, node->children[0]);
                        /* Simple busy wait for now */
                        free_result(&r);
                    }
                    break;
                }

                case TOK_WAIT_KEY:
                    /* TODO: implement key wait */
                    break;

                case TOK_BOOM:
                    amos_boom(state);
                    break;

                case TOK_SHOOT:
                    amos_shoot(state);
                    break;

                case TOK_BELL:
                    amos_bell(state);
                    break;

                case TOK_VOLUME: {
                    if (node->child_count > 0) {
                        eval_result_t r = eval_node(state, node->children[0]);
                        int vol = to_int(r);
                        for (int ch = 0; ch < 4; ch++)
                            state->paula.channels[ch].volume = (uint8_t)(vol > 64 ? 64 : vol);
                        free_result(&r);
                    }
                    break;
                }

                case TOK_RANDOMIZE: {
                    if (node->child_count > 0) {
                        eval_result_t r = eval_node(state, node->children[0]);
                        srand((unsigned)to_int(r));
                        free_result(&r);
                    } else {
                        srand((unsigned)time(NULL));
                    }
                    break;
                }

                case TOK_MODE: {
                    /* Mode Classic / Mode Modern */
                    if (node->child_count > 0 && node->children[0]->type == NODE_VARIABLE) {
                        const char *mode = node->children[0]->token.sval;
                        if (strcasecmp(mode, "Classic") == 0)
                            state->display_mode = AMOS_MODE_CLASSIC;
                        else if (strcasecmp(mode, "Modern") == 0)
                            state->display_mode = AMOS_MODE_MODERN;
                    }
                    break;
                }

                case TOK_DOUBLE_BUFFER: {
                    amos_screen_t *scr = &state->screens[state->current_screen];
                    if (scr->active && !scr->double_buffered) {
                        scr->back_buffer = calloc(scr->width * scr->height, sizeof(uint32_t));
                        scr->double_buffered = true;
                    }
                    break;
                }

                case TOK_SCREEN_SWAP: {
                    amos_screen_t *scr = &state->screens[state->current_screen];
                    if (scr->active && scr->double_buffered) {
                        uint32_t *tmp = scr->pixels;
                        scr->pixels = scr->back_buffer;
                        scr->back_buffer = tmp;
                    }
                    break;
                }

                case TOK_AUTOBACK: {
                    if (node->child_count > 0) {
                        eval_result_t r = eval_node(state, node->children[0]);
                        state->screens[state->current_screen].autoback = to_int(r);
                        free_result(&r);
                    }
                    break;
                }

                case TOK_HOME:
                    amos_screen_locate(state, 0, 0);
                    break;

                default:
                    break;
            }
            break;
        }

        case NODE_GOTO: {
            if (node->child_count > 0) {
                eval_result_t target = eval_node(state, node->children[0]);
                int idx = find_line_index(state, to_int(target));
                if (idx >= 0) {
                    state->current_line = idx;
                    state->current_pos = 0;
                }
                free_result(&target);
            }
            break;
        }

        case NODE_GOSUB: {
            if (node->child_count > 0) {
                eval_result_t target = eval_node(state, node->children[0]);
                int idx;

                /* Try as label first if it's a string */
                if (target.type == VAR_STRING) {
                    idx = find_label(state, target.sval);
                } else {
                    idx = find_line_index(state, to_int(target));
                }

                if (idx >= 0 && state->gosub_top < AMOS_MAX_GOSUB_DEPTH) {
                    gosub_entry_t *ge = &state->gosub_stack[state->gosub_top++];
                    ge->return_line = state->current_line;
                    ge->return_pos = state->current_pos;
                    state->current_line = idx;
                    state->current_pos = 0;
                }
                free_result(&target);
            }
            break;
        }

        case NODE_RETURN:
            if (state->gosub_top > 0) {
                gosub_entry_t *ge = &state->gosub_stack[--state->gosub_top];
                state->current_line = ge->return_line;
                state->current_pos = ge->return_pos;
            }
            break;

        case NODE_DATA:
            /* Data statements are read by READ, not executed */
            break;

        case NODE_READ: {
            /* Read through DATA statements */
            for (int i = 0; i < node->child_count; i++) {
                if (node->children[i]->type == NODE_VARIABLE) {
                    const char *var_name = node->children[i]->token.sval;
                    /* Find next DATA value */
                    while (state->data_line < state->line_count) {
                        amos_node_t *dl = state->lines[state->data_line].ast;
                        if (dl && dl->type == NODE_DATA &&
                            state->data_pos < dl->child_count) {
                            eval_result_t val = eval_node(state, dl->children[state->data_pos++]);
                            switch (val.type) {
                                case VAR_INTEGER: amos_var_set_int(state, var_name, val.ival); break;
                                case VAR_FLOAT:   amos_var_set_float(state, var_name, val.fval); break;
                                case VAR_STRING:  amos_var_set_string(state, var_name, val.sval); break;
                            }
                            free_result(&val);
                            break;
                        }
                        state->data_line++;
                        state->data_pos = 0;
                    }
                }
            }
            break;
        }

        case NODE_RESTORE:
            state->data_line = 0;
            state->data_pos = 0;
            break;

        default:
            break;
    }
}

/* ── Execute a program line ──────────────────────────────────────── */

void amos_execute_line(amos_state_t *state, int line_index)
{
    if (line_index < 0 || line_index >= state->line_count) return;

    amos_program_line_t *pl = &state->lines[line_index];

    /* Lazy parse if needed */
    if (!pl->ast && pl->text) {
        amos_token_list_t *tokens = amos_tokenize(pl->text);
        if (tokens && tokens->count > 0) {
            int pos = 0;
            pl->ast = amos_parse_line(tokens->tokens, &pos, tokens->count);

            /* Handle multiple statements per line (separated by :) */
            /* For now, just parse the first statement */
        }
        amos_token_list_free(tokens);
    }

    if (pl->ast) {
        amos_execute_node(state, pl->ast);
    }
}
