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
#include <ctype.h>

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

/* ── Array Helpers ───────────────────────────────────────────────── */

static amos_array_t *array_find(amos_state_t *state, const char *name)
{
    for (int i = 0; i < state->array_count; i++) {
        if (strcasecmp(state->arrays[i].name, name) == 0)
            return &state->arrays[i];
    }
    return NULL;
}

static amos_array_t *array_create(amos_state_t *state, const char *name,
                                   int *dims, int ndims)
{
    if (state->array_count >= AMOS_MAX_ARRAYS) return NULL;

    amos_array_t *arr = &state->arrays[state->array_count++];
    memset(arr, 0, sizeof(*arr));
    strncpy(arr->name, name, sizeof(arr->name) - 1);

    int len = (int)strlen(name);
    if (len > 0 && name[len - 1] == '$') arr->type = VAR_STRING;
    else if (len > 0 && name[len - 1] == '#') arr->type = VAR_FLOAT;
    else arr->type = VAR_INTEGER;

    arr->ndims = ndims;
    arr->total_elements = 1;
    for (int i = 0; i < ndims; i++) {
        arr->dims[i] = dims[i] + 1;  /* AMOS arrays are 0..N inclusive */
        arr->total_elements *= arr->dims[i];
    }

    if (arr->type == VAR_INTEGER)
        arr->data = calloc(arr->total_elements, sizeof(int32_t));
    else if (arr->type == VAR_FLOAT)
        arr->data = calloc(arr->total_elements, sizeof(double));
    else
        arr->data = calloc(arr->total_elements, sizeof(char *));

    return arr;
}

static int array_index(amos_array_t *arr, int *indices, int nidx)
{
    if (nidx != arr->ndims) return -1;
    int flat = 0;
    int multiplier = 1;
    for (int i = arr->ndims - 1; i >= 0; i--) {
        if (indices[i] < 0 || indices[i] >= arr->dims[i]) return -1;
        flat += indices[i] * multiplier;
        multiplier *= arr->dims[i];
    }
    return flat;
}


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

/* ── AMOS Error Messages ────────────────────────────────────────── */

static const char *amos_error_messages[] = {
    /* 0  */ "No error",
    /* 1  */ "Return without Gosub",
    /* 2  */ "Out of data",
    /* 3  */ "Division by zero",
    /* 4  */ "Overflow",
    /* 5  */ "Out of memory",
    /* 6  */ "Type mismatch",
    /* 7  */ "String too long",
    /* 8  */ "Array not dimensioned",
    /* 9  */ "Subscript out of range",
    /* 10 */ "Duplicate definition",
    /* 11 */ "Syntax error",
    /* 12 */ "Illegal function call",
    /* 13 */ "File not found",
    /* 14 */ "Disc full",
    /* 15 */ "Device I/O error",
    /* 16 */ "File already open",
    /* 17 */ "File not open",
    /* 18 */ "Input past end",
    /* 19 */ "Bad file name",
    /* 20 */ "Bank not reserved",
    /* 21 */ "Sprite/Bob not defined",
    /* 22 */ "Screen not opened",
    /* 23 */ "Label not defined",
    /* 24 */ "Procedure not found",
    /* 25 */ "Function not found",
    /* 26 */ "Next without For",
    /* 27 */ "Wend without While",
    /* 28 */ "Until without Repeat",
    /* 29 */ "Illegal instruction",
};
#define AMOS_NUM_ERROR_MSGS (sizeof(amos_error_messages) / sizeof(amos_error_messages[0]))

static const char *amos_get_error_message(int err_num)
{
    if (err_num >= 0 && err_num < (int)AMOS_NUM_ERROR_MSGS)
        return amos_error_messages[err_num];
    return "Unknown error";
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
    /* ── Input functions ─────────────────────────────────────────── */
    else if (strcasecmp(name, "Inkey$") == 0) {
        result = make_string(amos_inkey_str(state));
    }
    else if (strcasecmp(name, "Key State") == 0) {
        result = make_int(amos_key_state(state, to_int(args[0])));
    }
    else if (strcasecmp(name, "Scancode") == 0) {
        result = make_int(amos_scancode(state));
    }
    else if (strcasecmp(name, "Scanshift") == 0) {
        result = make_int(amos_scanshift(state));
    }
    else if (strcasecmp(name, "X Mouse") == 0) {
        result = make_int(amos_x_mouse(state));
    }
    else if (strcasecmp(name, "Y Mouse") == 0) {
        result = make_int(amos_y_mouse(state));
    }
    else if (strcasecmp(name, "Mouse Key") == 0) {
        result = make_int(amos_mouse_key(state));
    }
    else if (strcasecmp(name, "Mouse Click") == 0) {
        result = make_int(amos_mouse_click(state));
    }
    else if (strcasecmp(name, "Joy") == 0) {
        int port = argc > 0 ? to_int(args[0]) : 1;
        result = make_int(amos_joy(state, port));
    }
    else if (strcasecmp(name, "Jleft") == 0) {
        int port = argc > 0 ? to_int(args[0]) : 1;
        result = make_int((amos_joy(state, port) & 4) ? -1 : 0);
    }
    else if (strcasecmp(name, "Jright") == 0) {
        int port = argc > 0 ? to_int(args[0]) : 1;
        result = make_int((amos_joy(state, port) & 8) ? -1 : 0);
    }
    else if (strcasecmp(name, "Jup") == 0) {
        int port = argc > 0 ? to_int(args[0]) : 1;
        result = make_int((amos_joy(state, port) & 1) ? -1 : 0);
    }
    else if (strcasecmp(name, "Jdown") == 0) {
        int port = argc > 0 ? to_int(args[0]) : 1;
        result = make_int((amos_joy(state, port) & 2) ? -1 : 0);
    }
    else if (strcasecmp(name, "Fire") == 0) {
        int port = argc > 0 ? to_int(args[0]) : 1;
        result = make_int((amos_joy(state, port) & 16) ? -1 : 0);
    }
    /* ── String Functions ───────────────────────────────────────── */
    else if (strcasecmp(name, "Upper$") == 0) {
        if (args[0].type == VAR_STRING && args[0].sval) {
            char *buf = strdup(args[0].sval);
            for (int i = 0; buf[i]; i++) buf[i] = (char)toupper((unsigned char)buf[i]);
            result = make_string(buf);
            free(buf);
        } else {
            result = make_string("");
        }
    }
    else if (strcasecmp(name, "Lower$") == 0) {
        if (args[0].type == VAR_STRING && args[0].sval) {
            char *buf = strdup(args[0].sval);
            for (int i = 0; buf[i]; i++) buf[i] = (char)tolower((unsigned char)buf[i]);
            result = make_string(buf);
            free(buf);
        } else {
            result = make_string("");
        }
    }
    else if (strcasecmp(name, "Flip$") == 0) {
        if (args[0].type == VAR_STRING && args[0].sval) {
            int len = (int)strlen(args[0].sval);
            char *buf = malloc(len + 1);
            for (int i = 0; i < len; i++) buf[i] = args[0].sval[len - 1 - i];
            buf[len] = '\0';
            result = make_string(buf);
            free(buf);
        } else {
            result = make_string("");
        }
    }
    else if (strcasecmp(name, "Space$") == 0) {
        int n = to_int(args[0]);
        if (n < 0) n = 0;
        if (n > 65535) n = 65535;
        char *buf = malloc(n + 1);
        memset(buf, ' ', n);
        buf[n] = '\0';
        result = make_string(buf);
        free(buf);
    }
    else if (strcasecmp(name, "String$") == 0) {
        if (args[0].type == VAR_STRING && args[0].sval) {
            int n = to_int(args[1]);
            if (n < 0) n = 0;
            int slen = (int)strlen(args[0].sval);
            char *buf = malloc(slen * n + 1);
            buf[0] = '\0';
            for (int i = 0; i < n; i++) strcat(buf, args[0].sval);
            result = make_string(buf);
            free(buf);
        } else {
            result = make_string("");
        }
    }
    else if (strcasecmp(name, "Trim$") == 0) {
        if (args[0].type == VAR_STRING && args[0].sval) {
            const char *s = args[0].sval;
            while (*s == ' ') s++;
            int len = (int)strlen(s);
            while (len > 0 && s[len - 1] == ' ') len--;
            char *buf = malloc(len + 1);
            memcpy(buf, s, len);
            buf[len] = '\0';
            result = make_string(buf);
            free(buf);
        } else {
            result = make_string("");
        }
    }
    else if (strcasecmp(name, "Replace$") == 0) {
        if (args[0].type == VAR_STRING && args[1].type == VAR_STRING &&
            args[2].type == VAR_STRING && args[0].sval && args[1].sval && args[2].sval) {
            const char *src = args[0].sval;
            const char *old_s = args[1].sval;
            const char *new_s = args[2].sval;
            int old_len = (int)strlen(old_s);
            int new_len = (int)strlen(new_s);
            if (old_len == 0) {
                result = make_string(src);
            } else {
                /* Count occurrences */
                int count = 0;
                const char *p = src;
                while ((p = strstr(p, old_s)) != NULL) { count++; p += old_len; }
                int result_len = (int)strlen(src) + count * (new_len - old_len);
                char *buf = malloc(result_len + 1);
                char *dst = buf;
                p = src;
                while (*p) {
                    if (strncmp(p, old_s, old_len) == 0) {
                        memcpy(dst, new_s, new_len);
                        dst += new_len;
                        p += old_len;
                    } else {
                        *dst++ = *p++;
                    }
                }
                *dst = '\0';
                result = make_string(buf);
                free(buf);
            }
        } else {
            result = make_string("");
        }
    }
    else if (strcasecmp(name, "Hex$") == 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%X", (unsigned int)to_int(args[0]));
        result = make_string(buf);
    }
    else if (strcasecmp(name, "Bin$") == 0) {
        uint32_t val = (uint32_t)to_int(args[0]);
        char buf[33];
        int pos = 0;
        if (val == 0) {
            buf[pos++] = '0';
        } else {
            /* Find highest set bit */
            int start = 31;
            while (start > 0 && !(val & (1u << start))) start--;
            for (int i = start; i >= 0; i--) {
                buf[pos++] = (val & (1u << i)) ? '1' : '0';
            }
        }
        buf[pos] = '\0';
        result = make_string(buf);
    }
    /* ── Math Functions ─────────────────────────────────────────── */
    else if (strcasecmp(name, "Atan") == 0) {
        result = make_float(atan(to_number(args[0])));
    }
    else if (strcasecmp(name, "Asin") == 0) {
        result = make_float(asin(to_number(args[0])));
    }
    else if (strcasecmp(name, "Acos") == 0) {
        result = make_float(acos(to_number(args[0])));
    }
    else if (strcasecmp(name, "Deg") == 0) {
        result = make_float(to_number(args[0]) * 180.0 / M_PI);
    }
    else if (strcasecmp(name, "Rad") == 0) {
        result = make_float(to_number(args[0]) * M_PI / 180.0);
    }
    else if (strcasecmp(name, "Pi#") == 0) {
        result = make_float(M_PI);
    }
    else if (strcasecmp(name, "Fix") == 0) {
        double v = to_number(args[0]);
        result = make_int((int32_t)(v >= 0 ? floor(v) : ceil(v)));
    }
    /* ── Memory Stubs ───────────────────────────────────────────── */
    else if (strcasecmp(name, "Peek") == 0) {
        result = make_int(0);  /* stub */
    }
    else if (strcasecmp(name, "Free") == 0) {
        result = make_int(10000000);
    }
    /* ── Error Functions ────────────────────────────────────────── */
    else if (strcasecmp(name, "Err") == 0) {
        result = make_int(state->last_error);
    }
    else if (strcasecmp(name, "Erl") == 0) {
        result = make_int(state->last_error_line);
    }
    else if (strcasecmp(name, "Err$") == 0) {
        int err_num = argc > 0 ? to_int(args[0]) : state->last_error;
        result = make_string(amos_get_error_message(err_num));
    }
    else if (strcasecmp(name, "Errtrap") == 0) {
        /* AMOS Pro: returns the last trapped error number */
        result = make_int(state->last_error);
    }
    else if (strcasecmp(name, "Errn") == 0) {
        /* Errn — same as Err, last error number */
        result = make_int(state->last_error);
    }
    /* ── File I/O Functions ─────────────────────────────────────── */
    else if (strcasecmp(name, "Eof") == 0) {
        result = make_int(amos_file_eof(state, to_int(args[0])));
    }
    else if (strcasecmp(name, "Exist") == 0) {
        if (args[0].type == VAR_STRING && args[0].sval) {
            result = make_int(amos_file_exist(state, args[0].sval));
        } else {
            result = make_int(0);
        }
    }
    else if (strcasecmp(name, "Dir First$") == 0) {
        if (args[0].type == VAR_STRING && args[0].sval) {
            char *r = amos_dir_first(state, args[0].sval);
            result = make_string(r);
            free(r);
        } else {
            result = make_string("");
        }
    }
    else if (strcasecmp(name, "Dir Next$") == 0) {
        char *r = amos_dir_next(state);
        result = make_string(r);
        free(r);
    }
    else if (strcasecmp(name, "Filelen") == 0) {
        if (args[0].type == VAR_STRING && args[0].sval) {
            result = make_int(amos_file_length(state, args[0].sval));
        } else {
            result = make_int(0);
        }
    }
    /* ── New String Functions ──────────────────────────────────────── */
    else if (strcasecmp(name, "Tab$") == 0) {
        int n = to_int(args[0]);
        if (n < 0) n = 0;
        if (n > 65535) n = 65535;
        char *buf = malloc(n + 1);
        memset(buf, ' ', n);
        buf[n] = '\0';
        result = make_string(buf);
        free(buf);
    }
    else if (strcasecmp(name, "Insert$") == 0) {
        /* Insert$(source$, insert$, position) — insert string at 1-based position */
        if (args[0].type == VAR_STRING && args[0].sval &&
            args[1].type == VAR_STRING && args[1].sval) {
            const char *src = args[0].sval;
            const char *ins = args[1].sval;
            int pos = to_int(args[2]) - 1;  /* 1-based to 0-based */
            int slen = (int)strlen(src);
            int ilen = (int)strlen(ins);
            if (pos < 0) pos = 0;
            if (pos > slen) pos = slen;
            char *buf = malloc(slen + ilen + 1);
            memcpy(buf, src, pos);
            memcpy(buf + pos, ins, ilen);
            memcpy(buf + pos + ilen, src + pos, slen - pos + 1);
            result = make_string(buf);
            free(buf);
        } else {
            result = make_string("");
        }
    }
    /* ── Memory Stubs (Deek/Leek) ─────────────────────────────────── */
    else if (strcasecmp(name, "Deek") == 0) {
        result = make_int(0);  /* stub */
    }
    else if (strcasecmp(name, "Leek") == 0) {
        result = make_int(0);  /* stub */
    }
    /* ── Bank Functions ────────────────────────────────────────────── */
    else if (strcasecmp(name, "Start") == 0) {
        /* Start(n) — return bank base address (stubbed as bank_num * 65536) */
        int bank = to_int(args[0]);
        if (bank >= 1 && bank < AMOS_MAX_BANKS && state->banks[bank].data) {
            /* Return a fake but consistent address */
            result = make_int((int32_t)(intptr_t)state->banks[bank].data);
        } else {
            result = make_int(0);
        }
    }
    else if (strcasecmp(name, "Length") == 0) {
        /* Length(n) — return bank size */
        int bank = to_int(args[0]);
        if (bank >= 1 && bank < AMOS_MAX_BANKS && state->banks[bank].data) {
            result = make_int((int32_t)state->banks[bank].size);
        } else {
            result = make_int(0);
        }
    }
    /* ── Screen Position Functions ─────────────────────────────────── */
    else if (strcasecmp(name, "X Screen") == 0) {
        int sid = to_int(args[0]);
        if (sid >= 0 && sid < AMOS_MAX_SCREENS && state->screens[sid].active) {
            result = make_int(state->screens[sid].display_x);
        } else {
            result = make_int(0);
        }
    }
    else if (strcasecmp(name, "Y Screen") == 0) {
        int sid = to_int(args[0]);
        if (sid >= 0 && sid < AMOS_MAX_SCREENS && state->screens[sid].active) {
            result = make_int(state->screens[sid].display_y);
        } else {
            result = make_int(0);
        }
    }
    /* ── Colour function (read palette) ────────────────────────────── */
    else if (strcasecmp(name, "Colour") == 0) {
        int idx = to_int(args[0]);
        amos_screen_t *scr = &state->screens[state->current_screen];
        if (idx >= 0 && idx < 256 && scr->active) {
            result = make_int((int32_t)scr->palette[idx]);
        } else {
            result = make_int(0);
        }
    }
    else if (strcasecmp(name, "Point") == 0) {
        /* Point(x,y) — return color index at pixel position */
        int x = to_int(args[0]);
        int y = to_int(args[1]);
        amos_screen_t *scr = &state->screens[state->current_screen];
        if (scr->active && x >= 0 && x < scr->width && y >= 0 && y < scr->height) {
            uint32_t pixel = scr->pixels[y * scr->width + x];
            /* Find closest palette match */
            int best = 0;
            for (int c = 0; c < (1 << scr->depth); c++) {
                if (scr->palette[c] == pixel) { best = c; break; }
            }
            result = make_int(best);
        } else {
            result = make_int(0);
        }
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

        case NODE_ARRAY_ACCESS: {
            amos_array_t *arr = array_find(state, node->token.sval);
            if (!arr) return make_int(0);

            int indices[10] = {0};
            int nidx = 0;
            for (int i = 0; i < node->child_count && nidx < 10; i++) {
                eval_result_t idx = eval_node(state, node->children[i]);
                indices[nidx++] = to_int(idx);
                free_result(&idx);
            }

            int flat = array_index(arr, indices, nidx);
            if (flat < 0) return make_int(0);

            if (arr->type == VAR_INTEGER)
                return make_int(((int32_t *)arr->data)[flat]);
            else if (arr->type == VAR_FLOAT)
                return make_float(((double *)arr->data)[flat]);
            else {
                char **strs = (char **)arr->data;
                return make_string(strs[flat] ? strs[flat] : "");
            }
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

/* ── Block Scanning Helpers ───────────────────────────────────────── */

/* Scan forward to find matching Else or End If, respecting nesting */
static int scan_to_else_or_endif(amos_state_t *state, int from_line)
{
    int depth = 1;
    for (int i = from_line + 1; i < state->line_count; i++) {
        /* Lazy-parse the line to check its type */
        amos_program_line_t *pl = &state->lines[i];
        if (!pl->ast && pl->text) {
            amos_token_list_t *tokens = amos_tokenize(pl->text);
            if (tokens && tokens->count > 0) {
                int pos = 0;
                pl->ast = amos_parse_line(tokens->tokens, &pos, tokens->count);
            }
            amos_token_list_free(tokens);
        }
        if (!pl->ast) continue;

        if (pl->ast->type == NODE_IF) depth++;
        if (pl->ast->type == NODE_COMMAND && pl->ast->token.type == TOK_END_IF) {
            depth--;
            if (depth == 0) return i;
        }
        if (depth == 1 && pl->ast->type == NODE_COMMAND && pl->ast->token.type == TOK_ELSE) {
            return i;
        }
    }
    return state->line_count; /* not found — skip to end */
}

/* Scan forward to find matching End If, respecting nesting */
static int scan_to_endif(amos_state_t *state, int from_line)
{
    int depth = 1;
    for (int i = from_line + 1; i < state->line_count; i++) {
        amos_program_line_t *pl = &state->lines[i];
        if (!pl->ast && pl->text) {
            amos_token_list_t *tokens = amos_tokenize(pl->text);
            if (tokens && tokens->count > 0) {
                int pos = 0;
                pl->ast = amos_parse_line(tokens->tokens, &pos, tokens->count);
            }
            amos_token_list_free(tokens);
        }
        if (!pl->ast) continue;

        if (pl->ast->type == NODE_IF) depth++;
        if (pl->ast->type == NODE_COMMAND && pl->ast->token.type == TOK_END_IF) {
            depth--;
            if (depth == 0) return i;
        }
    }
    return state->line_count;
}

/* Scan forward to find matching Wend */
static int scan_to_wend(amos_state_t *state, int from_line)
{
    int depth = 1;
    for (int i = from_line + 1; i < state->line_count; i++) {
        amos_program_line_t *pl = &state->lines[i];
        if (!pl->ast && pl->text) {
            amos_token_list_t *tokens = amos_tokenize(pl->text);
            if (tokens && tokens->count > 0) {
                int pos = 0;
                pl->ast = amos_parse_line(tokens->tokens, &pos, tokens->count);
            }
            amos_token_list_free(tokens);
        }
        if (!pl->ast) continue;

        if (pl->ast->type == NODE_WHILE) depth++;
        if (pl->ast->type == NODE_COMMAND && pl->ast->token.type == TOK_WEND) {
            depth--;
            if (depth == 0) return i;
        }
    }
    return state->line_count;
}

/* Scan backward to find matching Repeat */
static int scan_to_repeat(amos_state_t *state, int from_line)
{
    int depth = 1;
    for (int i = from_line - 1; i >= 0; i--) {
        amos_program_line_t *pl = &state->lines[i];
        if (!pl->ast) continue;

        if (pl->ast->type == NODE_COMMAND && pl->ast->token.type == TOK_UNTIL) depth++;
        if (pl->ast->type == NODE_REPEAT) {
            depth--;
            if (depth == 0) return i;
        }
    }
    return 0;
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

        case NODE_PROCEDURE: {
            /* If we flow into a Procedure definition (not called), skip to End Proc */
            int depth = 1;
            for (int i = state->current_line + 1; i < state->line_count; i++) {
                if (!state->lines[i].ast && state->lines[i].text) {
                    amos_token_list_t *tl = amos_tokenize(state->lines[i].text);
                    if (tl && tl->count > 0) {
                        int p = 0;
                        state->lines[i].ast = amos_parse_line(tl->tokens, &p, tl->count);
                    }
                    amos_token_list_free(tl);
                }
                amos_node_t *ln = state->lines[i].ast;
                if (!ln) continue;
                if (ln->type == NODE_PROCEDURE) depth++;
                if (ln->type == NODE_COMMAND && ln->token.type == TOK_END_PROC) {
                    depth--;
                    if (depth == 0) {
                        state->current_line = i + 1;
                        return;
                    }
                }
            }
            /* No matching End Proc found — stop */
            state->running = false;
            break;
        }

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
            if (!name || node->child_count == 0) break;

            /* Check if this is an array assignment (more than 1 child = indices + value) */
            amos_array_t *arr = array_find(state, name);
            if (arr && node->child_count > 1) {
                /* Array assignment: children[0..N-2] = indices, children[N-1] = value */
                int indices[10] = {0};
                int nidx = 0;
                for (int i = 0; i < node->child_count - 1 && nidx < 10; i++) {
                    eval_result_t idx = eval_node(state, node->children[i]);
                    indices[nidx++] = to_int(idx);
                    free_result(&idx);
                }
                int flat = array_index(arr, indices, nidx);
                if (flat >= 0) {
                    eval_result_t val = eval_node(state, node->children[node->child_count - 1]);
                    if (arr->type == VAR_INTEGER)
                        ((int32_t *)arr->data)[flat] = to_int(val);
                    else if (arr->type == VAR_FLOAT)
                        ((double *)arr->data)[flat] = to_number(val);
                    else {
                        char **strs = (char **)arr->data;
                        free(strs[flat]);
                        strs[flat] = strdup(val.type == VAR_STRING ? val.sval : "");
                    }
                    free_result(&val);
                }
            } else {
                /* Simple variable assignment */
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
            if (node->child_count < 1) break;
            eval_result_t cond = eval_node(state, node->children[0]);
            bool is_true = to_int(cond) != 0;
            free_result(&cond);

            if (node->child_count >= 2) {
                /* Single-line If: Then-body is already parsed as children */
                if (is_true) {
                    amos_node_t *then_body = node->children[1];
                    for (int i = 0; i < then_body->child_count && state->running; i++) {
                        amos_execute_node(state, then_body->children[i]);
                    }
                } else if (node->child_count > 2) {
                    amos_node_t *else_body = node->children[2];
                    for (int i = 0; i < else_body->child_count && state->running; i++) {
                        amos_execute_node(state, else_body->children[i]);
                    }
                }
            } else {
                /* Multi-line If: condition only, body spans subsequent lines */
                if (!is_true) {
                    /* Skip to matching Else or End If */
                    int target = scan_to_else_or_endif(state, state->current_line);
                    state->current_line = target;
                    /* If we landed on Else, the next step will execute it and
                       continue into the else body. If End If, it's a no-op. */
                }
                /* If true, just continue to next line (the body) */
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
                case TOK_END_PROC: {
                    /* End Proc — return to caller (same as Return) */
                    if (state->gosub_top > 0) {
                        gosub_entry_t *ge = &state->gosub_stack[--state->gosub_top];
                        state->current_line = ge->return_line;
                        state->current_pos = ge->return_pos;
                    } else {
                        state->running = false;
                    }
                    break;
                }

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
                    /* Find matching While — walk backwards with nesting */
                    int depth = 1;
                    for (int i = state->current_line - 1; i >= 0; i--) {
                        amos_node_t *ln = state->lines[i].ast;
                        if (!ln) continue;
                        if (ln->type == NODE_COMMAND && ln->token.type == TOK_WEND) depth++;
                        if (ln->type == NODE_WHILE) {
                            depth--;
                            if (depth == 0) {
                                state->current_line = i;
                                return;
                            }
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

                case TOK_EXIT: {
                    /* Unconditional loop exit — pop Do/Loop gosub entry and
                     * skip forward past the matching Loop/Wend/Next/Until */
                    if (state->gosub_top > 0) {
                        state->gosub_top--;
                    }
                    /* Scan forward for matching Loop/Wend/Until/Next */
                    for (int i = state->current_line + 1; i < state->line_count; i++) {
                        /* Lazy-parse to populate ast */
                        if (!state->lines[i].ast && state->lines[i].text) {
                            amos_token_list_t *tl = amos_tokenize(state->lines[i].text);
                            if (tl && tl->count > 0) {
                                int p = 0;
                                state->lines[i].ast = amos_parse_line(tl->tokens, &p, tl->count);
                            }
                            amos_token_list_free(tl);
                        }
                        amos_node_t *ln = state->lines[i].ast;
                        if (!ln) continue;
                        if ((ln->type == NODE_COMMAND &&
                             (ln->token.type == TOK_LOOP || ln->token.type == TOK_WEND ||
                              ln->token.type == TOK_NEXT)) ||
                            ln->type == NODE_COMMAND && ln->token.type == TOK_UNTIL) {
                            state->current_line = i + 1;
                            return;
                        }
                    }
                    break;
                }

                case TOK_EXIT_IF: {
                    /* Conditional loop exit — Exit If condition */
                    if (node->child_count > 0) {
                        eval_result_t cond = eval_node(state, node->children[0]);
                        bool is_true = to_int(cond) != 0;
                        free_result(&cond);
                        if (is_true) {
                            /* Same as Exit — pop and skip to end of loop */
                            if (state->gosub_top > 0) {
                                state->gosub_top--;
                            }
                            for (int i = state->current_line + 1; i < state->line_count; i++) {
                                if (!state->lines[i].ast && state->lines[i].text) {
                                    amos_token_list_t *tl = amos_tokenize(state->lines[i].text);
                                    if (tl && tl->count > 0) {
                                        int p = 0;
                                        state->lines[i].ast = amos_parse_line(tl->tokens, &p, tl->count);
                                    }
                                    amos_token_list_free(tl);
                                }
                                amos_node_t *ln = state->lines[i].ast;
                                if (!ln) continue;
                                if ((ln->type == NODE_COMMAND &&
                                     (ln->token.type == TOK_LOOP || ln->token.type == TOK_WEND ||
                                      ln->token.type == TOK_NEXT)) ||
                                    ln->type == NODE_COMMAND && ln->token.type == TOK_UNTIL) {
                                    state->current_line = i + 1;
                                    return;
                                }
                            }
                        }
                    }
                    break;
                }

                case TOK_UNTIL: {
                    if (node->child_count > 0) {
                        eval_result_t cond = eval_node(state, node->children[0]);
                        bool is_true = to_int(cond) != 0;
                        free_result(&cond);
                        if (!is_true) {
                            /* Loop back to matching Repeat */
                            int target = scan_to_repeat(state, state->current_line);
                            state->current_line = target;
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

                case TOK_ELLIPSE: {
                    if (node->child_count >= 4) {
                        eval_result_t x = eval_node(state, node->children[0]);
                        eval_result_t y = eval_node(state, node->children[1]);
                        eval_result_t rx = eval_node(state, node->children[2]);
                        eval_result_t ry = eval_node(state, node->children[3]);
                        amos_screen_ellipse(state, to_int(x), to_int(y),
                                            to_int(rx), to_int(ry),
                                            state->screens[state->current_screen].ink_color);
                        free_result(&x); free_result(&y);
                        free_result(&rx); free_result(&ry);
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
                    /* Wait until a key is pressed */
#ifndef AMOS_TESTING
                    state->last_key = 0;
                    while (state->running && state->last_key == 0) {
                        platform_poll_events(state);
                        if (platform_should_quit()) {
                            state->running = false;
                            break;
                        }
                    }
#endif
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

                case TOK_TRACK_PLAY: {
                    if (node->child_count > 0) {
                        eval_result_t r = eval_node(state, node->children[0]);
                        if (r.type == VAR_STRING && r.sval) {
                            amos_track_play(state, r.sval);
                        }
                        free_result(&r);
                    }
                    break;
                }

                case TOK_TRACK_STOP:
                    amos_track_stop(state);
                    break;

                case TOK_TRACK_LOOP_ON:
                    amos_track_loop_on(state);
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

                case TOK_SPRITE: {
                    /* Sprite id,x,y,image */
                    int args[4] = {0, 0, 0, 1};
                    for (int i = 0; i < node->child_count && i < 4; i++) {
                        eval_result_t r = eval_node(state, node->children[i]);
                        args[i] = to_int(r); free_result(&r);
                    }
                    amos_sprite_set(state, args[0], args[1], args[2], args[3]);
                    break;
                }

                case TOK_BOB: {
                    /* Bob id,x,y,image */
                    int args[4] = {0, 0, 0, 1};
                    for (int i = 0; i < node->child_count && i < 4; i++) {
                        eval_result_t r = eval_node(state, node->children[i]);
                        args[i] = to_int(r); free_result(&r);
                    }
                    amos_bob_set(state, args[0], args[1], args[2], args[3]);
                    break;
                }

                case TOK_SPRITE_OFF: {
                    if (node->child_count > 0) {
                        eval_result_t r = eval_node(state, node->children[0]);
                        amos_sprite_off(state, to_int(r)); free_result(&r);
                    } else {
                        amos_sprite_off(state, -1);
                    }
                    break;
                }

                case TOK_BOB_OFF: {
                    if (node->child_count > 0) {
                        eval_result_t r = eval_node(state, node->children[0]);
                        amos_bob_off(state, to_int(r)); free_result(&r);
                    } else {
                        amos_bob_off(state, -1);
                    }
                    break;
                }

                case TOK_AMAL: {
                    /* Amal channel,"program" */
                    if (node->child_count >= 2) {
                        eval_result_t ch_r = eval_node(state, node->children[0]);
                        eval_result_t pg_r = eval_node(state, node->children[1]);
                        int ch_id = to_int(ch_r);
                        if (pg_r.type == VAR_STRING && pg_r.sval) {
                            amos_amal_compile(state, ch_id, pg_r.sval);
                        }
                        free_result(&ch_r);
                        free_result(&pg_r);
                    }
                    break;
                }

                case TOK_AMAL_ON: {
                    if (node->child_count > 0) {
                        eval_result_t r = eval_node(state, node->children[0]);
                        amos_amal_on(state, to_int(r));
                        free_result(&r);
                    } else {
                        for (int i = 0; i < AMOS_MAX_AMAL_CHANNELS; i++) {
                            if (state->amal[i].program)
                                amos_amal_on(state, i);
                        }
                    }
                    break;
                }

                case TOK_AMAL_OFF: {
                    if (node->child_count > 0) {
                        eval_result_t r = eval_node(state, node->children[0]);
                        amos_amal_off(state, to_int(r));
                        free_result(&r);
                    } else {
                        for (int i = 0; i < AMOS_MAX_AMAL_CHANNELS; i++)
                            amos_amal_off(state, i);
                    }
                    break;
                }

                case TOK_AMAL_FREEZE: {
                    if (node->child_count > 0) {
                        eval_result_t r = eval_node(state, node->children[0]);
                        amos_amal_freeze(state, to_int(r));
                        free_result(&r);
                    } else {
                        for (int i = 0; i < AMOS_MAX_AMAL_CHANNELS; i++)
                            amos_amal_freeze(state, i);
                    }
                    break;
                }

                case TOK_SYNCHRO:
                    amos_amal_synchro(state);
                    break;

                case TOK_ELSE: {
                    /* We reached Else during normal execution — the If-true body
                       just finished. Skip to matching End If. */
                    int target = scan_to_endif(state, state->current_line);
                    state->current_line = target;
                    break;
                }

                case TOK_END_IF:
                    /* No-op — just continue to next line */
                    break;

                case TOK_INPUT: {
                    /* Basic Input command: Input "prompt";VAR$
                     * For now, read characters one at a time via amos_inkey
                     * until Enter is pressed. Store result in the target variable.
                     */
#ifndef AMOS_TESTING
                    if (node->child_count > 0) {
                        /* First child may be a prompt string */
                        int var_idx = 0;
                        if (node->child_count > 1 &&
                            node->children[0]->type == NODE_STRING_LITERAL) {
                            amos_screen_print(state, node->children[0]->token.sval);
                            var_idx = 1;
                        }

                        /* Show cursor and collect input */
                        char input_buf[256] = {0};
                        int pos = 0;
                        state->last_key = 0;

                        while (state->running && pos < 255) {
                            platform_poll_events(state);
                            if (platform_should_quit()) {
                                state->running = false;
                                break;
                            }
                            int key = amos_inkey(state);
                            if (key == 13 || key == 10) {
                                /* Enter pressed */
                                break;
                            } else if (key == 8 || key == 127) {
                                /* Backspace */
                                if (pos > 0) pos--;
                            } else if (key >= 32 && key < 127) {
                                input_buf[pos++] = (char)key;
                                char ch[2] = {(char)key, 0};
                                amos_screen_print(state, ch);
                            }
                        }
                        input_buf[pos] = '\0';
                        amos_screen_print(state, "\n");

                        /* Store result */
                        if (var_idx < node->child_count &&
                            node->children[var_idx]->type == NODE_VARIABLE) {
                            const char *vname = node->children[var_idx]->token.sval;
                            int len = (int)strlen(vname);
                            if (len > 0 && vname[len - 1] == '$') {
                                amos_var_set_string(state, vname, input_buf);
                            } else {
                                amos_var_set_int(state, vname, (int32_t)atoi(input_buf));
                            }
                        }
                    }
#endif
                    break;
                }

                case TOK_SWAP: {
                    /* Swap A,B — swap values of two variables */
                    if (node->child_count >= 2 &&
                        node->children[0]->type == NODE_VARIABLE &&
                        node->children[1]->type == NODE_VARIABLE) {
                        const char *name_a = node->children[0]->token.sval;
                        const char *name_b = node->children[1]->token.sval;
                        /* Read both values first */
                        eval_result_t ra = eval_node(state, node->children[0]);
                        eval_result_t rb = eval_node(state, node->children[1]);
                        /* Write swapped */
                        switch (rb.type) {
                            case VAR_INTEGER: amos_var_set_int(state, name_a, rb.ival); break;
                            case VAR_FLOAT:   amos_var_set_float(state, name_a, rb.fval); break;
                            case VAR_STRING:  amos_var_set_string(state, name_a, rb.sval ? rb.sval : ""); break;
                        }
                        switch (ra.type) {
                            case VAR_INTEGER: amos_var_set_int(state, name_b, ra.ival); break;
                            case VAR_FLOAT:   amos_var_set_float(state, name_b, ra.fval); break;
                            case VAR_STRING:  amos_var_set_string(state, name_b, ra.sval ? ra.sval : ""); break;
                        }
                        free_result(&ra);
                        free_result(&rb);
                    }
                    break;
                }

                /* ── Error Handling Commands ─────────────────────────── */
                case TOK_ON_ERROR_GOTO: {
                    if (node->child_count > 0) {
                        eval_result_t r = eval_node(state, node->children[0]);
                        int target = to_int(r);
                        free_result(&r);
                        if (target == 0) {
                            /* On Error Goto 0 = disable error handler */
                            state->on_error_line = -1;
                        } else {
                            int idx = find_line_index(state, target);
                            if (idx < 0) {
                                /* Try as label */
                                if (node->children[0]->type == NODE_VARIABLE) {
                                    idx = find_label(state, node->children[0]->token.sval);
                                }
                            }
                            state->on_error_line = idx;
                        }
                    }
                    break;
                }

                case TOK_ON_ERROR_PROC: {
                    if (node->child_count > 0) {
                        eval_result_t r = eval_node(state, node->children[0]);
                        if (r.type == VAR_STRING && r.sval) {
                            strncpy(state->on_error_proc, r.sval, sizeof(state->on_error_proc) - 1);
                        } else if (node->children[0]->type == NODE_VARIABLE &&
                                   node->children[0]->token.sval) {
                            strncpy(state->on_error_proc, node->children[0]->token.sval,
                                    sizeof(state->on_error_proc) - 1);
                        }
                        free_result(&r);
                    }
                    break;
                }

                case TOK_RESUME: {
                    /* Resume — return to the line that caused the error */
                    if (state->resume_line >= 0) {
                        state->current_line = state->resume_line;
                        state->resume_line = -1;
                    }
                    break;
                }

                case TOK_RESUME_NEXT: {
                    /* Resume Next — continue at the line after the error */
                    if (state->resume_line >= 0) {
                        state->current_line = state->resume_line + 1;
                        state->resume_line = -1;
                    }
                    break;
                }

                case TOK_RESUME_LABEL: {
                    /* Resume label — jump to a specific label */
                    if (node->child_count > 0 &&
                        node->children[0]->type == NODE_VARIABLE &&
                        node->children[0]->token.sval) {
                        int idx = find_label(state, node->children[0]->token.sval);
                        if (idx >= 0) {
                            state->current_line = idx;
                        }
                    }
                    state->resume_line = -1;
                    break;
                }

                case TOK_TRAP: {
                    state->trap_mode = true;
                    break;
                }

                case TOK_ERROR: {
                    /* Error N — deliberately trigger an error */
                    if (node->child_count > 0) {
                        eval_result_t r = eval_node(state, node->children[0]);
                        int err_num = to_int(r);
                        free_result(&r);
                        state->last_error = err_num;
                        state->last_error_line = state->current_line;
                        if (state->trap_mode) {
                            /* Just set the error, continue */
                        } else if (state->on_error_line >= 0) {
                            state->resume_line = state->current_line;
                            state->current_line = state->on_error_line;
                        } else {
                            fprintf(stderr, "Error %d: %s at line %d\n",
                                    err_num, amos_get_error_message(err_num),
                                    state->current_line + 1);
                            state->running = false;
                        }
                    }
                    break;
                }

                case TOK_POKE: {
                    /* Poke addr,value — stub, no-op */
                    break;
                }

                /* ── Screen Manipulation Commands ──────────────────────── */

                case TOK_SCREEN_COPY: {
                    /* Screen Copy src,x1,y1,x2,y2 To dst,dx,dy */
                    int args[8] = {0};
                    int ai = 0;
                    for (int i = 0; i < node->child_count && ai < 8; i++) {
                        if (node->children[i]->type == NODE_COMMAND &&
                            node->children[i]->token.type == TOK_TO)
                            continue;
                        eval_result_t r = eval_node(state, node->children[i]);
                        args[ai++] = to_int(r);
                        free_result(&r);
                    }
                    if (ai >= 8) {
                        amos_screen_copy(state, args[0], args[1], args[2], args[3], args[4],
                                         args[5], args[6], args[7]);
                    }
                    break;
                }

                case TOK_GET_BLOCK: {
                    /* Get Block id, x, y, w, h */
                    int args[5] = {0};
                    for (int i = 0; i < node->child_count && i < 5; i++) {
                        eval_result_t r = eval_node(state, node->children[i]);
                        args[i] = to_int(r);
                        free_result(&r);
                    }
                    if (node->child_count >= 5)
                        amos_get_block(state, args[0], args[1], args[2], args[3], args[4]);
                    break;
                }

                case TOK_PUT_BLOCK: {
                    /* Put Block id, x, y */
                    int args[3] = {0};
                    for (int i = 0; i < node->child_count && i < 3; i++) {
                        eval_result_t r = eval_node(state, node->children[i]);
                        args[i] = to_int(r);
                        free_result(&r);
                    }
                    if (node->child_count >= 3)
                        amos_put_block(state, args[0], args[1], args[2]);
                    break;
                }

                case TOK_DEL_BLOCK: {
                    /* Del Block id */
                    if (node->child_count > 0) {
                        eval_result_t id = eval_node(state, node->children[0]);
                        amos_del_block(state, to_int(id));
                        free_result(&id);
                    }
                    break;
                }

                case TOK_SCROLL: {
                    /* Scroll dx, dy  OR  Scroll id, dx, dy (zone scroll) */
                    if (node->child_count == 2) {
                        eval_result_t dx_r = eval_node(state, node->children[0]);
                        eval_result_t dy_r = eval_node(state, node->children[1]);
                        amos_screen_scroll(state, to_int(dx_r), to_int(dy_r));
                        free_result(&dx_r); free_result(&dy_r);
                    } else if (node->child_count == 3) {
                        eval_result_t id = eval_node(state, node->children[0]);
                        eval_result_t dx_r = eval_node(state, node->children[1]);
                        eval_result_t dy_r = eval_node(state, node->children[2]);
                        amos_scroll_zone(state, to_int(id), to_int(dx_r), to_int(dy_r));
                        free_result(&id); free_result(&dx_r); free_result(&dy_r);
                    }
                    break;
                }

                case TOK_DEF_SCROLL: {
                    /* Def Scroll id, x1, y1, x2, y2 */
                    int args[5] = {0};
                    for (int i = 0; i < node->child_count && i < 5; i++) {
                        eval_result_t r = eval_node(state, node->children[i]);
                        args[i] = to_int(r);
                        free_result(&r);
                    }
                    if (node->child_count >= 5)
                        amos_def_scroll(state, args[0], args[1], args[2], args[3], args[4]);
                    break;
                }

                case TOK_SCREEN_TO_FRONT: {
                    if (node->child_count > 0) {
                        eval_result_t id = eval_node(state, node->children[0]);
                        amos_screen_to_front(state, to_int(id));
                        free_result(&id);
                    }
                    break;
                }

                case TOK_SCREEN_TO_BACK: {
                    if (node->child_count > 0) {
                        eval_result_t id = eval_node(state, node->children[0]);
                        amos_screen_to_back(state, to_int(id));
                        free_result(&id);
                    }
                    break;
                }

                case TOK_SCREEN_HIDE: {
                    if (node->child_count > 0) {
                        eval_result_t id = eval_node(state, node->children[0]);
                        amos_screen_hide(state, to_int(id));
                        free_result(&id);
                    }
                    break;
                }

                case TOK_SCREEN_SHOW: {
                    if (node->child_count > 0) {
                        eval_result_t id = eval_node(state, node->children[0]);
                        amos_screen_show(state, to_int(id));
                        free_result(&id);
                    }
                    break;
                }

                case TOK_PAINT: {
                    /* Paint x, y[, color] */
                    if (node->child_count >= 2) {
                        eval_result_t px = eval_node(state, node->children[0]);
                        eval_result_t py = eval_node(state, node->children[1]);
                        amos_screen_t *scr = &state->screens[state->current_screen];
                        int color = scr->ink_color;
                        if (node->child_count >= 3) {
                            eval_result_t c = eval_node(state, node->children[2]);
                            color = to_int(c);
                            free_result(&c);
                        }
                        amos_screen_paint(state, to_int(px), to_int(py), color);
                        free_result(&px); free_result(&py);
                    }
                    break;
                }

                case TOK_POLYGON: {
                    /* Polygon x1,y1 To x2,y2 To x3,y3 ... */
                    int coords[AST_MAX_CHILDREN];
                    int coord_count = 0;
                    for (int i = 0; i < node->child_count; i++) {
                        if (node->children[i]->type == NODE_COMMAND &&
                            node->children[i]->token.type == TOK_TO)
                            continue;
                        eval_result_t r = eval_node(state, node->children[i]);
                        if (coord_count < AST_MAX_CHILDREN)
                            coords[coord_count++] = to_int(r);
                        free_result(&r);
                    }
                    int npts = coord_count / 2;
                    if (npts >= 3) {
                        amos_screen_t *scr = &state->screens[state->current_screen];
                        amos_screen_polygon(state, coords, npts, scr->ink_color);
                    }
                    break;
                }

                /* ── File I/O Commands ──────────────────────────────── */

                case TOK_OPEN_IN: {
                    /* Open In channel,"path" */
                    if (node->child_count >= 2) {
                        eval_result_t ch = eval_node(state, node->children[0]);
                        eval_result_t path = eval_node(state, node->children[1]);
                        if (path.type == VAR_STRING && path.sval) {
                            amos_file_open_in(state, to_int(ch), path.sval);
                        }
                        free_result(&ch); free_result(&path);
                    }
                    break;
                }

                case TOK_OPEN_OUT: {
                    /* Open Out channel,"path" */
                    if (node->child_count >= 2) {
                        eval_result_t ch = eval_node(state, node->children[0]);
                        eval_result_t path = eval_node(state, node->children[1]);
                        if (path.type == VAR_STRING && path.sval) {
                            amos_file_open_out(state, to_int(ch), path.sval);
                        }
                        free_result(&ch); free_result(&path);
                    }
                    break;
                }

                case TOK_APPEND: {
                    /* Append channel,"path" */
                    if (node->child_count >= 2) {
                        eval_result_t ch = eval_node(state, node->children[0]);
                        eval_result_t path = eval_node(state, node->children[1]);
                        if (path.type == VAR_STRING && path.sval) {
                            amos_file_append(state, to_int(ch), path.sval);
                        }
                        free_result(&ch); free_result(&path);
                    }
                    break;
                }

                case TOK_CLOSE: {
                    /* Close channel */
                    if (node->child_count > 0) {
                        eval_result_t ch = eval_node(state, node->children[0]);
                        amos_file_close(state, to_int(ch));
                        free_result(&ch);
                    }
                    break;
                }

                case TOK_PRINT_FILE: {
                    /* Print #channel, expr */
                    if (node->child_count >= 2) {
                        eval_result_t ch = eval_node(state, node->children[0]);
                        int channel = to_int(ch);
                        free_result(&ch);

                        /* Concatenate all remaining children into output */
                        char buf[1024] = {0};
                        int bpos = 0;
                        for (int i = 1; i < node->child_count; i++) {
                            eval_result_t r = eval_node(state, node->children[i]);
                            char tmp[256];
                            switch (r.type) {
                                case VAR_INTEGER:
                                    snprintf(tmp, sizeof(tmp), "%d", r.ival);
                                    break;
                                case VAR_FLOAT:
                                    snprintf(tmp, sizeof(tmp), "%g", r.fval);
                                    break;
                                case VAR_STRING:
                                    snprintf(tmp, sizeof(tmp), "%s", r.sval ? r.sval : "");
                                    break;
                            }
                            int len = (int)strlen(tmp);
                            if (bpos + len < (int)sizeof(buf) - 1) {
                                memcpy(buf + bpos, tmp, len);
                                bpos += len;
                            }
                            free_result(&r);
                        }
                        buf[bpos] = '\0';
                        amos_file_print(state, channel, buf);
                    }
                    break;
                }

                case TOK_INPUT_FILE: {
                    /* Input #channel, var$ */
                    if (node->child_count >= 2) {
                        eval_result_t ch = eval_node(state, node->children[0]);
                        int channel = to_int(ch);
                        free_result(&ch);

                        for (int i = 1; i < node->child_count; i++) {
                            if (node->children[i]->type == NODE_VARIABLE) {
                                const char *vname = node->children[i]->token.sval;
                                int vlen = (int)strlen(vname);
                                if (vlen > 0 && vname[vlen - 1] == '$') {
                                    char *val = amos_file_input_str(state, channel);
                                    amos_var_set_string(state, vname, val);
                                    free(val);
                                } else {
                                    int val = amos_file_input_int(state, channel);
                                    amos_var_set_int(state, vname, val);
                                }
                            }
                        }
                    }
                    break;
                }

                case TOK_LINE_INPUT_FILE: {
                    /* Line Input #channel, var$ */
                    if (node->child_count >= 2) {
                        eval_result_t ch = eval_node(state, node->children[0]);
                        int channel = to_int(ch);
                        free_result(&ch);

                        if (node->children[1]->type == NODE_VARIABLE) {
                            const char *vname = node->children[1]->token.sval;
                            char *line = amos_file_input_line(state, channel);
                            amos_var_set_string(state, vname, line);
                            free(line);
                        }
                    }
                    break;
                }

                case TOK_KILL: {
                    /* Kill "path" */
                    if (node->child_count > 0) {
                        eval_result_t path = eval_node(state, node->children[0]);
                        if (path.type == VAR_STRING && path.sval) {
                            amos_file_kill(state, path.sval);
                        }
                        free_result(&path);
                    }
                    break;
                }

                case TOK_RENAME: {
                    /* Rename "old" To "new" (or Rename "old","new") */
                    if (node->child_count >= 2) {
                        /* Skip TOK_TO separator nodes */
                        eval_result_t old_r = {0};
                        eval_result_t new_r = {0};
                        int arg_idx = 0;
                        for (int i = 0; i < node->child_count; i++) {
                            if (node->children[i]->type == NODE_COMMAND &&
                                node->children[i]->token.type == TOK_TO)
                                continue;
                            eval_result_t r = eval_node(state, node->children[i]);
                            if (arg_idx == 0) old_r = r;
                            else if (arg_idx == 1) new_r = r;
                            else free_result(&r);
                            arg_idx++;
                        }
                        if (old_r.type == VAR_STRING && old_r.sval &&
                            new_r.type == VAR_STRING && new_r.sval) {
                            amos_file_rename(state, old_r.sval, new_r.sval);
                        }
                        free_result(&old_r);
                        free_result(&new_r);
                    }
                    break;
                }

                case TOK_MKDIR: {
                    /* Mkdir "path" */
                    if (node->child_count > 0) {
                        eval_result_t path = eval_node(state, node->children[0]);
                        if (path.type == VAR_STRING && path.sval) {
                            amos_file_mkdir(state, path.sval);
                        }
                        free_result(&path);
                    }
                    break;
                }

                case TOK_TEXT: {
                    /* Text x,y,text$ — draw text at pixel position */
                    if (node->child_count >= 3) {
                        eval_result_t x = eval_node(state, node->children[0]);
                        eval_result_t y = eval_node(state, node->children[1]);
                        eval_result_t t = eval_node(state, node->children[2]);
                        if (t.type == VAR_STRING && t.sval) {
                            /* Save cursor, draw at pixel position, restore */
                            amos_screen_t *scr = &state->screens[state->current_screen];
                            int save_cx = scr->cursor_x, save_cy = scr->cursor_y;
                            int char_x = to_int(x) / 8;
                            int char_y = to_int(y) / 8;
                            scr->cursor_x = char_x;
                            scr->cursor_y = char_y;
                            amos_screen_print(state, t.sval);
                            scr->cursor_x = save_cx;
                            scr->cursor_y = save_cy;
                        }
                        free_result(&x); free_result(&y); free_result(&t);
                    }
                    break;
                }

                case TOK_SET_FONT: {
                    /* Set Font n — store font number */
                    if (node->child_count > 0) {
                        eval_result_t r = eval_node(state, node->children[0]);
                        state->current_font = to_int(r);
                        free_result(&r);
                    }
                    break;
                }

                case TOK_GR_WRITING: {
                    /* Gr Writing mode — set graphics writing mode */
                    if (node->child_count > 0) {
                        eval_result_t r = eval_node(state, node->children[0]);
                        amos_screen_t *scr = &state->screens[state->current_screen];
                        scr->writing_mode = to_int(r);
                        free_result(&r);
                    }
                    break;
                }

                case TOK_SET_LINE: {
                    /* Set Line pattern — 16-bit mask for line drawing */
                    if (node->child_count > 0) {
                        eval_result_t r = eval_node(state, node->children[0]);
                        state->line_pattern = (uint16_t)to_int(r);
                        free_result(&r);
                    }
                    break;
                }

                case TOK_SET_PATTERN: {
                    /* Set Pattern n — fill pattern */
                    if (node->child_count > 0) {
                        eval_result_t r = eval_node(state, node->children[0]);
                        state->fill_pattern = to_int(r);
                        free_result(&r);
                    }
                    break;
                }

                case TOK_CLIP: {
                    /* Clip x1,y1 To x2,y2 — set clipping rectangle */
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
                        state->clip_x1 = coords[0];
                        state->clip_y1 = coords[1];
                        state->clip_x2 = coords[2];
                        state->clip_y2 = coords[3];
                        state->clip_enabled = true;
                    } else if (ci == 0) {
                        /* Clip with no args = disable clipping */
                        state->clip_enabled = false;
                    }
                    break;
                }

                case TOK_SCREEN_DISPLAY: {
                    /* Screen Display id,x,y,w,h */
                    int args[5] = {0, 0, 0, 320, 256};
                    for (int i = 0; i < node->child_count && i < 5; i++) {
                        eval_result_t r = eval_node(state, node->children[i]);
                        args[i] = to_int(r);
                        free_result(&r);
                    }
                    int sid = args[0];
                    if (sid >= 0 && sid < AMOS_MAX_SCREENS && state->screens[sid].active) {
                        state->screens[sid].display_x = args[1];
                        state->screens[sid].display_y = args[2];
                        state->screens[sid].display_w = args[3];
                        state->screens[sid].display_h = args[4];
                    }
                    break;
                }

                case TOK_DOKE: {
                    /* Doke addr,value — stub, no-op */
                    break;
                }

                case TOK_LOKE: {
                    /* Loke addr,value — stub, no-op */
                    break;
                }

                /* ── Bank Commands ─────────────────────────────── */

                case TOK_RESERVE_AS_WORK: {
                    /* Reserve As Work bank_num, size */
                    if (node->child_count >= 2) {
                        eval_result_t bn = eval_node(state, node->children[0]);
                        eval_result_t sz = eval_node(state, node->children[1]);
                        int bank = to_int(bn);
                        int size = to_int(sz);
                        free_result(&bn); free_result(&sz);
                        if (bank >= 1 && bank < AMOS_MAX_BANKS && size > 0) {
                            free(state->banks[bank].data);
                            state->banks[bank].data = calloc(1, size);
                            state->banks[bank].size = size;
                            state->banks[bank].type = BANK_WORK;
                        }
                    }
                    break;
                }

                case TOK_RESERVE_AS_DATA: {
                    /* Reserve As Data bank_num, size */
                    if (node->child_count >= 2) {
                        eval_result_t bn = eval_node(state, node->children[0]);
                        eval_result_t sz = eval_node(state, node->children[1]);
                        int bank = to_int(bn);
                        int size = to_int(sz);
                        free_result(&bn); free_result(&sz);
                        if (bank >= 1 && bank < AMOS_MAX_BANKS && size > 0) {
                            free(state->banks[bank].data);
                            state->banks[bank].data = calloc(1, size);
                            state->banks[bank].size = size;
                            state->banks[bank].type = BANK_DATA;
                        }
                    }
                    break;
                }

                case TOK_ERASE: {
                    /* Erase bank_num */
                    if (node->child_count > 0) {
                        eval_result_t bn = eval_node(state, node->children[0]);
                        int bank = to_int(bn);
                        free_result(&bn);
                        if (bank >= 1 && bank < AMOS_MAX_BANKS) {
                            free(state->banks[bank].data);
                            state->banks[bank].data = NULL;
                            state->banks[bank].size = 0;
                            state->banks[bank].type = BANK_EMPTY;
                        }
                    }
                    break;
                }

                /* ── Select/Case/End Select ─────────────────────── */

                case TOK_CASE: {
                    /* We hit a Case during normal execution — we just finished
                     * executing a matching case block. Skip to End Select. */
                    int depth = 1;
                    for (int i = state->current_line + 1; i < state->line_count; i++) {
                        amos_program_line_t *pl = &state->lines[i];
                        if (!pl->ast && pl->text) {
                            amos_token_list_t *toks = amos_tokenize(pl->text);
                            if (toks && toks->count > 0) {
                                int p = 0;
                                pl->ast = amos_parse_line(toks->tokens, &p, toks->count);
                            }
                            amos_token_list_free(toks);
                        }
                        if (!pl->ast) continue;
                        if (pl->ast->type == NODE_SELECT) depth++;
                        if (pl->ast->type == NODE_COMMAND && pl->ast->token.type == TOK_END_SELECT) {
                            depth--;
                            if (depth == 0) {
                                state->current_line = i;
                                return;
                            }
                        }
                    }
                    break;
                }

                case TOK_DEFAULT: {
                    /* Same as Case — skip to End Select when hit during fallthrough */
                    /* But Default is only skipped when a prior case matched.
                     * If we reach here during sequential execution, just continue. */
                    break;
                }

                case TOK_END_SELECT:
                    /* No-op — reached normally after executing a case body */
                    break;

                case TOK_LOAD: {
                    /* Load "filename",bank_num
                     * Routes to IFF loader or bank loader based on content/extension */
                    if (node->child_count >= 1) {
                        eval_result_t fn = eval_node(state, node->children[0]);
                        int bank_num = -1;
                        if (node->child_count >= 2) {
                            eval_result_t bn = eval_node(state, node->children[1]);
                            bank_num = to_int(bn);
                            free_result(&bn);
                        }
                        if (fn.type == VAR_STRING && fn.sval) {
                            /* Check extension to decide loader */
                            const char *f = fn.sval;
                            size_t flen = strlen(f);
                            bool is_abk = (flen >= 4 &&
                                (strcasecmp(f + flen - 4, ".abk") == 0));
                            bool is_iff = (flen >= 4 &&
                                (strcasecmp(f + flen - 4, ".iff") == 0));
                            if (is_abk) {
                                amos_exec_load_bank(state, f, bank_num);
                            } else if (is_iff) {
                                amos_exec_load_iff(state, f, bank_num >= 0 ? bank_num : state->current_screen);
                            } else {
                                /* Try bank loader first (checks magic), fall back to IFF */
                                if (amos_load_sprite_bank(state, f) != 0) {
                                    amos_exec_load_iff(state, f, bank_num >= 0 ? bank_num : state->current_screen);
                                }
                            }
                        }
                        free_result(&fn);
                    }
                    break;
                }

                case TOK_CENTRE: {
                    /* Centre "text" — print centered on current screen */
                    if (node->child_count > 0) {
                        eval_result_t val = eval_node(state, node->children[0]);
                        if (val.type == VAR_STRING && val.sval) {
                            amos_screen_t *scr = &state->screens[state->current_screen];
                            if (scr->active) {
                                int text_len = (int)strlen(val.sval);
                                int cols = scr->width / 8;
                                int pad = (cols - text_len) / 2;
                                if (pad > 0) scr->cursor_x = pad;
                            }
                            amos_screen_print(state, val.sval);
                            amos_screen_print(state, "\n");
                        }
                        free_result(&val);
                    }
                    break;
                }

                case TOK_FADE: {
                    /* Fade speed[,colour0,colour1,...] — stub (visual only) */
                    /* Skip all arguments — fade is a visual timing effect */
                    break;
                }

                case TOK_ADD: {
                    /* Add var,expr[,base To limit] — add to variable with optional wraparound */
                    if (node->child_count >= 2) {
                        amos_node_t *var_node = node->children[0];
                        if (var_node && var_node->type == NODE_VARIABLE && var_node->token.sval) {
                            eval_result_t addval = eval_node(state, node->children[1]);
                            amos_var_t *v = amos_var_get(state, var_node->token.sval);
                            if (!v) v = amos_var_set_int(state, var_node->token.sval, 0);
                            double result_val = (v->type == VAR_FLOAT ? v->fval : (double)v->ival) + to_number(addval);

                            /* Optional base To limit wraparound */
                            if (node->child_count >= 4) {
                                /* Find the To separator — skip it and get base and limit */
                                int ai = 2;
                                double base_val = 0, limit_val = 0;
                                /* children: var, addval, [base, TO, limit] or [base, limit] */
                                for (int i = 2; i < node->child_count; i++) {
                                    if (node->children[i]->type == NODE_COMMAND &&
                                        node->children[i]->token.type == TOK_TO) continue;
                                    if (ai == 2) {
                                        eval_result_t br = eval_node(state, node->children[i]);
                                        base_val = to_number(br);
                                        free_result(&br);
                                        ai = 3;
                                    } else {
                                        eval_result_t lr = eval_node(state, node->children[i]);
                                        limit_val = to_number(lr);
                                        free_result(&lr);
                                    }
                                }
                                /* Wraparound */
                                if (result_val > limit_val) result_val = base_val;
                                if (result_val < base_val) result_val = limit_val;
                            }

                            if (v->type == VAR_FLOAT) v->fval = result_val;
                            else v->ival = (int32_t)result_val;
                            free_result(&addval);
                        }
                    }
                    break;
                }

                case TOK_INC: {
                    /* Inc var — increment variable by 1 */
                    if (node->child_count >= 1) {
                        amos_node_t *var_node = node->children[0];
                        if (var_node && var_node->type == NODE_VARIABLE && var_node->token.sval) {
                            amos_var_t *v = amos_var_get(state, var_node->token.sval);
                            if (v) {
                                if (v->type == VAR_FLOAT) v->fval += 1.0;
                                else v->ival += 1;
                            } else {
                                amos_var_set_int(state, var_node->token.sval, 1);
                            }
                        }
                    }
                    break;
                }

                case TOK_DEC: {
                    /* Dec var — decrement variable by 1 */
                    if (node->child_count >= 1) {
                        amos_node_t *var_node = node->children[0];
                        if (var_node && var_node->type == NODE_VARIABLE && var_node->token.sval) {
                            amos_var_t *v = amos_var_get(state, var_node->token.sval);
                            if (v) {
                                if (v->type == VAR_FLOAT) v->fval -= 1.0;
                                else v->ival -= 1;
                            } else {
                                amos_var_set_int(state, var_node->token.sval, -1);
                            }
                        }
                    }
                    break;
                }

                case TOK_GET_SPRITE_PALETTE: {
                    /* Get Sprite Palette — copy sprite bank palette to current screen */
                    /* Stub: no-op for now (visual only) */
                    break;
                }

                case TOK_PEN: {
                    /* Pen colour — set text foreground colour */
                    if (node->child_count > 0) {
                        eval_result_t r = eval_node(state, node->children[0]);
                        amos_screen_t *scr = &state->screens[state->current_screen];
                        if (scr->active) scr->text_pen = to_int(r);
                        free_result(&r);
                    }
                    break;
                }

                case TOK_PAPER: {
                    /* Paper colour — set text background colour */
                    if (node->child_count > 0) {
                        eval_result_t r = eval_node(state, node->children[0]);
                        amos_screen_t *scr = &state->screens[state->current_screen];
                        if (scr->active) scr->text_paper = to_int(r);
                        free_result(&r);
                    }
                    break;
                }

                case TOK_HIDE:
                case TOK_SHOW:
                case TOK_FLASH_OFF:
                case TOK_FLASH:
                case TOK_CURS_OFF:
                case TOK_CURS_ON:
                case TOK_SET_PAINT:
                case TOK_APPEAR:
                case TOK_CLINE:
                case TOK_CLW:
                    /* Stubs: visual-only commands — no-op in headless */
                    break;

                case TOK_PASTE_BOB: {
                    /* Paste Bob image,x,y — paste a bob image (stub) */
                    break;
                }

                case TOK_CDOWN: {
                    /* Cdown — move cursor down one line */
                    amos_screen_t *scr = &state->screens[state->current_screen];
                    if (scr->active) scr->cursor_y++;
                    break;
                }

                case TOK_CUP: {
                    /* Cup — move cursor up one line */
                    amos_screen_t *scr = &state->screens[state->current_screen];
                    if (scr->active && scr->cursor_y > 0) scr->cursor_y--;
                    break;
                }

                case TOK_CLEFT: {
                    /* Cleft — move cursor left */
                    amos_screen_t *scr = &state->screens[state->current_screen];
                    if (scr->active && scr->cursor_x > 0) scr->cursor_x--;
                    break;
                }

                case TOK_CRIGHT: {
                    /* Cright — move cursor right */
                    amos_screen_t *scr = &state->screens[state->current_screen];
                    if (scr->active) scr->cursor_x++;
                    break;
                }

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

        case NODE_PROC_CALL: {
            /* Proc name[args] — find the matching Procedure definition and jump to it */
            const char *proc_name = node->token.sval;
            if (!proc_name) break;

            /* Scan program for matching Procedure name */
            int target = -1;
            for (int i = 0; i < state->line_count; i++) {
                /* Lazy-parse if needed */
                if (!state->lines[i].ast && state->lines[i].text) {
                    amos_token_list_t *tl = amos_tokenize(state->lines[i].text);
                    if (tl && tl->count > 0) {
                        int p = 0;
                        state->lines[i].ast = amos_parse_line(tl->tokens, &p, tl->count);
                    }
                    amos_token_list_free(tl);
                }
                amos_node_t *ln = state->lines[i].ast;
                if (ln && ln->type == NODE_PROCEDURE && ln->token.sval &&
                    strcasecmp(ln->token.sval, proc_name) == 0) {
                    target = i;
                    break;
                }
            }

            if (target >= 0 && state->gosub_top < AMOS_MAX_GOSUB_DEPTH) {
                /* Push return address */
                gosub_entry_t *ge = &state->gosub_stack[state->gosub_top++];
                ge->return_line = state->current_line;
                ge->return_pos = 0;
                /* Jump to the line after the Procedure definition */
                state->current_line = target + 1;
            } else if (target < 0) {
                snprintf(state->error_msg, sizeof(state->error_msg),
                         "Procedure not found: %s", proc_name);
                state->error_code = 24;
            }
            break;
        }

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

        case NODE_WHILE: {
            /* Evaluate condition — if false, skip to matching Wend */
            if (node->child_count > 0) {
                eval_result_t cond = eval_node(state, node->children[0]);
                bool is_true = to_int(cond) != 0;
                free_result(&cond);
                if (!is_true) {
                    int target = scan_to_wend(state, state->current_line);
                    state->current_line = target;
                }
                /* If true, continue to next line (the body) */
            }
            break;
        }

        case NODE_REPEAT:
            /* No-op — just mark the loop start point. Until handles the jump. */
            break;

        case NODE_RESTORE:
            state->data_line = 0;
            state->data_pos = 0;
            break;

        case NODE_DIM: {
            for (int i = 0; i < node->child_count; i++) {
                amos_node_t *arr_node = node->children[i];
                if (!arr_node || !arr_node->token.sval) continue;

                int dims[10] = {0};
                int ndims = 0;
                for (int j = 0; j < arr_node->child_count && ndims < 10; j++) {
                    eval_result_t d = eval_node(state, arr_node->children[j]);
                    dims[ndims++] = to_int(d);
                    free_result(&d);
                }
                array_create(state, arr_node->token.sval, dims, ndims);
            }
            break;
        }

        case NODE_SELECT: {
            /* Select expr — evaluate expression, then scan cases */
            if (node->child_count < 1) break;
            eval_result_t sel_val = eval_node(state, node->children[0]);

            /* Scan forward through Case / Default / End Select */
            bool found_match = false;
            for (int i = state->current_line + 1; i < state->line_count; i++) {
                amos_program_line_t *pl = &state->lines[i];
                if (!pl->ast && pl->text) {
                    amos_token_list_t *toks = amos_tokenize(pl->text);
                    if (toks && toks->count > 0) {
                        int p = 0;
                        pl->ast = amos_parse_line(toks->tokens, &p, toks->count);
                    }
                    amos_token_list_free(toks);
                }
                if (!pl->ast) continue;

                if (pl->ast->type == NODE_COMMAND && pl->ast->token.type == TOK_END_SELECT) {
                    /* No match found — jump past End Select */
                    state->current_line = i;
                    break;
                }

                if (pl->ast->type == NODE_COMMAND && pl->ast->token.type == TOK_DEFAULT) {
                    /* Default — always matches. Jump past Default line to body */
                    state->current_line = i + 1;
                    found_match = true;
                    break;
                }

                if (pl->ast->type == NODE_COMMAND && pl->ast->token.type == TOK_CASE) {
                    /* Check if any case value matches */
                    for (int c = 0; c < pl->ast->child_count; c++) {
                        eval_result_t case_val = eval_node(state, pl->ast->children[c]);
                        bool match_found = false;
                        if (sel_val.type == VAR_STRING && case_val.type == VAR_STRING) {
                            match_found = (strcmp(sel_val.sval, case_val.sval) == 0);
                        } else {
                            match_found = (to_number(sel_val) == to_number(case_val));
                        }
                        free_result(&case_val);
                        if (match_found) {
                            /* Jump past the Case line to the body */
                            state->current_line = i + 1;
                            found_match = true;
                            break;
                        }
                    }
                    if (found_match) break;
                }
            }
            free_result(&sel_val);
            break;
        }

        case NODE_ON_BRANCH: {
            /* On expr Goto/Gosub label1,label2,... */
            if (node->child_count < 2) break;
            eval_result_t idx_val = eval_node(state, node->children[0]);
            int idx = to_int(idx_val);
            free_result(&idx_val);

            /* AMOS On is 1-based: On 1 Goto L1,L2,L3 goes to L1 */
            if (idx >= 1 && idx < node->child_count) {
                amos_node_t *target_node = node->children[idx]; /* children[0]=expr, [1]=first target */

                int target_line = -1;
                if (target_node->type == NODE_VARIABLE && target_node->token.sval) {
                    target_line = find_label(state, target_node->token.sval);
                } else {
                    eval_result_t t = eval_node(state, target_node);
                    target_line = find_line_index(state, to_int(t));
                    free_result(&t);
                }

                if (target_line >= 0) {
                    if (node->token.type == TOK_GOSUB) {
                        /* Push return address */
                        if (state->gosub_top < AMOS_MAX_GOSUB_DEPTH) {
                            gosub_entry_t *ge = &state->gosub_stack[state->gosub_top++];
                            ge->return_line = state->current_line;
                            ge->return_pos = 0;
                        }
                    }
                    state->current_line = target_line;
                    state->current_pos = 0;
                }
            }
            break;
        }

        case NODE_EVERY: {
            /* Every n Gosub label / Every n Proc name */
            if (node->child_count >= 1) {
                eval_result_t interval = eval_node(state, node->children[0]);
                state->every_interval = to_int(interval);
                state->every_counter = state->every_interval;
                free_result(&interval);

                if (node->token.type == TOK_GOSUB && node->child_count >= 2) {
                    /* Every N Gosub label */
                    if (node->children[1]->type == NODE_VARIABLE && node->children[1]->token.sval) {
                        int target = find_label(state, node->children[1]->token.sval);
                        state->every_target_line = target;
                    } else {
                        eval_result_t t = eval_node(state, node->children[1]);
                        state->every_target_line = find_line_index(state, to_int(t));
                        free_result(&t);
                    }
                    state->every_target_proc[0] = '\0';
                } else if (node->token.type == TOK_PROC && node->child_count >= 2 &&
                           node->children[1]->token.sval) {
                    /* Every N Proc name */
                    strncpy(state->every_target_proc, node->children[1]->token.sval,
                            sizeof(state->every_target_proc) - 1);
                    state->every_target_line = -1;
                }

                if (state->every_interval == 0) {
                    /* Every 0 = disable */
                    state->every_target_line = -1;
                    state->every_target_proc[0] = '\0';
                }
            }
            break;
        }

        default:
            break;
    }
}

/* ── Execute a program line ──────────────────────────────────────── */

void amos_execute_line(amos_state_t *state, int line_index)
{
    if (line_index < 0 || line_index >= state->line_count) return;

    amos_program_line_t *pl = &state->lines[line_index];

    if (!pl->text) return;

    /* Tokenize the line fresh each time to support multi-statement lines.
     * The AST cache only stored the first statement; we now parse and
     * execute all colon-separated statements on the line. */
    amos_token_list_t *tokens = amos_tokenize(pl->text);
    if (!tokens || tokens->count == 0) {
        amos_token_list_free(tokens);
        return;
    }

    int pos = 0;
    int line_before = state->current_line;

    while (pos < tokens->count && tokens->tokens[pos].type != TOK_EOF && state->running) {
        /* Skip newlines and colons */
        if (tokens->tokens[pos].type == TOK_NEWLINE ||
            tokens->tokens[pos].type == TOK_COLON) {
            pos++;
            continue;
        }

        amos_node_t *node = amos_parse_line(tokens->tokens, &pos, tokens->count);
        if (node) {
            /* Also update the cached AST for the first statement (for block
             * scanning helpers that inspect pl->ast for If/While/etc.) */
            if (!pl->ast) {
                /* Cache only the first parsed node for scanning */
                pl->ast = node;
                amos_execute_node(state, node);
            } else {
                amos_execute_node(state, node);
                amos_node_free(node);
            }
        }

        /* If execution jumped elsewhere (Goto/Gosub/For/etc.), stop
         * processing further statements on this line */
        if (state->current_line != line_before) break;

        /* Skip colon separator if present */
        if (pos < tokens->count && tokens->tokens[pos].type == TOK_COLON) {
            pos++;
        }
    }

    amos_token_list_free(tokens);
}
