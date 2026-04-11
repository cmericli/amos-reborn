/*
 * expressions.c — AMOS expression evaluator
 *
 * Pratt parser for AMOS BASIC expressions with precedence climbing.
 * Handles arithmetic, comparisons, logical ops, string ops, and function calls.
 */

#include "amos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── Precedence Levels ───────────────────────────────────────────── */

static int get_precedence(amos_token_type_t type)
{
    switch (type) {
        case TOK_OR:             return 1;
        case TOK_AND:            return 2;
        case TOK_XOR:            return 2;
        case TOK_NOT:            return 3;
        case TOK_EQUAL:
        case TOK_NOT_EQUAL:
        case TOK_LESS:
        case TOK_GREATER:
        case TOK_LESS_EQUAL:
        case TOK_GREATER_EQUAL:  return 4;
        case TOK_PLUS:
        case TOK_MINUS:          return 5;
        case TOK_MULTIPLY:
        case TOK_DIVIDE:
        case TOK_MOD:            return 6;
        case TOK_POWER:          return 7;
        default:                 return 0;
    }
}

static bool is_binary_op(amos_token_type_t type)
{
    switch (type) {
        case TOK_PLUS: case TOK_MINUS: case TOK_MULTIPLY: case TOK_DIVIDE:
        case TOK_MOD: case TOK_POWER: case TOK_EQUAL: case TOK_NOT_EQUAL:
        case TOK_LESS: case TOK_GREATER: case TOK_LESS_EQUAL:
        case TOK_GREATER_EQUAL: case TOK_AND: case TOK_OR: case TOK_XOR:
            return true;
        default:
            return false;
    }
}

/* ── Built-in Functions ──────────────────────────────────────────── */

typedef struct {
    const char *name;
    int min_args;
    int max_args;
} builtin_func_t;

static const builtin_func_t builtins[] = {
    {"Rnd",    1, 1},
    {"Abs",    1, 1},
    {"Sgn",    1, 1},
    {"Int",    1, 1},
    {"Fix",    1, 1},
    {"Sin",    1, 1},
    {"Cos",    1, 1},
    {"Tan",    1, 1},
    {"Asin",   1, 1},
    {"Acos",   1, 1},
    {"Atan",   1, 1},
    {"Sqr",    1, 1},
    {"Log",    1, 1},
    {"Exp",    1, 1},
    {"Min",    2, 2},
    {"Max",    2, 2},
    {"Len",    1, 1},
    {"Val",    1, 1},
    {"Str$",   1, 1},
    {"Chr$",   1, 1},
    {"Asc",    1, 1},
    {"Mid$",   2, 3},
    {"Left$",  2, 2},
    {"Right$", 2, 2},
    {"Instr",  2, 3},
    {"Upper$", 1, 1},
    {"Lower$", 1, 1},
    {"Flip$",  1, 1},
    {"Trim$",  1, 1},
    {"Replace$", 3, 3},
    {"String$",2, 2},
    {"Space$", 1, 1},
    {"Hex$",   1, 2},
    {"Bin$",   1, 2},
    {"Deg",    1, 1},
    {"Rad",    1, 1},
    {"Peek",   1, 1},
    {"Free",   0, 0},
    {"Err",    0, 0},
    {"Erl",    0, 0},
    /* Screen functions */
    {"Screen Width",  0, 0},
    {"Screen Height", 0, 0},
    {"X Mouse",       0, 0},
    {"Y Mouse",       0, 0},
    {"Mouse Key",     0, 0},
    {"Mouse Click",   0, 0},
    {"Joy",           1, 1},
    {"Jup",           1, 1},
    {"Jdown",         1, 1},
    {"Jleft",         1, 1},
    {"Jright",        1, 1},
    {"Fire",          1, 1},
    {"Key State",     1, 1},
    {"Scancode",      0, 0},
    {"Inkey$",        0, 0},
    {"Point",         2, 2},
    {"Colour",        1, 1},
    {"Timer",         0, 0},
    /* Collision */
    {"Sprite Col",    1, 3},
    {"Bob Col",       1, 3},
    {"Sprite X",      1, 1},
    {"Sprite Y",      1, 1},
    {"Bob X",         1, 1},
    {"Bob Y",         1, 1},
    {"X Sprite",      1, 1},
    {"Y Sprite",      1, 1},
    {"X Bob",         1, 1},
    {"Y Bob",         1, 1},
    /* File I/O functions */
    {"Eof",           1, 1},
    {"Exist",         1, 1},
    {"Dir First$",    1, 1},
    {"Dir Next$",     0, 0},
    {"Filelen",       1, 1},
    /* String functions */
    {"Tab$",          1, 1},
    {"Insert$",       3, 3},
    /* Memory stubs */
    {"Deek",          1, 1},
    {"Leek",          1, 1},
    /* Bank functions */
    {"Start",         1, 1},
    {"Length",         1, 1},
    /* Screen position functions */
    {"X Screen",      1, 1},
    {"Y Screen",      1, 1},
    {NULL, 0, 0}
};

static bool is_builtin_function(const char *name)
{
    for (int i = 0; builtins[i].name; i++) {
        if (strcasecmp(name, builtins[i].name) == 0)
            return true;
    }
    return false;
}

/* Check if identifier is a zero-arg builtin that can be used without parens */
static bool is_noarg_builtin(const char *name)
{
    if (strcasecmp(name, "Err") == 0) return true;
    if (strcasecmp(name, "Erl") == 0) return true;
    if (strcasecmp(name, "Free") == 0) return true;
    if (strcasecmp(name, "Pi#") == 0) return true;
    return false;
}

/* ── Parser Helpers ──────────────────────────────────────────────── */

static amos_node_t *alloc_node(amos_node_type_t type, int line)
{
    amos_node_t *node = calloc(1, sizeof(amos_node_t));
    node->type = type;
    node->line = line;
    return node;
}

static void add_child(amos_node_t *parent, amos_node_t *child)
{
    if (parent->child_count < AST_MAX_CHILDREN) {
        parent->children[parent->child_count++] = child;
    }
}

/* ── Expression Parser ───────────────────────────────────────────── */

static amos_node_t *parse_primary(amos_token_t *tokens, int *pos, int count);
static amos_node_t *parse_expr(amos_token_t *tokens, int *pos, int count, int min_prec);

static amos_node_t *parse_primary(amos_token_t *tokens, int *pos, int count)
{
    if (*pos >= count) return NULL;

    amos_token_t *tok = &tokens[*pos];

    /* Integer literal */
    if (tok->type == TOK_INTEGER) {
        amos_node_t *node = alloc_node(NODE_INT_LITERAL, tok->line);
        node->token = *tok;
        (*pos)++;
        return node;
    }

    /* Float literal */
    if (tok->type == TOK_FLOAT) {
        amos_node_t *node = alloc_node(NODE_FLOAT_LITERAL, tok->line);
        node->token = *tok;
        (*pos)++;
        return node;
    }

    /* String literal */
    if (tok->type == TOK_STRING) {
        amos_node_t *node = alloc_node(NODE_STRING_LITERAL, tok->line);
        node->token = *tok;
        node->token.sval = strdup(tok->sval);
        (*pos)++;
        return node;
    }

    /* Parenthesized expression */
    if (tok->type == TOK_LPAREN) {
        (*pos)++;
        amos_node_t *expr = parse_expr(tokens, pos, count, 0);
        if (*pos < count && tokens[*pos].type == TOK_RPAREN) {
            (*pos)++;
        }
        return expr;
    }

    /* Unary minus */
    if (tok->type == TOK_MINUS) {
        (*pos)++;
        amos_node_t *operand = parse_primary(tokens, pos, count);
        if (!operand) return NULL;
        amos_node_t *node = alloc_node(NODE_UNARY_OP, tok->line);
        node->token = *tok;
        add_child(node, operand);
        return node;
    }

    /* Not */
    if (tok->type == TOK_NOT) {
        (*pos)++;
        amos_node_t *operand = parse_expr(tokens, pos, count, get_precedence(TOK_NOT));
        if (!operand) return NULL;
        amos_node_t *node = alloc_node(NODE_UNARY_OP, tok->line);
        node->token = *tok;
        add_child(node, operand);
        return node;
    }

    /* Identifier: variable, array access, or function call */
    if (tok->type == TOK_IDENTIFIER) {
        char *name = tok->sval;

        /* Multi-word function: X Screen(n) / Y Screen(n) */
        if ((strcasecmp(name, "X") == 0 || strcasecmp(name, "Y") == 0) &&
            *pos + 1 < count && tokens[*pos + 1].type == TOK_IDENTIFIER) {
            char combined[64];
            snprintf(combined, sizeof(combined), "%s %s", name, tokens[*pos + 1].sval);
            if (is_builtin_function(combined)) {
                amos_node_t *node = alloc_node(NODE_FUNCTION_CALL, tok->line);
                node->token = *tok;
                node->token.sval = strdup(combined);
                (*pos) += 2;
                if (*pos < count && tokens[*pos].type == TOK_LPAREN) {
                    (*pos)++;
                    while (*pos < count && tokens[*pos].type != TOK_RPAREN) {
                        amos_node_t *arg = parse_expr(tokens, pos, count, 0);
                        if (arg) add_child(node, arg);
                        if (*pos < count && tokens[*pos].type == TOK_COMMA)
                            (*pos)++;
                    }
                    if (*pos < count && tokens[*pos].type == TOK_RPAREN)
                        (*pos)++;
                }
                return node;
            }
        }

        /* Multi-word function: Dir First$(pattern) / Dir Next$() */
        if (strcasecmp(name, "Dir") == 0 && *pos + 1 < count &&
            tokens[*pos + 1].type == TOK_IDENTIFIER) {
            char combined[64];
            snprintf(combined, sizeof(combined), "Dir %s", tokens[*pos + 1].sval);
            if (is_builtin_function(combined)) {
                amos_node_t *node = alloc_node(NODE_FUNCTION_CALL, tok->line);
                node->token = *tok;
                node->token.sval = strdup(combined);
                (*pos) += 2;  /* skip "Dir" and "First$"/"Next$" */
                /* Parse arguments in parens if present */
                if (*pos < count && tokens[*pos].type == TOK_LPAREN) {
                    (*pos)++;
                    while (*pos < count && tokens[*pos].type != TOK_RPAREN) {
                        amos_node_t *arg = parse_expr(tokens, pos, count, 0);
                        if (arg) add_child(node, arg);
                        if (*pos < count && tokens[*pos].type == TOK_COMMA)
                            (*pos)++;
                    }
                    if (*pos < count && tokens[*pos].type == TOK_RPAREN)
                        (*pos)++;
                }
                return node;
            }
        }

        /* Zero-arg builtins that work without parens (Err, Erl, Free, Pi#) */
        if (is_noarg_builtin(name) &&
            !(*pos + 1 < count && tokens[*pos + 1].type == TOK_LPAREN)) {
            amos_node_t *node = alloc_node(NODE_FUNCTION_CALL, tok->line);
            node->token = *tok;
            node->token.sval = strdup(name);
            (*pos)++;
            return node;
        }

        /* Err$ — special: can be used as Err$ or Err$(n) */
        if (strcasecmp(name, "Err$") == 0) {
            amos_node_t *node = alloc_node(NODE_FUNCTION_CALL, tok->line);
            node->token = *tok;
            node->token.sval = strdup(name);
            (*pos)++;
            if (*pos < count && tokens[*pos].type == TOK_LPAREN) {
                (*pos)++;  /* skip ( */
                while (*pos < count && tokens[*pos].type != TOK_RPAREN) {
                    amos_node_t *arg = parse_expr(tokens, pos, count, 0);
                    if (arg) add_child(node, arg);
                    if (*pos < count && tokens[*pos].type == TOK_COMMA)
                        (*pos)++;
                }
                if (*pos < count && tokens[*pos].type == TOK_RPAREN)
                    (*pos)++;
            }
            return node;
        }

        /* Check for built-in function */
        if (is_builtin_function(name) && *pos + 1 < count &&
            tokens[*pos + 1].type == TOK_LPAREN) {
            amos_node_t *node = alloc_node(NODE_FUNCTION_CALL, tok->line);
            node->token = *tok;
            node->token.sval = strdup(name);
            (*pos)++;  /* skip name */
            (*pos)++;  /* skip ( */

            /* Parse arguments */
            while (*pos < count && tokens[*pos].type != TOK_RPAREN) {
                amos_node_t *arg = parse_expr(tokens, pos, count, 0);
                if (arg) add_child(node, arg);
                if (*pos < count && tokens[*pos].type == TOK_COMMA)
                    (*pos)++;
            }
            if (*pos < count && tokens[*pos].type == TOK_RPAREN)
                (*pos)++;
            return node;
        }

        /* Array access: NAME(index) */
        if (*pos + 1 < count && tokens[*pos + 1].type == TOK_LPAREN &&
            !is_builtin_function(name)) {
            amos_node_t *node = alloc_node(NODE_ARRAY_ACCESS, tok->line);
            node->token = *tok;
            node->token.sval = strdup(name);
            (*pos)++;  /* skip name */
            (*pos)++;  /* skip ( */

            while (*pos < count && tokens[*pos].type != TOK_RPAREN) {
                amos_node_t *idx = parse_expr(tokens, pos, count, 0);
                if (idx) add_child(node, idx);
                if (*pos < count && tokens[*pos].type == TOK_COMMA)
                    (*pos)++;
            }
            if (*pos < count && tokens[*pos].type == TOK_RPAREN)
                (*pos)++;
            return node;
        }

        /* Simple variable */
        amos_node_t *node = alloc_node(NODE_VARIABLE, tok->line);
        node->token = *tok;
        node->token.sval = strdup(name);
        (*pos)++;
        return node;
    }

    /* Timer keyword as expression */
    if (tok->type == TOK_TIMER) {
        amos_node_t *node = alloc_node(NODE_FUNCTION_CALL, tok->line);
        node->token = *tok;
        node->token.sval = strdup("Timer");
        (*pos)++;
        return node;
    }

    /* Mouse Key / Mouse Click — zero-arg functions */
    if (tok->type == TOK_MOUSE_KEY) {
        amos_node_t *node = alloc_node(NODE_FUNCTION_CALL, tok->line);
        node->token = *tok;
        node->token.sval = strdup("Mouse Key");
        (*pos)++;
        return node;
    }
    if (tok->type == TOK_MOUSE_CLICK) {
        amos_node_t *node = alloc_node(NODE_FUNCTION_CALL, tok->line);
        node->token = *tok;
        node->token.sval = strdup("Mouse Click");
        (*pos)++;
        return node;
    }

    /* Rnd(n) as keyword token */
    if (tok->type == TOK_REM && strcasecmp(tok->sval ? tok->sval : "", "Rnd") == 0) {
        /* Shouldn't actually hit this; Rnd comes through as identifier */
    }

    /* Screen keyword in expression context: Screen Width / Screen Height */
    if (tok->type == TOK_SCREEN) {
        if (*pos + 1 < count && tokens[*pos + 1].type == TOK_IDENTIFIER) {
            const char *next_name = tokens[*pos + 1].sval;
            if (strcasecmp(next_name, "Width") == 0 || strcasecmp(next_name, "Height") == 0) {
                char combined[32];
                snprintf(combined, sizeof(combined), "Screen %s", next_name);
                amos_node_t *node = alloc_node(NODE_FUNCTION_CALL, tok->line);
                node->token = *tok;
                node->token.sval = strdup(combined);
                (*pos) += 2;  /* skip Screen and Width/Height */
                return node;
            }
        }
    }

    /* Colour(n) — keyword used as function in expression context */
    if (tok->type == TOK_COLOUR) {
        amos_node_t *node = alloc_node(NODE_FUNCTION_CALL, tok->line);
        node->token = *tok;
        node->token.sval = strdup("Colour");
        (*pos)++;
        if (*pos < count && tokens[*pos].type == TOK_LPAREN) {
            (*pos)++;
            while (*pos < count && tokens[*pos].type != TOK_RPAREN) {
                amos_node_t *arg = parse_expr(tokens, pos, count, 0);
                if (arg) add_child(node, arg);
                if (*pos < count && tokens[*pos].type == TOK_COMMA)
                    (*pos)++;
            }
            if (*pos < count && tokens[*pos].type == TOK_RPAREN)
                (*pos)++;
        }
        return node;
    }

    return NULL;
}

static amos_node_t *parse_expr(amos_token_t *tokens, int *pos, int count, int min_prec)
{
    amos_node_t *left = parse_primary(tokens, pos, count);
    if (!left) return NULL;

    while (*pos < count) {
        amos_token_t *op = &tokens[*pos];
        if (!is_binary_op(op->type)) break;

        int prec = get_precedence(op->type);
        if (prec < min_prec) break;

        (*pos)++;
        amos_node_t *right = parse_expr(tokens, pos, count, prec + 1);
        if (!right) break;

        amos_node_t *binop = alloc_node(NODE_BINARY_OP, op->line);
        binop->token = *op;
        add_child(binop, left);
        add_child(binop, right);
        left = binop;
    }

    return left;
}

/* ── Public API ──────────────────────────────────────────────────── */

amos_node_t *amos_parse_expression(amos_token_t *tokens, int *pos, int count)
{
    return parse_expr(tokens, pos, count, 0);
}
