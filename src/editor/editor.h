/*
 * editor.h — AMOS 1.3 integrated editor
 *
 * The iconic dark-blue editor screen with 4-color syntax highlighting.
 * Provides a self-contained editing mode for AMOS source code.
 */

#ifndef AMOS_EDITOR_H
#define AMOS_EDITOR_H

#include "amos.h"

/* ── AMOS 1.3 Editor Palette ($RGB Amiga format) ───────────────── */

#define EDITOR_COLOR_BLACK     AMOS_RGBA(0x00, 0x00, 0x00, 0xFF)  /* $000 */
#define EDITOR_COLOR_DARK_BLUE AMOS_RGBA(0x00, 0x22, 0x88, 0xFF)  /* $017 — authentic AMOS dark blue */
#define EDITOR_COLOR_CYAN      AMOS_RGBA(0x00, 0xEE, 0xCC, 0xFF)  /* $0EC */
#define EDITOR_COLOR_ORANGE    AMOS_RGBA(0xDD, 0x66, 0x00, 0xFF)  /* $C40 boosted */
#define EDITOR_COLOR_WHITE     AMOS_RGBA(0xFF, 0xFF, 0xFF, 0xFF)  /* status text */

/* ── Editor Dimensions ─────────────────────────────────────────── */

#define EDITOR_SCREEN_W   320
#define EDITOR_SCREEN_H   256
#define EDITOR_CHAR_W       8
#define EDITOR_CHAR_H       8
#define EDITOR_COLS        (EDITOR_SCREEN_W / EDITOR_CHAR_W)   /* 40 */
#define EDITOR_ROWS        (EDITOR_SCREEN_H / EDITOR_CHAR_H)   /* 32 */

/* Layout: row 0 = status bar, rows 1-29 = code, row 30 = direct mode, row 31 = F-key bar */
#define EDITOR_STATUS_ROW     0
#define EDITOR_CODE_TOP       1
#define EDITOR_CODE_BOTTOM   29
#define EDITOR_VISIBLE_LINES (EDITOR_CODE_BOTTOM - EDITOR_CODE_TOP + 1)  /* 29 */
#define EDITOR_DIRECT_ROW    30
#define EDITOR_FKEY_ROW      31

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

    /* Status */
    char status_msg[80];
    int status_timer;               /* frames to display status message */

    /* File */
    char filename[256];
    bool modified;
} amos_editor_state_t;

/* ── Public API ────────────────────────────────────────────────── */

void amos_editor_init(amos_state_t *state);
void amos_editor_destroy(amos_state_t *state);
void amos_editor_tick(amos_state_t *state);
bool amos_editor_is_active(amos_state_t *state);
void amos_editor_render(amos_state_t *state);

/* Access the editor state (stored in a global — single editor instance) */
amos_editor_state_t *amos_editor_get_state(void);

#endif /* AMOS_EDITOR_H */
