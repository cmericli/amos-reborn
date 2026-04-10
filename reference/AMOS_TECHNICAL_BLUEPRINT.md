# AMOS Professional — Complete Technical Blueprint

**Generated:** 2026-04-10
**Sources:** AMOS-Professional-365 (68K assembly, 122,762 lines), AMOS 1.3 source (pervognsen/amos)
**Purpose:** Reimplementation reference for AMOS Reborn

---

## Table of Contents

1. [System Architecture & Memory Map](#1-system-architecture--memory-map)
2. [Interpreter Core](#2-interpreter-core)
3. [Display System](#3-display-system)
4. [Audio System](#4-audio-system)
5. [AMAL Engine](#5-amal-engine)
6. [Editor System](#6-editor-system)
7. [File Formats](#7-file-formats)
8. [Extension System](#8-extension-system)

---

## 1. System Architecture & Memory Map

### Memory Layout

AMOS divides memory relative to a master data zone pointer in register **A5**. The layout:

| Region | Contents |
|--------|----------|
| Work Buffers | BufLabel(32B), BufBob(~200B), AAreaBuf(86B), IFF buffers, Name1(256B), Buffer(1024B) |
| amos.library Data | Library private data (sized from header offset 8) |
| A5 Data Zone | ~4000+ bytes of runtime state (`DataLong` total) |
| Variable Space | `VarGlo` (globals), `VarLoc` (locals), 6 bytes per variable |
| String Buffer | `LoChaine` to `HiChaine`, grows upward |
| BASIC Stack | `BaLoop` to `HoLoop`, FOR/GOSUB/PROC frames |

### Key A5-Relative Offsets

| Offset | Field | Purpose |
|--------|-------|---------|
| 0 | `VblRout` (8 longs) | VBL interrupt routine slots |
| 32 | `AdTokens` (27 longs) | Token table addresses per extension |
| 248 | `ExtAdr` (26×16 bytes) | Extension function/init/end/bankchange pointers |
| ~696 | `DosBase`, `FloatBase`, etc. | AmigaOS library bases |
| ~970 | `BasSp` | Saved SP for error recovery |
| ~1230 | `VBLOCount`, `SScan`, `Seed` | Timing, scancode, RNG |
| ~1340 | `Prg_Source`, `Prg_Run` | Program execution pointers |
| ~1400 | `VarGlo`, `VarLoc`, `TabBas` | Variable tables |
| ~1410 | `ChVide`, `LoChaine`, `HiChaine` | String GC boundaries |
| ~1420 | `HoLoop`, `BaLoop`, `PLoop` | BASIC stack limits |

### Initialization Sequence

1. Save SP, detect PAL/NTSC from `ExecBase->VBlankFrequency` (50=PAL)
2. Open graphics/dos/intuition/math libraries
3. Load amos.library (W.Lib) from disk via `LoadSeg`
4. Allocate master data zone in Fast RAM, set A5
5. Set up work buffer pointers, BASIC stack limits
6. Initialize copper list, screen system, sprite engine, VBI handler
7. Load extensions from configured slots

### Memory Banks

Up to 16 banks (0-15), tracked via `ABanks(a5)` — 8 bytes each: `[address.l, flags_and_size.l]`.
- Bank 1: Sprites, Bank 2: Icons, Bank 3: Samples/Music, Bank 4: AMAL
- Flags: bit 31=data, bit 30=chip, bit 29=bob, bit 28=icon
- Data length = `flags AND $0FFFFFFF - 8`

### String Garbage Collection

Two algorithms:
- **Fast GC**: Allocates temp buffer, copies live strings sequentially, updates pointers. Marks copied strings with `$FFF0xxxx`.
- **Slow GC**: Fallback when temp buffer unavailable. Uses small sorting table, O(n²) worst case.
- `SET BUFFER n` controls initial string buffer size.

### VBI Handler (50Hz PAL / 60Hz NTSC)

1. Increment `T_VBLCount`
2. Set `BitVBL` (bit 15) in `T_Actualise`
3. Decrement `T_EveCpt` for EVERY timing
4. Call up to 8 VBL routines from `VblRout` slots

### Input Handling

- **Keyboard**: Custom handler via VBI. `Inkey` returns ASCII+scancode. `KEY STATE(n)` tests specific scancode.
- **Mouse**: CIA `$BFE001` for buttons, `$DFF00A`/`$DFF00C` for position counters.
- **Joystick**: `Joy(n)` returns bitmask: bit 0=up, 1=down, 2=left, 3=right, 4=fire.

---

## 2. Interpreter Core

### Token Format

Tokens are **word-sized offsets** from the `Tk` label — the token value IS the dispatch table index. No lookup needed.

**Variable tokens** embed a pre-computed offset into the variable area:
```
+0  2 bytes  Reserved (linking)
+2  1 byte   Name length
+3  1 byte   Type: bit0=float(#), bit1=string($)
+4  N bytes  ASCII name (lowercase), padded to even
```

**Constant tokens:**
- `0x003E` (TkEnt): Decimal integer → 4-byte BE value
- `0x0036` (TkHex): Hex integer → 4-byte BE value
- `0x0046` (TkFl): Float → 4-byte AMOS float (mantissa×2^(exp-88))
- `0x0026` (TkCh1): String → 2-byte length + ASCII + padding

### Expression Evaluator

Operator precedence is encoded in the **token ordering itself**. `Evalue` compares raw token word values against a threshold on the stack — higher token IDs bind tighter.

Three types: int=0, float=1, string=2. Automatic coercion via mathffp.library.

### Command Dispatch

The inner loop is 4 instructions:
```asm
ChrGet:  move.w  (a6)+,d0    ; read token word
         beq.s   InsRet       ; zero = end of line
         jmp     0(a4,d0.w)  ; index into token table, jump
InsRet:  ...                   ; advance to next line
```

Extensions use indirect dispatch through `AdTokens` array (up to 26 extension slots).

### Variable Storage

**NO hash table, NO runtime name lookup.** The verifier pre-pass assigns direct byte offsets, patched into the token stream. Runtime variable access is a single indexed load: `base + offset`.

Global vs local: sign bit of offset word. Variables are 6 bytes: 4-byte value + 2-byte type.

### Control Flow Stack

Uses a separate A3 stack (not hardware SP) with typed frames:
- **FOR**: 24 bytes (variable addr, step, limit, return addr)
- **REPEAT/WHILE/DO**: 10 bytes
- **GOSUB**: 12 bytes (magic cookie `"Gosb"`)
- **PROCEDURE**: 44+ bytes (magic cookie `"Proc"`, local vars, saved VarLoc)

### Notable Quirks

- String subtraction operator (removes substrings)
- Anti-crash magic cookies enable stack scanning for error recovery
- A patchable `jmp` instruction enables debug/follow mode
- `AMOS Basic` comparison is TRUE (-1), not 1

---

## 3. Display System

### Screen Data Structure

Each screen is `EcLong` bytes, max 12 screens (`EcMax = 12`).

**Bitplane pointers** (3 sets × 6 planes = 72 bytes):
- `EcLogic` (offset 0): Drawing target
- `EcPhysic` (offset 24): Displayed
- `EcCurrent` (offset 48): Amiga layers sees

**Key fields:**
- `EcCon0/EcCon2`: BPLCON0/2 register values
- `EcTx/EcTy`: Pixel dimensions
- `EcNPlan`: Bitplane count (1-6)
- `EcWX/EcWY/EcWTx/EcWTy`: Display window position/size
- `EcVX/EcVY`: Viewport offset (scrolling)
- `EcPal`: 32 × 16-bit palette ($0RGB)
- `EcFlags`: bit 7=hidden, bit 6=cloned, bit 5=double-buffered
- `EcAuto`: Autoback mode (0=off, 1=simple, 2=double-buffer)

### Screen Open

`Screen Open n,tx,ty,#colors,mode`:
- `tx` must be multiple of 16, range 16-1008
- `ty` range 1-1023
- Colors → plane count: 2→1, 4→2, 8→3, 16→4, 32→5, 64→6
- Mode: bit 15=hires($8000), bit 2=interlace(4), bit 11=HAM($0800)
- Creates Amiga BitMap, LayerInfo, Layer, RastPort via OS calls
- Allocates chip RAM for each bitplane

### Drawing Primitives

All drawing goes through the **AmigaOS graphics library** via the screen's RastPort:
- `Plot` → `WritePixel(rp, x, y)`
- `Draw` → `Draw(rp, x, y)` (line from current position)
- `Circle` → `DrawEllipse(rp, cx, cy, r, r)` (hires: x-radius doubled)
- `Box` → 4-point `PolyDraw`
- `Bar` → `RectFill(rp, x1, y1, x2, y2)`
- `Polygon` → `AreaMove` + `AreaDraw` + `AreaEnd` with TmpRas
- `Paint` → `Flood()` with TmpRas

### Sprite Engine

Up to 64 logical sprites (`HsNb=64`) multiplexed across 8 hardware channels.

**Per-sprite structure** (20 bytes `HsLong`):
- `HsPrev/HsNext`: Doubly-linked Y-sorted list
- `HsX/HsY`: Screen position
- `HsImage`: Pointer to sprite image descriptor
- `HsControl`: Amiga sprite control words

**Multiplexing**: Y-sorted linked list. During `HsAff`, engine builds position table tracking free channels per scanline. Tries to fit each sprite into available channel. Multicolor sprites need paired channels (even-aligned).

**Triple-buffered**: `HsLogic` → `HsPhysic` → `HsInter` to avoid tearing.

### Bob Engine

Bobs use the **blitter** for rendering. Structure ~120 bytes including two decor save areas.

**Three-phase cycle:**
1. **Erase** (`BobEff`): Blitter copies saved background back to screen. BltCon0=`$09F0`
2. **Save+Calculate** (`BobAct`): Save current background, compute clipping/blitter params. Three drawing routines: `BbA16` (aligned), `BbAP` (pixel-shifted), `BbAL` (oversized)
3. **Draw** (`BobAff`): Cookie-cut blit. Minterm `$0FCA`: `D = (A AND B) OR (NOT A AND C)` where A=mask, B=bob, C=background

**Bob flipping**: 256-byte lookup table `TRetour` for bit-reversal. X-flip reverses words, Y-flip reverses rows.

**Priority**: Bubble sort by Y-position. Lower Y = drawn first = behind.

### Copper List Builder

`EcCopper` produces copper lists for multi-screen compositing:
1. **Scanline decomposition**: Priority-sorted screens → horizontal bands
2. **Copper instruction generation**: Per band: WAIT + DMA disable + palette + bitplane pointers + display window + control registers

Two copper lists double-buffered (`CopLogic`/`CopPhysic`, 1024 bytes each).

### Rainbow System

Up to 4 rainbows. Per-scanline color register writes interleaved into copper list. Each rainbow line = WAIT + register write pair (4 bytes).

### Double Buffering

- `Double Buffer`: Allocates second bitplane set. Sets `EcAuto=2`.
- `Screen Swap`: Swaps `EcLogic ↔ EcPhysic` and patches copper list in-place via `CopMark`.
- Autoback mode 2: Full bob cycle per drawing command.

---

## 4. Audio System

### Paula Register Interface

| Channel | Base Address | Constant |
|---------|-------------|----------|
| 0 | `$DFF0A0` | MuChip0 |
| 1 | `$DFF0B0` | MuChip1 |
| 2 | `$DFF0C0` | MuChip2 |
| 3 | `$DFF0D0` | MuChip3 |

Per-channel registers: `+$00`=sample pointer, `+$04`=length (words), `+$06`=period, `+$08`=volume (0-64).

**Clock**: PAL=3,546,895 Hz, NTSC=3,579,545 Hz. Period = clock / frequency.

**DMA Wait**: After toggling DMA, wait 5 raster lines via `$DFF006` polling.

### Sample Format (Bank 5)

- Word at offset 0: number of samples
- Longword offset table (one per sample)
- Per sample: 6-byte name, 2 reserved, 2-byte frequency (Hz), 4-byte length, then raw 8-bit signed PCM

### Built-in Sound Effects

All three use **procedural waveform + envelope combinations**, not stored samples.

**Default waveforms** (generated at reset):
- Wave 0: White noise (LFSR with multiplier `$3171`)
- Wave 1: Square wave (127 for 128 bytes, -127 for 128 bytes)

Sub-octave versions generated by averaging: 256, 128, 64, 32, 16, 8, 4, 2 bytes.

**Envelope definitions** (duration_in_VBLs, target_volume pairs):
```
Boom:  1,64, 10,50, 50,0, 0,0     (Note 36/C2, noise wave, all 4 channels)
Shoot: 1,64, 10,0, 0,0             (Note 60/C4, noise wave, all 4 channels)
Bell:  1,64, 4,40, 25,0, 0,0       (Note 70/Bb4, square wave, all 4 channels)
```

Each effect plays on all 4 channels with note+1 per successive channel (detuned stereo spread).

### Tracker Module Playback

Standard Amiga MOD format. Two-phase DMA: frame 1 plays from sample start, frame 2 switches to loop point.

Supported effects: Arpeggio(0), Portamento up/down(1/2), Tone portamento(3), Vibrato(4), Volume slide(A), Position jump(B), Set volume(C), Pattern break(D), Filter(E1), Set speed(F).

Period range: $71 (B-3) to $358 (C-1). 32-entry sine table for vibrato.

### Envelope System

16.16 fixed-point linear interpolation. VBL processes per-channel: `EnvVol += EnvDelta`, high word → AUDxVOL. `-1` in envelope = loop back to start.

### SAY (Speech Synthesis)

Uses Amiga's `narrator.device` + `translator.library`. Default: rate=150, pitch=110, sex=male, freq=22200Hz. `~` prefix = raw phonemes. Async mode via `SendIO` with lip-sync via `Mouth Read`.

---

## 5. AMAL Engine

### AMAL Bytecode Table

| Offset | Command | Function |
|--------|---------|----------|
| $00 | Stop | Halt program |
| $10 | Wait | Suspend until next Synchro |
| $14 | Pause | One-frame pause |
| $18 | Move dx,dy,steps | Smooth interpolated movement |
| $1C | Jump label | Branch (counted per frame, max 10) |
| $20 | Let R=expr | Register assignment |
| $24 | If expr | Conditional (then Jump/Direct/Exit) |
| $28/$2C | For/Next | Loop |
| $30-$3C | A=/X=/Y=/R= | Write target registers |
| $40-$4C | =A/=X/=Y/=R | Read target registers |
| $50 | =On | -1 if Move active, 0 if idle |
| $5C | =Col(n) | Collision flag |
| $60-$68 | AU()/Exit | AutoTest block |
| $6C | Direct label | Redirect main program from autotest |
| $70 | Literal | Inline 16-bit number |
| $74/$78 | =XMouse/=YMouse | Mouse position |
| $80/$84 | =Joy(0)/=Joy(1) | Joystick state |
| $A0-$AC | +/-/*// | Arithmetic |
| $B0/$B4/$D8 | OR/AND/XOR | Bitwise |
| $CC | Anim | Animation sequence |
| $D0 | =Z(n) | Random number |

### Channel Data Structure (per channel)

| Offset | Field | Size | Purpose |
|--------|-------|------|---------|
| 0 | AmPrev/AmNext | 2×long | Linked list |
| 12 | AmPos | long | Current bytecode position (NULL=suspended) |
| 16 | AmAuto | long | Autotest bytecode position |
| 20 | AmAct | long | Target actualization record |
| 24 | AmBit | word | bit15=frozen, bits0-14=actualization bit |
| 26 | AmCpt | word | Move steps remaining |
| 28 | AmDeltX/Y | 2×long | 16.16 fixed-point deltas |
| 44 | AmAJsr | long | Anim callback pointer |
| 60 | AmIRegs | 10×word | R0-R9 registers |
| 80 | AmStart | ... | Bytecode begins |

### Registers

- **R0-R9**: Channel-local, 16-bit, stored at offset 60
- **RA-RZ**: 26 global shared registers at `T_AmRegs(a5)`
- **A**: Image number (actualization record +6)
- **X/Y**: Position (actualization record +2/+4)

### Synchro System

- **Synchro On** (default): VBI calls `Animeur` every frame. Max 16 channels.
- **Synchro Off**: BASIC `Synchro` command manually calls `Animeur`. Max 64 channels.

### Instruction Limit

Jump instructions decrement counter D6 (10 for main, 20 for autotest). At zero, position is saved and execution returns — prevents infinite loops from hanging VBI.

---

## 6. Editor System

### AMOS 1.3 Editor — 4-color Palette

| Index | $RGB | Color | Usage |
|-------|------|-------|-------|
| 0 | $000 | Black | Background |
| 1 | $017 | Dark blue | Text background |
| 2 | $0EC | Cyan | Keywords |
| 3 | $C40 | Orange/brown | User text |

### AMOS Pro Editor — 8-color Palette

| Index | $RGB | Color | Usage |
|-------|------|-------|-------|
| 0 | $000 | Black | Background |
| 1 | $06F | Blue | Primary |
| 2 | $077 | Teal | Text background |
| 3 | $EEE | Light gray | Normal text |
| 4 | $F00 | Red | Errors |
| 5 | $0DD | Bright cyan | Highlights |
| 6 | $0AA | Medium cyan | Secondary |
| 7 | $FF3 | Yellow | Accent |

### Editor Layout (AMOS Pro)

- Screen: 640×256, 8 colors
- Title bar: 16px tall (AMOS Pro logo 160×16 + 12 clickable buttons 32×16 + memory bars)
- Status bar: 11px per window (window#, line, column, free bytes, edit mode)
- Text: Topaz 8×8 font, text height = (256 - 16 - top_offset) / 8 lines
- Scroll animation: 300 pixels/frame vertical slide

### Syntax Highlighting

AMOS does NOT use conventional syntax highlighting. Source is stored **tokenized**; display is produced by detokenizing on-the-fly. Color distinction comes from the global pen/paper settings, not per-token coloring.

### Key Bindings (AMOS Pro)

| Key | Function |
|-----|----------|
| Cursor keys | Move cursor |
| Shift+Up/Down | Page up/down |
| Shift+Left/Right | Word left/right |
| Ctrl+Left/Right | Start/End of line |
| Ctrl+Shift+Up/Down | Top/Bottom of text |
| Return | New line / execute |
| ESC | Direct mode |
| Ctrl+B | Toggle block mark |
| Ctrl+C | Cut block |
| Ctrl+P | Paste block |
| Ctrl+U/Shift+U | Undo/Redo |
| Amiga+L/S | Load/Save |
| Amiga+F/N | Search/Next |
| Amiga+G | Goto line |
| F1 | Run |
| F2 | Test (syntax check) |
| F8 | Insert/Overwrite toggle |
| F9 | Open/Close procedure fold |

### Procedure Folding

Bit 7 of byte 10 in tokenized procedure header = fold state. When closed, detokenizer skips all lines between `Procedure` and `End Proc` using the body length at offset 4.

### Direct Mode

Triggered by ESC. Separate line editor, 160 chars wide. Commands executed via `Ed_RunDirect` → `L_Prg_RunIt`. Escape screen can be resized/moved by dragging.

---

## 7. File Formats

### .AMOS File Structure

```
Offset  Size    Content
0x00    16      "AMOS Basic V1.3 " or "AMOS Pro V?.?? "
                (byte 11: 'V'=tested, 'v'=untested)
0x10    4       Source code length (BE long)
0x14    N       Tokenized source lines
0x14+N  4       "AmBs" bank section marker
+4      2       Bank count
+6      ...     Individual banks (AmBk/AmSp/AmIc)
```

### Tokenized Line Format

```
Byte 0:  Line length / 2
Byte 1:  Indent level (spaces + 1)
Byte 2+: Token stream (16-bit words, big-endian)
         0x0000 = end of line
```

### Key Token IDs

| Token | Type | Extra Data |
|-------|------|-----------|
| 0x0006 | Variable | 4+N bytes (offset, length, type, name) |
| 0x000C | Label | 4+N bytes |
| 0x003E | Integer literal | 4 bytes (BE int32) |
| 0x0036 | Hex literal | 4 bytes (BE int32) |
| 0x0046 | Float literal | 4 bytes (AMOS float) |
| 0x0026 | String literal | 2-byte length + ASCII |
| 0x004E | Extension token | 1-byte ext#, 1 unused, 2-byte offset |
| 0x064A | Rem | 1+1+N bytes (unused, length, text) |
| 0x0376 | Procedure | 8 bytes (distance, seed, flags) |

### Memory Bank Format (AmBk)

```
Offset  Size    Content
0x00    4       "AmBk" magic
0x04    2       Bank number (1-16)
0x06    2       Memory type (0=chip, 1=fast)
0x08    4       Length (bits 0-27; bit 30=chip flag)
0x0C    8       Type name ("Sprites ", "Music   ", etc.)
0x14    N       Raw bank data
```

### Sprite/Icon Bank (AmSp/AmIc)

```
Header: "AmSp"/"AmIc" + 2-byte sprite count
Per sprite:
  +0  2   Width in 16-pixel words
  +2  2   Height in pixels
  +4  2   Number of bitplanes
  +6  2   X hotspot
  +8  2   Y hotspot
  +10 N   Interleaved planar image data
Footer: 64 bytes (32 × 2-byte palette, $0RGB)
```

### IFF/ILBM Loading

Recognized chunks: BMHD, CAMG, CMAP, CCRT, BODY, AMSC.
- BMHD: width, height, planes, compression
- BODY: Uncompressed or ByteRun1 (PackBits) decompression
- Row-by-row, plane-by-plane decompression into screen bitplanes

---

## 8. Extension System

### Architecture

Up to 26 extensions in numbered slots. Each extension registers at `ExtAdr + ExtNb*16`:
- `+0`: Data zone base address
- `+4`: Default/Reset handler
- `+8`: End/Quit handler
- `+12`: Bank change notification handler

### Extension Header

```
dc.l  C_Tk - C_Off      ; offset to token table
dc.l  C_Lib - C_Tk      ; offset to library routines
dc.l  C_Title - C_Lib   ; offset to title string
dc.l  C_End - C_Title    ; offset to end
dc.w  0 or -1            ; force-include flag
dc.b  "AP20"             ; V2.0 format magic
```

### Command Table Entry Format

```
dc.w  InstructionHandler, FunctionHandler
dc.b  "keyword nam","e"+$80    ; name (last char | $80)
dc.b  "I0,0t0,0"               ; param spec
dc.b  -1                        ; terminator (-2 = variant follows)
```

Parameter spec: `I`=instruction, `0`=int, `1`=float, `2`=string, `t`=TO separator.

### Coded Branch System

Extensions cannot use normal BSR/JSR between routines. Instead:
- `Rbsr L_Routine`: Intra-extension call (4 bytes encoded)
- `Rjsr L_Routine`: Cross-extension call to AMOSPro.Lib
- `SyCall`/`EcCall`: System/screen vector table calls

### Standard Extensions

| Slot | Extension | Commands |
|------|-----------|----------|
| 1 | Music | ~50 cmds: Track, Sam, Wave, Bell/Boom/Shoot, Say |
| 2 | Compact | Compression |
| 3 | Request | REQUEST ON/OFF/WB (3 commands) |
| 4 | 3D | AMOS 3D graphics |
| 5 | Compiler | COMPILE, SQUASH/UNSQUASH |
| 6 | IOPorts | Serial/parallel I/O |

### Creating Custom Extensions

Required: define `ExtNb`, emit header + offset table + token table + library routines. Constraints:
- All code PC-relative, fully relocatable
- No absolute references, no direct BSR/JSR between routines
- Each routine < 32KB
- Single hunk, no relocation table
- D6/D7 must be preserved
- Returns: D3=value, D2=type (0=int, 1=float, 2=string)
