/*
 * amos.h — AMOS Reborn master header
 *
 * Core types, constants, and forward declarations for the entire runtime.
 */

#ifndef AMOS_H
#define AMOS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ── Version ─────────────────────────────────────────────────────── */

#define AMOS_VERSION_MAJOR 0
#define AMOS_VERSION_MINOR 1
#define AMOS_VERSION_PATCH 0
#define AMOS_VERSION_STRING "0.1.0"

/* ── Limits ──────────────────────────────────────────────────────── */

#define AMOS_MAX_SCREENS     8
#define AMOS_MAX_SPRITES    64
#define AMOS_MAX_BOBS       64
#define AMOS_MAX_BANKS      16
#define AMOS_MAX_VARIABLES 4096
#define AMOS_MAX_STRINGS   4096
#define AMOS_MAX_ARRAYS     256
#define AMOS_MAX_LINE_LEN  1024
#define AMOS_MAX_PROGRAM_LINES 65536
#define AMOS_MAX_STACK_DEPTH   256
#define AMOS_MAX_GOSUB_DEPTH   128
#define AMOS_MAX_FOR_DEPTH     64
#define AMOS_MAX_PROC_DEPTH    64
#define AMOS_MAX_AMAL_CHANNELS 16

/* ── Dialect ─────────────────────────────────────────────────────── */

typedef enum {
    AMOS_DIALECT_13,        /* AMOS 1.3 */
    AMOS_DIALECT_PRO,       /* AMOS Professional */
} amos_dialect_t;

/* ── Display Mode ────────────────────────────────────────────────── */

typedef enum {
    AMOS_MODE_CLASSIC,      /* Amiga-accurate: planar, OCS/ECS limits, 50Hz */
    AMOS_MODE_MODERN,       /* Host-native: RGBA, arbitrary res, unlocked */
} amos_display_mode_t;

/* ── CRT Preset ──────────────────────────────────────────────────── */

typedef enum {
    CRT_PRESET_CLEAN,
    CRT_PRESET_VGA,
    CRT_PRESET_CRT,
    CRT_PRESET_AMBER,
    CRT_PRESET_GREEN,
    CRT_PRESET_TV,
    CRT_PRESET_COMMODORE,   /* Commodore 1084S monitor */
    CRT_PRESET_COUNT,
} crt_preset_t;

/* ── Variable Types ──────────────────────────────────────────────── */

typedef enum {
    VAR_INTEGER,            /* 32-bit signed: A, B, COUNT */
    VAR_FLOAT,              /* 64-bit double: A#, B# */
    VAR_STRING,             /* length-prefixed, GC'd: A$, B$ */
} amos_var_type_t;

/* ── Token Types ─────────────────────────────────────────────────── */

typedef enum {
    /* Literals */
    TOK_INTEGER,            /* 42 */
    TOK_FLOAT,              /* 3.14 */
    TOK_STRING,             /* "hello" */
    TOK_IDENTIFIER,         /* variable name */

    /* Operators */
    TOK_PLUS,               /* + */
    TOK_MINUS,              /* - */
    TOK_MULTIPLY,           /* * */
    TOK_DIVIDE,             /* / */
    TOK_MOD,                /* Mod */
    TOK_POWER,              /* ^ */
    TOK_EQUAL,              /* = */
    TOK_NOT_EQUAL,          /* <> */
    TOK_LESS,               /* < */
    TOK_GREATER,            /* > */
    TOK_LESS_EQUAL,         /* <= */
    TOK_GREATER_EQUAL,      /* >= */

    /* Logical */
    TOK_AND,
    TOK_OR,
    TOK_NOT,
    TOK_XOR,

    /* Punctuation */
    TOK_LPAREN,             /* ( */
    TOK_RPAREN,             /* ) */
    TOK_COMMA,              /* , */
    TOK_SEMICOLON,          /* ; */
    TOK_COLON,              /* : (statement separator) */
    TOK_HASH,               /* # (float suffix / channel) */
    TOK_DOLLAR,             /* $ (string suffix) */

    /* Keywords — Control Flow */
    TOK_IF,
    TOK_THEN,
    TOK_ELSE,
    TOK_END_IF,
    TOK_FOR,
    TOK_TO,
    TOK_STEP,
    TOK_NEXT,
    TOK_WHILE,
    TOK_WEND,
    TOK_REPEAT,
    TOK_UNTIL,
    TOK_DO,
    TOK_LOOP,
    TOK_GOTO,
    TOK_GOSUB,
    TOK_RETURN,
    TOK_ON,
    TOK_EXIT,
    TOK_EXIT_IF,
    TOK_POP,
    TOK_END,
    TOK_STOP,

    /* Keywords — Procedures (Pro) */
    TOK_PROCEDURE,
    TOK_END_PROC,
    TOK_PROC,
    TOK_SHARED,
    TOK_GLOBAL,
    TOK_LOCAL,
    TOK_PARAM,
    TOK_FN,

    /* Keywords — Data */
    TOK_LET,
    TOK_DIM,
    TOK_DATA,
    TOK_READ,
    TOK_RESTORE,
    TOK_SWAP,

    /* Keywords — I/O */
    TOK_PRINT,
    TOK_INPUT,
    TOK_LOCATE,
    TOK_CLS,
    TOK_HOME,

    /* Keywords — Graphics */
    TOK_SCREEN_OPEN,
    TOK_SCREEN_CLOSE,
    TOK_SCREEN_DISPLAY,
    TOK_SCREEN_OFFSET,
    TOK_SCREEN_CLONE,
    TOK_SCREEN,
    TOK_INK,
    TOK_COLOUR,
    TOK_PALETTE,
    TOK_PLOT,
    TOK_DRAW,
    TOK_BOX,
    TOK_BAR,
    TOK_CIRCLE,
    TOK_ELLIPSE,
    TOK_PAINT,
    TOK_POLYGON,
    TOK_TEXT,
    TOK_SET_FONT,
    TOK_GR_WRITING,

    /* Keywords — Sprites & Bobs */
    TOK_SPRITE,
    TOK_BOB,
    TOK_SET_SPRITE_BUFFER,
    TOK_SET_BOB,
    TOK_SPRITE_OFF,
    TOK_BOB_OFF,
    TOK_GET_SPRITE,
    TOK_GET_BOB,
    TOK_SPRITE_COL,
    TOK_BOB_COL,
    TOK_MOVE_X,
    TOK_MOVE_Y,
    TOK_ANIM,

    /* Keywords — Audio */
    TOK_BOOM,
    TOK_SHOOT,
    TOK_BELL,
    TOK_SAM_PLAY,
    TOK_SAM_RAW,
    TOK_SAM_LOOP_ON,
    TOK_SAM_LOOP_OFF,
    TOK_VOLUME,
    TOK_MUSIC,
    TOK_TRACK_PLAY,
    TOK_TRACK_STOP,
    TOK_TRACK_LOOP_ON,

    /* Keywords — AMAL */
    TOK_AMAL,
    TOK_AMAL_ON,
    TOK_AMAL_OFF,
    TOK_AMAL_FREEZE,
    TOK_SYNCHRO,

    /* Keywords — Banks */
    TOK_RESERVE,
    TOK_ERASE,
    TOK_LOAD,
    TOK_SAVE,
    TOK_BLOAD,
    TOK_BSAVE,

    /* Keywords — Screen Mode */
    TOK_RAINBOW,
    TOK_COPPER,
    TOK_DUAL_PLAYFIELD,
    TOK_SCREEN_SWAP,
    TOK_DOUBLE_BUFFER,
    TOK_AUTOBACK,

    /* Keywords — System */
    TOK_REM,
    TOK_WAIT_VBL,
    TOK_WAIT_KEY,
    TOK_WAIT,
    TOK_TIMER,
    TOK_RANDOMIZE,
    TOK_MODE,

    /* Keywords — Modern Extensions */
    TOK_SCREEN_MODE,        /* Mode Classic / Mode Modern */

    /* Special */
    TOK_NEWLINE,
    TOK_EOF,
    TOK_LABEL,              /* label: */
    TOK_LINE_NUMBER,        /* 10, 20, 30... */

    TOK_COUNT               /* total number of token types */
} amos_token_type_t;

/* ── Token ───────────────────────────────────────────────────────── */

typedef struct {
    amos_token_type_t type;
    union {
        int32_t ival;       /* TOK_INTEGER, TOK_LINE_NUMBER */
        double  fval;       /* TOK_FLOAT */
        char   *sval;       /* TOK_STRING, TOK_IDENTIFIER, TOK_LABEL */
    };
    int line;               /* source line number */
    int col;                /* source column */
} amos_token_t;

/* ── AST Node Types ──────────────────────────────────────────────── */

typedef enum {
    NODE_PROGRAM,
    NODE_LINE,
    NODE_COMMAND,           /* any AMOS command (Cls, Plot, etc.) */
    NODE_PRINT,
    NODE_LET,
    NODE_IF,
    NODE_FOR,
    NODE_WHILE,
    NODE_REPEAT,
    NODE_GOSUB,
    NODE_GOTO,
    NODE_RETURN,
    NODE_REM,
    NODE_END,
    NODE_DIM,
    NODE_LABEL,
    NODE_PROCEDURE,
    NODE_PROC_CALL,
    NODE_DATA,
    NODE_READ,
    NODE_RESTORE,

    /* Expressions */
    NODE_BINARY_OP,
    NODE_UNARY_OP,
    NODE_INT_LITERAL,
    NODE_FLOAT_LITERAL,
    NODE_STRING_LITERAL,
    NODE_VARIABLE,
    NODE_ARRAY_ACCESS,
    NODE_FUNCTION_CALL,
} amos_node_type_t;

/* ── AST Node ────────────────────────────────────────────────────── */

#define AST_MAX_CHILDREN 32

typedef struct amos_node {
    amos_node_type_t type;
    amos_token_t token;         /* primary token for this node */
    struct amos_node *children[AST_MAX_CHILDREN];
    int child_count;
    int line;                   /* source line for error reporting */
} amos_node_t;

/* ── Variable ────────────────────────────────────────────────────── */

typedef struct {
    char name[64];
    amos_var_type_t type;
    union {
        int32_t ival;
        double  fval;
        struct {
            char *data;
            int   len;
        } sval;
    };
} amos_var_t;

/* ── Array ───────────────────────────────────────────────────────── */

typedef struct {
    char name[64];
    amos_var_type_t type;
    int dims[10];               /* up to 10 dimensions */
    int ndims;
    void *data;                 /* flat array of int32_t, double, or char* */
    int total_elements;
} amos_array_t;

/* ── Screen State ────────────────────────────────────────────────── */

typedef struct {
    int id;
    bool active;
    int width, height;
    int depth;                  /* bitplanes (classic) or 32 (modern) */
    uint32_t palette[256];
    uint32_t *pixels;           /* RGBA framebuffer */
    int offset_x, offset_y;    /* Screen Offset */
    int display_x, display_y;  /* Screen Display position */
    int display_w, display_h;
    bool visible;
    int priority;               /* z-order (0 = front) */

    /* Text cursor */
    int cursor_x, cursor_y;
    int text_pen;               /* foreground color index */
    int text_paper;             /* background color index */

    /* Drawing state */
    int ink_color;              /* foreground */
    int paper_color;            /* background (for text) */
    int outline_color;          /* border color */
    int writing_mode;           /* graphics writing mode (replace, or, xor, and) */

    /* Double buffering */
    uint32_t *back_buffer;
    bool double_buffered;
    int autoback;               /* autoback mode */
} amos_screen_t;

/* ── Sprite/Bob ──────────────────────────────────────────────────── */

typedef struct {
    bool active;
    int image;                  /* sprite bank image index */
    int x, y;
    bool visible;
    int hot_x, hot_y;           /* hotspot offset */
} amos_sprite_t;

typedef struct {
    bool active;
    int image;
    int x, y;
    bool visible;
    int hot_x, hot_y;
    int screen_id;              /* which screen this bob belongs to */
} amos_bob_t;

/* ── Paula Channel ───────────────────────────────────────────────── */

typedef struct {
    const int8_t *sample_data;
    uint32_t sample_length;     /* in bytes */
    uint32_t repeat_offset;
    uint32_t repeat_length;     /* 0 = no loop */
    uint16_t period;            /* freq = PAL_CLOCK / (period * 2) */
    uint8_t  volume;            /* 0-64 */
    double   position;          /* fractional sample position */
    bool     active;
} paula_channel_t;

#define PAULA_PAL_CLOCK 3546895

typedef struct {
    paula_channel_t channels[4];
    bool    filter_enabled;     /* Amiga low-pass filter */
    double  filter_state_l;     /* Butterworth state */
    double  filter_state_r;
    int     output_rate;        /* host sample rate */
} paula_t;

/* ── Memory Bank ─────────────────────────────────────────────────── */

typedef enum {
    BANK_EMPTY = 0,
    BANK_SPRITES,       /* Bank 1 default */
    BANK_ICONS,         /* Bank 2 default */
    BANK_SAMPLES,       /* Bank 3 default */
    BANK_WORK,
    BANK_DATA,
} amos_bank_type_t;

typedef struct {
    amos_bank_type_t type;
    uint8_t *data;
    uint32_t size;
    char name[32];
} amos_bank_t;

/* ── AMAL Channel ────────────────────────────────────────────────── */

typedef struct {
    bool active;
    bool frozen;
    char *program;              /* AMAL program string */
    int pc;                     /* program counter */
    int registers[10];          /* R0-R9 */
    int target_type;            /* 0=sprite, 1=bob */
    int target_id;
    int wait_counter;
} amal_channel_t;

/* ── GOSUB Stack Entry ───────────────────────────────────────────── */

typedef struct {
    int return_line;
    int return_pos;             /* position within line */
} gosub_entry_t;

/* ── FOR Stack Entry ─────────────────────────────────────────────── */

typedef struct {
    char var_name[64];
    double limit;
    double step;
    int loop_line;
    int loop_pos;
} for_entry_t;

/* ── Program Line ────────────────────────────────────────────────── */

typedef struct {
    int number;                 /* line number (0 if unnumbered) */
    char *text;                 /* raw source text */
    amos_node_t *ast;           /* parsed AST (lazily populated) */
} amos_program_line_t;

/* ── CRT Parameters ─────────────────────────────────────────────── */

typedef struct {
    float scanline_intensity;
    float bloom_intensity;
    float curvature;
    float mask_intensity;
    float chroma_offset;
    float vignette_intensity;
    float noise_intensity;
    float flicker;
    float time;
    float tint_r, tint_g, tint_b;
} crt_params_t;

/* ── Main State ──────────────────────────────────────────────────── */

typedef struct {
    /* Dialect and mode */
    amos_dialect_t dialect;
    amos_display_mode_t display_mode;

    /* Program */
    amos_program_line_t lines[AMOS_MAX_PROGRAM_LINES];
    int line_count;
    int current_line;           /* execution pointer */
    int current_pos;            /* position within current line */
    bool running;
    bool direct_mode;           /* executing direct mode command */

    /* Variables */
    amos_var_t variables[AMOS_MAX_VARIABLES];
    int var_count;

    /* Arrays */
    amos_array_t arrays[AMOS_MAX_ARRAYS];
    int array_count;

    /* Stack */
    gosub_entry_t gosub_stack[AMOS_MAX_GOSUB_DEPTH];
    int gosub_top;
    for_entry_t for_stack[AMOS_MAX_FOR_DEPTH];
    int for_top;

    /* DATA pointer */
    int data_line;
    int data_pos;

    /* Screens */
    amos_screen_t screens[AMOS_MAX_SCREENS];
    int current_screen;         /* active screen index */

    /* Sprites & Bobs */
    amos_sprite_t sprites[AMOS_MAX_SPRITES];
    amos_bob_t bobs[AMOS_MAX_BOBS];

    /* Audio */
    paula_t paula;

    /* Banks */
    amos_bank_t banks[AMOS_MAX_BANKS];

    /* AMAL */
    amal_channel_t amal[AMOS_MAX_AMAL_CHANNELS];
    bool synchro;               /* SYNCHRO mode (wait for VBL) */

    /* Display */
    crt_params_t crt;
    crt_preset_t crt_preset;
    int window_width, window_height;

    /* Timing */
    uint32_t timer;             /* AMOS timer (50ths of a second) */
    double frame_time;          /* time of last frame */

    /* Error state */
    int error_code;
    char error_msg[256];
    int error_line;
} amos_state_t;

/* ── API: Lifecycle ──────────────────────────────────────────────── */

amos_state_t *amos_create(void);
void          amos_destroy(amos_state_t *state);
void          amos_reset(amos_state_t *state);

/* ── API: Program Loading ────────────────────────────────────────── */

int  amos_load_text(amos_state_t *state, const char *source);
int  amos_load_file(amos_state_t *state, const char *path);

/* ── API: Execution ──────────────────────────────────────────────── */

void amos_run(amos_state_t *state);
void amos_run_step(amos_state_t *state);  /* execute one statement */
void amos_stop(amos_state_t *state);
void amos_frame_tick(amos_state_t *state);

/* ── API: Tokenizer ──────────────────────────────────────────────── */

typedef struct {
    amos_token_t *tokens;
    int count;
    int capacity;
} amos_token_list_t;

amos_token_list_t *amos_tokenize(const char *source);
void               amos_token_list_free(amos_token_list_t *list);

/* ── API: Parser ─────────────────────────────────────────────────── */

amos_node_t *amos_parse_line(amos_token_t *tokens, int *pos, int count);
amos_node_t *amos_parse_expression(amos_token_t *tokens, int *pos, int count);
void         amos_node_free(amos_node_t *node);

/* ── API: Executor ───────────────────────────────────────────────── */

void amos_execute_node(amos_state_t *state, amos_node_t *node);
void amos_execute_line(amos_state_t *state, int line_index);

/* ── API: Variables ──────────────────────────────────────────────── */

amos_var_t *amos_var_get(amos_state_t *state, const char *name);
amos_var_t *amos_var_set_int(amos_state_t *state, const char *name, int32_t val);
amos_var_t *amos_var_set_float(amos_state_t *state, const char *name, double val);
amos_var_t *amos_var_set_string(amos_state_t *state, const char *name, const char *val);

/* ── API: Display ────────────────────────────────────────────────── */

int  amos_screen_open(amos_state_t *state, int id, int w, int h, int depth);
void amos_screen_close(amos_state_t *state, int id);
void amos_screen_cls(amos_state_t *state, int color);
void amos_screen_plot(amos_state_t *state, int x, int y, int color);
void amos_screen_draw(amos_state_t *state, int x1, int y1, int x2, int y2, int color);
void amos_screen_box(amos_state_t *state, int x1, int y1, int x2, int y2, int color);
void amos_screen_bar(amos_state_t *state, int x1, int y1, int x2, int y2, int color);
void amos_screen_circle(amos_state_t *state, int cx, int cy, int r, int color);
void amos_screen_print(amos_state_t *state, const char *text);
void amos_screen_locate(amos_state_t *state, int x, int y);
void amos_screen_ink(amos_state_t *state, int pen, int paper, int outline);
void amos_screen_colour(amos_state_t *state, int index, uint32_t rgb);
void amos_screen_palette(amos_state_t *state, uint32_t *colors, int count);

/* ── API: Audio ──────────────────────────────────────────────────── */

void amos_paula_init(paula_t *paula, int output_rate);
void amos_paula_mix(paula_t *paula, int16_t *buffer, int frames);
void amos_boom(amos_state_t *state);
void amos_shoot(amos_state_t *state);
void amos_bell(amos_state_t *state);
void amos_sam_play(amos_state_t *state, int channel, int sample_id);

/* ── API: CRT Shaders ────────────────────────────────────────────── */

int  crt_init(amos_state_t *state);
void crt_set_preset(amos_state_t *state, crt_preset_t preset);
void crt_render(amos_state_t *state, uint32_t *framebuffer, int fb_w, int fb_h);
void crt_shutdown(void);

/* ── API: Platform ───────────────────────────────────────────────── */

int  platform_init(amos_state_t *state, int width, int height, const char *title);
void platform_shutdown(void);
bool platform_should_quit(void);
void platform_poll_events(amos_state_t *state);
void platform_present(amos_state_t *state);
uint32_t platform_get_ticks(void);
void platform_audio_init(amos_state_t *state);
void platform_audio_shutdown(void);

/* ── Compositor ──────────────────────────────────────────────────── */

void compositor_render(amos_state_t *state, uint32_t *output, int out_w, int out_h);

/* ── Pixel Packing (little-endian safe for GL_RGBA GL_UNSIGNED_BYTE) ── */

/* Pack RGBA into uint32 so bytes in memory are [R, G, B, A] */
#define AMOS_RGBA(r, g, b, a) \
    ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))

/* ── Default Amiga Palette ───────────────────────────────────────── */

extern const uint32_t amos_default_palette_32[32];

/* ── Topaz Font ──────────────────────────────────────────────────── */

extern const uint8_t topaz_font_8x8[256][8];

#endif /* AMOS_H */
