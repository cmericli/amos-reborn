/*
 * editor.c — AMOS Reborn integrated editor (dual-mode)
 *
 * Pixel-perfect recreation of both editor screens:
 *   - AMOS 1.3: 4-color dark blue editor (keywords=cyan, text=orange)
 *   - AMOS Pro: 8-color silver editor (teal background, gray text)
 *
 * Topaz 8×8 font, 320×256 screen, syntax-colored source display.
 * Dialect is read from state->dialect to choose rendering path.
 */

#include "editor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <SDL.h>

/* ── Single global editor state ────────────────────────────────── */

static amos_editor_state_t g_editor = {0};

amos_editor_state_t *amos_editor_get_state(void)
{
    return &g_editor;
}

/* ── AMOS keyword table (for syntax coloring) ──────────────────── */
/* Case-insensitive matching. Multi-word entries first. */

static const char *editor_keywords[] = {
    "Screen Open", "Screen Close", "Screen Display", "Screen Offset",
    "Screen Clone", "Screen Swap", "Screen Mode", "Screen Copy",
    "Screen To Front", "Screen To Back", "Screen Hide", "Screen Show",
    "Set Sprite Buffer", "Set Bob", "Set Font",
    "Sprite Off", "Sprite Col", "Bob Off", "Bob Col",
    "Get Sprite", "Get Bob", "Get Block", "Put Block", "Del Block",
    "Move X", "Move Y",
    "Sam Play", "Sam Raw", "Sam Loop On", "Sam Loop Off",
    "Track Play", "Track Stop", "Track Loop On",
    "Amal On", "Amal Off", "Amal Freeze",
    "End If", "End Proc", "Exit If",
    "Wait Vbl", "Wait Key",
    "Double Buffer", "Dual Playfield", "Gr Writing",
    "Def Scroll", "On Error Goto", "On Error Proc",
    "Line Input", "Open In", "Open Out",
    "Resume Next", "Resume Label",
    /* Single-word keywords */
    "Screen", "If", "Then", "Else", "For", "To", "Step", "Next",
    "While", "Wend", "Repeat", "Until", "Do", "Loop",
    "Goto", "Gosub", "Return", "On", "Exit", "Pop", "End", "Stop",
    "Procedure", "Proc", "Shared", "Global", "Local", "Param", "Fn",
    "Let", "Dim", "Data", "Read", "Restore", "Swap",
    "Print", "Input", "Locate", "Cls", "Home",
    "Ink", "Colour", "Palette", "Plot", "Draw", "Box", "Bar",
    "Circle", "Ellipse", "Paint", "Polygon", "Text",
    "Sprite", "Bob", "Anim",
    "Boom", "Shoot", "Bell", "Volume", "Music",
    "Amal", "Synchro",
    "Reserve", "Erase", "Load", "Save", "Bload", "Bsave",
    "Rainbow", "Copper", "Autoback",
    "Rem", "Wait", "Timer", "Randomize", "Mode",
    "And", "Or", "Not", "Xor", "Mod",
    "Scroll", "Append", "Close", "Kill", "Rename", "Mkdir",
    "Poke", "Trap", "Error", "Resume",
    NULL
};

/* ── Keyword matching (case-insensitive, word-boundary aware) ──── */

static bool editor_match_keyword(const char *src, const char *kw, int *consumed)
{
    int i = 0;
    while (kw[i]) {
        if (tolower((unsigned char)src[i]) != tolower((unsigned char)kw[i]))
            return false;
        i++;
    }
    /* Must end at a word boundary */
    if (isalnum((unsigned char)src[i]) || src[i] == '_')
        return false;
    *consumed = i;
    return true;
}

/* Check if position in line is the start of a keyword.
 * Returns length of keyword if matched, 0 otherwise. */
static int editor_check_keyword(const char *text)
{
    for (int k = 0; editor_keywords[k]; k++) {
        int consumed = 0;
        if (editor_match_keyword(text, editor_keywords[k], &consumed))
            return consumed;
    }
    return 0;
}

/* Check if keyword is "Rem" */
static bool editor_is_rem(const char *text, int kw_len)
{
    return (kw_len == 3 &&
            tolower((unsigned char)text[0]) == 'r' &&
            tolower((unsigned char)text[1]) == 'e' &&
            tolower((unsigned char)text[2]) == 'm');
}

/* ── Line buffer management ────────────────────────────────────── */

static char *line_strdup(const char *s)
{
    if (!s) s = "";
    return strdup(s);
}

static void editor_ensure_lines(void)
{
    if (g_editor.line_count == 0) {
        g_editor.lines = calloc(EDITOR_MAX_LINES, sizeof(char *));
        g_editor.lines[0] = line_strdup("");
        g_editor.line_count = 1;
    }
}

static void editor_insert_line(int at, const char *text)
{
    if (g_editor.line_count >= EDITOR_MAX_LINES) return;
    if (at < 0) at = 0;
    if (at > g_editor.line_count) at = g_editor.line_count;

    /* Shift lines down */
    memmove(&g_editor.lines[at + 1], &g_editor.lines[at],
            (g_editor.line_count - at) * sizeof(char *));
    g_editor.lines[at] = line_strdup(text);
    g_editor.line_count++;
}

static void editor_delete_line(int at)
{
    if (at < 0 || at >= g_editor.line_count) return;
    if (g_editor.line_count <= 1) {
        /* Keep at least one line */
        free(g_editor.lines[0]);
        g_editor.lines[0] = line_strdup("");
        return;
    }

    free(g_editor.lines[at]);
    memmove(&g_editor.lines[at], &g_editor.lines[at + 1],
            (g_editor.line_count - at - 1) * sizeof(char *));
    g_editor.line_count--;
    g_editor.lines[g_editor.line_count] = NULL;
}

/* ── Pixel drawing helpers (direct to screen 0 pixels) ─────────── */

static inline void editor_put_pixel(amos_screen_t *scr, int x, int y, uint32_t color)
{
    if (x >= 0 && x < scr->width && y >= 0 && y < scr->height)
        scr->pixels[y * scr->width + x] = color;
}

static void editor_fill_rect(amos_screen_t *scr, int x1, int y1, int x2, int y2,
                              uint32_t color)
{
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++)
            editor_put_pixel(scr, x, y, color);
}

static void editor_draw_char(amos_screen_t *scr, int px, int py,
                              unsigned char ch, uint32_t fg, uint32_t bg)
{
    for (int row = 0; row < 8; row++) {
        uint8_t bits = topaz_font_8x8[ch][row];
        for (int col = 0; col < 8; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
            editor_put_pixel(scr, px + col, py + row, color);
        }
    }
}

static void editor_draw_string(amos_screen_t *scr, int col, int row,
                                const char *text, uint32_t fg, uint32_t bg)
{
    int px = col * EDITOR_CHAR_W;
    int py = row * EDITOR_CHAR_H;
    for (const char *p = text; *p && col < EDITOR_COLS; p++, col++) {
        editor_draw_char(scr, px, py, (unsigned char)*p, fg, bg);
        px += EDITOR_CHAR_W;
    }
}

/* Fill remaining columns on a row with a color */
static void editor_fill_row_from(amos_screen_t *scr, int col, int row, uint32_t color)
{
    int px = col * EDITOR_CHAR_W;
    int py = row * EDITOR_CHAR_H;
    editor_fill_rect(scr, px, py, EDITOR_SCREEN_W - 1, py + EDITOR_CHAR_H - 1, color);
}

/* ── Layout helpers (dialect-dependent) ────────────────────────── */

static int editor_visible_lines(amos_dialect_t d)
{
    return (d == AMOS_DIALECT_PRO) ? EDITORPRO_VISIBLE_LINES : EDITOR13_VISIBLE_LINES;
}

/* ── Scroll management ─────────────────────────────────────────── */

static void editor_ensure_cursor_visible(amos_dialect_t d)
{
    int vis = editor_visible_lines(d);
    if (g_editor.cursor_y < g_editor.scroll_y)
        g_editor.scroll_y = g_editor.cursor_y;
    if (g_editor.cursor_y >= g_editor.scroll_y + vis)
        g_editor.scroll_y = g_editor.cursor_y - vis + 1;
    if (g_editor.scroll_y < 0) g_editor.scroll_y = 0;
}

static void editor_clamp_cursor(void)
{
    if (g_editor.cursor_y < 0) g_editor.cursor_y = 0;
    if (g_editor.cursor_y >= g_editor.line_count)
        g_editor.cursor_y = g_editor.line_count - 1;

    int len = (int)strlen(g_editor.lines[g_editor.cursor_y]);
    if (g_editor.cursor_x > len) g_editor.cursor_x = len;
    if (g_editor.cursor_x < 0) g_editor.cursor_x = 0;
}

/* ── Rendering: AMOS 1.3 "Blue" Editor ─────────────────────────── */

/* Render one source line with AMOS 1.3 syntax coloring:
 *   Keywords = cyan ($0EC), everything else = orange ($C40)
 *   Rem lines = all cyan after the keyword
 *   Strings = orange
 */
static void editor_render_source_line_13(amos_screen_t *scr, int screen_row,
                                          const char *text, int line_num)
{
    int py = screen_row * EDITOR_CHAR_H;
    int col = 0;
    const char *p = text;

    /* Optional line number margin */
    if (g_editor.show_line_numbers) {
        char numstr[6];
        snprintf(numstr, sizeof(numstr), "%4d ", line_num + 1);
        for (int i = 0; numstr[i] && col < EDITOR_COLS; i++, col++) {
            editor_draw_char(scr, col * EDITOR_CHAR_W, py,
                             (unsigned char)numstr[i],
                             EDITOR13_COLOR_CYAN, EDITOR13_COLOR_DARK_BLUE);
        }
    }

    while (*p && col < EDITOR_COLS) {
        /* Skip spaces — draw as dark blue background */
        if (*p == ' ' || *p == '\t') {
            int spaces = (*p == '\t') ? (EDITOR_TAB_SIZE - (col % EDITOR_TAB_SIZE)) : 1;
            for (int s = 0; s < spaces && col < EDITOR_COLS; s++, col++) {
                editor_draw_char(scr, col * EDITOR_CHAR_W, py, ' ',
                                 EDITOR13_COLOR_ORANGE, EDITOR13_COLOR_DARK_BLUE);
            }
            p++;
            continue;
        }

        /* Check for keyword at word boundary */
        if (col == 0 || *(p - 1) == ' ' || *(p - 1) == '\t' || *(p - 1) == ':') {
            int kw_len = editor_check_keyword(p);
            if (kw_len > 0) {
                bool is_rem = editor_is_rem(p, kw_len);

                /* Draw keyword in cyan */
                for (int i = 0; i < kw_len && col < EDITOR_COLS; i++, col++) {
                    editor_draw_char(scr, col * EDITOR_CHAR_W, py,
                                     (unsigned char)p[i],
                                     EDITOR13_COLOR_CYAN, EDITOR13_COLOR_DARK_BLUE);
                }
                p += kw_len;

                if (is_rem) {
                    /* Rest of line is comment — all cyan */
                    while (*p && col < EDITOR_COLS) {
                        editor_draw_char(scr, col * EDITOR_CHAR_W, py,
                                         (unsigned char)*p,
                                         EDITOR13_COLOR_CYAN, EDITOR13_COLOR_DARK_BLUE);
                        col++;
                        p++;
                    }
                }
                continue;
            }
        }

        /* String literal — draw in orange */
        if (*p == '"') {
            editor_draw_char(scr, col * EDITOR_CHAR_W, py, '"',
                             EDITOR13_COLOR_ORANGE, EDITOR13_COLOR_DARK_BLUE);
            col++;
            p++;
            while (*p && *p != '"' && col < EDITOR_COLS) {
                editor_draw_char(scr, col * EDITOR_CHAR_W, py,
                                 (unsigned char)*p,
                                 EDITOR13_COLOR_ORANGE, EDITOR13_COLOR_DARK_BLUE);
                col++;
                p++;
            }
            if (*p == '"') {
                editor_draw_char(scr, col * EDITOR_CHAR_W, py, '"',
                                 EDITOR13_COLOR_ORANGE, EDITOR13_COLOR_DARK_BLUE);
                col++;
                p++;
            }
            continue;
        }

        /* Default: orange for identifiers, numbers, operators */
        editor_draw_char(scr, col * EDITOR_CHAR_W, py,
                         (unsigned char)*p,
                         EDITOR13_COLOR_ORANGE, EDITOR13_COLOR_DARK_BLUE);
        col++;
        p++;
    }

    /* Fill remaining columns with dark blue */
    editor_fill_row_from(scr, col, screen_row, EDITOR13_COLOR_DARK_BLUE);
}

static void editor_render_13(amos_state_t *state)
{
    amos_screen_t *scr = &state->screens[0];
    if (!scr->active) return;

    /* Fill entire screen with dark blue (the authentic AMOS look) */
    for (int i = 0; i < scr->width * scr->height; i++)
        scr->pixels[i] = EDITOR13_COLOR_DARK_BLUE;

    /* ── Status bar (row 0): black bg, cyan/orange text ──── */
    editor_fill_rect(scr, 0, 0, EDITOR_SCREEN_W - 1, EDITOR_CHAR_H - 1,
                     EDITOR13_COLOR_BLACK);

    char status[EDITOR_COLS + 1];
    if (g_editor.status_timer > 0) {
        snprintf(status, sizeof(status), " %s", g_editor.status_msg);
    } else {
        const char *name = g_editor.filename[0] ? g_editor.filename : "Untitled";
        snprintf(status, sizeof(status), " AMOS Reborn - %s%s  L:%d C:%d",
                 name,
                 g_editor.modified ? "*" : "",
                 g_editor.cursor_y + 1, g_editor.cursor_x + 1);
    }
    status[EDITOR_COLS] = '\0';
    editor_draw_string(scr, 0, EDITOR13_STATUS_ROW, status,
                       EDITOR13_COLOR_CYAN, EDITOR13_COLOR_BLACK);

    /* ── Source code area (rows 1-29) ──── */
    for (int row = EDITOR13_CODE_TOP; row <= EDITOR13_CODE_BOTTOM; row++) {
        int line_idx = g_editor.scroll_y + (row - EDITOR13_CODE_TOP);
        if (line_idx < g_editor.line_count) {
            editor_render_source_line_13(scr, row, g_editor.lines[line_idx], line_idx);
        } else {
            editor_fill_rect(scr, 0, row * EDITOR_CHAR_H,
                             EDITOR_SCREEN_W - 1,
                             row * EDITOR_CHAR_H + EDITOR_CHAR_H - 1,
                             EDITOR13_COLOR_DARK_BLUE);
        }
    }

    /* ── Cursor (blinking block — authentic AMOS style) ──── */
    if (g_editor.cursor_visible && !g_editor.direct_mode && g_editor.line_count > 0) {
        int screen_row = EDITOR13_CODE_TOP + (g_editor.cursor_y - g_editor.scroll_y);
        if (screen_row >= EDITOR13_CODE_TOP && screen_row <= EDITOR13_CODE_BOTTOM) {
            int cx = g_editor.cursor_x;
            if (g_editor.show_line_numbers) cx += 5;  /* offset for line number margin */
            if (cx > EDITOR_COLS - 1) cx = EDITOR_COLS - 1;
            int px = cx * EDITOR_CHAR_W;
            int py = screen_row * EDITOR_CHAR_H;

            /* Block cursor: cyan block */
            editor_fill_rect(scr, px, py,
                             px + EDITOR_CHAR_W - 1,
                             py + EDITOR_CHAR_H - 1,
                             EDITOR13_COLOR_CYAN);
            /* Draw character under cursor inverted */
            const char *line = g_editor.lines[g_editor.cursor_y];
            int len = (int)strlen(line);
            unsigned char ch = (g_editor.cursor_x < len)
                ? (unsigned char)line[g_editor.cursor_x] : ' ';
            editor_draw_char(scr, px, py, ch,
                             EDITOR13_COLOR_DARK_BLUE, EDITOR13_COLOR_CYAN);
        }
    }

    /* ── Direct mode bar (row 30) ──── */
    int drow = EDITOR13_DIRECT_ROW;
    editor_fill_rect(scr, 0, drow * EDITOR_CHAR_H,
                     EDITOR_SCREEN_W - 1,
                     drow * EDITOR_CHAR_H + EDITOR_CHAR_H - 1,
                     EDITOR13_COLOR_BLACK);

    /* Draw ">" prompt and direct mode text */
    editor_draw_char(scr, 0, drow * EDITOR_CHAR_H, '>',
                     EDITOR13_COLOR_CYAN, EDITOR13_COLOR_BLACK);
    {
        int px = EDITOR_CHAR_W;
        int py = drow * EDITOR_CHAR_H;
        for (int i = 0; g_editor.direct_line[i] && i < EDITOR_COLS - 1; i++) {
            editor_draw_char(scr, px, py, (unsigned char)g_editor.direct_line[i],
                             EDITOR13_COLOR_ORANGE, EDITOR13_COLOR_BLACK);
            px += EDITOR_CHAR_W;
        }
    }

    /* ── Direct mode cursor ──── */
    if (g_editor.cursor_visible && g_editor.direct_mode) {
        int px = (g_editor.direct_cursor + 1) * EDITOR_CHAR_W;
        int py = drow * EDITOR_CHAR_H;
        editor_fill_rect(scr, px, py,
                         px + EDITOR_CHAR_W - 1,
                         py + EDITOR_CHAR_H - 1,
                         EDITOR13_COLOR_CYAN);
    }

    /* ── Function key bar (row 31) ──── */
    int frow = EDITOR13_FKEY_ROW;
    editor_fill_rect(scr, 0, frow * EDITOR_CHAR_H,
                     EDITOR_SCREEN_W - 1,
                     frow * EDITOR_CHAR_H + EDITOR_CHAR_H - 1,
                     EDITOR13_COLOR_BLACK);

    /* F-key labels */
    static const struct { int col; const char *label; } fkeys13[] = {
        { 0, "F1Run" },
        { 5, "F2Tst" },
        {10, "F5Sav" },
        {15, "F7Ins" },
        {20, "F8Ovr" },
        {32, "F10Qut"},
    };
    for (int i = 0; i < (int)(sizeof(fkeys13) / sizeof(fkeys13[0])); i++) {
        const char *lbl = fkeys13[i].label;
        int c = fkeys13[i].col;
        int py = frow * EDITOR_CHAR_H;
        /* "Fn" part in cyan */
        int j = 0;
        while (lbl[j] && ((lbl[j] >= '0' && lbl[j] <= '9') || lbl[j] == 'F') && c < EDITOR_COLS) {
            editor_draw_char(scr, c * EDITOR_CHAR_W, py,
                             (unsigned char)lbl[j],
                             EDITOR13_COLOR_CYAN, EDITOR13_COLOR_BLACK);
            c++;
            j++;
        }
        /* Label part in orange */
        while (lbl[j] && c < EDITOR_COLS) {
            editor_draw_char(scr, c * EDITOR_CHAR_W, py,
                             (unsigned char)lbl[j],
                             EDITOR13_COLOR_ORANGE, EDITOR13_COLOR_BLACK);
            c++;
            j++;
        }
    }
}

/* ── Rendering: AMOS Professional "Silver" Editor ──────────────── */

/* Render one source line with AMOS Pro syntax coloring:
 *   Keywords = bright cyan ($0DD), comments = medium cyan ($0AA),
 *   Strings = yellow ($FF3), normal text = light gray ($EEE)
 *   All on teal ($077) background
 */
static void editor_render_source_line_pro(amos_screen_t *scr, int screen_row,
                                           const char *text, int line_num)
{
    int py = screen_row * EDITOR_CHAR_H;
    int col = 0;
    const char *p = text;

    /* Optional line number margin */
    if (g_editor.show_line_numbers) {
        char numstr[6];
        snprintf(numstr, sizeof(numstr), "%4d ", line_num + 1);
        for (int i = 0; numstr[i] && col < EDITOR_COLS; i++, col++) {
            editor_draw_char(scr, col * EDITOR_CHAR_W, py,
                             (unsigned char)numstr[i],
                             EDITORPRO_COLOR_MED_CYAN, EDITORPRO_COLOR_TEAL);
        }
    }

    while (*p && col < EDITOR_COLS) {
        /* Skip spaces — draw as teal background */
        if (*p == ' ' || *p == '\t') {
            int spaces = (*p == '\t') ? (EDITOR_TAB_SIZE - (col % EDITOR_TAB_SIZE)) : 1;
            for (int s = 0; s < spaces && col < EDITOR_COLS; s++, col++) {
                editor_draw_char(scr, col * EDITOR_CHAR_W, py, ' ',
                                 EDITORPRO_COLOR_LIGHTGRAY, EDITORPRO_COLOR_TEAL);
            }
            p++;
            continue;
        }

        /* Check for keyword at word boundary */
        if (col == 0 || *(p - 1) == ' ' || *(p - 1) == '\t' || *(p - 1) == ':') {
            int kw_len = editor_check_keyword(p);
            if (kw_len > 0) {
                bool is_rem = editor_is_rem(p, kw_len);

                /* Draw keyword in bright cyan */
                for (int i = 0; i < kw_len && col < EDITOR_COLS; i++, col++) {
                    editor_draw_char(scr, col * EDITOR_CHAR_W, py,
                                     (unsigned char)p[i],
                                     EDITORPRO_COLOR_BRIGHT_CYAN, EDITORPRO_COLOR_TEAL);
                }
                p += kw_len;

                if (is_rem) {
                    /* Rest of line is comment — medium cyan */
                    while (*p && col < EDITOR_COLS) {
                        editor_draw_char(scr, col * EDITOR_CHAR_W, py,
                                         (unsigned char)*p,
                                         EDITORPRO_COLOR_MED_CYAN, EDITORPRO_COLOR_TEAL);
                        col++;
                        p++;
                    }
                }
                continue;
            }
        }

        /* String literal — draw in yellow */
        if (*p == '"') {
            editor_draw_char(scr, col * EDITOR_CHAR_W, py, '"',
                             EDITORPRO_COLOR_YELLOW, EDITORPRO_COLOR_TEAL);
            col++;
            p++;
            while (*p && *p != '"' && col < EDITOR_COLS) {
                editor_draw_char(scr, col * EDITOR_CHAR_W, py,
                                 (unsigned char)*p,
                                 EDITORPRO_COLOR_YELLOW, EDITORPRO_COLOR_TEAL);
                col++;
                p++;
            }
            if (*p == '"') {
                editor_draw_char(scr, col * EDITOR_CHAR_W, py, '"',
                                 EDITORPRO_COLOR_YELLOW, EDITORPRO_COLOR_TEAL);
                col++;
                p++;
            }
            continue;
        }

        /* Default: light gray for identifiers, numbers, operators */
        editor_draw_char(scr, col * EDITOR_CHAR_W, py,
                         (unsigned char)*p,
                         EDITORPRO_COLOR_LIGHTGRAY, EDITORPRO_COLOR_TEAL);
        col++;
        p++;
    }

    /* Fill remaining columns with teal */
    editor_fill_row_from(scr, col, screen_row, EDITORPRO_COLOR_TEAL);
}

static void editor_render_pro(amos_state_t *state)
{
    amos_screen_t *scr = &state->screens[0];
    if (!scr->active) return;

    /* Fill entire screen with teal (code area background) */
    for (int i = 0; i < scr->width * scr->height; i++)
        scr->pixels[i] = EDITORPRO_COLOR_TEAL;

    /* ── Title bar (rows 0-1, 16px): black background ──── */
    editor_fill_rect(scr, 0, 0, EDITOR_SCREEN_W - 1,
                     EDITORPRO_TITLE_ROWS * EDITOR_CHAR_H - 1,
                     EDITORPRO_COLOR_BLACK);

    /* "AMOS Professional" in blue on black, centered vertically in title */
    editor_draw_string(scr, 1, 0, "AMOS Professional",
                       EDITORPRO_COLOR_BLUE, EDITORPRO_COLOR_BLACK);

    /* Memory indicator on the right side of title bar */
    char mem_str[20];
    snprintf(mem_str, sizeof(mem_str), "Free:%dK",
             (EDITOR_MAX_LINES - g_editor.line_count) * EDITOR_MAX_LINE_LEN / 1024);
    int mem_col = EDITOR_COLS - (int)strlen(mem_str) - 1;
    if (mem_col > 0) {
        editor_draw_string(scr, mem_col, 0, mem_str,
                           EDITORPRO_COLOR_LIGHTGRAY, EDITORPRO_COLOR_BLACK);
    }

    /* Second row of title: thin separator line */
    {
        int py = EDITOR_CHAR_H;
        for (int x = 0; x < EDITOR_SCREEN_W; x++) {
            editor_put_pixel(scr, x, py, EDITORPRO_COLOR_BLUE);
            editor_put_pixel(scr, x, py + 1, EDITORPRO_COLOR_BLUE);
        }
        /* Fill rest of second title row with black */
        editor_fill_rect(scr, 0, py + 2, EDITOR_SCREEN_W - 1,
                         py + EDITOR_CHAR_H - 1, EDITORPRO_COLOR_BLACK);
    }

    /* ── Source code area (rows 2-28) ──── */
    for (int row = EDITORPRO_CODE_TOP; row <= EDITORPRO_CODE_BOTTOM; row++) {
        int line_idx = g_editor.scroll_y + (row - EDITORPRO_CODE_TOP);
        if (line_idx < g_editor.line_count) {
            editor_render_source_line_pro(scr, row, g_editor.lines[line_idx], line_idx);
        } else {
            editor_fill_rect(scr, 0, row * EDITOR_CHAR_H,
                             EDITOR_SCREEN_W - 1,
                             row * EDITOR_CHAR_H + EDITOR_CHAR_H - 1,
                             EDITORPRO_COLOR_TEAL);
        }
    }

    /* ── Cursor (blinking block — Pro style: light gray block) ──── */
    if (g_editor.cursor_visible && !g_editor.direct_mode && g_editor.line_count > 0) {
        int screen_row = EDITORPRO_CODE_TOP + (g_editor.cursor_y - g_editor.scroll_y);
        if (screen_row >= EDITORPRO_CODE_TOP && screen_row <= EDITORPRO_CODE_BOTTOM) {
            int cx = g_editor.cursor_x;
            if (g_editor.show_line_numbers) cx += 5;
            if (cx > EDITOR_COLS - 1) cx = EDITOR_COLS - 1;
            int px = cx * EDITOR_CHAR_W;
            int py = screen_row * EDITOR_CHAR_H;

            /* Block cursor: light gray block */
            editor_fill_rect(scr, px, py,
                             px + EDITOR_CHAR_W - 1,
                             py + EDITOR_CHAR_H - 1,
                             EDITORPRO_COLOR_LIGHTGRAY);
            /* Draw character under cursor inverted */
            const char *line = g_editor.lines[g_editor.cursor_y];
            int len = (int)strlen(line);
            unsigned char ch = (g_editor.cursor_x < len)
                ? (unsigned char)line[g_editor.cursor_x] : ' ';
            editor_draw_char(scr, px, py, ch,
                             EDITORPRO_COLOR_TEAL, EDITORPRO_COLOR_LIGHTGRAY);
        }
    }

    /* ── Status bar (row 29): black bg ──── */
    int srow = EDITORPRO_STATUS_ROW;
    editor_fill_rect(scr, 0, srow * EDITOR_CHAR_H,
                     EDITOR_SCREEN_W - 1,
                     srow * EDITOR_CHAR_H + EDITOR_CHAR_H - 1,
                     EDITORPRO_COLOR_BLACK);

    {
        char status[EDITOR_COLS + 1];
        if (g_editor.status_timer > 0) {
            snprintf(status, sizeof(status), " %s", g_editor.status_msg);
            editor_draw_string(scr, 0, srow, status,
                               EDITORPRO_COLOR_YELLOW, EDITORPRO_COLOR_BLACK);
        } else {
            const char *name = g_editor.filename[0] ? g_editor.filename : "Untitled";
            snprintf(status, sizeof(status), " W1 %s%s  L:%d C:%d %s",
                     name,
                     g_editor.modified ? "*" : "",
                     g_editor.cursor_y + 1, g_editor.cursor_x + 1,
                     g_editor.insert_mode ? "INS" : "OVR");
            status[EDITOR_COLS] = '\0';
            editor_draw_string(scr, 0, srow, status,
                               EDITORPRO_COLOR_LIGHTGRAY, EDITORPRO_COLOR_BLACK);
        }
    }

    /* ── Direct mode bar (row 30) ──── */
    int drow = EDITORPRO_DIRECT_ROW;
    editor_fill_rect(scr, 0, drow * EDITOR_CHAR_H,
                     EDITOR_SCREEN_W - 1,
                     drow * EDITOR_CHAR_H + EDITOR_CHAR_H - 1,
                     EDITORPRO_COLOR_BLACK);

    editor_draw_char(scr, 0, drow * EDITOR_CHAR_H, '>',
                     EDITORPRO_COLOR_BRIGHT_CYAN, EDITORPRO_COLOR_BLACK);
    {
        int px = EDITOR_CHAR_W;
        int py = drow * EDITOR_CHAR_H;
        for (int i = 0; g_editor.direct_line[i] && i < EDITOR_COLS - 1; i++) {
            editor_draw_char(scr, px, py, (unsigned char)g_editor.direct_line[i],
                             EDITORPRO_COLOR_LIGHTGRAY, EDITORPRO_COLOR_BLACK);
            px += EDITOR_CHAR_W;
        }
    }

    /* ── Direct mode cursor ──── */
    if (g_editor.cursor_visible && g_editor.direct_mode) {
        int px = (g_editor.direct_cursor + 1) * EDITOR_CHAR_W;
        int py = drow * EDITOR_CHAR_H;
        editor_fill_rect(scr, px, py,
                         px + EDITOR_CHAR_W - 1,
                         py + EDITOR_CHAR_H - 1,
                         EDITORPRO_COLOR_BRIGHT_CYAN);
    }

    /* ── Function key bar (row 31) ──── */
    int frow = EDITORPRO_FKEY_ROW;
    editor_fill_rect(scr, 0, frow * EDITOR_CHAR_H,
                     EDITOR_SCREEN_W - 1,
                     frow * EDITOR_CHAR_H + EDITOR_CHAR_H - 1,
                     EDITORPRO_COLOR_BLACK);

    static const struct { int col; const char *label; } fkeyspro[] = {
        { 0, "F1Run" },
        { 5, "F2Tst" },
        {10, "F5Sav" },
        {15, "F8I/O" },
        {20, "F9Fld" },
        {32, "F10Qut"},
    };
    for (int i = 0; i < (int)(sizeof(fkeyspro) / sizeof(fkeyspro[0])); i++) {
        const char *lbl = fkeyspro[i].label;
        int c = fkeyspro[i].col;
        int py = frow * EDITOR_CHAR_H;
        int j = 0;
        /* "Fn" part in blue */
        while (lbl[j] && ((lbl[j] >= '0' && lbl[j] <= '9') || lbl[j] == 'F') && c < EDITOR_COLS) {
            editor_draw_char(scr, c * EDITOR_CHAR_W, py,
                             (unsigned char)lbl[j],
                             EDITORPRO_COLOR_BLUE, EDITORPRO_COLOR_BLACK);
            c++;
            j++;
        }
        /* Label part in light gray */
        while (lbl[j] && c < EDITOR_COLS) {
            editor_draw_char(scr, c * EDITOR_CHAR_W, py,
                             (unsigned char)lbl[j],
                             EDITORPRO_COLOR_LIGHTGRAY, EDITORPRO_COLOR_BLACK);
            c++;
            j++;
        }
    }
}

/* ── Public render dispatch ────────────────────────────────────── */

void amos_editor_render(amos_state_t *state)
{
    if (state->dialect == AMOS_DIALECT_PRO) {
        editor_render_pro(state);
    } else {
        editor_render_13(state);
    }
}

/* ── Set palette on screen for current dialect ─────────────────── */

static void editor_set_palette(amos_state_t *state)
{
    amos_screen_t *scr = &state->screens[0];
    if (state->dialect == AMOS_DIALECT_PRO) {
        scr->palette[0] = EDITORPRO_COLOR_BLACK;
        scr->palette[1] = EDITORPRO_COLOR_BLUE;
        scr->palette[2] = EDITORPRO_COLOR_TEAL;
        scr->palette[3] = EDITORPRO_COLOR_LIGHTGRAY;
        scr->palette[4] = EDITORPRO_COLOR_RED;
        scr->palette[5] = EDITORPRO_COLOR_BRIGHT_CYAN;
        scr->palette[6] = EDITORPRO_COLOR_MED_CYAN;
        scr->palette[7] = EDITORPRO_COLOR_YELLOW;
    } else {
        scr->palette[0] = EDITOR13_COLOR_BLACK;
        scr->palette[1] = EDITOR13_COLOR_DARK_BLUE;
        scr->palette[2] = EDITOR13_COLOR_CYAN;
        scr->palette[3] = EDITOR13_COLOR_ORANGE;
    }
}

/* ── Input handling ────────────────────────────────────────────── */

static void editor_insert_char_at_cursor(char ch)
{
    char *line = g_editor.lines[g_editor.cursor_y];
    int len = (int)strlen(line);

    if (len >= EDITOR_MAX_LINE_LEN - 1) return;

    char *newline = malloc(len + 2);
    if (g_editor.insert_mode || g_editor.cursor_x >= len) {
        /* Insert mode: shift right */
        memcpy(newline, line, g_editor.cursor_x);
        newline[g_editor.cursor_x] = ch;
        strcpy(newline + g_editor.cursor_x + 1, line + g_editor.cursor_x);
    } else {
        /* Overwrite mode: replace character */
        strcpy(newline, line);
        newline[g_editor.cursor_x] = ch;
    }

    free(g_editor.lines[g_editor.cursor_y]);
    g_editor.lines[g_editor.cursor_y] = newline;
    g_editor.cursor_x++;
    g_editor.modified = true;
}

static void editor_handle_backspace(void)
{
    if (g_editor.cursor_x > 0) {
        char *line = g_editor.lines[g_editor.cursor_y];
        int len = (int)strlen(line);
        char *newline = malloc(len);
        memcpy(newline, line, g_editor.cursor_x - 1);
        strcpy(newline + g_editor.cursor_x - 1, line + g_editor.cursor_x);
        free(g_editor.lines[g_editor.cursor_y]);
        g_editor.lines[g_editor.cursor_y] = newline;
        g_editor.cursor_x--;
        g_editor.modified = true;
    } else if (g_editor.cursor_y > 0) {
        /* Join with previous line */
        char *prev = g_editor.lines[g_editor.cursor_y - 1];
        char *curr = g_editor.lines[g_editor.cursor_y];
        int prev_len = (int)strlen(prev);
        char *joined = malloc(prev_len + strlen(curr) + 1);
        strcpy(joined, prev);
        strcat(joined, curr);
        free(g_editor.lines[g_editor.cursor_y - 1]);
        g_editor.lines[g_editor.cursor_y - 1] = joined;
        editor_delete_line(g_editor.cursor_y);
        g_editor.cursor_y--;
        g_editor.cursor_x = prev_len;
        g_editor.modified = true;
    }
}

static void editor_handle_delete(void)
{
    char *line = g_editor.lines[g_editor.cursor_y];
    int len = (int)strlen(line);

    if (g_editor.cursor_x < len) {
        char *newline = malloc(len);
        memcpy(newline, line, g_editor.cursor_x);
        strcpy(newline + g_editor.cursor_x, line + g_editor.cursor_x + 1);
        free(g_editor.lines[g_editor.cursor_y]);
        g_editor.lines[g_editor.cursor_y] = newline;
        g_editor.modified = true;
    } else if (g_editor.cursor_y < g_editor.line_count - 1) {
        /* Join with next line */
        char *curr = g_editor.lines[g_editor.cursor_y];
        char *next_line = g_editor.lines[g_editor.cursor_y + 1];
        char *joined = malloc(strlen(curr) + strlen(next_line) + 1);
        strcpy(joined, curr);
        strcat(joined, next_line);
        free(g_editor.lines[g_editor.cursor_y]);
        g_editor.lines[g_editor.cursor_y] = joined;
        editor_delete_line(g_editor.cursor_y + 1);
        g_editor.modified = true;
    }
}

static void editor_handle_return(void)
{
    char *line = g_editor.lines[g_editor.cursor_y];

    /* Split line at cursor */
    char *remainder = line_strdup(line + g_editor.cursor_x);
    char *first_part = malloc(g_editor.cursor_x + 1);
    memcpy(first_part, line, g_editor.cursor_x);
    first_part[g_editor.cursor_x] = '\0';

    free(g_editor.lines[g_editor.cursor_y]);
    g_editor.lines[g_editor.cursor_y] = first_part;

    editor_insert_line(g_editor.cursor_y + 1, remainder);
    free(remainder);

    g_editor.cursor_y++;
    g_editor.cursor_x = 0;
    g_editor.modified = true;
}

/* Build the full source text from all lines */
static char *editor_build_source(void)
{
    int total = 0;
    for (int i = 0; i < g_editor.line_count; i++)
        total += (int)strlen(g_editor.lines[i]) + 1;

    char *source = malloc(total + 1);
    source[0] = '\0';
    for (int i = 0; i < g_editor.line_count; i++) {
        strcat(source, g_editor.lines[i]);
        strcat(source, "\n");
    }
    return source;
}

/* Save the current program to disk */
static void editor_save(amos_state_t *state)
{
    (void)state;
    if (!g_editor.filename[0]) {
        snprintf(g_editor.filename, sizeof(g_editor.filename), "program.amos");
    }

    char *source = editor_build_source();
    FILE *f = fopen(g_editor.filename, "w");
    if (f) {
        fputs(source, f);
        fclose(f);
        g_editor.modified = false;
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Saved: %s", g_editor.filename);
        g_editor.status_timer = 100;
        fprintf(stderr, "[Editor] Saved: %s\n", g_editor.filename);
    } else {
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Error saving: %s", g_editor.filename);
        g_editor.status_timer = 150;
        fprintf(stderr, "[Editor] Save failed: %s\n", g_editor.filename);
    }
    free(source);
}

/* Execute a direct mode command */
static void editor_execute_direct(amos_state_t *state)
{
    if (g_editor.direct_line[0] == '\0') return;

    fprintf(stderr, "[Editor] Direct: %s\n", g_editor.direct_line);

    char *source = line_strdup(g_editor.direct_line);
    g_editor.direct_line[0] = '\0';
    g_editor.direct_cursor = 0;

    amos_load_text(state, source);
    state->direct_mode = true;
    state->running = true;

    while (state->running && state->current_line < state->line_count) {
        amos_run_step(state);
    }

    state->direct_mode = false;
    state->running = false;
    free(source);

    snprintf(g_editor.status_msg, sizeof(g_editor.status_msg), "Ok");
    g_editor.status_timer = 50;
}

/* Run the program from the editor */
static void editor_run_program(amos_state_t *state)
{
    char *source = editor_build_source();
    if (!source || source[0] == '\0') {
        free(source);
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg), "Nothing to run");
        g_editor.status_timer = 50;
        return;
    }
    fprintf(stderr, "[Editor] Running program (%d lines)\n", g_editor.line_count);

    amos_reset(state);
    amos_screen_open(state, 0, 320, 256, 5);

    amos_load_text(state, source);
    free(source);

    g_editor.active = false;
    amos_run(state);
}

/* Toggle between AMOS 1.3 and Pro editor styles */
static void editor_toggle_dialect(amos_state_t *state)
{
    if (state->dialect == AMOS_DIALECT_PRO) {
        state->dialect = AMOS_DIALECT_13;
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Switched to AMOS 1.3 editor");
    } else {
        state->dialect = AMOS_DIALECT_PRO;
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Switched to AMOS Pro editor");
    }
    g_editor.status_timer = 100;
    editor_set_palette(state);

    /* Adjust scroll for potentially different visible lines count */
    editor_ensure_cursor_visible(state->dialect);

    fprintf(stderr, "[Editor] Dialect: %s\n",
            state->dialect == AMOS_DIALECT_PRO ? "AMOS Pro" : "AMOS 1.3");
}

/* Handle a single SDL key event in the editor */
static void editor_handle_key(amos_state_t *state, SDL_Keysym key)
{
    bool ctrl = (key.mod & KMOD_CTRL) != 0;
    bool shift = (key.mod & KMOD_SHIFT) != 0;
    amos_dialect_t d = state->dialect;
    int vis = editor_visible_lines(d);

    /* Reset blink on any keypress */
    g_editor.blink_counter = 0;
    g_editor.cursor_visible = true;

    /* ── Direct mode input ──── */
    if (g_editor.direct_mode) {
        switch (key.sym) {
            case SDLK_ESCAPE:
                g_editor.direct_mode = false;
                return;

            case SDLK_RETURN:
                editor_execute_direct(state);
                g_editor.direct_mode = false;
                return;

            case SDLK_BACKSPACE:
                if (g_editor.direct_cursor > 0) {
                    int len = (int)strlen(g_editor.direct_line);
                    memmove(g_editor.direct_line + g_editor.direct_cursor - 1,
                            g_editor.direct_line + g_editor.direct_cursor,
                            len - g_editor.direct_cursor + 1);
                    g_editor.direct_cursor--;
                }
                return;

            case SDLK_LEFT:
                if (g_editor.direct_cursor > 0) g_editor.direct_cursor--;
                return;

            case SDLK_RIGHT:
                if (g_editor.direct_cursor < (int)strlen(g_editor.direct_line))
                    g_editor.direct_cursor++;
                return;

            case SDLK_HOME:
                g_editor.direct_cursor = 0;
                return;

            case SDLK_END:
                g_editor.direct_cursor = (int)strlen(g_editor.direct_line);
                return;

            default:
                return;
        }
    }

    /* ── Edit mode input ──── */
    switch (key.sym) {
        /* Function keys */
        case SDLK_F1:
            editor_run_program(state);
            return;

        case SDLK_F5:
            editor_save(state);
            return;

        case SDLK_F7:
            g_editor.insert_mode = true;
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg), "Insert mode");
            g_editor.status_timer = 50;
            return;

        case SDLK_F8:
            g_editor.insert_mode = !g_editor.insert_mode;
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     g_editor.insert_mode ? "Insert mode" : "Overwrite mode");
            g_editor.status_timer = 50;
            return;

        case SDLK_F10:
            g_editor.active = false;
            state->running = false;
            return;

        /* Navigation */
        case SDLK_UP:
            if (shift) {
                /* Page up */
                g_editor.cursor_y -= vis;
                if (g_editor.cursor_y < 0) g_editor.cursor_y = 0;
            } else if (g_editor.cursor_y > 0) {
                g_editor.cursor_y--;
            }
            editor_clamp_cursor();
            editor_ensure_cursor_visible(d);
            return;

        case SDLK_DOWN:
            if (shift) {
                /* Page down */
                g_editor.cursor_y += vis;
                if (g_editor.cursor_y >= g_editor.line_count)
                    g_editor.cursor_y = g_editor.line_count - 1;
            } else if (g_editor.cursor_y < g_editor.line_count - 1) {
                g_editor.cursor_y++;
            }
            editor_clamp_cursor();
            editor_ensure_cursor_visible(d);
            return;

        case SDLK_LEFT:
            if (ctrl) {
                /* Start of line */
                g_editor.cursor_x = 0;
            } else if (shift) {
                /* Word left */
                if (g_editor.cursor_x > 0) {
                    const char *line = g_editor.lines[g_editor.cursor_y];
                    g_editor.cursor_x--;
                    while (g_editor.cursor_x > 0 && line[g_editor.cursor_x] == ' ')
                        g_editor.cursor_x--;
                    while (g_editor.cursor_x > 0 && line[g_editor.cursor_x - 1] != ' ')
                        g_editor.cursor_x--;
                }
            } else if (g_editor.cursor_x > 0) {
                g_editor.cursor_x--;
            } else if (g_editor.cursor_y > 0) {
                g_editor.cursor_y--;
                g_editor.cursor_x = (int)strlen(g_editor.lines[g_editor.cursor_y]);
            }
            editor_ensure_cursor_visible(d);
            return;

        case SDLK_RIGHT: {
            int len = (int)strlen(g_editor.lines[g_editor.cursor_y]);
            if (ctrl) {
                /* End of line */
                g_editor.cursor_x = len;
            } else if (shift) {
                /* Word right */
                const char *line = g_editor.lines[g_editor.cursor_y];
                while (g_editor.cursor_x < len && line[g_editor.cursor_x] != ' ')
                    g_editor.cursor_x++;
                while (g_editor.cursor_x < len && line[g_editor.cursor_x] == ' ')
                    g_editor.cursor_x++;
            } else if (g_editor.cursor_x < len) {
                g_editor.cursor_x++;
            } else if (g_editor.cursor_y < g_editor.line_count - 1) {
                g_editor.cursor_y++;
                g_editor.cursor_x = 0;
            }
            editor_ensure_cursor_visible(d);
            return;
        }

        case SDLK_HOME:
            if (ctrl) {
                g_editor.cursor_y = 0;
                g_editor.cursor_x = 0;
                g_editor.scroll_y = 0;
            } else {
                g_editor.cursor_x = 0;
            }
            editor_ensure_cursor_visible(d);
            return;

        case SDLK_END:
            if (ctrl) {
                g_editor.cursor_y = g_editor.line_count - 1;
                g_editor.cursor_x = (int)strlen(g_editor.lines[g_editor.cursor_y]);
            } else {
                g_editor.cursor_x = (int)strlen(g_editor.lines[g_editor.cursor_y]);
            }
            editor_ensure_cursor_visible(d);
            return;

        case SDLK_PAGEUP:
            g_editor.cursor_y -= vis;
            if (g_editor.cursor_y < 0) g_editor.cursor_y = 0;
            editor_clamp_cursor();
            editor_ensure_cursor_visible(d);
            return;

        case SDLK_PAGEDOWN:
            g_editor.cursor_y += vis;
            if (g_editor.cursor_y >= g_editor.line_count)
                g_editor.cursor_y = g_editor.line_count - 1;
            editor_clamp_cursor();
            editor_ensure_cursor_visible(d);
            return;

        /* Editing */
        case SDLK_RETURN:
            editor_handle_return();
            editor_ensure_cursor_visible(d);
            return;

        case SDLK_BACKSPACE:
            editor_handle_backspace();
            editor_ensure_cursor_visible(d);
            return;

        case SDLK_DELETE:
            editor_handle_delete();
            return;

        case SDLK_TAB:
            for (int i = 0; i < EDITOR_TAB_SIZE; i++)
                editor_insert_char_at_cursor(' ');
            return;

        case SDLK_ESCAPE:
            g_editor.direct_mode = true;
            return;

        /* Ctrl shortcuts */
        case SDLK_q:
            if (ctrl) {
                g_editor.active = false;
                state->running = false;
            }
            return;

        case SDLK_d:
            if (ctrl) {
                editor_toggle_dialect(state);
            }
            return;

        case SDLK_l:
            if (ctrl) {
                /* Toggle line numbers */
                g_editor.show_line_numbers = !g_editor.show_line_numbers;
                snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                         g_editor.show_line_numbers ? "Line numbers ON" : "Line numbers OFF");
                g_editor.status_timer = 50;
            }
            return;

        default:
            break;
    }

    (void)shift;
}

/* ── Load a file into the editor buffer ────────────────────────── */

void amos_editor_load_file(amos_state_t *state, const char *path)
{
    (void)state;

    /* Try to detect .AMOS binary format by reading header */
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Cannot open: %s", path);
        g_editor.status_timer = 150;
        fprintf(stderr, "[Editor] Cannot open: %s\n", path);
        return;
    }

    /* Check for .AMOS binary header */
    char header[17];
    memset(header, 0, sizeof(header));
    size_t nread = fread(header, 1, 16, f);
    fclose(f);

    bool is_amos_binary = false;
    if (nread >= 16) {
        if (strncmp(header, "AMOS Basic", 10) == 0 ||
            strncmp(header, "AMOS Pro", 8) == 0) {
            is_amos_binary = true;
        }
    }

    if (is_amos_binary) {
        /* Load and detokenize using the runtime loader */
        fprintf(stderr, "[Editor] Detected .AMOS binary format\n");

        /* Read entire file */
        f = fopen(path, "rb");
        if (!f) return;
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t *data = malloc(size);
        fread(data, 1, size, f);
        fclose(f);

        /* Detokenize */
        char *source = amos_detokenize(data + 0x14,
                                       size > 0x14 ? (size_t)(size - 0x14) : 0);
        free(data);

        if (source) {
            /* Clear existing lines */
            for (int i = 0; i < g_editor.line_count; i++)
                free(g_editor.lines[i]);
            g_editor.line_count = 0;

            /* Parse source into lines */
            char *p = source;
            while (*p) {
                char *eol = strchr(p, '\n');
                if (eol) {
                    *eol = '\0';
                    editor_insert_line(g_editor.line_count, p);
                    p = eol + 1;
                } else {
                    editor_insert_line(g_editor.line_count, p);
                    break;
                }
            }
            free(source);
        } else {
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Detokenize failed: %s", path);
            g_editor.status_timer = 150;
            return;
        }

        /* Set dialect based on header */
        if (strncmp(header, "AMOS Pro", 8) == 0) {
            state->dialect = AMOS_DIALECT_PRO;
        } else {
            state->dialect = AMOS_DIALECT_13;
        }
    } else {
        /* Load as plain text */
        f = fopen(path, "r");
        if (!f) return;

        /* Clear existing lines */
        for (int i = 0; i < g_editor.line_count; i++)
            free(g_editor.lines[i]);
        g_editor.line_count = 0;

        char buf[EDITOR_MAX_LINE_LEN];
        while (fgets(buf, sizeof(buf), f)) {
            /* Strip trailing newline */
            int len = (int)strlen(buf);
            while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
                buf[--len] = '\0';
            editor_insert_line(g_editor.line_count, buf);
        }
        fclose(f);
    }

    if (g_editor.line_count == 0) {
        g_editor.lines[0] = line_strdup("");
        g_editor.line_count = 1;
    }

    strncpy(g_editor.filename, path, sizeof(g_editor.filename) - 1);
    g_editor.filename[sizeof(g_editor.filename) - 1] = '\0';
    g_editor.cursor_x = 0;
    g_editor.cursor_y = 0;
    g_editor.scroll_y = 0;
    g_editor.modified = false;

    snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
             "Loaded: %s (%d lines)", path, g_editor.line_count);
    g_editor.status_timer = 100;
    fprintf(stderr, "[Editor] Loaded: %s (%d lines)\n", path, g_editor.line_count);
}

/* ── Initialization ────────────────────────────────────────────── */

static void editor_init_common(amos_state_t *state)
{
    memset(&g_editor, 0, sizeof(g_editor));
    g_editor.active = true;
    g_editor.insert_mode = true;
    g_editor.cursor_visible = true;
    g_editor.show_line_numbers = false;
    g_editor.lines = calloc(EDITOR_MAX_LINES, sizeof(char *));

    /* Start with one empty line */
    g_editor.lines[0] = line_strdup("");
    g_editor.line_count = 1;

    /* Open screen 0 with editor dimensions */
    amos_screen_open(state, 0, EDITOR_SCREEN_W, EDITOR_SCREEN_H, 2);

    /* Set palette for current dialect */
    editor_set_palette(state);

    /* Use clean CRT preset for editor */
    crt_set_preset(state, CRT_PRESET_CLEAN);

    /* Ensure SDL text input is enabled */
    SDL_StartTextInput();
}

void amos_editor_init(amos_state_t *state)
{
    state->dialect = AMOS_DIALECT_13;
    editor_init_common(state);

    snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
             "AMOS 1.3 Editor ready");
    g_editor.status_timer = 100;

    fprintf(stderr, "[Editor] Initialized (320x256, 4-color AMOS 1.3 palette)\n");
}

void amos_editor_init_pro(amos_state_t *state)
{
    state->dialect = AMOS_DIALECT_PRO;
    editor_init_common(state);

    snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
             "AMOS Professional Editor ready");
    g_editor.status_timer = 100;

    fprintf(stderr, "[Editor] Initialized (320x256, 8-color AMOS Pro palette)\n");
}

void amos_editor_destroy(amos_state_t *state)
{
    (void)state;
    if (g_editor.lines) {
        for (int i = 0; i < g_editor.line_count; i++)
            free(g_editor.lines[i]);
        free(g_editor.lines);
        g_editor.lines = NULL;
    }
    g_editor.line_count = 0;
    g_editor.active = false;
}

/* ── Tick (one frame) ──────────────────────────────────────────── */

void amos_editor_tick(amos_state_t *state)
{
    if (!g_editor.active) return;

    editor_ensure_lines();

    /* Process SDL events for editor input */
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                g_editor.active = false;
                state->running = false;
                platform_request_quit();
                return;

            case SDL_KEYDOWN:
                editor_handle_key(state, event.key.keysym);
                break;

            case SDL_TEXTINPUT:
                /* Handle printable character input */
                if (event.text.text[0] >= 32 && event.text.text[0] < 127) {
                    if (g_editor.direct_mode) {
                        int len = (int)strlen(g_editor.direct_line);
                        if (len < EDITOR_DIRECT_LEN - 1) {
                            char ch = event.text.text[0];
                            memmove(g_editor.direct_line + g_editor.direct_cursor + 1,
                                    g_editor.direct_line + g_editor.direct_cursor,
                                    len - g_editor.direct_cursor + 1);
                            g_editor.direct_line[g_editor.direct_cursor] = ch;
                            g_editor.direct_cursor++;
                        }
                    } else {
                        editor_insert_char_at_cursor(event.text.text[0]);
                        g_editor.modified = true;
                    }
                    g_editor.blink_counter = 0;
                    g_editor.cursor_visible = true;
                }
                break;

            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    int draw_w, draw_h;
                    SDL_GL_GetDrawableSize(SDL_GetWindowFromID(event.window.windowID),
                                           &draw_w, &draw_h);
                    state->window_width = draw_w;
                    state->window_height = draw_h;
                }
                break;

            default:
                break;
        }
    }

    /* Cursor blink: toggle every 25 frames (~500ms at 50Hz) */
    g_editor.blink_counter++;
    if (g_editor.blink_counter >= 25) {
        g_editor.blink_counter = 0;
        g_editor.cursor_visible = !g_editor.cursor_visible;
    }

    /* Status message timer */
    if (g_editor.status_timer > 0) g_editor.status_timer--;

    /* Render the editor screen */
    amos_editor_render(state);

    /* Present display */
    platform_present(state);
}

bool amos_editor_is_active(amos_state_t *state)
{
    (void)state;
    return g_editor.active;
}
