/*
 * amal.c — AMAL (AMOS Animation Language) interpreter
 *
 * AMAL is a mini scripting language that runs independently from BASIC
 * to animate sprites and bobs. Each channel has its own bytecode, registers,
 * and frame-synced execution.
 *
 * AMAL programs are strings like "M 0,100,50;P" (Move dx=0,dy=100 over 50
 * steps then Pause). The interpreter compiles these to bytecode and executes
 * one frame's worth of instructions per tick.
 */

#include "amos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── Bytecode Opcodes ───────────────────────────────────────────── */

enum amal_opcode {
    AMAL_STOP = 0,
    AMAL_PAUSE,
    AMAL_WAIT,
    AMAL_MOVE,              /* dx(32), dy(32), steps(16) — 16.16 fixed-point deltas */
    AMAL_ANIM,              /* count(8), then pairs of image(16), delay(16) */
    AMAL_JUMP,              /* target_offset(16) */
    AMAL_LET,               /* register(8), then expression bytes, AMAL_EXPR_END */
    AMAL_IF,                /* expression bytes, AMAL_EXPR_END, target_offset(16) */
    AMAL_FOR,               /* register(8), start_expr, AMAL_EXPR_END, end_expr, AMAL_EXPR_END */
    AMAL_NEXT,              /* register(8) */
    AMAL_SET_X,             /* expression bytes, AMAL_EXPR_END */
    AMAL_SET_Y,
    AMAL_SET_A,
    AMAL_DIRECT,            /* target_offset(16) */
    AMAL_AUTOTEST_BEGIN,
    AMAL_AUTOTEST_END,

    /* Expression opcodes (used within expression streams) */
    AMAL_EXPR_NUM,          /* value(32) — signed 32-bit */
    AMAL_EXPR_REG,          /* register(8): 0-9 for R0-R9 */
    AMAL_EXPR_GREG,         /* global register(8): 0-25 for RA-RZ */
    AMAL_EXPR_X,
    AMAL_EXPR_Y,
    AMAL_EXPR_A,
    AMAL_EXPR_XM,           /* X Mouse */
    AMAL_EXPR_YM,           /* Y Mouse */
    AMAL_EXPR_J0,           /* Joystick 0 */
    AMAL_EXPR_J1,           /* Joystick 1 */
    AMAL_EXPR_ON,           /* -1 if Move active, 0 if idle */
    AMAL_EXPR_RAND,         /* Z(n) — random 0..n-1. Followed by expression for n. */
    AMAL_EXPR_ADD,
    AMAL_EXPR_SUB,
    AMAL_EXPR_MUL,
    AMAL_EXPR_DIV,
    AMAL_EXPR_OR,
    AMAL_EXPR_AND,
    AMAL_EXPR_XOR,
    AMAL_EXPR_EQ,
    AMAL_EXPR_NE,
    AMAL_EXPR_LT,
    AMAL_EXPR_GT,
    AMAL_EXPR_LE,
    AMAL_EXPR_GE,
    AMAL_EXPR_NEG,          /* unary negation */
    AMAL_EXPR_END,
};

/* ── Bytecode Buffer ────────────────────────────────────────────── */

#define AMAL_MAX_BYTECODE 4096
#define AMAL_MAX_LABELS    26
#define AMAL_JUMP_LIMIT    10
#define AMAL_EXPR_STACK    64

/* Extended channel data stored alongside the basic amal_channel_t in amos.h */
typedef struct {
    uint8_t  code[AMAL_MAX_BYTECODE];
    int      code_len;
    int      label_offsets[AMAL_MAX_LABELS]; /* A-Z label byte offsets, -1 = unused */
    int      autotest_offset;                /* -1 if no autotest */
    int      autotest_end;                   /* offset after AU() block */

    /* Move state (16.16 fixed-point) */
    int32_t  move_accum_x;     /* accumulated fractional position */
    int32_t  move_accum_y;
    int32_t  move_delta_x;     /* 16.16 delta per step */
    int32_t  move_delta_y;
    int      move_steps;       /* remaining steps */

    /* Anim state */
    int      anim_offset;      /* bytecode offset of current AMAL_ANIM instruction */
    int      anim_index;       /* current frame in the anim sequence */
    int      anim_delay;       /* frames remaining on current anim frame */

    /* For loop state — simple single-level */
    int      for_reg;
    int      for_end;
    int      for_loop_offset;  /* bytecode offset to loop back to */

    /* Wait for synchro */
    bool     waiting_synchro;

    /* Direct redirect */
    int      direct_offset;    /* -1 = no redirect pending */
} amal_ext_t;

static amal_ext_t amal_ext[AMOS_MAX_AMAL_CHANNELS];

/* ── Compiler: AMAL string -> bytecode ──────────────────────────── */

typedef struct {
    const char *src;
    int         pos;
    int         len;
    uint8_t    *code;
    int         code_len;
    int         label_offsets[AMAL_MAX_LABELS];
    /* Forward jump fixups */
    struct { int code_pos; int label; } fixups[256];
    int         fixup_count;
    int         autotest_offset;
    int         autotest_end;
} amal_compiler_t;

static void emit8(amal_compiler_t *c, uint8_t v)
{
    if (c->code_len < AMAL_MAX_BYTECODE)
        c->code[c->code_len++] = v;
}

static void emit16(amal_compiler_t *c, int16_t v)
{
    emit8(c, (uint8_t)(v >> 8));
    emit8(c, (uint8_t)(v & 0xFF));
}

static void emit32(amal_compiler_t *c, int32_t v)
{
    emit8(c, (uint8_t)((v >> 24) & 0xFF));
    emit8(c, (uint8_t)((v >> 16) & 0xFF));
    emit8(c, (uint8_t)((v >> 8) & 0xFF));
    emit8(c, (uint8_t)(v & 0xFF));
}

static int read16(const uint8_t *p)
{
    return (int16_t)((p[0] << 8) | p[1]);
}

static int32_t read32(const uint8_t *p)
{
    return (int32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

static char peek(amal_compiler_t *c)
{
    while (c->pos < c->len && (c->src[c->pos] == ' ' || c->src[c->pos] == '\t'))
        c->pos++;
    return (c->pos < c->len) ? c->src[c->pos] : '\0';
}

static char next_char(amal_compiler_t *c)
{
    char ch = peek(c);
    if (ch) c->pos++;
    return ch;
}

static bool match_char(amal_compiler_t *c, char ch)
{
    if (peek(c) == ch) { c->pos++; return true; }
    return false;
}

static int parse_number(amal_compiler_t *c)
{
    int neg = 0;
    if (peek(c) == '-') { neg = 1; c->pos++; }
    int val = 0;
    while (c->pos < c->len && isdigit((unsigned char)c->src[c->pos])) {
        val = val * 10 + (c->src[c->pos] - '0');
        c->pos++;
    }
    return neg ? -val : val;
}

/* Compile an AMAL expression (infix to postfix bytecode) */
/* Simple recursive descent: expr -> term ((+|-|OR|XOR) term)* */
/*                            term -> factor ((*|/|AND) factor)* */
/*                            factor -> atom | -factor | (expr) */
/*                            atom -> number | =X | =Y | =A | =Rn | =XM | =YM | =J0 | =J1 | =Z(expr) | =On */

static void compile_expr(amal_compiler_t *c);

static void compile_atom(amal_compiler_t *c)
{
    char ch = peek(c);

    if (ch == '(') {
        c->pos++;
        compile_expr(c);
        match_char(c, ')');
        return;
    }

    if (ch == '=' || (ch >= 'A' && ch <= 'Z' && c->pos + 1 < c->len)) {
        /* Check for register reads: =X, =Y, =A, =Rn, =XM, =YM, =J0, =J1, =Z(n), =On */
        if (ch == '=') {
            c->pos++;
            ch = peek(c);
        }
        if (ch == 'X' && c->pos + 1 < c->len && toupper(c->src[c->pos + 1]) == 'M') {
            c->pos += 2;
            emit8(c, AMAL_EXPR_XM);
            return;
        }
        if (ch == 'Y' && c->pos + 1 < c->len && toupper(c->src[c->pos + 1]) == 'M') {
            c->pos += 2;
            emit8(c, AMAL_EXPR_YM);
            return;
        }
        if (ch == 'J') {
            c->pos++;
            char d = next_char(c);
            emit8(c, (d == '1') ? AMAL_EXPR_J1 : AMAL_EXPR_J0);
            return;
        }
        if (ch == 'Z') {
            c->pos++;
            match_char(c, '(');
            compile_expr(c);
            match_char(c, ')');
            emit8(c, AMAL_EXPR_RAND);
            return;
        }
        if (ch == 'O' && c->pos + 1 < c->len && toupper(c->src[c->pos + 1]) == 'N') {
            c->pos += 2;
            emit8(c, AMAL_EXPR_ON);
            return;
        }
        if (ch == 'X') { c->pos++; emit8(c, AMAL_EXPR_X); return; }
        if (ch == 'Y') { c->pos++; emit8(c, AMAL_EXPR_Y); return; }
        if (ch == 'A') { c->pos++; emit8(c, AMAL_EXPR_A); return; }
        if (ch == 'R') {
            c->pos++;
            int reg = parse_number(c);
            if (reg < 0) reg = 0;
            if (reg > 9) reg = 9;
            emit8(c, AMAL_EXPR_REG);
            emit8(c, (uint8_t)reg);
            return;
        }
        /* Global register: single letter A-Z (not preceded by = already handled above for X,Y,A) */
        if (ch >= 'A' && ch <= 'Z') {
            c->pos++;
            emit8(c, AMAL_EXPR_GREG);
            emit8(c, (uint8_t)(ch - 'A'));
            return;
        }
    }

    /* Number */
    if (isdigit((unsigned char)ch) || ch == '-') {
        int val = parse_number(c);
        emit8(c, AMAL_EXPR_NUM);
        emit32(c, val);
        return;
    }

    /* Fallback: 0 */
    emit8(c, AMAL_EXPR_NUM);
    emit32(c, 0);
}

static void compile_factor(amal_compiler_t *c)
{
    if (peek(c) == '-') {
        c->pos++;
        compile_factor(c);
        emit8(c, AMAL_EXPR_NEG);
        return;
    }
    compile_atom(c);
}

static void compile_term(amal_compiler_t *c)
{
    compile_factor(c);
    for (;;) {
        char ch = peek(c);
        if (ch == '*') { c->pos++; compile_factor(c); emit8(c, AMAL_EXPR_MUL); }
        else if (ch == '/') { c->pos++; compile_factor(c); emit8(c, AMAL_EXPR_DIV); }
        else if (ch == '&') { c->pos++; compile_factor(c); emit8(c, AMAL_EXPR_AND); }
        else break;
    }
}

static void compile_comparison(amal_compiler_t *c)
{
    compile_term(c);
    for (;;) {
        char ch = peek(c);
        if (ch == '+') { c->pos++; compile_term(c); emit8(c, AMAL_EXPR_ADD); }
        else if (ch == '-') { c->pos++; compile_term(c); emit8(c, AMAL_EXPR_SUB); }
        else if (ch == '|') { c->pos++; compile_term(c); emit8(c, AMAL_EXPR_OR); }
        else if (ch == '!') { c->pos++; compile_term(c); emit8(c, AMAL_EXPR_XOR); }
        else break;
    }
}

static void compile_expr(amal_compiler_t *c)
{
    compile_comparison(c);
    for (;;) {
        char ch = peek(c);
        if (ch == '=' && c->pos + 1 < c->len &&
            c->src[c->pos + 1] != 'X' && c->src[c->pos + 1] != 'Y' &&
            c->src[c->pos + 1] != 'A' && c->src[c->pos + 1] != 'R' &&
            c->src[c->pos + 1] != 'Z' && c->src[c->pos + 1] != 'O' &&
            c->src[c->pos + 1] != 'J' &&
            !isupper((unsigned char)c->src[c->pos + 1])) {
            c->pos++;
            compile_comparison(c);
            emit8(c, AMAL_EXPR_EQ);
        } else if (ch == '<' && c->pos + 1 < c->len && c->src[c->pos + 1] == '>') {
            c->pos += 2;
            compile_comparison(c);
            emit8(c, AMAL_EXPR_NE);
        } else if (ch == '<') {
            c->pos++;
            if (peek(c) == '=') { c->pos++; compile_comparison(c); emit8(c, AMAL_EXPR_LE); }
            else { compile_comparison(c); emit8(c, AMAL_EXPR_LT); }
        } else if (ch == '>') {
            c->pos++;
            if (peek(c) == '=') { c->pos++; compile_comparison(c); emit8(c, AMAL_EXPR_GE); }
            else { compile_comparison(c); emit8(c, AMAL_EXPR_GT); }
        } else {
            break;
        }
    }
}

static void compile_expr_with_end(amal_compiler_t *c)
{
    compile_expr(c);
    emit8(c, AMAL_EXPR_END);
}

/* Emit a jump with fixup */
static void emit_jump_fixup(amal_compiler_t *c, int label)
{
    if (c->fixup_count < 256) {
        c->fixups[c->fixup_count].code_pos = c->code_len;
        c->fixups[c->fixup_count].label = label;
        c->fixup_count++;
    }
    emit16(c, 0); /* placeholder */
}

static void skip_whitespace_and_sep(amal_compiler_t *c)
{
    while (c->pos < c->len) {
        char ch = c->src[c->pos];
        if (ch == ' ' || ch == '\t' || ch == ';' || ch == '\n' || ch == '\r')
            c->pos++;
        else
            break;
    }
}

static bool compile_statement(amal_compiler_t *c)
{
    skip_whitespace_and_sep(c);
    if (c->pos >= c->len) return false;

    char ch = toupper((unsigned char)c->src[c->pos]);

    /* Check for label: single uppercase letter followed by nothing or a command */
    /* Labels are like "A M 10,10,50" where A is a label at the start */
    /* In AMAL, labels are defined inline with commands. A label is a single letter
       that appears as the first character of a segment (after ; separator) */

    /* Check for explicit label definition: uppercase letter followed by space or command */
    if (isupper((unsigned char)ch) && c->pos + 1 < c->len) {
        char next = c->src[c->pos + 1];
        /* A label is a single uppercase letter followed by space, then another command */
        /* But we also need to distinguish from commands like M, P, W, etc. */
        /* AMAL labels are defined by their position — a letter at the start of
           a statement that is NOT a recognized command letter is a label.
           Recognized: A(nim), D(irect), F(or), I(f), J(ump), L(et), M(ove),
                       N(ext), P(ause), W(ait), X=, Y=, AU( */

        /* Actually, labels ARE single letters A-Z that appear as prefixes.
           For example "A M 10,10,50" means label A, then Move.
           But "A 1,5;2,5" is the Anim command.
           Resolution: a label is a single uppercase letter followed by a space
           and then another uppercase letter (the actual command). */
        bool is_label = false;
        if (next == ' ' || next == '\t') {
            /* peek ahead past whitespace to see if there's a command letter */
            int save = c->pos;
            c->pos += 2;
            char after = peek(c);
            c->pos = save;
            /* If the next non-space char is an AMAL command letter, this is a label */
            if (isupper((unsigned char)after) && ch != 'A' && ch != 'M' && ch != 'P'
                && ch != 'W' && ch != 'J' && ch != 'L' && ch != 'I' && ch != 'F'
                && ch != 'N' && ch != 'D' && ch != 'X' && ch != 'Y') {
                is_label = true;
            }
            /* Also handle the case where a known command letter IS used as label
               if followed by another command letter. E.g., "A M 10,10,50"
               where A is label and M is Move. But "A 1,5;2,5" is Anim.
               Disambiguate: if the letter is followed by space + another letter, it's a label. */
            if (!is_label && isupper((unsigned char)after) && after != ch) {
                /* Check if after is a valid command start */
                if (after == 'M' || after == 'P' || after == 'W' || after == 'J' ||
                    after == 'L' || after == 'I' || after == 'F' || after == 'N' ||
                    after == 'D' || after == 'A' || after == 'X' || after == 'Y') {
                    is_label = true;
                }
            }
        }

        if (is_label) {
            int label_idx = ch - 'A';
            c->label_offsets[label_idx] = c->code_len;
            c->pos++; /* consume the label letter */
            skip_whitespace_and_sep(c);
            if (c->pos >= c->len) return false;
            ch = toupper((unsigned char)c->src[c->pos]);
        }
    }

    switch (ch) {
    case 'M': {
        /* Move dx,dy,steps */
        c->pos++;
        skip_whitespace_and_sep(c);
        int dx = parse_number(c);
        match_char(c, ',');
        int dy = parse_number(c);
        match_char(c, ',');
        int steps = parse_number(c);
        if (steps <= 0) steps = 1;
        emit8(c, AMAL_MOVE);
        /* Store dx and dy as raw integers; fixed-point conversion at runtime */
        emit32(c, dx);
        emit32(c, dy);
        emit16(c, (int16_t)steps);
        break;
    }
    case 'A': {
        /* Could be Anim or AU( autotest) */
        if (c->pos + 1 < c->len && toupper(c->src[c->pos + 1]) == 'U') {
            /* AU( ... ) autotest block */
            c->pos += 2;
            match_char(c, '(');
            skip_whitespace_and_sep(c);
            emit8(c, AMAL_AUTOTEST_BEGIN);
            c->autotest_offset = c->code_len;
            /* Compile statements until ) */
            while (c->pos < c->len && peek(c) != ')') {
                compile_statement(c);
                skip_whitespace_and_sep(c);
            }
            match_char(c, ')');
            emit8(c, AMAL_AUTOTEST_END);
            c->autotest_end = c->code_len;
            break;
        }
        /* Anim: A image,delay;image,delay;... */
        c->pos++;
        skip_whitespace_and_sep(c);
        /* Collect animation frames */
        int frames[128]; /* pairs: image, delay */
        int frame_count = 0;
        while (c->pos < c->len && frame_count < 126) {
            if (!isdigit((unsigned char)peek(c)) && peek(c) != '-') break;
            frames[frame_count++] = parse_number(c);
            match_char(c, ',');
            frames[frame_count++] = parse_number(c);
            /* Check for more frames separated by ; */
            skip_whitespace_and_sep(c);
            /* If next char is a digit, continue; otherwise stop anim */
            char nx = peek(c);
            if (!isdigit((unsigned char)nx) && nx != '-') break;
        }
        emit8(c, AMAL_ANIM);
        emit8(c, (uint8_t)(frame_count / 2));
        for (int i = 0; i < frame_count; i += 2) {
            emit16(c, (int16_t)frames[i]);
            emit16(c, (int16_t)frames[i + 1]);
        }
        break;
    }
    case 'P':
        /* Pause */
        c->pos++;
        emit8(c, AMAL_PAUSE);
        break;
    case 'W':
        /* Wait (for Synchro) */
        c->pos++;
        emit8(c, AMAL_WAIT);
        break;
    case 'J': {
        /* Jump label */
        c->pos++;
        skip_whitespace_and_sep(c);
        char label = toupper((unsigned char)next_char(c));
        emit8(c, AMAL_JUMP);
        if (label >= 'A' && label <= 'Z') {
            emit_jump_fixup(c, label - 'A');
        } else {
            emit16(c, 0);
        }
        break;
    }
    case 'L': {
        /* Let Rn=expr */
        c->pos++;
        skip_whitespace_and_sep(c);
        char r = toupper((unsigned char)next_char(c));
        if (r == 'R') {
            int reg = parse_number(c);
            match_char(c, '=');
            emit8(c, AMAL_LET);
            emit8(c, (uint8_t)reg);
            compile_expr_with_end(c);
        }
        break;
    }
    case 'I': {
        /* If expr J label or If expr D label */
        c->pos++;
        skip_whitespace_and_sep(c);
        emit8(c, AMAL_IF);
        compile_expr_with_end(c);
        skip_whitespace_and_sep(c);
        char action = toupper((unsigned char)next_char(c));
        skip_whitespace_and_sep(c);
        char label = toupper((unsigned char)next_char(c));
        if (action == 'D') {
            /* If ... Direct — change where to redirect */
            /* We encode this as IF + DIRECT in the false-skip path.
               Actually, simpler: encode the action type inline. */
            /* Encode as: IF expr DIRECT target */
            /* Replace the IF opcode we just emitted with a compound approach.
               Actually, keep it simple: IF evaluates expr; if nonzero, execute next instruction.
               Next instruction will be JUMP or DIRECT. If zero, skip next instruction. */
            /* The IF opcode followed by jump target. If condition is false, skip the jump. */
            emit8(c, 1); /* 1 = DIRECT action */
            if (label >= 'A' && label <= 'Z')
                emit_jump_fixup(c, label - 'A');
            else
                emit16(c, 0);
        } else {
            /* J (Jump) */
            emit8(c, 0); /* 0 = JUMP action */
            if (label >= 'A' && label <= 'Z')
                emit_jump_fixup(c, label - 'A');
            else
                emit16(c, 0);
        }
        break;
    }
    case 'F': {
        /* For Rn=start To end */
        c->pos++;
        skip_whitespace_and_sep(c);
        char r = toupper((unsigned char)next_char(c));
        (void)r; /* should be 'R' */
        int reg = parse_number(c);
        match_char(c, '=');
        emit8(c, AMAL_FOR);
        emit8(c, (uint8_t)reg);
        compile_expr_with_end(c);
        /* Skip "To" keyword */
        skip_whitespace_and_sep(c);
        if (c->pos + 1 < c->len && toupper(c->src[c->pos]) == 'T' &&
            toupper(c->src[c->pos + 1]) == 'O') {
            c->pos += 2;
        }
        skip_whitespace_and_sep(c);
        compile_expr_with_end(c);
        break;
    }
    case 'N': {
        /* Next Rn */
        c->pos++;
        skip_whitespace_and_sep(c);
        char r = toupper((unsigned char)next_char(c));
        (void)r;
        int reg = parse_number(c);
        emit8(c, AMAL_NEXT);
        emit8(c, (uint8_t)reg);
        break;
    }
    case 'X': {
        /* X=expr */
        c->pos++;
        match_char(c, '=');
        emit8(c, AMAL_SET_X);
        compile_expr_with_end(c);
        break;
    }
    case 'Y': {
        /* Y=expr */
        c->pos++;
        match_char(c, '=');
        emit8(c, AMAL_SET_Y);
        compile_expr_with_end(c);
        break;
    }
    case 'D': {
        /* Direct label */
        c->pos++;
        skip_whitespace_and_sep(c);
        char label = toupper((unsigned char)next_char(c));
        emit8(c, AMAL_DIRECT);
        if (label >= 'A' && label <= 'Z')
            emit_jump_fixup(c, label - 'A');
        else
            emit16(c, 0);
        break;
    }
    default:
        /* Unknown — skip character */
        c->pos++;
        break;
    }

    return true;
}

int amos_amal_compile(amos_state_t *state, int channel, const char *program)
{
    if (channel < 0 || channel >= AMOS_MAX_AMAL_CHANNELS)
        return -1;

    amal_compiler_t comp;
    memset(&comp, 0, sizeof(comp));
    comp.src = program;
    comp.len = (int)strlen(program);
    comp.pos = 0;
    comp.code = amal_ext[channel].code;
    comp.code_len = 0;
    comp.fixup_count = 0;
    comp.autotest_offset = -1;
    comp.autotest_end = -1;
    for (int i = 0; i < AMAL_MAX_LABELS; i++)
        comp.label_offsets[i] = -1;

    /* First character of program can be a label */
    while (comp.pos < comp.len) {
        compile_statement(&comp);
    }

    /* Emit trailing stop */
    emit8(&comp, AMAL_STOP);

    /* Resolve fixups */
    for (int i = 0; i < comp.fixup_count; i++) {
        int label = comp.fixups[i].label;
        int offset = comp.label_offsets[label];
        int pos = comp.fixups[i].code_pos;
        if (offset >= 0) {
            comp.code[pos] = (uint8_t)((offset >> 8) & 0xFF);
            comp.code[pos + 1] = (uint8_t)(offset & 0xFF);
        }
    }

    /* Copy results */
    amal_ext_t *ext = &amal_ext[channel];
    ext->code_len = comp.code_len;
    memcpy(ext->label_offsets, comp.label_offsets, sizeof(comp.label_offsets));
    ext->autotest_offset = comp.autotest_offset;
    ext->autotest_end = comp.autotest_end;
    ext->move_steps = 0;
    ext->move_accum_x = 0;
    ext->move_accum_y = 0;
    ext->move_delta_x = 0;
    ext->move_delta_y = 0;
    ext->anim_offset = -1;
    ext->anim_index = 0;
    ext->anim_delay = 0;
    ext->for_reg = -1;
    ext->waiting_synchro = false;
    ext->direct_offset = -1;

    /* Set up the channel */
    amal_channel_t *ch = &state->amal[channel];
    if (ch->program) free(ch->program);
    ch->program = strdup(program);
    ch->pc = 0;
    ch->target_type = 0; /* sprite by default */
    ch->target_id = channel;
    ch->wait_counter = 0;
    memset(ch->registers, 0, sizeof(ch->registers));

    return 0;
}

/* ── Expression Evaluator ───────────────────────────────────────── */

static int eval_amal_expr(amos_state_t *state, int channel, const uint8_t *code, int *pc)
{
    int stack[AMAL_EXPR_STACK];
    int sp = 0;
    amal_channel_t *ch = &state->amal[channel];
    amal_ext_t *ext = &amal_ext[channel];

#define PUSH(v) do { if (sp < AMAL_EXPR_STACK) stack[sp++] = (v); } while(0)
#define POP()   (sp > 0 ? stack[--sp] : 0)

    for (;;) {
        uint8_t op = code[(*pc)++];
        switch (op) {
        case AMAL_EXPR_NUM:
            PUSH(read32(&code[*pc]));
            *pc += 4;
            break;
        case AMAL_EXPR_REG:
            PUSH(ch->registers[code[(*pc)++]]);
            break;
        case AMAL_EXPR_GREG:
            PUSH(state->amal_global_regs[code[(*pc)++]]);
            break;
        case AMAL_EXPR_X: {
            int tid = ch->target_id;
            if (ch->target_type == 0 && tid >= 0 && tid < AMOS_MAX_SPRITES)
                PUSH(state->sprites[tid].x);
            else if (ch->target_type == 1 && tid >= 0 && tid < AMOS_MAX_BOBS)
                PUSH(state->bobs[tid].x);
            else
                PUSH(0);
            break;
        }
        case AMAL_EXPR_Y: {
            int tid = ch->target_id;
            if (ch->target_type == 0 && tid >= 0 && tid < AMOS_MAX_SPRITES)
                PUSH(state->sprites[tid].y);
            else if (ch->target_type == 1 && tid >= 0 && tid < AMOS_MAX_BOBS)
                PUSH(state->bobs[tid].y);
            else
                PUSH(0);
            break;
        }
        case AMAL_EXPR_A: {
            int tid = ch->target_id;
            if (ch->target_type == 0 && tid >= 0 && tid < AMOS_MAX_SPRITES)
                PUSH(state->sprites[tid].image);
            else if (ch->target_type == 1 && tid >= 0 && tid < AMOS_MAX_BOBS)
                PUSH(state->bobs[tid].image);
            else
                PUSH(0);
            break;
        }
        case AMAL_EXPR_XM:
            PUSH(0); /* TODO: mouse X */
            break;
        case AMAL_EXPR_YM:
            PUSH(0); /* TODO: mouse Y */
            break;
        case AMAL_EXPR_J0:
        case AMAL_EXPR_J1:
            PUSH(0); /* TODO: joystick */
            break;
        case AMAL_EXPR_ON:
            PUSH(ext->move_steps > 0 ? -1 : 0);
            break;
        case AMAL_EXPR_RAND: {
            int n = POP();
            PUSH(n > 0 ? (rand() % n) : 0);
            break;
        }
        case AMAL_EXPR_ADD: { int b = POP(); int a = POP(); PUSH(a + b); break; }
        case AMAL_EXPR_SUB: { int b = POP(); int a = POP(); PUSH(a - b); break; }
        case AMAL_EXPR_MUL: { int b = POP(); int a = POP(); PUSH(a * b); break; }
        case AMAL_EXPR_DIV: { int b = POP(); int a = POP(); PUSH(b ? a / b : 0); break; }
        case AMAL_EXPR_OR:  { int b = POP(); int a = POP(); PUSH(a | b); break; }
        case AMAL_EXPR_AND: { int b = POP(); int a = POP(); PUSH(a & b); break; }
        case AMAL_EXPR_XOR: { int b = POP(); int a = POP(); PUSH(a ^ b); break; }
        case AMAL_EXPR_EQ:  { int b = POP(); int a = POP(); PUSH(a == b ? -1 : 0); break; }
        case AMAL_EXPR_NE:  { int b = POP(); int a = POP(); PUSH(a != b ? -1 : 0); break; }
        case AMAL_EXPR_LT:  { int b = POP(); int a = POP(); PUSH(a < b ? -1 : 0); break; }
        case AMAL_EXPR_GT:  { int b = POP(); int a = POP(); PUSH(a > b ? -1 : 0); break; }
        case AMAL_EXPR_LE:  { int b = POP(); int a = POP(); PUSH(a <= b ? -1 : 0); break; }
        case AMAL_EXPR_GE:  { int b = POP(); int a = POP(); PUSH(a >= b ? -1 : 0); break; }
        case AMAL_EXPR_NEG: { int a = POP(); PUSH(-a); break; }
        case AMAL_EXPR_END:
            return POP();
        default:
            return POP();
        }
    }
#undef PUSH
#undef POP
}

/* ── Write to target sprite/bob ─────────────────────────────────── */

static void amal_set_x(amos_state_t *state, amal_channel_t *ch, int val)
{
    if (ch->target_type == 0 && ch->target_id >= 0 && ch->target_id < AMOS_MAX_SPRITES) {
        state->sprites[ch->target_id].x = val;
    } else if (ch->target_type == 1 && ch->target_id >= 0 && ch->target_id < AMOS_MAX_BOBS) {
        state->bobs[ch->target_id].x = val;
    }
}

static void amal_set_y(amos_state_t *state, amal_channel_t *ch, int val)
{
    if (ch->target_type == 0 && ch->target_id >= 0 && ch->target_id < AMOS_MAX_SPRITES) {
        state->sprites[ch->target_id].y = val;
    } else if (ch->target_type == 1 && ch->target_id >= 0 && ch->target_id < AMOS_MAX_BOBS) {
        state->bobs[ch->target_id].y = val;
    }
}

static void amal_set_image(amos_state_t *state, amal_channel_t *ch, int val)
{
    if (ch->target_type == 0 && ch->target_id >= 0 && ch->target_id < AMOS_MAX_SPRITES) {
        state->sprites[ch->target_id].image = val;
    } else if (ch->target_type == 1 && ch->target_id >= 0 && ch->target_id < AMOS_MAX_BOBS) {
        state->bobs[ch->target_id].image = val;
    }
}

static int amal_get_x(amos_state_t *state, amal_channel_t *ch)
{
    if (ch->target_type == 0 && ch->target_id >= 0 && ch->target_id < AMOS_MAX_SPRITES)
        return state->sprites[ch->target_id].x;
    if (ch->target_type == 1 && ch->target_id >= 0 && ch->target_id < AMOS_MAX_BOBS)
        return state->bobs[ch->target_id].x;
    return 0;
}

static int amal_get_y(amos_state_t *state, amal_channel_t *ch)
{
    if (ch->target_type == 0 && ch->target_id >= 0 && ch->target_id < AMOS_MAX_SPRITES)
        return state->sprites[ch->target_id].y;
    if (ch->target_type == 1 && ch->target_id >= 0 && ch->target_id < AMOS_MAX_BOBS)
        return state->bobs[ch->target_id].y;
    return 0;
}

/* ── Execute one frame of a single AMAL channel ────────────────── */

static void amal_exec_channel(amos_state_t *state, int channel)
{
    amal_channel_t *ch = &state->amal[channel];
    amal_ext_t *ext = &amal_ext[channel];

    if (!ch->active || ch->frozen)
        return;

    /* Handle ongoing Move */
    if (ext->move_steps > 0) {
        ext->move_accum_x += ext->move_delta_x;
        ext->move_accum_y += ext->move_delta_y;

        /* Apply integer part of accumulated position */
        int ix = ext->move_accum_x >> 16;
        int iy = ext->move_accum_y >> 16;
        ext->move_accum_x -= ix << 16;
        ext->move_accum_y -= iy << 16;

        amal_set_x(state, ch, amal_get_x(state, ch) + ix);
        amal_set_y(state, ch, amal_get_y(state, ch) + iy);

        ext->move_steps--;
        if (ext->move_steps > 0)
            return; /* Still moving, don't execute more bytecode */
    }

    /* Handle ongoing Anim */
    if (ext->anim_offset >= 0 && ext->anim_delay > 0) {
        ext->anim_delay--;
        if (ext->anim_delay > 0)
            return; /* Still on current anim frame */

        /* Advance to next anim frame */
        ext->anim_index++;
        int count = ext->code[ext->anim_offset + 1]; /* number of frames */
        if (ext->anim_index >= count)
            ext->anim_index = 0; /* loop */

        /* Read frame data */
        int base = ext->anim_offset + 2 + ext->anim_index * 4;
        int image = read16(&ext->code[base]);
        int delay = read16(&ext->code[base + 2]);
        amal_set_image(state, ch, image);
        ext->anim_delay = delay;
        return; /* Don't advance PC yet */
    }

    /* Handle waiting for synchro */
    if (ext->waiting_synchro)
        return; /* Will be cleared by Synchro command */

    /* Check for Direct redirect */
    if (ext->direct_offset >= 0) {
        ch->pc = ext->direct_offset;
        ext->direct_offset = -1;
    }

    /* Execute bytecode instructions */
    int jump_count = AMAL_JUMP_LIMIT;
    const uint8_t *code = ext->code;

    while (ch->pc < ext->code_len && jump_count > 0) {
        uint8_t opcode = code[ch->pc++];

        switch (opcode) {
        case AMAL_STOP:
            ch->pc--; /* stay on STOP */
            return;

        case AMAL_PAUSE:
            return; /* one-frame pause */

        case AMAL_WAIT:
            ext->waiting_synchro = true;
            return;

        case AMAL_MOVE: {
            int dx = read32(&code[ch->pc]); ch->pc += 4;
            int dy = read32(&code[ch->pc]); ch->pc += 4;
            int steps = read16(&code[ch->pc]); ch->pc += 2;
            if (steps <= 0) steps = 1;

            /* Convert to 16.16 fixed-point deltas */
            ext->move_delta_x = (dx << 16) / steps;
            ext->move_delta_y = (dy << 16) / steps;
            ext->move_accum_x = 0;
            ext->move_accum_y = 0;
            ext->move_steps = steps;

            /* Execute first step immediately */
            ext->move_accum_x += ext->move_delta_x;
            ext->move_accum_y += ext->move_delta_y;
            int ix = ext->move_accum_x >> 16;
            int iy = ext->move_accum_y >> 16;
            ext->move_accum_x -= ix << 16;
            ext->move_accum_y -= iy << 16;
            amal_set_x(state, ch, amal_get_x(state, ch) + ix);
            amal_set_y(state, ch, amal_get_y(state, ch) + iy);
            ext->move_steps--;
            return;
        }

        case AMAL_ANIM: {
            int count = code[ch->pc++];
            ext->anim_offset = ch->pc - 2; /* point back to AMAL_ANIM opcode */
            ext->anim_index = 0;
            if (count > 0) {
                int image = read16(&code[ch->pc]);
                int delay = read16(&code[ch->pc + 2]);
                amal_set_image(state, ch, image);
                ext->anim_delay = delay;
            }
            /* Skip past all anim data */
            ch->pc += count * 4;
            return; /* Wait for animation */
        }

        case AMAL_JUMP: {
            int target = read16(&code[ch->pc]); ch->pc += 2;
            ch->pc = target;
            jump_count--;
            break; /* continue execution from new position */
        }

        case AMAL_LET: {
            int reg = code[ch->pc++];
            int val = eval_amal_expr(state, channel, code, &ch->pc);
            if (reg >= 0 && reg <= 9)
                ch->registers[reg] = val;
            break;
        }

        case AMAL_IF: {
            int cond = eval_amal_expr(state, channel, code, &ch->pc);
            int action = code[ch->pc++]; /* 0=jump, 1=direct */
            int target = read16(&code[ch->pc]); ch->pc += 2;
            if (cond) {
                if (action == 1) {
                    /* Direct: redirect main program */
                    ext->direct_offset = target;
                } else {
                    /* Jump */
                    ch->pc = target;
                    jump_count--;
                }
            }
            break;
        }

        case AMAL_FOR: {
            int reg = code[ch->pc++];
            int start = eval_amal_expr(state, channel, code, &ch->pc);
            int end = eval_amal_expr(state, channel, code, &ch->pc);
            if (reg >= 0 && reg <= 9) {
                ch->registers[reg] = start;
                ext->for_reg = reg;
                ext->for_end = end;
                ext->for_loop_offset = ch->pc;
            }
            break;
        }

        case AMAL_NEXT: {
            int reg = code[ch->pc++];
            if (reg >= 0 && reg <= 9 && ext->for_reg == reg) {
                ch->registers[reg]++;
                if (ch->registers[reg] <= ext->for_end) {
                    ch->pc = ext->for_loop_offset;
                    jump_count--;
                } else {
                    ext->for_reg = -1;
                }
            }
            break;
        }

        case AMAL_SET_X: {
            int val = eval_amal_expr(state, channel, code, &ch->pc);
            amal_set_x(state, ch, val);
            break;
        }

        case AMAL_SET_Y: {
            int val = eval_amal_expr(state, channel, code, &ch->pc);
            amal_set_y(state, ch, val);
            break;
        }

        case AMAL_SET_A: {
            int val = eval_amal_expr(state, channel, code, &ch->pc);
            amal_set_image(state, ch, val);
            break;
        }

        case AMAL_DIRECT: {
            int target = read16(&code[ch->pc]); ch->pc += 2;
            ext->direct_offset = target;
            break;
        }

        case AMAL_AUTOTEST_BEGIN:
            /* Skip past autotest block during normal execution */
            /* Find matching AUTOTEST_END */
            while (ch->pc < ext->code_len && code[ch->pc] != AMAL_AUTOTEST_END)
                ch->pc++;
            if (ch->pc < ext->code_len) ch->pc++; /* skip the END marker */
            break;

        case AMAL_AUTOTEST_END:
            /* Should not reach here during normal execution */
            break;

        default:
            /* Unknown opcode, stop */
            return;
        }
    }
}

/* ── Run autotest for a channel ─────────────────────────────────── */

static void amal_exec_autotest(amos_state_t *state, int channel)
{
    amal_ext_t *ext = &amal_ext[channel];
    amal_channel_t *ch = &state->amal[channel];

    if (!ch->active || ch->frozen || ext->autotest_offset < 0)
        return;

    /* Save PC and run autotest block */
    int saved_pc = ch->pc;
    ch->pc = ext->autotest_offset;

    int jump_count = 20; /* autotest gets 20 jumps */
    const uint8_t *code = ext->code;

    while (ch->pc < ext->code_len && ch->pc < ext->autotest_end && jump_count > 0) {
        uint8_t opcode = code[ch->pc++];

        switch (opcode) {
        case AMAL_AUTOTEST_END:
            ch->pc = saved_pc;
            return;

        case AMAL_DIRECT: {
            int target = read16(&code[ch->pc]); ch->pc += 2;
            ext->direct_offset = target;
            break;
        }

        case AMAL_IF: {
            int cond = eval_amal_expr(state, channel, code, &ch->pc);
            int action = code[ch->pc++];
            int target = read16(&code[ch->pc]); ch->pc += 2;
            if (cond) {
                if (action == 1) {
                    ext->direct_offset = target;
                } else {
                    ch->pc = target;
                    jump_count--;
                }
            }
            break;
        }

        case AMAL_LET: {
            int reg = code[ch->pc++];
            int val = eval_amal_expr(state, channel, code, &ch->pc);
            if (reg >= 0 && reg <= 9)
                ch->registers[reg] = val;
            break;
        }

        case AMAL_SET_X: {
            int val = eval_amal_expr(state, channel, code, &ch->pc);
            amal_set_x(state, ch, val);
            break;
        }

        case AMAL_SET_Y: {
            int val = eval_amal_expr(state, channel, code, &ch->pc);
            amal_set_y(state, ch, val);
            break;
        }

        case AMAL_SET_A: {
            int val = eval_amal_expr(state, channel, code, &ch->pc);
            amal_set_image(state, ch, val);
            break;
        }

        default:
            /* Unknown in autotest, restore PC and return */
            ch->pc = saved_pc;
            return;
        }
    }

    ch->pc = saved_pc;
}

/* ── Public API ─────────────────────────────────────────────────── */

void amos_amal_tick(amos_state_t *state)
{
    for (int i = 0; i < AMOS_MAX_AMAL_CHANNELS; i++) {
        if (!state->amal[i].active)
            continue;

        /* Run autotest first */
        amal_exec_autotest(state, i);

        /* Then main program */
        amal_exec_channel(state, i);
    }
}

void amos_amal_on(amos_state_t *state, int channel)
{
    if (channel < 0 || channel >= AMOS_MAX_AMAL_CHANNELS) return;
    state->amal[channel].active = true;
    state->amal[channel].frozen = false;
    state->amal[channel].pc = 0;
    amal_ext[channel].waiting_synchro = false;
    amal_ext[channel].direct_offset = -1;
}

void amos_amal_off(amos_state_t *state, int channel)
{
    if (channel < 0 || channel >= AMOS_MAX_AMAL_CHANNELS) return;
    state->amal[channel].active = false;
    amal_ext[channel].move_steps = 0;
    amal_ext[channel].anim_offset = -1;
    amal_ext[channel].waiting_synchro = false;
}

void amos_amal_freeze(amos_state_t *state, int channel)
{
    if (channel < 0 || channel >= AMOS_MAX_AMAL_CHANNELS) return;
    state->amal[channel].frozen = true;
}

void amos_amal_synchro(amos_state_t *state)
{
    /* Release all channels waiting for synchro */
    for (int i = 0; i < AMOS_MAX_AMAL_CHANNELS; i++) {
        if (state->amal[i].active && amal_ext[i].waiting_synchro) {
            amal_ext[i].waiting_synchro = false;
        }
    }
    /* Then tick all channels */
    amos_amal_tick(state);
}

int amos_amreg(amos_state_t *state, int reg)
{
    if (reg >= 0 && reg < 26)
        return state->amal_global_regs[reg];
    return 0;
}

void amos_amreg_set(amos_state_t *state, int reg, int value)
{
    if (reg >= 0 && reg < 26)
        state->amal_global_regs[reg] = value;
}
