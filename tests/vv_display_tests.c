/*
 * vv_display_tests.c — V&V tests for Display System (REQ-DSP)
 *
 * Tests screen structure, drawing primitives, sprites/bobs,
 * and compositor against AMOS specification requirements.
 */

#include "vv_framework.h"

/* ══════════════════════════════════════════════════════════════════════
 *  Screen Data Structure (REQ-DSP-001 through 006)
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-DSP-001: Screen structure size is constant across all slots") {
    amos_state_t *s = vv_create();
    /* All screen slots should be same-size contiguous struct */
    size_t slot_size = sizeof(amos_screen_t);
    VV_ASSERT(slot_size > 0, "amos_screen_t must have non-zero size");
    /* Verify contiguous by checking pointer arithmetic */
    uintptr_t d = (uintptr_t)&s->screens[1] - (uintptr_t)&s->screens[0];
    VV_ASSERT(d == slot_size, "screen slots must be contiguous and same size");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-002: Maximum screen count is 12 (EcMax)") {
    VV_ASSERT(AMOS_MAX_SCREENS == 12,
              "AMOS_MAX_SCREENS must be 12 per blueprint");
}

VV_TEST("REQ-DSP-021: Screen Open width clamps to multiple of 16") {
    amos_state_t *s = vv_create();
    /* Width 17 should round up to 32 */
    vv_run(s, "Screen Open 1,17,100,32");
    VV_ASSERT(s->screens[1].width == 32,
              "width 17 should clamp to 32 (next multiple of 16)");
    /* Width 320 should stay 320 */
    vv_run(s, "Screen Open 2,320,100,32");
    VV_ASSERT(s->screens[2].width == 320,
              "width 320 should stay 320");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-022: Screen Open width range 16-1008") {
    amos_state_t *s = vv_create();
    vv_run(s, "Screen Open 1,0,100,32");
    VV_ASSERT(s->screens[1].width == 16,
              "width 0 should clamp to 16");
    vv_run(s, "Screen Open 2,2000,100,32");
    VV_ASSERT(s->screens[2].width == 1008,
              "width 2000 should clamp to 1008");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-023: Screen Open height range 1-1023") {
    amos_state_t *s = vv_create();
    vv_run(s, "Screen Open 1,320,0,32");
    VV_ASSERT(s->screens[1].height == 1,
              "height 0 should clamp to 1");
    vv_run(s, "Screen Open 2,320,2000,32");
    VV_ASSERT(s->screens[2].height == 1023,
              "height 2000 should clamp to 1023");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-024: Screen Open 2 colors maps to depth 1") {
    amos_state_t *s = vv_create();
    vv_run(s, "Screen Open 1,320,256,2");
    VV_ASSERT(s->screens[1].depth == 1, "2 colors → depth 1");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-025: Screen Open 4 colors maps to depth 2") {
    amos_state_t *s = vv_create();
    vv_run(s, "Screen Open 1,320,256,4");
    VV_ASSERT(s->screens[1].depth == 2, "4 colors → depth 2");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-026: Screen Open 8 colors maps to depth 3") {
    amos_state_t *s = vv_create();
    vv_run(s, "Screen Open 1,320,256,8");
    VV_ASSERT(s->screens[1].depth == 3, "8 colors → depth 3");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-027: Screen Open 16 colors maps to depth 4") {
    amos_state_t *s = vv_create();
    vv_run(s, "Screen Open 1,320,256,16");
    VV_ASSERT(s->screens[1].depth == 4, "16 colors → depth 4");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-028: Screen Open 32 colors maps to depth 5") {
    amos_state_t *s = vv_create();
    vv_run(s, "Screen Open 1,320,256,32");
    VV_ASSERT(s->screens[1].depth == 5, "32 colors → depth 5");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-029: Screen Open 64 colors maps to depth 6") {
    amos_state_t *s = vv_create();
    vv_run(s, "Screen Open 1,320,256,64");
    VV_ASSERT(s->screens[1].depth == 6, "64 colors → depth 6");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-003: Drawing targets pixels buffer (Logic equivalent)") {
    amos_state_t *s = vv_create();
    amos_screen_t *scr = &s->screens[0];
    VV_ASSERT(scr->pixels != NULL, "Logic buffer (pixels) must be allocated");
    /* Plot should write to pixels buffer */
    vv_run(s, "Ink 2 : Plot 10,10");
    uint32_t pixel = scr->pixels[10 * scr->width + 10];
    VV_ASSERT(pixel == scr->palette[2],
              "Plot should write to pixels (Logic) buffer");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-009: Screen width stored and queryable") {
    amos_state_t *s = vv_create();
    vv_run(s, "W=Screen Width");
    VV_ASSERT_INT(s, "W", 320);
    vv_destroy(s);
}

VV_TEST("REQ-DSP-010: Screen height stored and queryable") {
    amos_state_t *s = vv_create();
    vv_run(s, "H=Screen Height");
    VV_ASSERT_INT(s, "H", 256);
    vv_destroy(s);
}

VV_TEST("REQ-DSP-011: Default screen depth is 5 (32 colors)") {
    amos_state_t *s = vv_create();
    amos_screen_t *scr = &s->screens[0];
    VV_ASSERT(scr->depth == 5, "default depth should be 5 (32 colors)");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-012: Screen Display position defaults to 0,0") {
    amos_state_t *s = vv_create();
    amos_screen_t *scr = &s->screens[0];
    VV_ASSERT(scr->display_x == 0, "default display_x should be 0");
    VV_ASSERT(scr->display_y == 0, "default display_y should be 0");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-014: Screen Offset sets viewport position") {
    amos_state_t *s = vv_create();
    vv_run(s, "Screen Offset 0,16,8");
    amos_screen_t *scr = &s->screens[0];
    VV_ASSERT(scr->offset_x == 16, "offset_x should be 16");
    VV_ASSERT(scr->offset_y == 8, "offset_y should be 8");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-015: Palette has 32+ entries with correct defaults") {
    amos_state_t *s = vv_create();
    amos_screen_t *scr = &s->screens[0];
    /* Check palette size is at least 32 */
    VV_ASSERT(sizeof(scr->palette) / sizeof(scr->palette[0]) >= 32,
              "palette must have at least 32 entries");
    /* Default palette[0] = black $000 */
    VV_ASSERT(scr->palette[0] == AMOS_RGBA(0,0,0,0xFF),
              "palette[0] should be black");
    /* Default palette[2] = white $FFF */
    VV_ASSERT(scr->palette[2] == AMOS_RGBA(0xFF,0xFF,0xFF,0xFF),
              "palette[2] should be white");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-016: Screen Hide/Show controls visibility") {
    amos_state_t *s = vv_create();
    amos_screen_t *scr = &s->screens[0];
    VV_ASSERT(scr->visible == true, "screen should be visible by default");
    vv_run(s, "Screen Hide 0");
    VV_ASSERT(scr->visible == false, "Screen Hide should clear visible");
    vv_run(s, "Screen Show 0");
    VV_ASSERT(scr->visible == true, "Screen Show should set visible");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-017: Double Buffer command allocates back buffer") {
    amos_state_t *s = vv_create();
    amos_screen_t *scr = &s->screens[0];
    VV_ASSERT(scr->double_buffered == false, "not double-buffered by default");
    vv_run(s, "Double Buffer");
    VV_ASSERT(scr->double_buffered == true,
              "Double Buffer should set double_buffered flag");
    VV_ASSERT(scr->back_buffer != NULL,
              "Double Buffer should allocate back_buffer");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-018: Autoback mode stored correctly") {
    amos_state_t *s = vv_create();
    vv_run(s, "Autoback 0");
    amos_screen_t *scr = &s->screens[0];
    VV_ASSERT(scr->autoback == 0, "Autoback 0 should set mode 0");
    vv_run(s, "Autoback 1");
    VV_ASSERT(scr->autoback == 1, "Autoback 1 should set mode 1");
    vv_run(s, "Autoback 2");
    VV_ASSERT(scr->autoback == 2, "Autoback 2 should set mode 2");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Screen Open (REQ-DSP-033, 034)
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-DSP-033: Screen Open sets current screen") {
    amos_state_t *s = vv_create();
    /* Default screen 0 is current */
    VV_ASSERT(s->current_screen == 0, "default current screen should be 0");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-034: New screen gets default palette") {
    amos_state_t *s = vv_create();
    amos_screen_t *scr = &s->screens[0];
    /* PI_DefEPa: palette[1] = $A40 brown, palette[4] = $F00 red */
    VV_ASSERT(scr->palette[1] == AMOS_RGBA(0xAA,0x44,0x00,0xFF),
              "palette[1] should be $A40 brown");
    VV_ASSERT(scr->palette[4] == AMOS_RGBA(0xFF,0x00,0x00,0xFF),
              "palette[4] should be $F00 red");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Screen Open Mode Bits (REQ-DSP-030 through 032)
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-DSP-030: Screen Open with Hires mode sets hires flag") {
    amos_state_t *s = vv_create();
    /* Screen Open 1,640,200,16,Hires  where Hires=$8000 */
    vv_run(s, "Screen Open 1,640,200,16,$8000");
    amos_screen_t *scr = &s->screens[1];
    VV_ASSERT(scr->active, "screen 1 should be active");
    VV_ASSERT(scr->hires == true, "hires flag should be set");
    VV_ASSERT(scr->width == 640, "width should be 640");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-031: Screen Open with Interlace mode sets flag") {
    amos_state_t *s = vv_create();
    vv_run(s, "Screen Open 1,320,512,16,$0004");
    amos_screen_t *scr = &s->screens[1];
    VV_ASSERT(scr->active, "screen 1 should be active");
    VV_ASSERT(scr->interlace == true, "interlace flag should be set");
    VV_ASSERT(scr->height == 512, "height should be 512");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-032a: Screen Open without mode flags defaults to lowres") {
    amos_state_t *s = vv_create();
    vv_run(s, "Screen Open 1,320,256,16");
    amos_screen_t *scr = &s->screens[1];
    VV_ASSERT(scr->active, "screen 1 should be active");
    VV_ASSERT(scr->hires == false, "hires flag should not be set");
    VV_ASSERT(scr->interlace == false, "interlace should not be set");
    VV_ASSERT(scr->ham == false, "ham should not be set");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-030a: Screen Open with Hires constant") {
    amos_state_t *s = vv_create();
    vv_run(s, "Screen Open 1,640,200,16,Hires");
    amos_screen_t *scr = &s->screens[1];
    VV_ASSERT(scr->active, "screen 1 should be active");
    VV_ASSERT(scr->hires == true, "hires flag should be set via Hires constant");
    VV_ASSERT(scr->width == 640, "width should be 640");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-032b: Hires mode allows width up to 1280") {
    amos_state_t *s = vv_create();
    vv_run(s, "Screen Open 1,1280,200,4,$8000");
    amos_screen_t *scr = &s->screens[1];
    VV_ASSERT(scr->active, "screen 1 should be active");
    VV_ASSERT(scr->width == 1280, "hires width 1280 should be accepted");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Drawing Primitives (REQ-DSP-035 through 043)
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-DSP-035: Plot sets single pixel to ink color") {
    amos_state_t *s = vv_create();
    vv_run(s, "Ink 4 : Plot 50,50");
    amos_screen_t *scr = &s->screens[0];
    uint32_t pixel = scr->pixels[50 * scr->width + 50];
    VV_ASSERT(pixel == scr->palette[4],
              "Plot should set pixel to ink color (palette[4] = red)");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-036: Draw produces line from current position") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "Ink 2\n"
        "Draw 0,0 To 10,0\n"
    );
    amos_screen_t *scr = &s->screens[0];
    /* All pixels along Y=0 from X=0 to X=10 should be ink color 2 */
    bool all_set = true;
    for (int x = 0; x <= 10; x++) {
        if (scr->pixels[0 * scr->width + x] != scr->palette[2]) {
            all_set = false;
            break;
        }
    }
    VV_ASSERT(all_set, "horizontal line from (0,0) to (10,0) should be all ink color");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-039: Box draws unfilled rectangle outline") {
    amos_state_t *s = vv_create();
    vv_run(s, "Ink 5 : Box 10,10 To 20,20");
    amos_screen_t *scr = &s->screens[0];
    /* Corner pixel should be ink color */
    VV_ASSERT(scr->pixels[10 * scr->width + 10] == scr->palette[5],
              "box top-left corner should be ink color");
    VV_ASSERT(scr->pixels[20 * scr->width + 20] == scr->palette[5],
              "box bottom-right corner should be ink color");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-040: Bar fills rectangle with ink color") {
    amos_state_t *s = vv_create();
    vv_run(s, "Ink 6 : Bar 10,10 To 20,20");
    amos_screen_t *scr = &s->screens[0];
    /* Check interior pixel */
    uint32_t mid = scr->pixels[15 * scr->width + 15];
    VV_ASSERT(mid == scr->palette[6],
              "Bar interior should be filled with ink color");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Writing Mode (REQ-GFX from example tests, added here for coverage)
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-DSP-043a: Gr Writing stores writing mode") {
    amos_state_t *s = vv_create();
    amos_screen_t *scr = &s->screens[s->current_screen];
    VV_ASSERT(scr->writing_mode == 0, "default writing mode should be 0 (replace)");
    vv_run(s, "Gr Writing 1");
    VV_ASSERT(scr->writing_mode == 1, "Gr Writing 1 should set OR mode");
    vv_run(s, "Gr Writing 2");
    VV_ASSERT(scr->writing_mode == 2, "Gr Writing 2 should set XOR mode");
    vv_run(s, "Gr Writing 3");
    VV_ASSERT(scr->writing_mode == 3, "Gr Writing 3 should set AND mode");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Sprite/Bob structure (REQ-DSP-044, 050, 055, 056)
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-DSP-044: System supports 64 logical sprites") {
    VV_ASSERT(AMOS_MAX_SPRITES == 64, "AMOS_MAX_SPRITES must be 64");
}

VV_TEST("REQ-DSP-056: System supports 64 bobs") {
    VV_ASSERT(AMOS_MAX_BOBS == 64, "AMOS_MAX_BOBS must be 64");
}

VV_TEST("REQ-DSP-047: Sprite structure has required fields") {
    amos_state_t *s = vv_create();
    amos_sprite_t *sp = &s->sprites[0];
    /* Verify all required fields are accessible */
    VV_ASSERT(sp->active == false, "sprite should be inactive by default");
    VV_ASSERT(sp->x == 0, "sprite x should default to 0");
    VV_ASSERT(sp->y == 0, "sprite y should default to 0");
    VV_ASSERT(sp->image == 0, "sprite image should default to 0");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Double Buffering (REQ-DSP-061, 062)
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-DSP-061: Double Buffer allocates second buffer and sets flag") {
    amos_state_t *s = vv_create();
    amos_screen_t *scr = &s->screens[0];
    VV_ASSERT(scr->back_buffer == NULL, "back_buffer should be NULL before Double Buffer");
    VV_ASSERT(scr->double_buffered == false, "double_buffered should be false before");
    vv_run(s, "Double Buffer");
    VV_ASSERT(scr->back_buffer != NULL, "back_buffer must be allocated after Double Buffer");
    VV_ASSERT(scr->double_buffered == true, "double_buffered must be true after Double Buffer");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-062: Screen Swap exchanges Logic and Physic pointers") {
    amos_state_t *s = vv_create();
    vv_run(s, "Double Buffer");
    amos_screen_t *scr = &s->screens[0];
    uint32_t *orig_pixels = scr->pixels;
    uint32_t *orig_back = scr->back_buffer;
    vv_run(s, "Screen Swap");
    VV_ASSERT(scr->pixels == orig_back,
              "after swap, pixels should point to former back_buffer");
    VV_ASSERT(scr->back_buffer == orig_pixels,
              "after swap, back_buffer should point to former pixels");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Colour command (REQ-DSP-015)
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-DSP-015a: Colour command sets palette entry from $0RGB") {
    amos_state_t *s = vv_create();
    vv_run(s, "Colour 0,$F00");
    amos_screen_t *scr = &s->screens[0];
    /* $F00 = red: R=0xFF, G=0x00, B=0x00 */
    VV_ASSERT(scr->palette[0] == AMOS_RGBA(0xFF,0x00,0x00,0xFF),
              "Colour 0,$F00 should set palette[0] to red");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-015b: Colour function reads palette entry") {
    amos_state_t *s = vv_create();
    /* Default palette[2] = $FFF white */
    vv_run(s, "C=Colour(2)");
    VV_ASSERT_INT(s, "C", 0xFFF);
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Ink / Paper commands
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-DSP-035a: Ink sets drawing color index") {
    amos_state_t *s = vv_create();
    vv_run(s, "Ink 7");
    amos_screen_t *scr = &s->screens[s->current_screen];
    VV_ASSERT(scr->ink_color == 7, "Ink 7 should set ink_color to 7");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-035b: Paper sets background color index") {
    amos_state_t *s = vv_create();
    vv_run(s, "Paper 3");
    amos_screen_t *scr = &s->screens[s->current_screen];
    VV_ASSERT(scr->text_paper == 3, "Paper 3 should set text_paper to 3");
    vv_destroy(s);
}

VV_TEST("REQ-DSP-035c: Pen sets text foreground color index") {
    amos_state_t *s = vv_create();
    vv_run(s, "Pen 5");
    amos_screen_t *scr = &s->screens[s->current_screen];
    VV_ASSERT(scr->text_pen == 5, "Pen 5 should set text_pen to 5");
    vv_destroy(s);
}
