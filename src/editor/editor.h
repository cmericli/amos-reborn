/*
 * editor.h — AMOS Reborn integrated editor (dual-mode)
 *
 * Supports both the AMOS 1.3 "blue" editor (4-color) and
 * AMOS Professional "silver" editor (8-color), pixel-perfect.
 */

#ifndef AMOS_EDITOR_H
#define AMOS_EDITOR_H

#include "amos.h"

/* ── AMOS 1.3 Editor Palette ($RGB Amiga format) ───────────────── */
/* $017 boosted for modern displays: R:0x00, G:0x22, B:0x88        */

#define EDITOR13_COLOR_BLACK     AMOS_RGBA(0x00, 0x00, 0x00, 0xFF)  /* $000 — index 0 */
#define EDITOR13_COLOR_DARK_BLUE AMOS_RGBA(0x00, 0x22, 0x88, 0xFF)  /* $017 — index 1 */
#define EDITOR13_COLOR_CYAN      AMOS_RGBA(0x00, 0xEE, 0xCC, 0xFF)  /* $0EC — index 2 */
#define EDITOR13_COLOR_ORANGE    AMOS_RGBA(0xCC, 0x44, 0x00, 0xFF)  /* $C40 — index 3 */

/* ── AMOS Professional Editor Palette ($RGB Amiga format) ─────── */

#define EDITORPRO_COLOR_BLACK       AMOS_RGBA(0x00, 0x00, 0x00, 0xFF)  /* $000 — index 0 */
#define EDITORPRO_COLOR_BLUE        AMOS_RGBA(0x00, 0x66, 0xFF, 0xFF)  /* $06F — index 1 */
#define EDITORPRO_COLOR_TEAL        AMOS_RGBA(0x00, 0x77, 0x77, 0xFF)  /* $077 — index 2 */
#define EDITORPRO_COLOR_LIGHTGRAY   AMOS_RGBA(0xEE, 0xEE, 0xEE, 0xFF)  /* $EEE — index 3 */
#define EDITORPRO_COLOR_RED         AMOS_RGBA(0xFF, 0x00, 0x00, 0xFF)  /* $F00 — index 4 */
#define EDITORPRO_COLOR_BRIGHT_CYAN AMOS_RGBA(0x00, 0xDD, 0xDD, 0xFF)  /* $0DD — index 5 */
#define EDITORPRO_COLOR_MED_CYAN    AMOS_RGBA(0x00, 0xAA, 0xAA, 0xFF)  /* $0AA — index 6 */
#define EDITORPRO_COLOR_YELLOW      AMOS_RGBA(0xFF, 0xFF, 0x33, 0xFF)  /* $FF3 — index 7 */

/* ── Editor Dimensions ─────────────────────────────────────────── */

#define EDITOR_SCREEN_W   320
#define EDITOR_SCREEN_H   256
#define EDITOR_CHAR_W       8
#define EDITOR_CHAR_H       8
#define EDITOR_COLS        (EDITOR_SCREEN_W / EDITOR_CHAR_W)   /* 40 */
#define EDITOR_ROWS        (EDITOR_SCREEN_H / EDITOR_CHAR_H)   /* 32 */

/* ── AMOS 1.3 Layout ──────────────────────────────────────────── */
/* Row 0 = status bar, rows 1-29 = code, row 30 = direct mode, row 31 = F-key bar */

#define EDITOR13_STATUS_ROW     0
#define EDITOR13_CODE_TOP       1
#define EDITOR13_CODE_BOTTOM   29
#define EDITOR13_VISIBLE_LINES (EDITOR13_CODE_BOTTOM - EDITOR13_CODE_TOP + 1)  /* 29 */
#define EDITOR13_DIRECT_ROW    30
#define EDITOR13_FKEY_ROW      31

/* ── AMOS Pro Layout ──────────────────────────────────────────── */
/* Title bar: 2 rows (16px), code: rows 2-28, status: row 29, direct: row 30, fkey: row 31 */

#define EDITORPRO_TITLE_TOP      0
#define EDITORPRO_TITLE_ROWS     2    /* 16px = 2 rows of 8px */
#define EDITORPRO_CODE_TOP       2
#define EDITORPRO_CODE_BOTTOM   28
#define EDITORPRO_VISIBLE_LINES (EDITORPRO_CODE_BOTTOM - EDITORPRO_CODE_TOP + 1)  /* 27 */
#define EDITORPRO_STATUS_ROW    29
#define EDITORPRO_DIRECT_ROW    30
#define EDITORPRO_FKEY_ROW      31

/* ── Editor Limits ─────────────────────────────────────────────── */

#define EDITOR_MAX_LINES    4096
#define EDITOR_MAX_LINE_LEN  256
#define EDITOR_DIRECT_LEN    256
#define EDITOR_TAB_SIZE        4

/* ── Editor State ──────────────────────────────────────────────── */

typedef struct {
    bool active;                    /* true when editor is displayed */

    /* Source code buffer */
    char **lines;                   /* array of null-terminated strings */
    int line_count;                 /* number of lines in buffer */

    /* Cursor */
    int cursor_x;                   /* column in current line */
    int cursor_y;                   /* line index (absolute, not screen) */
    int scroll_y;                   /* first visible line index */

    /* Direct mode */
    char direct_line[EDITOR_DIRECT_LEN];
    int direct_cursor;
    bool direct_mode;               /* true = focus in direct mode bar */

    /* Edit state */
    bool insert_mode;               /* true = insert, false = overwrite */
    int blink_counter;              /* cursor blink frame counter */
    bool cursor_visible;            /* current blink state */
    bool show_line_numbers;         /* toggle line number margin */

    /* Status */
    char status_msg[80];
    int status_timer;               /* frames to display status message */

    /* File */
    char filename[256];
    bool modified;
} amos_editor_state_t;

/* ── Public API ────────────────────────────────────────────────── */

void amos_editor_init(amos_state_t *state);
void amos_editor_init_pro(amos_state_t *state);
void amos_editor_destroy(amos_state_t *state);
void amos_editor_tick(amos_state_t *state);
bool amos_editor_is_active(amos_state_t *state);
void amos_editor_render(amos_state_t *state);
void amos_editor_load_file(amos_state_t *state, const char *path);

/* Access the editor state (stored in a global — single editor instance) */
amos_editor_state_t *amos_editor_get_state(void);

#endif /* AMOS_EDITOR_H */
