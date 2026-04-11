# REQ-DSP: Display System Requirements

**Source:** AMOS_TECHNICAL_BLUEPRINT.md Section 3 (Display System)
**Total Requirements:** 62
**Extracted:** 2026-04-10
**Legend:** IMPL = implemented, PARTIAL = partially implemented, TODO = not yet implemented

---

## 1. Screen Data Structure

```
REQ-DSP-001: Screen Structure Size — Each screen occupies EcLong bytes of contiguous storage
  Acceptance: sizeof(amos_screen_t) is constant and identical for all screen slots
  Level: 1 (unit)
  Source: Blueprint S3 "Screen Data Structure"
  Status: IMPL
```

```
REQ-DSP-002: Maximum Screen Count — System supports at most 12 screens (EcMax=12)
  Acceptance: AMOS_MAX_SCREENS >= 12; Screen Open 12,320,256,5 must fail; Screen Open 11,320,256,5 must succeed
  Level: 2 (subsystem)
  Source: Blueprint S3 "Screen Data Structure"
  Status: PARTIAL — AMOS_MAX_SCREENS is 8, blueprint says 12
```

```
REQ-DSP-003: Bitplane Pointer Set — Logic — EcLogic (offset 0) holds 6 bitplane pointers for drawing target
  Acceptance: Drawing operations (Plot, Draw, Bar) write to the Logic bitplane set, not Physic
  Level: 2 (subsystem)
  Source: Blueprint S3 "Bitplane pointers"
  Status: IMPL — implemented as pixels (RGBA), drawing targets pixels buffer
```

```
REQ-DSP-004: Bitplane Pointer Set — Physic — EcPhysic (offset 24) holds 6 bitplane pointers for displayed output
  Acceptance: Compositor reads from Physic set; after Screen Swap, previously-Logic becomes Physic
  Level: 2 (subsystem)
  Source: Blueprint S3 "Bitplane pointers"
  Status: IMPL — implemented as back_buffer for double buffering
```

```
REQ-DSP-005: Bitplane Pointer Set — Current — EcCurrent (offset 48) holds 6 bitplane pointers for Amiga Layers
  Acceptance: Third pointer set exists distinct from Logic and Physic
  Level: 1 (unit)
  Source: Blueprint S3 "Bitplane pointers"
  Status: TODO — no EcCurrent equivalent in modern RGBA implementation
```

```
REQ-DSP-006: Three Pointer Sets Independence — Logic, Physic, Current are 3 independent sets of 6 plane pointers (72 bytes total)
  Acceptance: Modifying one set does not affect the other two
  Level: 1 (unit)
  Source: Blueprint S3 "Bitplane pointers"
  Status: PARTIAL — only 2 buffers (pixels, back_buffer), no third
```

## 2. Screen Key Fields

```
REQ-DSP-007: EcCon0 — Screen stores BPLCON0 register value
  Acceptance: EcCon0 correctly encodes hires bit ($8000), HAM bit ($0800), and bitplane count
  Level: 1 (unit)
  Source: Blueprint S3 "Key fields"
  Status: TODO — no BPLCON0 equivalent stored
```

```
REQ-DSP-008: EcCon2 — Screen stores BPLCON2 register value
  Acceptance: EcCon2 field exists and stores sprite priority relative to playfields
  Level: 1 (unit)
  Source: Blueprint S3 "Key fields"
  Status: TODO — no BPLCON2 equivalent stored
```

```
REQ-DSP-009: EcTx — Screen pixel width stored and queryable
  Acceptance: After Screen Open 0,320,256,5 the screen width is 320
  Level: 1 (unit)
  Source: Blueprint S3 "Key fields"
  Status: IMPL — stored as amos_screen_t.width
```

```
REQ-DSP-010: EcTy — Screen pixel height stored and queryable
  Acceptance: After Screen Open 0,320,256,5 the screen height is 256
  Level: 1 (unit)
  Source: Blueprint S3 "Key fields"
  Status: IMPL — stored as amos_screen_t.height
```

```
REQ-DSP-011: EcNPlan Range — Bitplane count (depth) must be 1-6
  Acceptance: Screen Open with 2 colors -> depth 1; 64 colors -> depth 6; depth 0 and 7 rejected
  Level: 2 (subsystem)
  Source: Blueprint S3 "Key fields"
  Status: PARTIAL — depth stored but no range validation
```

```
REQ-DSP-012: Display Window Position — EcWX/EcWY store display window position on output
  Acceptance: Screen Display 0,100,50 positions screen 0 at output coordinates (100,50)
  Level: 2 (subsystem)
  Source: Blueprint S3 "Key fields"
  Status: IMPL — display_x/display_y in compositor
```

```
REQ-DSP-013: Display Window Size — EcWTx/EcWTy store display window pixel dimensions
  Acceptance: Screen Display 0,0,0,160,128 clips screen output to 160x128 pixels
  Level: 2 (subsystem)
  Source: Blueprint S3 "Key fields"
  Status: IMPL — display_w/display_h in compositor
```

```
REQ-DSP-014: Viewport Offset — EcVX/EcVY store scrolling viewport offset
  Acceptance: Screen Offset 0,16,8 shifts the visible area by (16,8) pixels within the framebuffer
  Level: 2 (subsystem)
  Source: Blueprint S3 "Key fields"
  Status: IMPL — offset_x/offset_y used in compositor
```

```
REQ-DSP-015: Palette — 32-entry palette of 16-bit $0RGB values
  Acceptance: palette array has at least 32 entries; Colour 0,$F00 sets index 0 to red ($F00)
  Level: 1 (unit)
  Source: Blueprint S3 "Key fields"
  Status: IMPL — palette[256] with $0RGB->RGBA conversion in amos_screen_colour
```

```
REQ-DSP-016: EcFlags Hidden Bit — Bit 7 of EcFlags controls screen visibility
  Acceptance: Screen Hide sets hidden; Screen Show clears it; hidden screen not composited
  Level: 2 (subsystem)
  Source: Blueprint S3 "Key fields"
  Status: IMPL — visible bool in amos_screen_t, compositor checks it
```

```
REQ-DSP-017: EcFlags Double-Buffer Bit — Bit 5 of EcFlags indicates double-buffer active
  Acceptance: After Double Buffer command, double_buffered flag is true
  Level: 2 (subsystem)
  Source: Blueprint S3 "Key fields"
  Status: IMPL — double_buffered bool in amos_screen_t
```

```
REQ-DSP-018: EcAuto Mode 0 — Autoback off (no automatic redraw)
  Acceptance: Autoback 0 sets autoback=0; drawing does not trigger bob cycle
  Level: 2 (subsystem)
  Source: Blueprint S3 "Key fields"
  Status: IMPL — autoback field stored, value assignment implemented
```

```
REQ-DSP-019: EcAuto Mode 1 — Autoback simple (auto screen update after each command)
  Acceptance: Autoback 1 sets autoback=1
  Level: 2 (subsystem)
  Source: Blueprint S3 "Key fields"
  Status: PARTIAL — value stored but no auto-update behavior triggered
```

```
REQ-DSP-020: EcAuto Mode 2 — Autoback double-buffer (full bob cycle per drawing command)
  Acceptance: Autoback 2 sets autoback=2; each draw triggers erase/save/draw bob cycle
  Level: 2 (subsystem)
  Source: Blueprint S3 "Key fields"
  Status: PARTIAL — value stored but bob cycle not triggered
```

## 3. Screen Open

```
REQ-DSP-021: Screen Open Width Multiple of 16 — tx must be a multiple of 16
  Acceptance: Screen Open 0,17,256,5 must fail or clamp to 16; Screen Open 0,320,256,5 succeeds with width=320
  Level: 2 (subsystem)
  Source: Blueprint S3 "Screen Open"
  Status: TODO — no width alignment validation in amos_screen_open
```

```
REQ-DSP-022: Screen Open Width Range — tx must be in range 16-1008
  Acceptance: Screen Open 0,0,256,5 fails; Screen Open 0,1024,256,5 fails; Screen Open 0,1008,256,5 succeeds
  Level: 2 (subsystem)
  Source: Blueprint S3 "Screen Open"
  Status: TODO — no range validation in amos_screen_open
```

```
REQ-DSP-023: Screen Open Height Range — ty must be in range 1-1023
  Acceptance: Screen Open 0,320,0,5 fails; Screen Open 0,320,1024,5 fails; Screen Open 0,320,1023,5 succeeds
  Level: 2 (subsystem)
  Source: Blueprint S3 "Screen Open"
  Status: TODO — no range validation in amos_screen_open
```

```
REQ-DSP-024: Color-to-Plane Mapping 2->1 — 2 colors maps to 1 bitplane
  Acceptance: Screen Open 0,320,256,2 sets depth=1
  Level: 1 (unit)
  Source: Blueprint S3 "Screen Open"
  Status: PARTIAL — depth passed directly, no color-to-plane conversion
```

```
REQ-DSP-025: Color-to-Plane Mapping 4->2 — 4 colors maps to 2 bitplanes
  Acceptance: Screen Open 0,320,256,4 sets depth=2
  Level: 1 (unit)
  Source: Blueprint S3 "Screen Open"
  Status: PARTIAL — depth passed directly
```

```
REQ-DSP-026: Color-to-Plane Mapping 8->3 — 8 colors maps to 3 bitplanes
  Acceptance: Screen Open 0,320,256,8 sets depth=3
  Level: 1 (unit)
  Source: Blueprint S3 "Screen Open"
  Status: PARTIAL — depth passed directly
```

```
REQ-DSP-027: Color-to-Plane Mapping 16->4 — 16 colors maps to 4 bitplanes
  Acceptance: Screen Open 0,320,256,16 sets depth=4
  Level: 1 (unit)
  Source: Blueprint S3 "Screen Open"
  Status: PARTIAL — depth passed directly
```

```
REQ-DSP-028: Color-to-Plane Mapping 32->5 — 32 colors maps to 5 bitplanes
  Acceptance: Screen Open 0,320,256,32 sets depth=5
  Level: 1 (unit)
  Source: Blueprint S3 "Screen Open"
  Status: PARTIAL — depth passed directly
```

```
REQ-DSP-029: Color-to-Plane Mapping 64->6 — 64 colors maps to 6 bitplanes
  Acceptance: Screen Open 0,320,256,64 sets depth=6
  Level: 1 (unit)
  Source: Blueprint S3 "Screen Open"
  Status: PARTIAL — depth passed directly
```

```
REQ-DSP-030: Screen Open Mode Hires — Bit 15 ($8000) enables hires mode (640px)
  Acceptance: Screen Open 0,640,256,4,$8000 succeeds; circle x-radius is doubled in hires
  Level: 2 (subsystem)
  Source: Blueprint S3 "Screen Open"
  Status: TODO — no mode parameter handling
```

```
REQ-DSP-031: Screen Open Mode Interlace — Bit 2 ($0004) enables interlace (512 lines)
  Acceptance: Screen Open 0,320,512,4,$0004 succeeds with interlaced vertical resolution
  Level: 2 (subsystem)
  Source: Blueprint S3 "Screen Open"
  Status: TODO — no mode parameter handling
```

```
REQ-DSP-032: Screen Open Mode HAM — Bit 11 ($0800) enables HAM mode (Hold-And-Modify)
  Acceptance: Screen Open 0,320,256,4096,$0800 sets HAM flag
  Level: 2 (subsystem)
  Source: Blueprint S3 "Screen Open"
  Status: TODO — no HAM mode handling
```

```
REQ-DSP-033: Screen Open Sets Current — Opening a screen makes it the current screen
  Acceptance: After Screen Open 3,320,256,5, current_screen == 3
  Level: 2 (subsystem)
  Source: Blueprint S3 "Screen Open"
  Status: IMPL — state->current_screen = id in amos_screen_open
```

```
REQ-DSP-034: Screen Open Palette Init — New screen gets default palette up to color count
  Acceptance: Screen Open 0,320,256,4 initializes palette[0..3] from default; palette[4] is black/zero
  Level: 2 (subsystem)
  Source: Blueprint S3 "Screen Open"
  Status: IMPL — copies amos_default_palette_32 up to num_colors
```

## 4. Drawing Primitives

```
REQ-DSP-035: Plot — Plot x,y sets single pixel to ink color
  Acceptance: Plot 100,50 sets pixel (100,50) to current ink; Point(100,50) returns ink index
  Level: 2 (subsystem)
  Source: Blueprint S3 "Drawing Primitives"
  Status: IMPL — amos_screen_plot implemented
```

```
REQ-DSP-036: Draw — Draw x,y draws line from current position to (x,y)
  Acceptance: Draw from (0,0) to (100,100) places pixels along diagonal; Bresenham-accurate
  Level: 2 (subsystem)
  Source: Blueprint S3 "Drawing Primitives"
  Status: IMPL — amos_screen_draw with Bresenham algorithm
```

```
REQ-DSP-037: Circle — Circle cx,cy,r draws circular outline
  Acceptance: Circle 100,100,50 draws pixels at distance 50 from center (100,100)
  Level: 2 (subsystem)
  Source: Blueprint S3 "Drawing Primitives"
  Status: IMPL — amos_screen_circle with midpoint algorithm
```

```
REQ-DSP-038: Circle Hires X-Radius Doubling — In hires mode, x-radius is doubled
  Acceptance: In hires screen, Circle 100,100,50 draws with effective rx=100, ry=50 (ellipse)
  Level: 2 (subsystem)
  Source: Blueprint S3 "Circle"
  Status: TODO — no hires x-radius doubling logic
```

```
REQ-DSP-039: Box — Box x1,y1,x2,y2 draws unfilled rectangle outline
  Acceptance: Box 10,10,50,50 draws 4 lines forming rectangle; interior pixels unchanged
  Level: 2 (subsystem)
  Source: Blueprint S3 "Drawing Primitives"
  Status: IMPL — amos_screen_box via 4 Draw calls
```

```
REQ-DSP-040: Bar — Bar x1,y1,x2,y2 draws filled rectangle
  Acceptance: Bar 10,10,50,50 fills all pixels in rectangle with ink color
  Level: 2 (subsystem)
  Source: Blueprint S3 "Drawing Primitives"
  Status: IMPL — amos_screen_bar with coordinate swap for inverted ranges
```

```
REQ-DSP-041: Polygon — Polygon draws filled polygon from vertex list
  Acceptance: Polygon with 3 vertices (triangle) fills interior; scanline fill correct at edges
  Level: 2 (subsystem)
  Source: Blueprint S3 "Drawing Primitives"
  Status: IMPL — amos_screen_polygon with scanline algorithm
```

```
REQ-DSP-042: Paint — Paint x,y flood-fills connected region of same color
  Acceptance: Paint inside a box fills interior; stops at box edges; does not leak outside
  Level: 2 (subsystem)
  Source: Blueprint S3 "Drawing Primitives"
  Status: IMPL — amos_screen_paint with iterative scanline flood fill
```

```
REQ-DSP-043: Ellipse — Ellipse cx,cy,rx,ry draws elliptical outline
  Acceptance: Ellipse 100,100,80,40 draws oval with horizontal radius 80, vertical radius 40
  Level: 2 (subsystem)
  Source: Blueprint S3 "Drawing Primitives" (DrawEllipse)
  Status: IMPL — amos_screen_ellipse with midpoint algorithm
```

## 5. Sprite Engine

```
REQ-DSP-044: Sprite Count — System supports 64 logical sprites (HsNb=64)
  Acceptance: AMOS_MAX_SPRITES == 64; Sprite 63,100,100,1 succeeds; Sprite 64,100,100,1 fails
  Level: 1 (unit)
  Source: Blueprint S3 "Sprite Engine"
  Status: IMPL — AMOS_MAX_SPRITES = 64
```

```
REQ-DSP-045: Hardware Channels — 8 hardware sprite channels for multiplexing
  Acceptance: With >8 sprites on same scanline, lowest-priority sprites are dropped
  Level: 2 (subsystem)
  Source: Blueprint S3 "Sprite Engine"
  Status: TODO — no hardware channel limit emulation; all 64 rendered simultaneously
```

```
REQ-DSP-046: Y-Sorted Multiplexing — Sprites are Y-sorted via doubly-linked list for scanline allocation
  Acceptance: Sprite rendering order follows Y-position; higher Y sprites rendered after lower Y
  Level: 2 (subsystem)
  Source: Blueprint S3 "Sprite Engine"
  Status: TODO — sprites rendered in slot order, not Y-sorted
```

```
REQ-DSP-047: Sprite Structure Size — Per-sprite structure is 20 bytes (HsLong)
  Acceptance: Sprite data structure contains HsPrev, HsNext, HsX, HsY, HsImage, HsControl fields
  Level: 1 (unit)
  Source: Blueprint S3 "Sprite Engine"
  Status: PARTIAL — amos_sprite_t has different layout (active, image, x, y, visible, hot_x, hot_y)
```

```
REQ-DSP-048: Sprite Triple Buffering — HsLogic -> HsPhysic -> HsInter pipeline prevents tearing
  Acceptance: Sprite position updates go to Logic; display reads from Physic; swap cycles all three
  Level: 2 (subsystem)
  Source: Blueprint S3 "Sprite Engine"
  Status: TODO — no triple buffering for sprites
```

```
REQ-DSP-049: Multicolor Sprite Pairing — Multicolor sprites require paired even-aligned hardware channels
  Acceptance: Setting multicolor sprite on odd channel fails or pairs with adjacent even channel
  Level: 2 (subsystem)
  Source: Blueprint S3 "Sprite Engine"
  Status: TODO — no multicolor sprite pairing
```

```
REQ-DSP-050: Sprite Collision Detection — Sprite Col(n) returns first colliding sprite (1-based) or 0
  Acceptance: Two overlapping sprites: Sprite Col(0) returns nonzero; non-overlapping returns 0
  Level: 2 (subsystem)
  Source: Blueprint S3 "Sprite Engine"
  Status: IMPL — amos_sprite_col with bounding-box AABB test
```

## 6. Bob Engine

```
REQ-DSP-051: Bob Three-Phase Cycle — Bobs render via Erase (BobEff), Save+Calc (BobAct), Draw (BobAff)
  Acceptance: Bob rendering preserves background under bob; erasing restores original pixels
  Level: 2 (subsystem)
  Source: Blueprint S3 "Bob Engine"
  Status: TODO — bobs rendered directly without save/restore cycle
```

```
REQ-DSP-052: Bob Cookie-Cut Minterm — Draw phase uses minterm $0FCA: D=(A AND B) OR (NOT A AND C)
  Acceptance: Bob pixels with mask=1 show bob data; mask=0 shows background (transparent)
  Level: 2 (subsystem)
  Source: Blueprint S3 "Bob Engine"
  Status: PARTIAL — alpha-based transparency in render_sprite_image, not true cookie-cut
```

```
REQ-DSP-053: Bob Y-Priority Sort — Bobs bubble-sorted by Y-position; lower Y drawn first (behind)
  Acceptance: Bob at Y=50 appears behind bob at Y=100; overlapping bobs layer correctly
  Level: 2 (subsystem)
  Source: Blueprint S3 "Bob Engine"
  Status: TODO — bobs rendered in slot order, no Y-sort
```

```
REQ-DSP-054: Bob Flip Table — 256-byte TRetour lookup table for bit-reversal; X-flip reverses words, Y-flip reverses rows
  Acceptance: Bob with X-flip mirrors horizontally; bob with Y-flip mirrors vertically
  Level: 2 (subsystem)
  Source: Blueprint S3 "Bob Engine"
  Status: TODO — no flip support for bobs
```

```
REQ-DSP-055: Bob Collision Detection — Bob Col(n) returns first colliding bob (1-based) or 0
  Acceptance: Two overlapping bobs: Bob Col(0) returns nonzero; separated bobs return 0
  Level: 2 (subsystem)
  Source: Blueprint S3 "Bob Engine"
  Status: IMPL — amos_bob_col with bounding-box AABB test
```

```
REQ-DSP-056: Bob Count — System supports up to 64 bobs
  Acceptance: AMOS_MAX_BOBS == 64; Bob 63,100,100,1 succeeds; Bob 64 fails
  Level: 1 (unit)
  Source: Blueprint S3 "Bob Engine"
  Status: IMPL — AMOS_MAX_BOBS = 64
```

## 7. Copper List Builder

```
REQ-DSP-057: Copper Double Buffer — Two copper lists (CopLogic/CopPhysic), 1024 bytes each, double-buffered
  Acceptance: Copper list build writes to Logic; display reads from Physic; swap exchanges them
  Level: 2 (subsystem)
  Source: Blueprint S3 "Copper List Builder"
  Status: TODO — no copper list emulation
```

```
REQ-DSP-058: Copper Priority-Sorted Scanline Decomposition — Multi-screen compositing via priority-sorted scanline bands
  Acceptance: Two overlapping screens composited with correct priority; higher-priority screen occludes lower
  Level: 2 (subsystem)
  Source: Blueprint S3 "Copper List Builder"
  Status: PARTIAL — compositor sorts by priority, but no copper list generation
```

## 8. Rainbow System

```
REQ-DSP-059: Rainbow Count — Up to 4 simultaneous rainbows supported
  Acceptance: Rainbow 0-3 can be active simultaneously; Rainbow 4 fails
  Level: 2 (subsystem)
  Source: Blueprint S3 "Rainbow System"
  Status: TODO — no rainbow system implemented
```

```
REQ-DSP-060: Rainbow Per-Scanline Color — Each rainbow writes color register per scanline via copper
  Acceptance: Rainbow on color register 0 produces visible color gradient across screen height
  Level: 3 (golden)
  Source: Blueprint S3 "Rainbow System"
  Status: TODO — no rainbow system implemented
```

## 9. Double Buffering

```
REQ-DSP-061: Double Buffer Allocation — Double Buffer command allocates second bitplane set, sets EcAuto=2
  Acceptance: After Double Buffer: back_buffer is non-NULL, double_buffered is true
  Level: 2 (subsystem)
  Source: Blueprint S3 "Double Buffering"
  Status: IMPL — calloc of back_buffer, double_buffered set true
```

```
REQ-DSP-062: Screen Swap — Screen Swap exchanges Logic and Physic pointers
  Acceptance: After Screen Swap: pixels pointer swapped with back_buffer; drawing now targets former display
  Level: 2 (subsystem)
  Source: Blueprint S3 "Double Buffering"
  Status: IMPL — pointer swap of pixels and back_buffer in executor
```

---

## Summary

| Status | Count |
|--------|-------|
| IMPL   | 24    |
| PARTIAL| 13    |
| TODO   | 25    |
| **Total** | **62** |

### Key Gaps

1. **Screen Open validation** (REQ-DSP-021..023): No width alignment, range, or height range checks
2. **Color-to-plane mapping** (REQ-DSP-024..029): Depth passed raw, no colors-to-planes conversion
3. **Mode bits** (REQ-DSP-030..032): Hires, interlace, HAM modes not parsed or stored
4. **Max screens** (REQ-DSP-002): Implementation has 8, blueprint specifies 12
5. **Sprite multiplexing** (REQ-DSP-045..049): No hardware channel emulation, Y-sorting, or triple buffering
6. **Bob engine** (REQ-DSP-051..054): No save/restore cycle, no Y-sort, no flip table
7. **Copper/Rainbow** (REQ-DSP-057..060): Entire subsystems not yet implemented
8. **Hires circle doubling** (REQ-DSP-038): x-radius not doubled in hires mode
