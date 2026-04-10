/*
 * amos_loader.c — .AMOS tokenized file loader
 *
 * Reads original .AMOS binary files (AMOS 1.3 and AMOS Professional),
 * detokenizes them to plain text, and loads the result into the interpreter.
 */

#include "amos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

/* ── Big-Endian Helpers ────────────────────────────────────────── */

#define BE16(p) (uint16_t)(((p)[0] << 8) | (p)[1])
#define BE32(p) (uint32_t)(((p)[0] << 24) | ((p)[1] << 16) | ((p)[2] << 8) | (p)[3])
#define BE32S(p) (int32_t)BE32(p)

/* ── Token IDs (special tokens — same in 1.3 and Pro) ─────────── */

#define TK_VAR       0x0006   /* Variable reference */
#define TK_LABEL_DEF 0x000C   /* Label definition */
#define TK_PROC_REF  0x0012   /* Procedure reference */
#define TK_LABEL_REF 0x0018   /* Label reference (goto/gosub target) */
#define TK_BIN       0x001E   /* Binary constant */
#define TK_STRING    0x0026   /* String literal */
#define TK_STRING2   0x002E   /* String literal (variant) */
#define TK_HEX       0x0036   /* Hex integer literal */
#define TK_INT       0x003E   /* Decimal integer literal */
#define TK_FLOAT     0x0046   /* Float literal (AMOS format) */
#define TK_EXT       0x004E   /* Extension token */

/* ── Header Detection ──────────────────────────────────────────── */

#define AMOS_HEADER_SIZE 16
#define AMOS_SRC_OFFSET  20   /* 16 header + 4 source length */

typedef enum {
    AMOS_FILE_13,       /* "AMOS Basic V..." */
    AMOS_FILE_PRO,      /* "AMOS Pro..." */
    AMOS_FILE_UNKNOWN,
} amos_file_type_t;

/* ── Keyword Token Table ───────────────────────────────────────── */

/*
 * Keyword tokens are word-sized offsets from the Tk label in the
 * original 68000 token dispatch table. This table maps those offsets
 * to keyword strings for both AMOS 1.3 and AMOS Professional.
 *
 * AMOS Pro's token table has extra entries compared to 1.3, causing
 * offset shifts in some regions. Both sets of offsets are included.
 *
 * Operator tokens use negative offsets (high 16-bit unsigned values)
 * since operators appear before the Tk label in the dispatch table.
 */

/*
 * Token flags: control how the detokenizer handles the word(s)
 * following a keyword token.
 */
#define TF_NONE   0x00   /* No special handling */
#define TF_BRANCH  0x01   /* Next word is a branch distance — skip it */
#define TF_BRANCH2 0x02   /* Next TWO words are branch distances — skip them */

static const struct {
    uint16_t token;
    const char *keyword;
    uint8_t flags;
} token_table[] = {
    /* ── Operators (before Tk, negative offsets) ────────────────── */
    {0xFF3E, " Xor ", TF_NONE},
    {0xFF4C, " Or ", TF_NONE},
    {0xFF58, " And ", TF_NONE},
    {0xFF66, "<>", TF_NONE},         /* <> variant 1 */
    {0xFF70, "<>", TF_NONE},         /* >< variant */
    {0xFF7A, "<=", TF_NONE},         /* <= variant 1 */
    {0xFF84, "<=", TF_NONE},         /* =< variant */
    {0xFF8E, ">=", TF_NONE},         /* >= variant 1 */
    {0xFF98, ">=", TF_NONE},         /* => variant */
    {0xFFA2, "=", TF_NONE},          /* equals/assignment */
    {0xFFAC, "<", TF_NONE},
    {0xFFB6, ">", TF_NONE},
    {0xFFC0, "+", TF_NONE},
    {0xFFCA, "-", TF_NONE},
    {0xFFD4, " Mod ", TF_NONE},
    {0xFFE2, "*", TF_NONE},
    {0xFFEC, "/", TF_NONE},
    {0xFFF6, "^", TF_NONE},

    /* ── Punctuation ────────────────────────────────────────────── */
    {0x0054, " : ", TF_NONE},        /* Statement separator */
    {0x005C, ",", TF_NONE},          /* Comma */
    {0x0064, " ; ", TF_NONE},        /* Semicolon */
    {0x006C, "#", TF_NONE},          /* Hash */
    {0x0074, "(", TF_NONE},          /* Left paren */
    {0x007C, ")", TF_NONE},          /* Right paren */
    {0x0084, "[", TF_NONE},          /* Left bracket */
    {0x008C, "]", TF_NONE},          /* Right bracket */

    /* ── Keywords (AMOS 1.3 offsets) ────────────────────────────── */
    /* Flow control keywords — 1.3 */
    {0x0090, "To ", TF_NONE},
    {0x0094, "To ", TF_NONE},         /* Pro */
    {0x0098, "Not ", TF_NONE},
    {0x00A2, "Swap ", TF_NONE},
    {0x00AC, "Def Fn ", TF_NONE},
    {0x00B8, "Fn ", TF_NONE},
    {0x00C0, "Follow Off", TF_NONE},
    {0x00D0, "Follow", TF_NONE},
    {0x00DC, "Resume Next", TF_NONE},
    {0x00EE, "Inkey$", TF_NONE},
    {0x00F2, "Inkey$", TF_NONE},      /* Pro */
    {0x00FA, "Repeat$", TF_NONE},
    {0x010A, "Zone$", TF_NONE},
    {0x0118, "Border$", TF_NONE},
    {0x0128, "Double Buffer", TF_NONE},
    {0x013C, "Start", TF_NONE},
    {0x0148, "Length", TF_NONE},
    {0x0156, "Doke ", TF_NONE},
    {0x0164, "On Menu Del", TF_NONE},
    {0x0176, "On Menu On", TF_NONE},
    {0x0186, "On Menu Off", TF_NONE},
    {0x0198, "Every On", TF_NONE},
    {0x01A6, "Every Off", TF_NONE},
    {0x01B6, "Logbase", TF_NONE},
    {0x01C4, "Logic", TF_NONE},
    {0x01D8, "Asc", TF_NONE},
    {0x01E2, "As ", TF_NONE},
    {0x01EA, "Call ", TF_NONE},
    {0x01F4, "Execall", TF_NONE},
    {0x0202, "Gfxcall", TF_NONE},
    {0x0210, "Doscall", TF_NONE},
    {0x0214, "Doscall", TF_NONE},      /* AMOS Pro offset */
    {0x021E, "Intcall", TF_NONE},
    {0x022C, "Freeze", TF_NONE},

    /* ── Loops & Branches (1.3 / Pro offsets) ───────────────────── */
    {0x0238, "For ", TF_BRANCH},         /* 1.3 */
    {0x023C, "For ", TF_BRANCH},         /* Pro */
    {0x0242, "Next ", TF_NONE},        /* 1.3 */
    {0x0246, "Next ", TF_NONE},        /* Pro */
    {0x024C, "Repeat", TF_BRANCH},       /* 1.3 */
    {0x0250, "Repeat", TF_BRANCH},       /* Pro */
    {0x0258, "Until ", TF_NONE},       /* 1.3 */
    {0x025C, "Until ", TF_NONE},       /* Pro */
    {0x0264, "While ", TF_BRANCH},       /* 1.3 */
    {0x0268, "While ", TF_BRANCH},       /* Pro */
    {0x0270, "Wend", TF_NONE},         /* 1.3 */
    {0x0274, "Wend", TF_NONE},         /* Pro */
    {0x027A, "Do", TF_BRANCH},           /* 1.3 */
    {0x027E, "Do", TF_BRANCH},           /* Pro */
    {0x0282, "Loop", TF_NONE},         /* 1.3 */
    {0x0286, "Loop", TF_NONE},         /* Pro */
    {0x028C, "Exit If ", TF_BRANCH2},     /* 1.3 */
    {0x0290, "Exit If ", TF_BRANCH2},     /* Pro */
    {0x029A, "Exit", TF_BRANCH2},         /* 1.3 */
    {0x029E, "Exit", TF_BRANCH2},         /* Pro */
    {0x02A4, "Goto ", TF_NONE},        /* 1.3 */
    {0x02A8, "Goto ", TF_NONE},        /* Pro */
    {0x02AE, "Gosub ", TF_NONE},       /* 1.3 */
    {0x02B2, "Gosub ", TF_NONE},       /* Pro */
    {0x02BA, "If ", TF_BRANCH},          /* 1.3 */
    {0x02BE, "If ", TF_BRANCH},          /* Pro */
    {0x02C2, "Then ", TF_NONE},        /* 1.3 */
    {0x02C6, "Then ", TF_NONE},        /* Pro */
    {0x02CC, "Else ", TF_BRANCH},        /* 1.3 */
    {0x02D0, "Else ", TF_BRANCH},        /* Pro */
    {0x02D6, "End If", TF_NONE},       /* 1.3 */
    {0x02DA, "End If", TF_NONE},       /* Pro */
    {0x02E2, "On Error ", TF_BRANCH},    /* 1.3 */
    {0x02E6, "On Error ", TF_BRANCH},    /* Pro */
    {0x02F0, "On Break Proc ", TF_BRANCH},
    {0x0304, "On Menu ", TF_BRANCH},
    {0x0312, "On ", TF_BRANCH},
    {0x031A, "Resume Label ", TF_NONE},
    {0x032C, "Resume", TF_NONE},
    {0x0338, "Pop Proc", TF_NONE},
    {0x0346, "Every ", TF_BRANCH},
    {0x0352, "Step ", TF_NONE},
    {0x035C, "Return", TF_NONE},
    {0x035E, "Return", TF_NONE},       /* Pro variant */
    {0x0368, "Pop", TF_NONE},
    {0x0372, "Procedure ", TF_BRANCH},
    {0x0376, "Procedure ", TF_BRANCH},   /* Pro */
    {0x0382, "Proc ", TF_BRANCH},
    {0x038C, "End Proc", TF_NONE},
    {0x0390, "End Proc", TF_NONE},     /* Pro */
    {0x039A, "Shared ", TF_NONE},
    {0x03A6, "Global ", TF_NONE},
    {0x03AA, "Global ", TF_NONE},      /* Pro */
    {0x03B2, "End", TF_NONE},
    {0x03B6, "End", TF_NONE},          /* Pro */
    {0x03BC, "Stop", TF_NONE},
    {0x03C6, "Param#", TF_NONE},
    {0x03D2, "Param$", TF_NONE},
    {0x03D6, "Param$", TF_NONE},       /* Pro */
    {0x03DE, "Param", TF_NONE},
    {0x03E2, "Param", TF_NONE},        /* Pro */
    {0x03EA, "Error ", TF_NONE},
    {0x03F6, "Errn", TF_NONE},
    {0x0400, "Data ", TF_NONE},
    {0x0404, "Data ", TF_NONE},        /* Pro */
    {0x040A, "Read ", TF_NONE},
    {0x040E, "Read ", TF_NONE},        /* Pro */
    {0x0414, "Restore ", TF_NONE},
    {0x0418, "Restore ", TF_NONE},     /* Pro */
    {0x0422, "Break Off", TF_NONE},
    {0x0426, "Break Off", TF_NONE},   /* Pro */
    {0x0432, "Break On", TF_NONE},
    {0x0440, "Inc ", TF_NONE},
    {0x044A, "Dec ", TF_NONE},
    {0x0454, "Add ", TF_NONE},
    {0x0466, "Print #", TF_NONE},
    {0x0472, "Print ", TF_NONE},
    {0x047E, "Lprint ", TF_NONE},
    {0x048A, "Input$", TF_NONE},
    {0x04A2, "Using ", TF_NONE},
    {0x04AE, "Input #", TF_NONE},
    {0x04BA, "Line Input #", TF_NONE},
    {0x04BE, "Line Input #", TF_NONE},  /* Pro */
    {0x04CC, "Input ", TF_NONE},
    {0x04D8, "Line Input ", TF_NONE},
    {0x04E8, "Run ", TF_NONE},
    {0x04F6, "Run ", TF_NONE},          /* Pro variant (confirmed in Font8x8) */
    {0x04FA, "Set Buffer ", TF_NONE},
    {0x050A, "Mid$", TF_NONE},
    {0x0524, "Left$", TF_NONE},
    {0x0528, "Left$", TF_NONE},         /* Pro */
    {0x0532, "Right$", TF_NONE},
    {0x0542, "Flip$", TF_NONE},
    {0x054E, "Chr$", TF_NONE},
    {0x0552, "Chr$", TF_NONE},          /* Pro */
    {0x055A, "Space$", TF_NONE},
    {0x0568, "String$", TF_NONE},
    {0x0578, "Upper$", TF_NONE},
    {0x057C, "Upper$", TF_NONE},        /* Pro */
    {0x0586, "Lower$", TF_NONE},
    {0x0594, "Str$", TF_NONE},
    {0x05A0, "Val", TF_NONE},
    {0x05AA, "Bin$", TF_NONE},
    {0x05C0, "Hex$", TF_NONE},
    {0x05D6, "Len", TF_NONE},
    {0x05E0, "Instr", TF_NONE},
    {0x05E4, "Instr", TF_NONE},         /* Pro */
    {0x05FC, "Tab$", TF_NONE},
    {0x0606, "Free", TF_NONE},
    {0x0610, "Varptr", TF_NONE},
    {0x0614, "Varptr", TF_NONE},        /* Pro */
    {0x061C, "Remember X", TF_NONE},
    {0x062C, "Remember Y", TF_NONE},
    {0x063C, "Dim ", TF_NONE},
    {0x0646, "Rem ", TF_NONE},
    {0x064A, "Rem ", TF_NONE},          /* Pro Rem (documented in blueprint) */
    {0x064E, "'", TF_NONE},             /* 1.3 apostrophe comment */
    {0x0652, "'", TF_NONE},             /* Pro apostrophe comment */
    {0x0654, "Sort ", TF_NONE},
    {0x065E, "Match", TF_NONE},
    {0x066C, "Edit", TF_NONE},
    {0x0676, "Direct", TF_NONE},
    {0x0682, "Rnd", TF_NONE},
    {0x068C, "Randomize ", TF_NONE},
    {0x069C, "Sgn", TF_NONE},
    {0x06A6, "Abs", TF_NONE},
    {0x06B0, "Int", TF_NONE},
    {0x06BA, "Radian", TF_NONE},
    {0x06C6, "Degree", TF_NONE},
    {0x06D2, "Pi#", TF_NONE},
    {0x06DC, "Fix ", TF_NONE},
    {0x06E6, "Min", TF_NONE},
    {0x06F2, "Max", TF_NONE},
    {0x06FE, "Sin", TF_NONE},
    {0x0708, "Cos", TF_NONE},
    {0x0712, "Tan", TF_NONE},
    {0x071C, "Asin", TF_NONE},
    {0x0728, "Acos", TF_NONE},
    {0x0734, "Atan", TF_NONE},
    {0x0740, "Hsin", TF_NONE},
    {0x074C, "Hcos", TF_NONE},
    {0x0758, "Htan", TF_NONE},
    {0x0764, "Sqr", TF_NONE},
    {0x076E, "Log", TF_NONE},
    {0x0778, "Ln", TF_NONE},
    {0x0782, "Exp", TF_NONE},

    /* ── Menus ──────────────────────────────────────────────────── */
    {0x078C, "Menu To Bank ", TF_NONE},
    {0x07A0, "Bank To Menu ", TF_NONE},
    {0x07B4, "Menu On", TF_NONE},
    {0x07C2, "Menu Off", TF_NONE},
    {0x07D0, "Menu Calc", TF_NONE},
    {0x07E0, "Menu Mouse On", TF_NONE},
    {0x07F4, "Menu Mouse Off", TF_NONE},
    {0x0808, "Menu Base ", TF_NONE},
    {0x081A, "Set Menu ", TF_NONE},
    {0x082E, "X Menu", TF_NONE},
    {0x083C, "Y Menu", TF_NONE},
    {0x084A, "Menu Key ", TF_NONE},
    {0x085E, "Menu Bar ", TF_NONE},
    {0x086E, "Menu Line ", TF_NONE},
    {0x087E, "Menu Tline ", TF_NONE},
    {0x0890, "Menu Movable ", TF_NONE},
    {0x08A4, "Menu Static ", TF_NONE},
    {0x08B6, "Menu Item Movable ", TF_NONE},
    {0x08CE, "Menu Item Static ", TF_NONE},
    {0x08E6, "Menu Active ", TF_NONE},
    {0x08F8, "Menu Inactive ", TF_NONE},
    {0x090C, "Menu Separate ", TF_NONE},
    {0x0920, "Menu Link ", TF_NONE},
    {0x0930, "Menu Called ", TF_NONE},
    {0x0942, "Menu Once ", TF_NONE},
    {0x0952, "Menu Del", TF_NONE},
    {0x0960, "Menu$", TF_NONE},
    {0x096C, "Choice", TF_NONE},

    /* ── Screen Commands ────────────────────────────────────────── */
    {0x0982, "Screen Copy ", TF_NONE},
    {0x09D2, "Screen Clone ", TF_NONE},
    {0x09E6, "Screen Open ", TF_NONE},
    {0x0A00, "Screen Close ", TF_NONE},
    {0x0A04, "Screen Close ", TF_NONE},   /* Pro */
    {0x0A14, "Screen Display ", TF_NONE},
    {0x0A18, "Screen Display ", TF_NONE}, /* Pro */
    {0x0A32, "Screen Offset ", TF_NONE},
    {0x0A4A, "Screen Size", TF_NONE},
    {0x0A5A, "Screen Colour", TF_NONE},
    {0x0A6E, "Screen To Front", TF_NONE},
    {0x0A8C, "Screen To Back", TF_NONE},
    {0x0AAA, "Screen Hide", TF_NONE},
    {0x0AC4, "Screen Show", TF_NONE},
    {0x0ADE, "Screen Swap", TF_NONE},
    {0x0AF8, "Save Iff ", TF_NONE},
    {0x0B12, "View", TF_NONE},
    {0x0B1C, "Auto View Off", TF_NONE},
    {0x0B30, "Auto View On", TF_NONE},
    {0x0B42, "Screen Base", TF_NONE},
    {0x0B54, "Screen Width", TF_NONE},
    {0x0B70, "Screen Height", TF_NONE},
    {0x0B8C, "Get Palette ", TF_NONE},
    {0x0BAA, "Cls", TF_NONE},
    {0x0BB8, "Cls ", TF_NONE},             /* Pro variant with arg */
    {0x0BCC, "Def Scroll ", TF_NONE},
    {0x0BEA, "X Hard", TF_NONE},
    {0x0C02, "Y Hard", TF_NONE},
    {0x0C1A, "X Screen", TF_NONE},
    {0x0C34, "Y Screen", TF_NONE},
    {0x0C4E, "X Text", TF_NONE},
    {0x0C5C, "Y Text", TF_NONE},
    {0x0C6A, "Screen ", TF_NONE},
    {0x0C80, "Hires", TF_NONE},
    {0x0C8C, "Lowres", TF_NONE},
    {0x0C98, "Dual Playfield ", TF_NONE},
    {0x0CB0, "Dual Priority ", TF_NONE},
    {0x0CC6, "Wait Vbl", TF_NONE},
    {0x0CCA, "Wait Vbl", TF_NONE},        /* Pro */
    {0x0CD4, "Default Palette", TF_NONE},
    {0x0CEA, "Default", TF_NONE},
    {0x0CF8, "Palette ", TF_NONE},
    {0x0D06, "Colour Back ", TF_NONE},
    {0x0D18, "Colour ", TF_NONE},
    {0x0D30, "Flash Off", TF_NONE},
    {0x0D34, "Flash Off", TF_NONE},       /* Pro */
    {0x0D40, "Flash ", TF_NONE},
    {0x0D4E, "Shift Off", TF_NONE},
    {0x0D5E, "Shift Up ", TF_NONE},
    {0x0D74, "Shift Down ", TF_NONE},
    {0x0D8C, "Set Rainbow ", TF_NONE},
    {0x0DBE, "Rainbow Del", TF_NONE},
    {0x0DD8, "Rainbow ", TF_NONE},
    {0x0DEC, "Rain ", TF_NONE},
    {0x0DFA, "Fade ", TF_NONE},
    {0x0E04, "Phybase", TF_NONE},
    {0x0E12, "Physic", TF_NONE},
    {0x0E28, "Autoback ", TF_NONE},
    {0x0E38, "Plot ", TF_NONE},
    {0x0E52, "Point", TF_NONE},
    {0x0E60, "Draw To ", TF_NONE},
    {0x0E70, "Draw ", TF_NONE},
    {0x0E82, "Ellipse ", TF_NONE},
    {0x0E96, "Circle ", TF_NONE},
    {0x0EA8, "Polyline ", TF_NONE},
    {0x0EB6, "Polygon ", TF_NONE},
    {0x0EC4, "Bar ", TF_NONE},
    {0x0ED4, "Box ", TF_NONE},
    {0x0EE4, "Paint ", TF_NONE},
    {0x0F00, "Gr Locate ", TF_NONE},
    {0x0F12, "Text Length", TF_NONE},
    {0x0F24, "Text Styles", TF_NONE},
    {0x0F36, "Text Base", TF_NONE},
    {0x0F46, "Text ", TF_NONE},
    {0x0F56, "Set Text ", TF_NONE},
    {0x0F66, "Set Paint ", TF_NONE},
    {0x0F76, "Get Fonts", TF_NONE},
    {0x0F86, "Get Disc Fonts", TF_NONE},
    {0x0F9A, "Get Rom Fonts", TF_NONE},
    {0x0FAE, "Set Font ", TF_NONE},
    {0x0FBE, "Font$", TF_NONE},
    {0x0FCA, "Hslider ", TF_NONE},
    {0x0FE4, "Vslider ", TF_NONE},
    {0x0FFE, "Set Slider ", TF_NONE},
    {0x101E, "Set Pattern ", TF_NONE},
    {0x1030, "Set Line ", TF_NONE},
    {0x1040, "Ink ", TF_NONE},
    {0x1062, "Gr Writing ", TF_NONE},
    {0x1074, "Clip", TF_NONE},
    {0x108E, "Set Tempras", TF_NONE},
    {0x10B2, "Appear ", TF_NONE},
    {0x10D2, "Zoom ", TF_NONE},
    {0x10F0, "Get Cblock ", TF_NONE},
    {0x110A, "Put Cblock ", TF_NONE},
    {0x1128, "Del Cblock", TF_NONE},
    {0x1142, "Get Block ", TF_NONE},
    {0x116E, "Put Block ", TF_NONE},
    {0x11AA, "Del Block", TF_NONE},
    {0x11C2, "Key Speed ", TF_NONE},
    {0x11D4, "Key State", TF_NONE},
    {0x11E4, "Key Shift", TF_NONE},
    {0x11F4, "Joy", TF_NONE},
    {0x11FE, "Jup", TF_NONE},
    {0x1208, "Jdown", TF_NONE},
    {0x1214, "Jleft", TF_NONE},
    {0x1220, "Jright", TF_NONE},
    {0x122E, "Fire", TF_NONE},
    {0x123A, "True", TF_NONE},
    {0x1244, "False", TF_NONE},
    {0x1250, "Put Key ", TF_NONE},
    {0x125E, "Scancode", TF_NONE},
    {0x126C, "Scanshift", TF_NONE},
    {0x1280, "Clear Key", TF_NONE},      /* Pro */
    {0x127C, "Clear Key", TF_NONE},
    {0x128C, "Wait Key", TF_NONE},
    {0x129A, "Wait ", TF_NONE},
    {0x12A6, "Key$", TF_NONE},
    {0x12B2, "Scan$", TF_NONE},
    {0x12CA, "Timer", TF_NONE},
    {0x12D6, "Wind Open ", TF_NONE},
    {0x1316, "Wind Close", TF_NONE},
    {0x1326, "Wind Save", TF_NONE},
    {0x1336, "Wind Move ", TF_NONE},
    {0x1348, "Wind Size ", TF_NONE},
    {0x135A, "Window ", TF_NONE},
    {0x1368, "Windon", TF_NONE},
    {0x1374, "Locate ", TF_NONE},
    {0x1384, "Clw", TF_NONE},
    {0x138E, "Home", TF_NONE},
    {0x1398, "Curs Pen ", TF_NONE},
    {0x13A8, "Pen$", TF_NONE},
    {0x13B4, "Paper$", TF_NONE},
    {0x13C2, "At", TF_NONE},
    {0x13CE, "Pen ", TF_NONE},
    {0x13D2, "Pen ", TF_NONE},            /* Pro */
    {0x13D8, "Paper ", TF_NONE},
    {0x13DC, "Paper ", TF_NONE},          /* Pro */
    {0x13E4, "Centre ", TF_NONE},
    {0x13F2, "Border ", TF_NONE},
    {0x1404, "Writing ", TF_NONE},
    {0x141E, "Title Top ", TF_NONE},
    {0x142E, "Title Bottom ", TF_NONE},
    {0x1442, "Curs Off", TF_NONE},
    {0x1446, "Curs Off", TF_NONE},        /* Pro */
    {0x1450, "Curs On", TF_NONE},
    {0x145E, "Inverse Off", TF_NONE},
    {0x1470, "Inverse On", TF_NONE},
    {0x1480, "Under Off", TF_NONE},
    {0x1490, "Under On", TF_NONE},
    {0x149E, "Shade Off", TF_NONE},
    {0x14AE, "Shade On", TF_NONE},
    {0x14BC, "Scroll Off", TF_NONE},
    {0x14CC, "Scroll On", TF_NONE},
    {0x14DC, "Scroll ", TF_NONE},
    {0x14EA, "Cup$", TF_NONE},
    {0x14F4, "Cdown$", TF_NONE},
    {0x1500, "Cleft$", TF_NONE},
    {0x150C, "Cright$", TF_NONE},
    {0x151A, "Cup", TF_NONE},
    {0x1524, "Cdown", TF_NONE},
    {0x1530, "Cleft", TF_NONE},
    {0x153C, "Cright", TF_NONE},
    {0x1548, "Memorize X", TF_NONE},
    {0x1558, "Memorize Y", TF_NONE},
    {0x1568, "Cmove$", TF_NONE},
    {0x1578, "Cmove ", TF_NONE},
    {0x1586, "Cline", TF_NONE},
    {0x159A, "Hscroll ", TF_NONE},
    {0x15A8, "Vscroll ", TF_NONE},
    {0x15B6, "Set Tab ", TF_NONE},
    {0x15C4, "Set Curs ", TF_NONE},
    {0x15E2, "X Curs", TF_NONE},
    {0x15EE, "Y Curs", TF_NONE},
    {0x15FA, "X Graphic", TF_NONE},
    {0x160A, "Y Graphic", TF_NONE},
    {0x161A, "Xgr", TF_NONE},
    {0x1624, "Ygr", TF_NONE},
    {0x162E, "Reserve Zone", TF_NONE},
    {0x164A, "Reset Zone", TF_NONE},
    {0x1664, "Set Zone ", TF_NONE},
    {0x167C, "Zone", TF_NONE},
    {0x1696, "Hzone", TF_NONE},
    {0x16B2, "Scin", TF_NONE},
    {0x16CC, "Mouse Screen", TF_NONE},
    {0x16DE, "Mouse Zone", TF_NONE},
    {0x16EE, "Set Input ", TF_NONE},
    {0x16F2, "Set Input ", TF_NONE},      /* Pro */
    {0x1700, "Close Workbench", TF_NONE},
    {0x1716, "Close Editor", TF_NONE},
    {0x1728, "Dir First$", TF_NONE},
    {0x173A, "Dir Next$", TF_NONE},
    {0x174A, "Exist", TF_NONE},
    {0x1756, "Dir$", TF_NONE},
    {0x178C, "Ldir", TF_NONE},
    {0x17A0, "Dir ", TF_NONE},
    {0x17B2, "Set Dir ", TF_NONE},
    {0x17D0, "Load Iff ", TF_NONE},
    {0x17EA, "Mask Iff ", TF_NONE},
    {0x17FA, "Picture", TF_NONE},
    {0x1808, "Bload ", TF_NONE},
    {0x1816, "Bsave ", TF_NONE},
    {0x1826, "Pload ", TF_NONE},
    {0x1834, "Save ", TF_NONE},
    {0x184A, "Load ", TF_NONE},
    {0x1860, "Dfree", TF_NONE},
    {0x186C, "Mkdir ", TF_NONE},
    {0x1878, "Lof", TF_NONE},
    {0x1882, "Eof", TF_NONE},
    {0x188C, "Pof", TF_NONE},
    {0x1898, "Port", TF_NONE},
    {0x18A4, "Open Random ", TF_NONE},
    {0x18B8, "Open In ", TF_NONE},
    {0x18BC, "Open In ", TF_NONE},        /* Pro */
    {0x18C8, "Open Out ", TF_NONE},
    {0x18CC, "Open Out ", TF_NONE},       /* Pro */
    {0x18DA, "Open Port ", TF_NONE},
    {0x18EC, "Append ", TF_NONE},
    {0x18FC, "Close ", TF_NONE},
    {0x1900, "Close ", TF_NONE},          /* Pro */
    {0x1910, "Parent", TF_NONE},
    {0x191C, "Rename ", TF_NONE},
    {0x192C, "Kill ", TF_NONE},
    {0x1938, "Drive", TF_NONE},
    {0x1944, "Field ", TF_NONE},
    {0x1950, "Fsel$", TF_NONE},

    /* ── Sprites & Bobs ─────────────────────────────────────────── */
    {0x1982, "Set Sprite Buffer ", TF_NONE},
    {0x199A, "Sprite Off", TF_NONE},
    {0x19B4, "Sprite Priority ", TF_NONE},
    {0x19CA, "Sprite Update Off", TF_NONE},
    {0x19E2, "Sprite Update On", TF_NONE},
    {0x19F8, "Sprite Update", TF_NONE},
    {0x1A0C, "Spritebob Col", TF_NONE},
    {0x1A2E, "Sprite Col", TF_NONE},
    {0x1A4C, "Set Hardcol ", TF_NONE},
    {0x1A60, "Hardcol", TF_NONE},
    {0x1A6E, "Sprite Base", TF_NONE},
    {0x1A80, "Icon Base", TF_NONE},
    {0x1A90, "Sprite ", TF_NONE},
    {0x1AA4, "Bob Off", TF_NONE},
    {0x1ABA, "Bob Update Off", TF_NONE},
    {0x1ACE, "Bob Update On", TF_NONE},
    {0x1AE2, "Bob Update", TF_NONE},
    {0x1AF2, "Bob Clear", TF_NONE},
    {0x1B02, "Bob Draw", TF_NONE},
    {0x1B10, "Bobsprite Col", TF_NONE},
    {0x1B32, "Bob Col", TF_NONE},
    {0x1B4E, "Col", TF_NONE},
    {0x1B58, "Limit Bob", TF_NONE},
    {0x1B86, "Set Bob ", TF_NONE},
    {0x1B9A, "Bob ", TF_NONE},
    {0x1BAA, "Get Sprite Palette", TF_NONE},
    {0x1BCC, "Get Sprite ", TF_NONE},
    {0x1BF8, "Get Bob ", TF_NONE},
    {0x1C22, "Del Sprite ", TF_NONE},
    {0x1C3E, "Del Bob ", TF_NONE},
    {0x1C58, "Del Icon ", TF_NONE},
    {0x1C72, "Ins Sprite ", TF_NONE},
    {0x1C84, "Ins Bob ", TF_NONE},
    {0x1C92, "Ins Icon ", TF_NONE},
    {0x1CA2, "Get Icon Palette", TF_NONE},
    {0x1CC2, "Get Icon ", TF_NONE},
    {0x1CEC, "Put Bob ", TF_NONE},
    {0x1CFA, "Paste Bob ", TF_NONE},
    {0x1D0E, "Paste Icon ", TF_NONE},
    {0x1D24, "Make Mask", TF_NONE},
    {0x1D3C, "No Mask", TF_NONE},
    {0x1D52, "Make Icon Mask", TF_NONE},
    {0x1D70, "No Icon Mask", TF_NONE},
    {0x1D8C, "Hot Spot ", TF_NONE},
    {0x1DAA, "Priority On", TF_NONE},
    {0x1DBC, "Priority Off", TF_NONE},
    {0x1DCE, "Hide On", TF_NONE},
    {0x1DDC, "Hide", TF_NONE},
    {0x1DE6, "Show On", TF_NONE},
    {0x1DF4, "Show", TF_NONE},
    {0x1DFE, "Change Mouse ", TF_NONE},
    {0x1E12, "X Mouse", TF_NONE},
    {0x1E20, "Y Mouse", TF_NONE},
    {0x1E2E, "Mouse Key", TF_NONE},
    {0x1E3E, "Mouse Click", TF_NONE},
    {0x1E50, "Limit Mouse", TF_NONE},
    {0x1E78, "Unfreeze", TF_NONE},
    {0x1E86, "Move X ", TF_NONE},
    {0x1EA2, "Move Y ", TF_NONE},
    {0x1EBE, "Move Off", TF_NONE},
    {0x1ED6, "Move On", TF_NONE},
    {0x1EEC, "Move Freeze", TF_NONE},
    {0x1F06, "Anim Off", TF_NONE},
    {0x1F1E, "Anim On", TF_NONE},
    {0x1F34, "Anim Freeze", TF_NONE},
    {0x1F4E, "Anim ", TF_NONE},
    {0x1F68, "Movon", TF_NONE},
    {0x1F74, "Chanan", TF_NONE},
    {0x1F82, "Chanmv", TF_NONE},
    {0x1F90, "Channel ", TF_NONE},
    {0x1F9E, "Amreg", TF_NONE},
    {0x1FB8, "Amal On", TF_NONE},
    {0x1FCE, "Amal Off", TF_NONE},
    {0x1FE6, "Amal Freeze", TF_NONE},
    {0x2000, "Amalerr", TF_NONE},
    {0x200E, "Amal ", TF_NONE},
    {0x2028, "Amplay ", TF_NONE},
    {0x2046, "Synchro On", TF_NONE},
    {0x2056, "Synchro Off", TF_NONE},
    {0x2068, "Synchro", TF_NONE},
    {0x2076, "Update Off", TF_NONE},
    {0x2086, "Update On", TF_NONE},
    {0x2096, "Update Every ", TF_NONE},
    {0x20AA, "Update", TF_NONE},
    {0x20B6, "X Bob", TF_NONE},
    {0x20C2, "Y Bob", TF_NONE},
    {0x20CE, "X Sprite", TF_NONE},
    {0x20DE, "Y Sprite", TF_NONE},
    {0x20EE, "Reserve As Work ", TF_NONE},
    {0x2106, "Reserve As Chip Work ", TF_NONE},
    {0x2124, "Reserve As Data ", TF_NONE},
    {0x213C, "Reserve As Chip Data ", TF_NONE},
    {0x215A, "Erase ", TF_NONE},
    {0x2166, "List Bank", TF_NONE},
    {0x2176, "Chip Free", TF_NONE},
    {0x2186, "Fast Free", TF_NONE},
    {0x2196, "Fill ", TF_NONE},
    {0x21A6, "Copy ", TF_NONE},
    {0x21B6, "Hunt", TF_NONE},
    {0x21C6, "Poke ", TF_NONE},
    {0x21D4, "Loke ", TF_NONE},
    {0x21E2, "Peek", TF_NONE},
    {0x21EE, "Deek", TF_NONE},
    {0x21FA, "Leek", TF_NONE},
    {0x2206, "Bset ", TF_NONE},
    {0x2214, "Bclr ", TF_NONE},
    {0x2222, "Bchg ", TF_NONE},
    {0x2230, "Btst", TF_NONE},
    {0x223E, "Ror.b ", TF_NONE},
    {0x224C, "Ror.w ", TF_NONE},
    {0x225A, "Ror.l ", TF_NONE},
    {0x2268, "Rol.b ", TF_NONE},
    {0x2276, "Rol.w ", TF_NONE},
    {0x2284, "Rol.l ", TF_NONE},
    {0x2292, "Areg", TF_NONE},
    {0x229E, "Dreg", TF_NONE},
    {0x22A2, "Dreg", TF_NONE},            /* Pro */
    {0x22AA, "Copper On", TF_NONE},
    {0x22BA, "Copper Off", TF_NONE},
    {0x22CA, "Cop Swap", TF_NONE},
    {0x22D8, "Cop Reset", TF_NONE},
    {0x22E8, "Cop Wait ", TF_NONE},
    {0x2308, "Cop Movel ", TF_NONE},
    {0x231A, "Cop Move ", TF_NONE},
    {0x232C, "Cop Logic", TF_NONE},
    {0x233C, "Prg First$", TF_NONE},
    {0x234E, "Prg Next$", TF_NONE},
    {0x235E, "Psel$", TF_NONE},
    {0x2390, "Prun ", TF_NONE},
    {0x239C, "Bgrab ", TF_NONE},
    {0x23A8, "Put ", TF_NONE},
    {0x23B4, "Get ", TF_NONE},
    {0x23C0, "System", TF_NONE},
    {0x23CC, "Multi Wait", TF_NONE},
    {0x23DC, "I Bob", TF_NONE},
    {0x23E8, "I Sprite", TF_NONE},
    {0x23F8, "Priority Reverse On", TF_NONE},
    {0x2412, "Priority Reverse Off", TF_NONE},
    {0x242C, "Dev First$", TF_NONE},
    {0x243E, "Dev Next$", TF_NONE},
    {0x244E, "Hrev Block ", TF_NONE},
    {0x2460, "Vrev Block ", TF_NONE},
    {0x2472, "Hrev", TF_NONE},
    {0x247E, "Vrev", TF_NONE},
    {0x248A, "Rev", TF_NONE},
    {0x2494, "Bank Swap ", TF_NONE},
    {0x24A6, "Amos To Front", TF_NONE},
    {0x24BA, "Amos To Back", TF_NONE},
    {0x24CC, "Amos Here", TF_NONE},
    {0x24DC, "Amos Lock", TF_NONE},
    {0x24EC, "Amos Unlock", TF_NONE},
    {0x24FE, "Display Height", TF_NONE},
    {0x2512, "Ntsc", TF_NONE},
    {0x251C, "Laced", TF_NONE},
    {0x2528, "Prg State", TF_NONE},
    {0x2538, "Command Line$", TF_NONE},
    {0x254C, "Disc Info$", TF_NONE},

    /* ── AMOS Pro Specific Tokens ───────────────────────────────── */
    /* These are Pro-only tokens observed in actual .AMOS files */
    /* Missing Pro variants of standard tokens */
    {0x039E, "Shared ", TF_NONE},        /* Pro Shared */
    {0x046A, "Print #", TF_NONE},        /* Pro Print # */
    {0x050E, "Mid$", TF_NONE},           /* Pro Mid$ (3-arg variant) */
    {0x05A4, "Val", TF_NONE},            /* Pro Val */
    {0x05DA, "Len", TF_NONE},            /* Pro Len */

    /* Pro variants (+4 from 1.3 base) — discovered from real .AMOS programs */
    {0x011C, "Border$", TF_NONE},             /* Pro Border$ */
    {0x0356, "Step ", TF_NONE},               /* Pro Step */
    {0x0360, "Return", TF_NONE},              /* Pro Return */
    {0x0386, "Proc ", TF_BRANCH},             /* Pro Proc */
    {0x03EE, "Error ", TF_NONE},              /* Pro Error */
    {0x0458, "Add ", TF_NONE},                /* Pro Add */
    {0x0476, "Print ", TF_NONE},              /* Pro Print */
    {0x0640, "Dim ", TF_NONE},                /* Pro Dim */
    {0x0670, "Edit", TF_NONE},                /* Pro Edit */
    {0x0686, "Rnd", TF_NONE},                 /* Pro Rnd */
    {0x0986, "Screen Copy ", TF_NONE},         /* Pro Screen Copy */
    {0x09EA, "Screen Open ", TF_NONE},         /* Pro Screen Open */
    {0x0A72, "Screen To Front", TF_NONE},      /* Pro Screen To Front */
    {0x0BAE, "Cls", TF_NONE},                 /* Pro Cls (no arg) */
    {0x0BB2, "Cls ", TF_NONE},                /* Pro Cls (with arg) */
    {0x0C6E, "Screen ", TF_NONE},              /* Pro Screen */
    {0x0C84, "Hires", TF_NONE},               /* Pro Hires */
    {0x0C90, "Lowres", TF_NONE},              /* Pro Lowres */
    {0x0CFC, "Palette ", TF_NONE},             /* Pro Palette */
    {0x0D1C, "Colour ", TF_NONE},              /* Pro Colour */
    {0x0DDC, "Rainbow ", TF_NONE},             /* Pro Rainbow */
    {0x0DFE, "Fade ", TF_NONE},                /* Pro Fade */
    {0x0E3C, "Plot ", TF_NONE},                /* Pro Plot */
    {0x0E74, "Draw ", TF_NONE},                /* Pro Draw */
    {0x0E86, "Ellipse ", TF_NONE},             /* Pro Ellipse */
    {0x0E9A, "Circle ", TF_NONE},              /* Pro Circle */
    {0x0EC8, "Bar ", TF_NONE},                 /* Pro Bar */
    {0x0ED8, "Box ", TF_NONE},                 /* Pro Box */
    {0x0F4A, "Text ", TF_NONE},                /* Pro Text */
    {0x0F6A, "Set Paint ", TF_NONE},           /* Pro Set Paint */
    {0x0FB2, "Set Font ", TF_NONE},            /* Pro Set Font */
    {0x1022, "Set Pattern ", TF_NONE},         /* Pro Set Pattern */
    {0x1044, "Ink ", TF_NONE},                 /* Pro Ink */
    {0x1078, "Clip", TF_NONE},                 /* Pro Clip */
    {0x10B6, "Appear ", TF_NONE},              /* Pro Appear */
    {0x129E, "Wait ", TF_NONE},                /* Pro Wait */
    {0x1378, "Locate ", TF_NONE},              /* Pro Locate */
    {0x13E8, "Centre ", TF_NONE},              /* Pro Centre */
    {0x14E0, "Scroll ", TF_NONE},              /* Pro Scroll */
    {0x1528, "Cdown", TF_NONE},               /* Pro Cdown */
    {0x158A, "Cline", TF_NONE},               /* Pro Cline */
    {0x1668, "Set Zone ", TF_NONE},            /* Pro Set Zone */
    {0x17D4, "Load Iff ", TF_NONE},            /* Pro Load Iff */
    {0x184E, "Load ", TF_NONE},                /* Pro Load */
    {0x1A94, "Sprite ", TF_NONE},              /* Pro Sprite */
    {0x1BAE, "Get Sprite Palette", TF_NONE},   /* Pro Get Sprite Palette */
    {0x1CFE, "Paste Bob ", TF_NONE},           /* Pro Paste Bob */
    {0x1DE0, "Hide", TF_NONE},                /* Pro Hide */
    {0x1E32, "Mouse Key", TF_NONE},           /* Pro Mouse Key */
    {0x1FBC, "Amal On", TF_NONE},             /* Pro Amal On */
    {0x2946, "Err$", TF_NONE},                /* Pro Err$ (error message) */

    /* Pro-only tokens */
    {0x2578, "Comp Test Off", TF_NONE},
    {0x259A, "Trap ", TF_NONE},
    {0x2676, "", TF_NONE},              /* Pro accessor call prefix */
    {0x2694, "", TF_NONE},              /* Pro accessor */
    {0x26A0, "", TF_NONE},              /* Pro accessor */
    {0x26B2, "", TF_NONE},              /* Pro accessor */
    {0x26D8, "", TF_NONE},              /* Pro accessor */
    {0x2704, "", TF_NONE},              /* Pro accessor */
    {0x2750, "", TF_NONE},              /* Pro accessor */
    {0x28EE, "", TF_NONE},              /* Pro accessor */
    {0x2962, "Errtrap", TF_NONE},       /* Pro */
    {0x2A40, "", TF_NONE},              /* Pro accessor call */
    {0x2AB0, "", TF_NONE},              /* Pro accessor call */

    /* Sentinel */
    {0x0000, NULL, TF_NONE},
};

#define TOKEN_TABLE_SIZE (sizeof(token_table) / sizeof(token_table[0]) - 1)

/* ── Token Lookup ──────────────────────────────────────────────── */

static const char *lookup_token(uint16_t token, uint8_t *flags_out)
{
    for (size_t i = 0; i < TOKEN_TABLE_SIZE; i++) {
        if (token_table[i].token == token) {
            if (flags_out) *flags_out = token_table[i].flags;
            return token_table[i].keyword;
        }
    }
    if (flags_out) *flags_out = TF_NONE;
    return NULL;
}

/* ── AMOS Float → double Conversion ───────────────────────────── */

/*
 * AMOS float format (Motorola FFP — Fast Floating Point):
 *   Bits 31-8: 24-bit mantissa (with implicit bit 31 set = 1.xxx)
 *   Bit 7:     sign (0=positive, 1=negative)
 *   Bits 6-0:  biased exponent (bias = 64, so exp = bits - 64)
 *
 * Value = (-1)^sign * mantissa * 2^(exponent - 88)
 *   where mantissa is the 24-bit value interpreted as 0.xxx binary
 *   and 88 = 64 (bias) + 24 (mantissa bits)
 */
static double amos_float_to_double(uint32_t raw)
{
    if (raw == 0) return 0.0;

    uint32_t mantissa = (raw >> 8) & 0x00FFFFFF;
    int sign = (raw >> 7) & 1;
    int exponent = raw & 0x7F;

    if (mantissa == 0) return 0.0;

    /* Mantissa is 24 bits with the MSB being 1 (normalized) */
    double value = (double)mantissa / (double)(1 << 24);
    value *= pow(2.0, exponent - 64);

    return sign ? -value : value;
}

/* ── Detect File Type ──────────────────────────────────────────── */

static amos_file_type_t detect_file_type(const uint8_t *data, size_t length)
{
    if (length < AMOS_HEADER_SIZE + 4) return AMOS_FILE_UNKNOWN;

    if (memcmp(data, "AMOS Basic", 10) == 0) return AMOS_FILE_13;
    if (memcmp(data, "AMOS Pro",    8) == 0) return AMOS_FILE_PRO;

    return AMOS_FILE_UNKNOWN;
}

/* ── Detokenizer ───────────────────────────────────────────────── */

/*
 * Append text to a dynamically growing output buffer.
 */
typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} strbuf_t;

static void sb_init(strbuf_t *sb, size_t initial)
{
    sb->cap = initial;
    sb->buf = malloc(sb->cap);
    sb->len = 0;
    sb->buf[0] = '\0';
}

static void sb_ensure(strbuf_t *sb, size_t extra)
{
    while (sb->len + extra + 1 > sb->cap) {
        sb->cap *= 2;
        sb->buf = realloc(sb->buf, sb->cap);
    }
}

static void sb_append(strbuf_t *sb, const char *s)
{
    size_t slen = strlen(s);
    sb_ensure(sb, slen);
    memcpy(sb->buf + sb->len, s, slen);
    sb->len += slen;
    sb->buf[sb->len] = '\0';
}

static void sb_appendf(strbuf_t *sb, const char *fmt, ...)
{
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    sb_append(sb, tmp);
}

static void sb_appendc(strbuf_t *sb, char c)
{
    sb_ensure(sb, 1);
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
}

char *amos_detokenize(const uint8_t *data, size_t length)
{
    strbuf_t out;
    sb_init(&out, length * 4);  /* Rough estimate: 4x expansion */

    size_t pos = 0;
    int line_count = 0;

    while (pos < length) {
        if (pos + 1 >= length) break;

        uint8_t line_len_half = data[pos];
        uint8_t indent_byte  = data[pos + 1];

        if (line_len_half == 0) break;

        size_t line_bytes = (size_t)line_len_half * 2;
        if (pos + line_bytes > length) {
            fprintf(stderr, "amos_loader: line %d truncated at offset 0x%zX\n",
                    line_count, pos);
            break;
        }

        /* Emit indentation */
        int indent = (indent_byte > 1) ? indent_byte - 1 : 0;
        for (int i = 0; i < indent; i++) {
            sb_appendc(&out, ' ');
        }

        /* Parse token stream within this line */
        size_t tpos = pos + 2;
        size_t line_end = pos + line_bytes;
        while (tpos + 1 < line_end) {
            uint16_t token = BE16(data + tpos);

            if (token == 0x0000) {
                /* End of line marker */
                tpos += 2;
                break;
            }

            /* ── Special tokens with inline data ──────────────── */

            if (token == TK_VAR) {
                /* Variable reference:
                 * +2: 2 bytes reserved (linking offset)
                 * +4: 1 byte name length
                 * +5: 1 byte type flags (bit0=float#, bit1=string$)
                 * +6: N bytes name, padded to even
                 */
                if (tpos + 5 >= line_end) goto skip_token;
                uint8_t name_len = data[tpos + 4];
                uint8_t type_flags = data[tpos + 5];
                if (tpos + 6 + name_len > line_end) goto skip_token;

                /* Output variable name (skip null bytes) */
                for (int i = 0; i < name_len; i++) {
                    char c = (char)data[tpos + 6 + i];
                    if (c != '\0') sb_appendc(&out, c);
                }
                /* Append type suffix */
                if (type_flags & 1) sb_appendc(&out, '#');
                if (type_flags & 2) sb_appendc(&out, '$');

                size_t padded = (name_len + 1) & ~(size_t)1;
                tpos += 6 + padded;
                continue;
            }

            if (token == TK_LABEL_DEF) {
                /* Label definition — same format as variable */
                if (tpos + 5 >= line_end) goto skip_token;
                uint8_t name_len = data[tpos + 4];
                if (tpos + 6 + name_len > line_end) goto skip_token;

                for (int i = 0; i < name_len; i++) {
                    char c = (char)data[tpos + 6 + i];
                    if (c != '\0') sb_appendc(&out, c);
                }
                sb_appendc(&out, ':');

                size_t padded = (name_len + 1) & ~(size_t)1;
                tpos += 6 + padded;
                continue;
            }

            if (token == TK_PROC_REF) {
                /* Procedure reference — same format as variable */
                if (tpos + 5 >= line_end) goto skip_token;
                uint8_t name_len = data[tpos + 4];
                uint8_t type_flags = data[tpos + 5];
                (void)type_flags;
                if (tpos + 6 + name_len > line_end) goto skip_token;

                for (int i = 0; i < name_len; i++) {
                    char c = (char)data[tpos + 6 + i];
                    if (c != '\0') sb_appendc(&out, c);
                }

                size_t padded = (name_len + 1) & ~(size_t)1;
                tpos += 6 + padded;
                continue;
            }

            if (token == TK_LABEL_REF) {
                /* Label reference (goto/gosub target) — same format */
                if (tpos + 5 >= line_end) goto skip_token;
                uint8_t name_len = data[tpos + 4];
                if (tpos + 6 + name_len > line_end) goto skip_token;

                for (int i = 0; i < name_len; i++) {
                    char c = (char)data[tpos + 6 + i];
                    if (c != '\0') sb_appendc(&out, c);
                }

                size_t padded = (name_len + 1) & ~(size_t)1;
                tpos += 6 + padded;
                continue;
            }

            if (token == TK_INT) {
                /* Decimal integer — 4-byte BE int32 */
                if (tpos + 5 >= line_end) goto skip_token;
                int32_t val = BE32S(data + tpos + 2);
                sb_appendf(&out, "%d", val);
                tpos += 6;
                continue;
            }

            if (token == TK_HEX) {
                /* Hex integer — 4-byte BE uint32 */
                if (tpos + 5 >= line_end) goto skip_token;
                uint32_t val = BE32(data + tpos + 2);
                sb_appendf(&out, "$%X", val);
                tpos += 6;
                continue;
            }

            if (token == TK_BIN) {
                /* Binary integer — 4-byte BE uint32 */
                if (tpos + 5 >= line_end) goto skip_token;
                uint32_t val = BE32(data + tpos + 2);
                sb_append(&out, "%");
                /* Output binary digits */
                if (val == 0) {
                    sb_appendc(&out, '0');
                } else {
                    int started = 0;
                    for (int bit = 31; bit >= 0; bit--) {
                        if (val & ((uint32_t)1 << bit)) {
                            sb_appendc(&out, '1');
                            started = 1;
                        } else if (started) {
                            sb_appendc(&out, '0');
                        }
                    }
                }
                tpos += 6;
                continue;
            }

            if (token == TK_FLOAT) {
                /* AMOS float — 4-byte Motorola FFP */
                if (tpos + 5 >= line_end) goto skip_token;
                uint32_t raw = BE32(data + tpos + 2);
                double val = amos_float_to_double(raw);
                sb_appendf(&out, "%g", val);
                tpos += 6;
                continue;
            }

            if (token == TK_STRING || token == TK_STRING2) {
                /* String literal — 2-byte BE length + ASCII + pad */
                if (tpos + 3 >= line_end) goto skip_token;
                uint16_t str_len = BE16(data + tpos + 2);
                if (tpos + 4 + str_len > length) goto skip_token;

                sb_appendc(&out, '"');
                for (int i = 0; i < str_len; i++) {
                    sb_appendc(&out, (char)data[tpos + 4 + i]);
                }
                sb_appendc(&out, '"');

                size_t padded = (str_len + 1) & ~(size_t)1;
                tpos += 4 + padded;
                continue;
            }

            if (token == TK_EXT) {
                /* Extension token:
                 * +2: 1 byte extension number
                 * +3: 1 byte unused
                 * +4: 2 bytes offset into extension token table
                 */
                if (tpos + 5 >= line_end) goto skip_token;
                uint8_t ext_num = data[tpos + 2];
                uint16_t ext_off = BE16(data + tpos + 4);
                sb_appendf(&out, "[Ext%d:$%04X]", ext_num, ext_off);
                tpos += 6;
                continue;
            }

            /* ── Rem / Comment tokens ─────────────────────────── */
            /* These have token values in the 0x064x-0x065x range
             * Format: token(2) + unused(1) + length(1) + text(N) */
            if (token == 0x064A || token == 0x0646) {
                /* Rem statement */
                if (tpos + 3 >= line_end) goto skip_token;
                uint8_t text_len = data[tpos + 3];
                sb_append(&out, "Rem");
                if (text_len > 0 && tpos + 4 + text_len <= length) {
                    sb_appendc(&out, ' ');
                    for (int i = 0; i < text_len; i++) {
                        sb_appendc(&out, (char)data[tpos + 4 + i]);
                    }
                }
                size_t padded = (text_len + 1) & ~(size_t)1;
                tpos += 4 + padded;
                continue;
            }

            if (token == 0x0652 || token == 0x064E) {
                /* ' (apostrophe comment) — same format as Rem */
                if (tpos + 3 >= line_end) goto skip_token;
                uint8_t text_len = data[tpos + 3];
                sb_appendc(&out, '\'');
                if (text_len > 0 && tpos + 4 + text_len <= length) {
                    sb_appendc(&out, ' ');
                    for (int i = 0; i < text_len; i++) {
                        sb_appendc(&out, (char)data[tpos + 4 + i]);
                    }
                }
                size_t padded = (text_len + 1) & ~(size_t)1;
                tpos += 4 + padded;
                continue;
            }

            /* ── Procedure definition token (0x0376 AMOS Pro) ──── */
            if (token == 0x0376 || token == 0x0372) {
                /* Procedure token: 2 bytes token + 8 bytes metadata
                 * (2-byte distance, 2-byte seed, 4-byte flags)
                 * The procedure name follows as a proc name token.
                 */
                sb_append(&out, "Procedure ");
                tpos += 2 + 8;  /* Skip token + 8 metadata bytes */
                continue;
            }

            /* ── Regular keyword lookup ────────────────────────── */
            {
                uint8_t flags = TF_NONE;
                const char *kw = lookup_token(token, &flags);
                if (kw) {
                    if (kw[0] != '\0') {
                        sb_append(&out, kw);
                    }
                    tpos += 2;
                    /* Skip branch distance word(s) if this token has them */
                    if ((flags & TF_BRANCH2) && tpos + 3 < line_end) {
                        tpos += 4;  /* Skip 2 words */
                    } else if ((flags & TF_BRANCH) && tpos + 1 < line_end) {
                        tpos += 2;  /* Skip 1 word */
                    }

                    /* Smart spacing: if keyword doesn't end with space/paren/operator,
                     * and the next token is a literal, variable, or keyword that starts
                     * with an alphanumeric, add a space to prevent concatenation */
                    if (kw[0] != '\0' && tpos + 1 < line_end) {
                        size_t klen = strlen(kw);
                        char last_ch = kw[klen - 1];
                        if (last_ch != ' ' && last_ch != '(' && last_ch != ')' &&
                            last_ch != ',' && last_ch != ';') {
                            uint16_t next_tok = BE16(data + tpos);
                            /* Check if next token will produce alphanumeric output */
                            if (next_tok == TK_VAR || next_tok == TK_INT ||
                                next_tok == TK_FLOAT || next_tok == TK_HEX ||
                                next_tok == TK_BIN || next_tok == TK_STRING ||
                                next_tok == TK_STRING2 || next_tok == TK_PROC_REF ||
                                next_tok == TK_LABEL_REF) {
                                sb_appendc(&out, ' ');
                            }
                        }
                    }

                    continue;
                }
            }

            /* ── Unknown token — output as placeholder ─────────── */
        skip_token:
            sb_appendf(&out, "[?0x%04X]", token);
            tpos += 2;
        }

        /* End of line */
        sb_appendc(&out, '\n');
        pos += line_bytes;
        line_count++;
    }

    fprintf(stderr, "amos_loader: detokenized %d lines\n", line_count);
    return out.buf;
}

/* ── Bank Section Handler ──────────────────────────────────────── */

static void process_banks(const uint8_t *data, size_t offset, size_t length)
{
    if (offset + 6 > length) return;
    if (memcmp(data + offset, "AmBs", 4) != 0) {
        fprintf(stderr, "amos_loader: no bank section found\n");
        return;
    }

    uint16_t bank_count = BE16(data + offset + 4);
    fprintf(stderr, "amos_loader: bank section found, %d bank(s)\n", bank_count);

    size_t bpos = offset + 6;
    for (int i = 0; i < bank_count && bpos + 4 < length; i++) {
        char magic[5] = {0};
        memcpy(magic, data + bpos, 4);

        if (memcmp(magic, "AmBk", 4) == 0) {
            /* Standard bank */
            if (bpos + 20 > length) break;
            uint16_t bank_num = BE16(data + bpos + 4);
            uint16_t mem_type = BE16(data + bpos + 6);
            uint32_t bank_len = BE32(data + bpos + 8) & 0x0FFFFFFF;
            char type_name[9] = {0};
            memcpy(type_name, data + bpos + 12, 8);
            fprintf(stderr, "  Bank %d: type=\"%.8s\" size=%u mem=%s\n",
                    bank_num, type_name, bank_len,
                    mem_type == 0 ? "chip" : "fast");
            bpos += 20 + bank_len;
        } else if (memcmp(magic, "AmSp", 4) == 0 ||
                   memcmp(magic, "AmIc", 4) == 0) {
            /* Sprite/Icon bank — skip for now */
            fprintf(stderr, "  %.4s bank detected (skipping)\n", magic);
            /* Need to calculate size from sprite count and data */
            break;  /* Can't easily skip without parsing */
        } else {
            fprintf(stderr, "  Unknown bank magic: %.4s at offset 0x%zX\n",
                    magic, bpos);
            break;
        }
    }
}

/* ── Main Loader Function ──────────────────────────────────────── */

int amos_load_amos_file(amos_state_t *state, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Cannot open .AMOS file: %s", path);
        state->error_code = 2;
        return -1;
    }

    /* Read entire file */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size < AMOS_SRC_OFFSET) {
        fclose(f);
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "File too small for .AMOS format: %s", path);
        state->error_code = 2;
        return -1;
    }

    uint8_t *data = malloc(file_size);
    size_t bytes_read = fread(data, 1, file_size, f);
    fclose(f);

    if ((long)bytes_read != file_size) {
        free(data);
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Read error on .AMOS file: %s", path);
        state->error_code = 2;
        return -1;
    }

    /* Detect file type */
    amos_file_type_t ftype = detect_file_type(data, file_size);
    if (ftype == AMOS_FILE_UNKNOWN) {
        free(data);
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Not a valid .AMOS file: %s", path);
        state->error_code = 2;
        return -1;
    }

    /* Print header info */
    char header_str[17] = {0};
    memcpy(header_str, data, 16);
    fprintf(stderr, "amos_loader: header=\"%s\" (%s)\n",
            header_str, ftype == AMOS_FILE_13 ? "AMOS 1.3" : "AMOS Pro");

    /* Read source code length */
    uint32_t src_len = BE32(data + 16);
    fprintf(stderr, "amos_loader: source length=%u bytes\n", src_len);

    if (AMOS_SRC_OFFSET + src_len > (size_t)file_size) {
        fprintf(stderr, "amos_loader: warning: source length exceeds file size, truncating\n");
        src_len = file_size - AMOS_SRC_OFFSET;
    }

    /* Set dialect based on file type */
    if (ftype == AMOS_FILE_13) {
        state->dialect = AMOS_DIALECT_13;
    } else {
        state->dialect = AMOS_DIALECT_PRO;
    }

    /* Detokenize */
    char *source = amos_detokenize(data + AMOS_SRC_OFFSET, src_len);

    /* Process bank section (after source) */
    size_t bank_offset = AMOS_SRC_OFFSET + src_len;
    if (bank_offset + 6 <= (size_t)file_size) {
        process_banks(data, bank_offset, file_size);
    }

    free(data);

    /* Load the detokenized source into the interpreter */
    int result = amos_load_text(state, source);
    free(source);

    fprintf(stderr, "amos_loader: loaded %d lines from %s\n", result, path);
    return result;
}
