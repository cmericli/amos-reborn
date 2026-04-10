/*
 * tokenizer.c — AMOS BASIC source text to token stream
 *
 * Converts plain text AMOS source into a flat token array.
 * Handles AMOS keyword recognition (multi-word keywords like "Screen Open"),
 * numeric literals, string literals, identifiers, and operators.
 */

#include "amos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── Keyword Table ───────────────────────────────────────────────── */

typedef struct {
    const char *text;
    amos_token_type_t type;
} keyword_entry_t;

/* Multi-word keywords must come before single-word ones with the same prefix */
static const keyword_entry_t keywords[] = {
    /* Multi-word keywords (order matters — longest match first) */
    {"On Error Goto",   TOK_ON_ERROR_GOTO},
    {"On Error Proc",   TOK_ON_ERROR_PROC},
    {"Resume Next",     TOK_RESUME_NEXT},
    {"Resume Label",    TOK_RESUME_LABEL},
    {"Screen Open",     TOK_SCREEN_OPEN},
    {"Screen Close",    TOK_SCREEN_CLOSE},
    {"Screen Display",  TOK_SCREEN_DISPLAY},
    {"Screen Offset",   TOK_SCREEN_OFFSET},
    {"Screen Clone",    TOK_SCREEN_CLONE},
    {"Screen Copy",     TOK_SCREEN_COPY},
    {"Screen Swap",     TOK_SCREEN_SWAP},
    {"Screen Mode",     TOK_SCREEN_MODE},
    {"Screen To Front", TOK_SCREEN_TO_FRONT},
    {"Screen To Back",  TOK_SCREEN_TO_BACK},
    {"Screen Hide",     TOK_SCREEN_HIDE},
    {"Screen Show",     TOK_SCREEN_SHOW},
    {"Get Block",       TOK_GET_BLOCK},
    {"Put Block",       TOK_PUT_BLOCK},
    {"Del Block",       TOK_DEL_BLOCK},
    {"Def Scroll",      TOK_DEF_SCROLL},
    {"Set Sprite Buffer", TOK_SET_SPRITE_BUFFER},
    {"Paste Bob",       TOK_PASTE_BOB},
    {"Flash Off",       TOK_FLASH_OFF},
    {"Curs Off",        TOK_CURS_OFF},
    {"Curs On",         TOK_CURS_ON},
    {"Mouse Click",     TOK_MOUSE_CLICK},
    {"Mouse Key",       TOK_MOUSE_KEY},
    {"Set Paint",       TOK_SET_PAINT},
    {"Set Bob",         TOK_SET_BOB},
    {"Set Font",        TOK_SET_FONT},
    {"Sprite Off",      TOK_SPRITE_OFF},
    {"Sprite Col",      TOK_SPRITE_COL},
    {"Bob Off",         TOK_BOB_OFF},
    {"Bob Col",         TOK_BOB_COL},
    {"Get Sprite Palette", TOK_GET_SPRITE_PALETTE},
    {"Get Sprite",      TOK_GET_SPRITE},
    {"Get Bob",         TOK_GET_BOB},
    {"Move X",          TOK_MOVE_X},
    {"Move Y",          TOK_MOVE_Y},
    {"Sam Play",        TOK_SAM_PLAY},
    {"Sam Raw",         TOK_SAM_RAW},
    {"Sam Loop On",     TOK_SAM_LOOP_ON},
    {"Sam Loop Off",    TOK_SAM_LOOP_OFF},
    {"Track Play",      TOK_TRACK_PLAY},
    {"Track Stop",      TOK_TRACK_STOP},
    {"Track Loop On",   TOK_TRACK_LOOP_ON},
    {"Amal On",         TOK_AMAL_ON},
    {"Amal Off",        TOK_AMAL_OFF},
    {"Amal Freeze",     TOK_AMAL_FREEZE},
    {"End If",          TOK_END_IF},
    {"End Proc",        TOK_END_PROC},
    {"Exit If",         TOK_EXIT_IF},
    {"Wait Vbl",        TOK_WAIT_VBL},
    {"Wait Key",        TOK_WAIT_KEY},
    {"Double Buffer",   TOK_DOUBLE_BUFFER},
    {"Dual Playfield",  TOK_DUAL_PLAYFIELD},
    {"Gr Writing",      TOK_GR_WRITING},
    {"Set Line",        TOK_SET_LINE},
    {"Set Pattern",     TOK_SET_PATTERN},
    {"Reserve As Work", TOK_RESERVE_AS_WORK},
    {"Reserve As Data", TOK_RESERVE_AS_DATA},
    {"End Select",      TOK_END_SELECT},
    {"Open In",         TOK_OPEN_IN},
    {"Open Out",        TOK_OPEN_OUT},
    {"Line Input",      TOK_LINE_INPUT_FILE},

    /* Single-word keywords */
    {"Screen",      TOK_SCREEN},
    {"If",          TOK_IF},
    {"Then",        TOK_THEN},
    {"Else",        TOK_ELSE},
    {"For",         TOK_FOR},
    {"To",          TOK_TO},
    {"Step",        TOK_STEP},
    {"Next",        TOK_NEXT},
    {"While",       TOK_WHILE},
    {"Wend",        TOK_WEND},
    {"Repeat",      TOK_REPEAT},
    {"Until",       TOK_UNTIL},
    {"Do",          TOK_DO},
    {"Loop",        TOK_LOOP},
    {"Goto",        TOK_GOTO},
    {"Gosub",       TOK_GOSUB},
    {"Return",      TOK_RETURN},
    {"On",          TOK_ON},
    {"Exit",        TOK_EXIT},
    {"Pop",         TOK_POP},
    {"End",         TOK_END},
    {"Stop",        TOK_STOP},
    {"Procedure",   TOK_PROCEDURE},
    {"Proc",        TOK_PROC},
    {"Shared",      TOK_SHARED},
    {"Global",      TOK_GLOBAL},
    {"Local",       TOK_LOCAL},
    {"Param",       TOK_PARAM},
    {"Fn",          TOK_FN},
    {"Let",         TOK_LET},
    {"Dim",         TOK_DIM},
    {"Data",        TOK_DATA},
    {"Read",        TOK_READ},
    {"Restore",     TOK_RESTORE},
    {"Swap",        TOK_SWAP},
    {"Print",       TOK_PRINT},
    {"Input",       TOK_INPUT},
    {"Locate",      TOK_LOCATE},
    {"Cls",         TOK_CLS},
    {"Home",        TOK_HOME},
    {"Ink",         TOK_INK},
    {"Colour",      TOK_COLOUR},
    {"Palette",     TOK_PALETTE},
    {"Plot",        TOK_PLOT},
    {"Draw",        TOK_DRAW},
    {"Box",         TOK_BOX},
    {"Bar",         TOK_BAR},
    {"Circle",      TOK_CIRCLE},
    {"Ellipse",     TOK_ELLIPSE},
    {"Paint",       TOK_PAINT},
    {"Polygon",     TOK_POLYGON},
    {"Text",        TOK_TEXT},
    {"Sprite",      TOK_SPRITE},
    {"Bob",         TOK_BOB},
    {"Anim",        TOK_ANIM},
    {"Boom",        TOK_BOOM},
    {"Shoot",       TOK_SHOOT},
    {"Bell",        TOK_BELL},
    {"Volume",      TOK_VOLUME},
    {"Music",       TOK_MUSIC},
    {"Amal",        TOK_AMAL},
    {"Synchro",     TOK_SYNCHRO},
    {"Reserve",     TOK_RESERVE},
    {"Erase",       TOK_ERASE},
    {"Load",        TOK_LOAD},
    {"Save",        TOK_SAVE},
    {"Bload",       TOK_BLOAD},
    {"Bsave",       TOK_BSAVE},
    {"Rainbow",     TOK_RAINBOW},
    {"Copper",      TOK_COPPER},
    {"Autoback",    TOK_AUTOBACK},
    {"Scroll",      TOK_SCROLL},
    {"Resume",      TOK_RESUME},
    {"Trap",        TOK_TRAP},
    {"Error",       TOK_ERROR},
    {"Poke",        TOK_POKE},
    {"Deek",        TOK_DEEK},
    {"Doke",        TOK_DOKE},
    {"Leek",        TOK_LEEK},
    {"Loke",        TOK_LOKE},
    {"Select",      TOK_SELECT},
    {"Case",        TOK_CASE},
    {"Default",     TOK_DEFAULT},
    {"Every",       TOK_EVERY},
    {"Clip",        TOK_CLIP},
    {"Append",      TOK_APPEND},
    {"Close",       TOK_CLOSE},
    {"Kill",        TOK_KILL},
    {"Rename",      TOK_RENAME},
    {"Mkdir",       TOK_MKDIR},
    {"Centre",      TOK_CENTRE},
    {"Center",      TOK_CENTRE},      /* American spelling */
    {"Fade",        TOK_FADE},
    {"Add",         TOK_ADD},
    {"Inc",         TOK_INC},
    {"Dec",         TOK_DEC},
    {"Pen",         TOK_PEN},
    {"Paper",       TOK_PAPER},
    {"Hide",        TOK_HIDE},
    {"Show",        TOK_SHOW},
    {"Flash",       TOK_FLASH},
    {"Cdown",       TOK_CDOWN},
    {"Cup",         TOK_CUP},
    {"Cleft",       TOK_CLEFT},
    {"Cright",      TOK_CRIGHT},
    {"Appear",      TOK_APPEAR},
    {"Cline",       TOK_CLINE},
    {"Clw",         TOK_CLW},
    {"Rem",         TOK_REM},
    {"Wait",        TOK_WAIT},
    {"Timer",       TOK_TIMER},
    {"Randomize",   TOK_RANDOMIZE},
    {"Mode",        TOK_MODE},
    {"And",         TOK_AND},
    {"Or",          TOK_OR},
    {"Not",         TOK_NOT},
    {"Xor",         TOK_XOR},
    {"Mod",         TOK_MOD},

    {NULL, TOK_EOF}
};

/* ── Token List Management ───────────────────────────────────────── */

static amos_token_list_t *token_list_create(void)
{
    amos_token_list_t *list = calloc(1, sizeof(amos_token_list_t));
    list->capacity = 256;
    list->tokens = calloc(list->capacity, sizeof(amos_token_t));
    return list;
}

static void token_list_push(amos_token_list_t *list, amos_token_t tok)
{
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->tokens = realloc(list->tokens, list->capacity * sizeof(amos_token_t));
    }
    list->tokens[list->count++] = tok;
}

void amos_token_list_free(amos_token_list_t *list)
{
    if (!list) return;
    for (int i = 0; i < list->count; i++) {
        amos_token_t *t = &list->tokens[i];
        if ((t->type == TOK_STRING || t->type == TOK_IDENTIFIER ||
             t->type == TOK_LABEL) && t->sval) {
            free(t->sval);
        }
    }
    free(list->tokens);
    free(list);
}

/* ── Case-insensitive prefix match ───────────────────────────────── */

static bool match_keyword(const char *src, const char *kw, int *consumed)
{
    int i = 0;
    while (kw[i]) {
        if (tolower((unsigned char)src[i]) != tolower((unsigned char)kw[i]))
            return false;
        i++;
    }
    /* Keyword must end at a non-alnum boundary (unless it's a space in multi-word) */
    if (isalnum((unsigned char)src[i]) || src[i] == '_')
        return false;
    *consumed = i;
    return true;
}

/* ── Main Tokenizer ──────────────────────────────────────────────── */

amos_token_list_t *amos_tokenize(const char *source)
{
    amos_token_list_t *list = token_list_create();
    const char *p = source;
    int line = 1;
    int col = 1;

    while (*p) {
        /* Skip spaces (not newlines) */
        if (*p == ' ' || *p == '\t') {
            p++;
            col++;
            continue;
        }

        /* Newline */
        if (*p == '\n') {
            amos_token_t tok = {.type = TOK_NEWLINE, .line = line, .col = col};
            token_list_push(list, tok);
            p++;
            line++;
            col = 1;
            continue;
        }

        /* Carriage return */
        if (*p == '\r') {
            p++;
            continue;
        }

        /* Line number at start of line (digits at column 1 or after newline) */
        if (isdigit((unsigned char)*p) && col == 1) {
            /* Check if this is a line number (digits followed by space then keyword/ident) */
            const char *start = p;
            int32_t num = 0;
            while (isdigit((unsigned char)*p)) {
                num = num * 10 + (*p - '0');
                p++;
                col++;
            }
            /* If followed by space and more content, treat as line number */
            if (*p == ' ' || *p == '\t' || *p == ':') {
                amos_token_t tok = {.type = TOK_LINE_NUMBER, .line = line, .col = 1};
                tok.ival = num;
                token_list_push(list, tok);
                continue;
            }
            /* Otherwise, it's a regular integer */
            p = start;
            col = 1;
            /* Fall through to number parsing below */
        }

        /* Comment: ' or Rem */
        if (*p == '\'') {
            amos_token_t tok = {.type = TOK_REM, .line = line, .col = col};
            p++;
            const char *start = p;
            while (*p && *p != '\n') p++;
            tok.sval = strndup(start, p - start);
            token_list_push(list, tok);
            continue;
        }

        /* String literal */
        if (*p == '"') {
            p++; col++;
            const char *start = p;
            while (*p && *p != '"' && *p != '\n') {
                p++; col++;
            }
            amos_token_t tok = {.type = TOK_STRING, .line = line, .col = col};
            tok.sval = strndup(start, p - start);
            token_list_push(list, tok);
            if (*p == '"') { p++; col++; }
            continue;
        }

        /* Operators and punctuation */
        {
            amos_token_t tok = {.line = line, .col = col};
            bool found = true;
            switch (*p) {
                case '+': tok.type = TOK_PLUS; break;
                case '-': tok.type = TOK_MINUS; break;
                case '*': tok.type = TOK_MULTIPLY; break;
                case '/': tok.type = TOK_DIVIDE; break;
                case '^': tok.type = TOK_POWER; break;
                case '(': tok.type = TOK_LPAREN; break;
                case ')': tok.type = TOK_RPAREN; break;
                case ',': tok.type = TOK_COMMA; break;
                case ';': tok.type = TOK_SEMICOLON; break;
                case ':': tok.type = TOK_COLON; break;
                case '#': tok.type = TOK_HASH; break;
                case '$':
                    /* Check if this is a hex literal ($FFF) vs string suffix (A$) */
                    if (isxdigit((unsigned char)p[1])) { found = false; }
                    else { tok.type = TOK_DOLLAR; }
                    break;
                case '=': tok.type = TOK_EQUAL; break;
                case '<':
                    if (p[1] == '>') { tok.type = TOK_NOT_EQUAL; p++; col++; }
                    else if (p[1] == '=') { tok.type = TOK_LESS_EQUAL; p++; col++; }
                    else tok.type = TOK_LESS;
                    break;
                case '>':
                    if (p[1] == '=') { tok.type = TOK_GREATER_EQUAL; p++; col++; }
                    else tok.type = TOK_GREATER;
                    break;
                default: found = false;
            }
            if (found) {
                token_list_push(list, tok);
                p++; col++;
                continue;
            }
        }

        /* Number (integer or float) */
        if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)p[1]))) {
            const char *start = p;
            bool is_float = false;

            /* Hex: $xxxx */
            if (*p == '$') {
                p++; col++;
                int32_t val = 0;
                while (isxdigit((unsigned char)*p)) {
                    val = val * 16 + (isdigit((unsigned char)*p) ? *p - '0' :
                          tolower((unsigned char)*p) - 'a' + 10);
                    p++; col++;
                }
                amos_token_t tok = {.type = TOK_INTEGER, .line = line, .col = col};
                tok.ival = val;
                token_list_push(list, tok);
                continue;
            }

            while (isdigit((unsigned char)*p)) { p++; col++; }
            if (*p == '.' && isdigit((unsigned char)p[1])) {
                is_float = true;
                p++; col++;
                while (isdigit((unsigned char)*p)) { p++; col++; }
            }

            amos_token_t tok = {.line = line, .col = col};
            if (is_float) {
                tok.type = TOK_FLOAT;
                tok.fval = strtod(start, NULL);
            } else {
                tok.type = TOK_INTEGER;
                tok.ival = (int32_t)strtol(start, NULL, 10);
            }
            token_list_push(list, tok);
            continue;
        }

        /* Hex literal with $ prefix */
        if (*p == '$' && isxdigit((unsigned char)p[1])) {
            p++; col++;
            int32_t val = 0;
            while (isxdigit((unsigned char)*p)) {
                val = val * 16 + (isdigit((unsigned char)*p) ? *p - '0' :
                      tolower((unsigned char)*p) - 'a' + 10);
                p++; col++;
            }
            amos_token_t tok = {.type = TOK_INTEGER, .line = line, .col = col};
            tok.ival = val;
            token_list_push(list, tok);
            continue;
        }

        /* Keywords and identifiers */
        if (isalpha((unsigned char)*p) || *p == '_') {
            /* Try multi-word keywords first, then single-word */
            bool matched = false;
            for (int k = 0; keywords[k].text; k++) {
                int consumed = 0;
                if (match_keyword(p, keywords[k].text, &consumed)) {
                    /* Special case: Rem consumes rest of line */
                    if (keywords[k].type == TOK_REM) {
                        p += consumed;
                        col += consumed;
                        while (*p == ' ' || *p == '\t') { p++; col++; }
                        const char *start = p;
                        while (*p && *p != '\n') { p++; col++; }
                        amos_token_t tok = {.type = TOK_REM, .line = line, .col = col};
                        tok.sval = strndup(start, p - start);
                        token_list_push(list, tok);
                    } else {
                        amos_token_t tok = {.type = keywords[k].type, .line = line, .col = col};
                        token_list_push(list, tok);
                        p += consumed;
                        col += consumed;
                    }
                    matched = true;
                    break;
                }
            }
            if (matched) continue;

            /* Identifier */
            const char *start = p;
            while (isalnum((unsigned char)*p) || *p == '_') { p++; col++; }

            /* Check for type suffix: $ (string) or # (float) */
            /* The suffix is part of the identifier name */
            char suffix = 0;
            if (*p == '$' || *p == '#') {
                suffix = *p;
                p++; col++;
            }

            int len = (int)(p - start);
            char *name = malloc(len + 1);
            memcpy(name, start, len);
            name[len] = '\0';

            /* Check if this is a label (identifier followed by :) */
            if (*p == ':' && suffix == 0) {
                amos_token_t tok = {.type = TOK_LABEL, .line = line, .col = col};
                tok.sval = name;
                token_list_push(list, tok);
                p++; col++;  /* consume the : */
            } else {
                amos_token_t tok = {.type = TOK_IDENTIFIER, .line = line, .col = col};
                tok.sval = name;
                token_list_push(list, tok);
            }
            continue;
        }

        /* Unknown character — skip */
        p++;
        col++;
    }

    /* Final EOF token */
    amos_token_t eof = {.type = TOK_EOF, .line = line, .col = col};
    token_list_push(list, eof);

    return list;
}
