# AMOS Reborn

A native cross-platform reimplementation of AMOS Basic (Amiga, 1990) in C11 + SDL2 + OpenGL.

## Overview

AMOS Basic was the creative programming environment that defined a generation of Amiga developers. Created by Francois Lionet and published by Europress Software in 1990, AMOS made it possible to write games, demos, and multimedia applications using a dialect of BASIC that had direct access to the Amiga's custom hardware -- sprites, copper lists, the Paula sound chip, and the blitter. AMOS Professional followed in 1993 with an improved editor and extended command set. Together, they produced thousands of games and demos across the Amiga community.

AMOS Reborn is the first native cross-platform reimplementation of the AMOS runtime. Unlike AOZ Studio (the official successor, which transpiles AMOS to JavaScript), this project implements a from-scratch C interpreter and rendering pipeline that runs AMOS programs natively on macOS, Linux, and Windows, with WebAssembly support via Emscripten. The goal is cycle-accurate behavior where it matters (timing, palette, audio) and modern convenience where it helps (resolution scaling, CRT shader presets, OpenGL compositing).

The reimplementation is built on a line-by-line study of the original 68000 assembly source -- 134K lines of AMOS Professional source code, distilled into two technical blueprints that drive every design decision. This is not a port. It is a clean-room reconstruction of how AMOS actually works, informed by the original machine code.

## Features

### Interpreter
- [x] Tokenizer, parser, and AST-walking executor
- [x] Integer, float, and string variables (A, A#, A$)
- [x] Arrays (Dim, multi-dimensional)
- [x] Control flow: If/Then/Else, For/Next, While/Wend, Repeat/Until, Do/Loop
- [x] Gosub/Return, Goto, labels
- [x] Procedures (AMOS Professional dialect)
- [x] Data/Read/Restore
- [x] Expression evaluator with operator precedence
- [x] .AMOS tokenized file loader (load original Amiga programs)

### Graphics
- [x] Multi-screen system (up to 8 screens, independent palettes)
- [x] Drawing primitives: Plot, Draw, Box, Bar, Circle, Ellipse
- [x] 4096-color Amiga palette (12-bit RGB)
- [x] Text rendering with authentic Topaz 8x8 bitmap font
- [x] Screen compositing with z-order and priority
- [x] Double buffering and Autoback
- [x] IFF/ILBM image loading (uncompressed + ByteRun1)
- [x] Block operations (Get Block, Put Block)
- [x] Scroll zones (Def Scroll, Scroll)

### Sprites and Bobs
- [x] Hardware-style sprites (up to 64)
- [x] Bobs (blitter objects, screen-bound)
- [x] Sprite/Bob collision detection
- [x] Sprite rendering and compositing

### Audio
- [x] Paula sound chip emulation (4 channels, period-based frequency)
- [x] Amiga low-pass filter emulation
- [x] Built-in sound effects: Boom, Shoot, Bell
- [x] Sample playback (Sam Play, Sam Raw)
- [x] Volume envelopes with multi-segment definitions
- [x] MOD tracker playback (via optional libxmp)

### AMAL (AMOS Animation Language)
- [x] AMAL program compiler and interpreter
- [x] Movement commands (M for linear interpolation)
- [x] Animation commands (A for frame sequencing)
- [x] Register operations (R0-R9 per channel, RA-RZ global)
- [x] Flow control (Jump, Labels)
- [x] Synchro mode (synchronized to VBL)
- [x] 16 independent AMAL channels

### Display
- [x] 7 CRT shader presets (OpenGL 3.3)
- [x] Scanline simulation, bloom, curvature, shadow mask
- [x] Phosphor tinting (amber, green, Commodore 1084S)
- [x] Chromatic aberration and vignette
- [x] Noise and flicker for period-accurate feel

### Input
- [x] Keyboard (Inkey$, Scancode, Key State, Scanshift)
- [x] Mouse (X Mouse, Y Mouse, Mouse Key, Mouse Click)
- [x] Joystick emulation via keyboard (Joy)

### Planned
- [ ] Built-in program editor
- [ ] WASM/Emscripten browser build
- [ ] Rainbow and Copper effects
- [ ] Dual Playfield mode
- [ ] Screen Copy between screens
- [ ] Paint (flood fill)
- [ ] Full AMAL trig functions (sine/cosine paths)
- [ ] Sample bank loading from .abk files

## Screenshots

*Screenshots will be added once the project reaches visual parity milestones.*

| Screenshot | Description |
|------------|-------------|
| demo1_graphics.png | Rainbow bars, concentric circles, and box frames with Topaz font text |
| demo2_starfield.png | Parallax starfield with color-cycling horizontal bars |
| demo3_plasma.png | Plasma palette cycling with bouncing ball physics |
| demo4_sprites.png | 16 sprites bouncing with grid background |
| crt_presets.png | Side-by-side comparison of all 7 CRT shader presets |

## Quick Start

### Prerequisites

- CMake 3.16+
- SDL2
- OpenGL 3.3+ capable GPU
- C11 compiler (GCC, Clang, or MSVC)
- Optional: libxmp (for MOD tracker playback)

### Building on macOS

```bash
brew install sdl2 cmake

cd amos-reborn
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)
```

### Building on Ubuntu / Debian

```bash
sudo apt install build-essential cmake libsdl2-dev libgl-dev

cd amos-reborn
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

For MOD tracker support, also install libxmp:

```bash
# macOS
brew install libxmp

# Ubuntu
sudo apt install libxmp-dev
```

### Running

```bash
# Run the built-in graphics demo
./amos-reborn

# Load a demo program
./amos-reborn ../demos/demo1.txt

# Load an original .AMOS tokenized file from the Amiga
./amos-reborn classic_program.AMOS
```

### Running Tests

```bash
cd build
./amos-test
```

All 43 tests should pass across 5 test suites (tokenizer, parser, executor, expressions, IFF loader).

## CRT Shader Presets

Press **Ctrl+T** at any time to cycle through 7 display presets:

| Preset | Description |
|--------|-------------|
| Clean | No post-processing. Sharp pixels, modern display. |
| VGA | Subtle scanlines and mild bloom. PC monitor feel. |
| CRT | Full CRT simulation: scanlines, curvature, shadow mask, bloom. |
| Amber | Amber phosphor monochrome. Hercules terminal aesthetic. |
| Green | Green phosphor monochrome. Classic hacker terminal. |
| TV | Consumer television: heavy scanlines, chromatic aberration, noise. |
| Commodore | Commodore 1084S monitor: warm tint, moderate curvature, period-accurate color. |

All presets use a single OpenGL 3.3 fragment shader with tunable parameters for scanline intensity, bloom, curvature, shadow mask, chromatic offset, vignette, noise, flicker, and phosphor tint.

## Architecture

AMOS Reborn is organized in four layers:

```
+--------------------------------------------------+
|                   Platform Layer                  |
|           SDL2 + OpenGL + Audio Callback          |
+--------------------------------------------------+
|                   Display Layer                   |
|   Screens | Compositor | CRT Shader | Sprites    |
+--------------------------------------------------+
|                 Interpreter Layer                 |
|  Tokenizer | Parser | Executor | AMAL | Loader   |
+--------------------------------------------------+
|                    HAL Layer                      |
|         Classic (Amiga-accurate) | Modern         |
+--------------------------------------------------+
```

**Platform** handles window creation, input polling, audio device management, and frame presentation via SDL2. OpenGL is used only for the CRT shader pass.

**Display** manages up to 8 independent screens, each with its own RGBA framebuffer, palette, and text cursor. The compositor merges screens by z-order priority. Sprites and Bobs are rendered into the composite. The CRT shader applies post-processing as a full-screen quad.

**Interpreter** implements the full AMOS execution pipeline. The tokenizer produces a token stream from source text (or detokenizes original .AMOS binary files). The parser builds an AST per line. The executor walks the AST, dispatching commands and evaluating expressions. The AMAL engine runs as a parallel interpreter with its own program counter, registers, and instruction set.

**HAL** (Hardware Abstraction Layer) provides the switch between Classic mode (Amiga-accurate timing, planar display, OCS/ECS palette limits, 50Hz VBL) and Modern mode (host-native resolution, RGBA, unlocked framerate).

### Source Layout

```
src/
  interpreter/    Tokenizer, parser, executor, expressions, AMAL, .AMOS loader
  display/        Screen, drawing, compositor, CRT shader, sprites, IFF, Topaz font
  audio/          Paula emulation, sound effects, mixer, MOD tracker
  hal/            Classic and Modern display mode abstraction
  platform/       SDL2 window/event loop, input handling
  main.c          Entry point and program loading
include/
  amos.h          Master header: all types, constants, and API declarations
tests/
  test_*.c        Unit tests for each subsystem
reference/
  AMOS_TECHNICAL_BLUEPRINT.md     System architecture reference (from 134K lines of 68K asm)
  INTERPRETER_BLUEPRINT.md        Token format and interpreter internals
  amos-1.3/                       Original AMOS 1.3 source (pervognsen/amos)
  AMOS-Professional-365/          AMOS Professional 68000 assembly (122K lines)
```

## AMOS Compatibility

| Category | Status | Notes |
|----------|--------|-------|
| Core BASIC | Working | Variables, arrays, control flow, procedures, expressions |
| Graphics primitives | Working | Plot, Draw, Box, Bar, Circle, Ellipse, Cls, Ink, Palette |
| Screen system | Working | Open, Close, Display, Offset, z-order, double buffer |
| Text output | Working | Print, Locate, Topaz font, pen/paper colors |
| Sprites | Working | Set, move, collision detect, up to 64 |
| Bobs | Working | Screen-bound blitter objects |
| AMAL | Working | Movement, animation, registers, labels, jumps |
| Audio (Paula) | Working | 4-channel, effects (Boom/Shoot/Bell), samples, envelopes |
| IFF/ILBM loading | Working | Uncompressed and ByteRun1 compressed |
| .AMOS file loading | Working | Detokenizes original Amiga binaries |
| MOD tracker | Optional | Requires libxmp |
| Rainbow/Copper | Planned | Not yet implemented |
| Dual Playfield | Planned | Not yet implemented |
| Editor | Planned | Not yet implemented |

## Technical Blueprint

The `reference/` directory contains the engineering foundation for this project:

- **AMOS_TECHNICAL_BLUEPRINT.md** -- Complete system architecture derived from the AMOS Professional 68000 assembly source (122,762 lines). Covers memory layout, interpreter dispatch, display system, audio, AMAL, file formats, and the extension system.

- **INTERPRETER_BLUEPRINT.md** -- Token format, variable encoding, expression evaluation, and the internal dispatch mechanism. Documents how the original tokenizer, parser, and executor actually work at the assembly level.

- **AMOS-Professional-365/** -- The original AMOS Professional source code in 68000 assembly. This is the primary reference for behavioral accuracy.

- **amos-1.3/** -- The AMOS 1.3 source from pervognsen/amos on GitHub.

## Demo Programs

Six demo programs are included in `demos/`:

| Demo | File | Description |
|------|------|-------------|
| Graphics Showcase | demo1.txt | Static graphics: rainbow bars, concentric circles, box frames, diagonal lines. Exercises core drawing commands. |
| Starfield | demo2_starfield.txt | Animated 3-layer parallax starfield with color-cycling horizontal bars and a bouncing white bar. |
| Plasma + Balls | demo3_plasma.txt | Palette-cycling plasma background with 8 bouncing balls. Demonstrates dynamic palette manipulation and physics. |
| Sprites | demo4_sprites.txt | 16 sprites bouncing off screen edges over a grid background with a pulsing center circle. |
| Input Test | demo5_input.txt | Interactive input demo: keyboard, mouse, and joystick. Move a colored square with arrows/WASD, click to change color. |
| AMAL | demo6_amal.txt | Three sprites driven by the AMAL animation engine: linear movement, frame animation, and circular path. |

Run any demo:

```bash
./amos-reborn demos/demo3_plasma.txt
```

## Roadmap

AMOS Reborn is under active development. Current status: **v0.3.0** (26 source files, ~9,300 lines of C, 43 passing tests).

Near-term priorities:

1. **Program editor** -- Integrated line editor with syntax highlighting, matching the original AMOS editor workflow
2. **WebAssembly build** -- Browser-playable via Emscripten (CMakeLists.txt already supports it)
3. **Rainbow and Copper** -- Amiga copper list emulation for per-scanline palette changes
4. **Paint (flood fill)** -- The last major missing drawing primitive
5. **Sample bank loading** -- Load .abk sample banks from original AMOS programs
6. **Dual Playfield** -- Two independent scrolling playfields, a signature Amiga capability

Longer-term:

- Full AMOS Professional command coverage
- Extension system (.lib loading)
- Network play extensions
- Mobile builds (iOS, Android via SDL2)

## Building for WebAssembly

The CMakeLists.txt includes Emscripten support. To build for the browser:

```bash
mkdir build-wasm && cd build-wasm
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release
emmake make -j$(nproc)

# Produces amos-reborn.html, amos-reborn.js, amos-reborn.wasm
# Serve with any HTTP server:
python3 -m http.server 8080
```

Note: WASM builds are experimental. Audio and file loading require additional Emscripten configuration.

## Project Stats

| Metric | Value |
|--------|-------|
| Language | C11 |
| Source files | 26 (.c) + 1 header |
| Lines of code | ~9,300 |
| Test suites | 5 (tokenizer, parser, executor, expressions, IFF) |
| Tests passing | 43 / 43 |
| Demo programs | 6 |
| CRT presets | 7 |
| Reference material | 134K lines of original 68K assembly |
| Dependencies | SDL2, OpenGL 3.3 (optional: libxmp) |

## Credits and References

**Original AMOS Basic** by Francois Lionet, published by Europress Software (1990). AMOS Professional (1993) extended the language with procedures, a new editor, and additional commands.

### Source References

- [pervognsen/amos](https://github.com/pervognsen/amos) -- AMOS 1.3 source code (68000 assembly)
- AMOS Professional 365 -- Complete AMOS Professional source (122,762 lines of 68K assembly), used as the primary reimplementation reference

### Related Projects

- [AOZ Studio](https://aoz.studio/) -- Official AMOS successor by Francois Lionet. Transpiles AMOS to JavaScript. Different approach: AOZ targets the browser; AMOS Reborn targets native execution.

## License

MIT License. See [LICENSE](LICENSE) for details.

---

AMOS Reborn is not affiliated with Europress Software, Clickteam, or AOZ Studio. AMOS Basic is a trademark of its respective owners. This is an independent open-source reimplementation for preservation and education.
