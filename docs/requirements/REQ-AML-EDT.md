# AMOS Reborn -- AMAL Engine & Editor System Requirements

**Document:** REQ-AML-EDT
**Generated:** 2026-04-10
**Source:** AMOS_TECHNICAL_BLUEPRINT.md sections 5 (AMAL Engine) and 6 (Editor System)
**Scope:** Formal V&V requirements with acceptance criteria

---

## Conventions

- **Level 1** = unit test (single function)
- **Level 2** = subsystem test (compiler + interpreter interaction)
- **Level 3** = golden test (full program, compared against Amiga reference)
- **Status:** IMPL = implemented and testable, PARTIAL = partially implemented, TODO = not yet implemented

---

## AMAL Engine (REQ-AML)

### Bytecode Table -- Opcodes

```
REQ-AML-001: AMAL bytecode -- Stop opcode
  Acceptance: Compiling "S" (a stop-only program) produces trailing bytecode 0x00 (AMAL_STOP);
              executing halts the channel (pc stays on STOP, no further instructions run)
  Level: 2 (subsystem)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $00
  Status: IMPL
```

```
REQ-AML-002: AMAL bytecode -- Wait opcode
  Acceptance: Compiling "W" produces AMAL_WAIT; executing sets waiting_synchro=true and
              suspends execution until amos_amal_synchro() is called
  Level: 2 (subsystem)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $10
  Status: IMPL
```

```
REQ-AML-003: AMAL bytecode -- Pause opcode
  Acceptance: Compiling "P" produces AMAL_PAUSE; executing returns immediately (one-frame pause),
              next tick resumes at the instruction after Pause
  Level: 2 (subsystem)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $14
  Status: IMPL
```

```
REQ-AML-004: AMAL bytecode -- Move opcode with 16.16 fixed-point deltas
  Acceptance: Compiling "M 100,200,50" produces AMAL_MOVE followed by dx(32-bit), dy(32-bit),
              steps(16-bit); at runtime, delta_x = (100 << 16) / 50, delta_y = (200 << 16) / 50;
              after 50 ticks, target X incremented by exactly 100, target Y by exactly 200
  Level: 2 (subsystem)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $18
  Status: IMPL
```

```
REQ-AML-005: AMAL bytecode -- Jump opcode
  Acceptance: Compiling "A J A" (label A, jump to A) produces AMAL_JUMP with 16-bit target
              offset resolved to the bytecode position of label A; executing sets pc = target
  Level: 2 (subsystem)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $1C
  Status: IMPL
```

```
REQ-AML-006: AMAL bytecode -- Let opcode (register assignment)
  Acceptance: Compiling "L R0=42" produces AMAL_LET, register(8-bit)=0, followed by expression
              bytecode for literal 42 + EXPR_END; executing sets R0 = 42
  Level: 2 (subsystem)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $20
  Status: IMPL
```

```
REQ-AML-007: AMAL bytecode -- If opcode (conditional jump)
  Acceptance: Compiling "I R0=1 J A" produces AMAL_IF, expression bytes, EXPR_END,
              action_byte=0 (jump), target_offset(16-bit); executing evaluates expression,
              if nonzero jumps to label A, otherwise falls through
  Level: 2 (subsystem)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $24
  Status: IMPL
```

```
REQ-AML-008: AMAL bytecode -- If with Direct action
  Acceptance: Compiling "I R0=1 D B" produces AMAL_IF with action_byte=1 (direct);
              executing sets direct_offset to label B's position, redirecting main program
  Level: 2 (subsystem)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $24 (Direct variant)
  Status: IMPL
```

```
REQ-AML-009: AMAL bytecode -- For/Next loop
  Acceptance: Compiling "F R0=1 To 10; ... ;N R0" produces AMAL_FOR(register=0, start_expr,
              end_expr) and AMAL_NEXT(register=0); loop body executes exactly 10 times,
              R0 increments from 1 to 10
  Level: 2 (subsystem)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $28/$2C
  Status: IMPL
```

```
REQ-AML-010: AMAL bytecode -- A= (Set Image)
  Acceptance: Compiling "A=5" produces AMAL_SET_A followed by expression;
              executing sets the target sprite/bob image to 5
  Level: 2 (subsystem)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $30 (A=)
  Status: PARTIAL (compiler emits AMAL_SET_A but 'A=' syntax not explicitly parsed;
          only standalone A as Anim is parsed. A= write path exists via AMAL_SET_A opcode)
```

```
REQ-AML-011: AMAL bytecode -- X= (Set X position)
  Acceptance: Compiling "X=100" produces AMAL_SET_X followed by expression + EXPR_END;
              executing sets target sprite/bob X to 100
  Level: 2 (subsystem)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $34 (X=)
  Status: IMPL
```

```
REQ-AML-012: AMAL bytecode -- Y= (Set Y position)
  Acceptance: Compiling "Y=200" produces AMAL_SET_Y followed by expression + EXPR_END;
              executing sets target sprite/bob Y to 200
  Level: 2 (subsystem)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $38 (Y=)
  Status: IMPL
```

```
REQ-AML-013: AMAL bytecode -- R= (Set Register from expression)
  Acceptance: Compiling "L R5=99" produces AMAL_LET with reg=5;
              executing sets channel register R5 to 99
  Level: 2 (subsystem)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $3C (R=)
  Status: IMPL (via Let opcode)
```

```
REQ-AML-014: AMAL bytecode -- =A (Read Image)
  Acceptance: In expression context, "=A" compiles to AMAL_EXPR_A;
              evaluating pushes current target image number onto expression stack
  Level: 1 (unit)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $40 (=A)
  Status: IMPL
```

```
REQ-AML-015: AMAL bytecode -- =X (Read X position)
  Acceptance: In expression context, "=X" compiles to AMAL_EXPR_X;
              evaluating pushes current target sprite/bob X onto expression stack
  Level: 1 (unit)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $44 (=X)
  Status: IMPL
```

```
REQ-AML-016: AMAL bytecode -- =Y (Read Y position)
  Acceptance: In expression context, "=Y" compiles to AMAL_EXPR_Y;
              evaluating pushes current target sprite/bob Y onto expression stack
  Level: 1 (unit)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $48 (=Y)
  Status: IMPL
```

```
REQ-AML-017: AMAL bytecode -- =R (Read Register)
  Acceptance: In expression context, "=R3" compiles to AMAL_EXPR_REG + register_index(8-bit)=3;
              evaluating pushes ch->registers[3] onto expression stack
  Level: 1 (unit)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $4C (=R)
  Status: IMPL
```

```
REQ-AML-018: AMAL bytecode -- =On (Move active flag)
  Acceptance: In expression context, "=On" compiles to AMAL_EXPR_ON;
              evaluating returns -1 if move_steps > 0, else 0
  Level: 1 (unit)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $50
  Status: IMPL
```

```
REQ-AML-019: AMAL bytecode -- =Col(n) (Collision flag)
  Acceptance: In expression context, "=Col(n)" compiles to a collision-check opcode;
              evaluating returns the collision flag for the specified object
  Level: 2 (subsystem)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $5C
  Status: TODO (no AMAL_EXPR_COL opcode or collision system in amal.c)
```

```
REQ-AML-020: AMAL bytecode -- AU() (Autotest block)
  Acceptance: Compiling "AU(I R0=1 D A)" produces AMAL_AUTOTEST_BEGIN at the start
              and AMAL_AUTOTEST_END at the end; autotest_offset is recorded;
              during normal execution the block is skipped; each tick calls
              amal_exec_autotest() which runs the block with jump_limit=20
  Level: 2 (subsystem)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $60
  Status: IMPL
```

```
REQ-AML-021: AMAL bytecode -- Exit (from autotest)
  Acceptance: Exit opcode within AU() block terminates autotest execution
              and restores saved PC
  Level: 2 (subsystem)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $68
  Status: TODO (no explicit AMAL_EXIT opcode; autotest exits on AUTOTEST_END or unknown opcode)
```

```
REQ-AML-022: AMAL bytecode -- Direct opcode
  Acceptance: Compiling "D A" produces AMAL_DIRECT + target_offset(16-bit);
              executing sets direct_offset = target, which redirects main program
              PC on next tick
  Level: 2 (subsystem)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $6C
  Status: IMPL
```

```
REQ-AML-023: AMAL bytecode -- Literal (inline 16-bit number)
  Acceptance: Expression compiler emits AMAL_EXPR_NUM with a 32-bit value for inline
              numeric literals; evaluator pushes the value onto the expression stack
  Level: 1 (unit)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $70
  Status: IMPL (uses 32-bit AMAL_EXPR_NUM rather than 16-bit Literal; functionally equivalent)
```

```
REQ-AML-024: AMAL bytecode -- =XMouse (Mouse X position)
  Acceptance: In expression context, "=XM" compiles to AMAL_EXPR_XM;
              evaluating pushes current mouse X coordinate onto expression stack
  Level: 1 (unit)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $74
  Status: PARTIAL (opcode exists and compiles; evaluator returns 0 with TODO comment)
```

```
REQ-AML-025: AMAL bytecode -- =YMouse (Mouse Y position)
  Acceptance: In expression context, "=YM" compiles to AMAL_EXPR_YM;
              evaluating pushes current mouse Y coordinate onto expression stack
  Level: 1 (unit)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $78
  Status: PARTIAL (opcode exists and compiles; evaluator returns 0 with TODO comment)
```

```
REQ-AML-026: AMAL bytecode -- =Joy(0) (Joystick 0)
  Acceptance: In expression context, "=J0" compiles to AMAL_EXPR_J0;
              evaluating returns joystick 0 bitmask (bit0=up, 1=down, 2=left, 3=right, 4=fire)
  Level: 1 (unit)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $80
  Status: PARTIAL (opcode exists and compiles; evaluator returns 0 with TODO comment)
```

```
REQ-AML-027: AMAL bytecode -- =Joy(1) (Joystick 1)
  Acceptance: In expression context, "=J1" compiles to AMAL_EXPR_J1;
              evaluating returns joystick 1 bitmask
  Level: 1 (unit)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $84
  Status: PARTIAL (opcode exists and compiles; evaluator returns 0 with TODO comment)
```

```
REQ-AML-028: AMAL bytecode -- Addition operator
  Acceptance: Expression "3+5" compiles to NUM(3), NUM(5), EXPR_ADD;
              evaluating returns 8
  Level: 1 (unit)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $A0
  Status: IMPL
```

```
REQ-AML-029: AMAL bytecode -- Subtraction operator
  Acceptance: Expression "10-4" compiles to NUM(10), NUM(4), EXPR_SUB;
              evaluating returns 6
  Level: 1 (unit)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $A4
  Status: IMPL
```

```
REQ-AML-030: AMAL bytecode -- Multiplication operator
  Acceptance: Expression "6*7" compiles to NUM(6), NUM(7), EXPR_MUL;
              evaluating returns 42
  Level: 1 (unit)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $A8
  Status: IMPL
```

```
REQ-AML-031: AMAL bytecode -- Division operator
  Acceptance: Expression "20/4" compiles to NUM(20), NUM(4), EXPR_DIV;
              evaluating returns 5; division by zero returns 0
  Level: 1 (unit)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $AC
  Status: IMPL
```

```
REQ-AML-032: AMAL bytecode -- OR operator
  Acceptance: Expression "5|3" compiles to NUM(5), NUM(3), EXPR_OR;
              evaluating returns 7 (bitwise OR)
  Level: 1 (unit)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $B0
  Status: IMPL
```

```
REQ-AML-033: AMAL bytecode -- AND operator
  Acceptance: Expression "7&3" compiles to NUM(7), NUM(3), EXPR_AND;
              evaluating returns 3 (bitwise AND)
  Level: 1 (unit)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $B4
  Status: IMPL
```

```
REQ-AML-034: AMAL bytecode -- XOR operator
  Acceptance: Expression "5!3" compiles to NUM(5), NUM(3), EXPR_XOR;
              evaluating returns 6 (bitwise XOR)
  Level: 1 (unit)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $D8
  Status: IMPL
```

```
REQ-AML-035: AMAL bytecode -- Anim opcode
  Acceptance: Compiling "A 1,5;2,5;3,5" produces AMAL_ANIM, count=3, then 3 pairs of
              (image(16-bit), delay(16-bit)); executing sets image to frame[0].image,
              decrements delay each tick, cycles through frames, loops to frame 0
  Level: 2 (subsystem)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $CC
  Status: IMPL
```

```
REQ-AML-036: AMAL bytecode -- =Z(n) (Random number)
  Acceptance: In expression context, "=Z(100)" compiles expression for 100 then
              AMAL_EXPR_RAND; evaluating returns a value in range [0, 99]
  Level: 1 (unit)
  Source: Blueprint S5 "AMAL Bytecode Table" -- offset $D0
  Status: IMPL
```

### Channel Data Structure

```
REQ-AML-040: Channel data -- linked list pointers (AmPrev/AmNext at offset 0)
  Acceptance: Channel structure contains linked list fields at the start;
              channels can be iterated via next pointers
  Level: 2 (subsystem)
  Source: Blueprint S5 "Channel Data Structure" -- offset 0
  Status: TODO (current implementation uses flat array state->amal[i], no linked list)
```

```
REQ-AML-041: Channel data -- AmPos (bytecode position at offset 12)
  Acceptance: Channel has a program counter (pc) field; NULL/0 = suspended, nonzero = active
  Level: 2 (subsystem)
  Source: Blueprint S5 "Channel Data Structure" -- offset 12
  Status: IMPL (amal_channel_t.pc serves this role)
```

```
REQ-AML-042: Channel data -- AmAuto (autotest position at offset 16)
  Acceptance: Channel tracks autotest bytecode offset separately from main program;
              autotest executes every tick before main program
  Level: 2 (subsystem)
  Source: Blueprint S5 "Channel Data Structure" -- offset 16
  Status: IMPL (amal_ext_t.autotest_offset)
```

```
REQ-AML-043: Channel data -- AmAct (target actualization record at offset 20)
  Acceptance: Channel has target_type (sprite=0, bob=1) and target_id fields
              referencing the actualization record
  Level: 2 (subsystem)
  Source: Blueprint S5 "Channel Data Structure" -- offset 20
  Status: IMPL (amal_channel_t.target_type + target_id)
```

```
REQ-AML-044: Channel data -- AmBit (freeze/actualization at offset 24)
  Acceptance: Channel has a frozen flag (bit 15); freezing suspends execution
              without resetting state; thawing resumes from saved position
  Level: 2 (subsystem)
  Source: Blueprint S5 "Channel Data Structure" -- offset 24
  Status: IMPL (amal_channel_t.frozen)
```

```
REQ-AML-045: Channel data -- AmCpt (Move steps remaining at offset 26)
  Acceptance: During Move execution, remaining step count is tracked;
              when it reaches 0, Move is complete and bytecode resumes
  Level: 2 (subsystem)
  Source: Blueprint S5 "Channel Data Structure" -- offset 26
  Status: IMPL (amal_ext_t.move_steps)
```

```
REQ-AML-046: Channel data -- AmDeltX/AmDeltY (16.16 deltas at offset 28)
  Acceptance: Move deltas are stored as 16.16 fixed-point values;
              move_delta_x = (dx << 16) / steps; fractional accumulation is tracked
              in move_accum_x/y, integer part applied per tick
  Level: 2 (subsystem)
  Source: Blueprint S5 "Channel Data Structure" -- offset 28
  Status: IMPL (amal_ext_t.move_delta_x/y, move_accum_x/y)
```

```
REQ-AML-047: Channel data -- AmIRegs (R0-R9 at offset 60)
  Acceptance: Each channel has 10 x 16-bit registers R0-R9 stored contiguously;
              Let R0=42 sets registers[0]=42; registers are channel-local
  Level: 1 (unit)
  Source: Blueprint S5 "Channel Data Structure" -- offset 60
  Status: IMPL (amal_channel_t.registers[10], stored as int not int16_t)
```

### Registers

```
REQ-AML-050: Registers -- R0-R9 channel-local
  Acceptance: Each of the 16 channels has its own R0-R9; writing R5 in channel 0
              does not affect R5 in channel 1; values are 16-bit signed
  Level: 2 (subsystem)
  Source: Blueprint S5 "Registers"
  Status: IMPL (channel-local; note: stored as int, not strictly 16-bit)
```

```
REQ-AML-051: Registers -- RA-RZ global shared
  Acceptance: 26 global registers RA-RZ stored at state->amal_global_regs[0..25];
              accessible from any channel; writing RA in channel 0 is visible from channel 3
  Level: 2 (subsystem)
  Source: Blueprint S5 "Registers"
  Status: IMPL (state->amal_global_regs[26])
```

```
REQ-AML-052: Registers -- A/X/Y read and write target
  Acceptance: A reads/writes the image number of the target sprite/bob;
              X reads/writes the X position; Y reads/writes the Y position
  Level: 2 (subsystem)
  Source: Blueprint S5 "Registers"
  Status: IMPL (via amal_set_x/y/image and AMAL_EXPR_X/Y/A)
```

### Jump Limit

```
REQ-AML-055: Jump limit -- main program max 10
  Acceptance: In main program execution, jump_count starts at 10;
              each AMAL_JUMP decrements it; at 0, execution suspends
              and resumes next tick (prevents VBI hang from infinite loops)
  Level: 2 (subsystem)
  Source: Blueprint S5 "Instruction Limit"
  Status: IMPL (AMAL_JUMP_LIMIT = 10)
```

```
REQ-AML-056: Jump limit -- autotest max 20
  Acceptance: In autotest execution, jump_count starts at 20;
              each jump in autotest decrements it; at 0, autotest returns
  Level: 2 (subsystem)
  Source: Blueprint S5 "Instruction Limit"
  Status: IMPL (hardcoded 20 in amal_exec_autotest)
```

### Synchro System

```
REQ-AML-060: Synchro On mode -- VBI-driven, max 16 channels
  Acceptance: Default mode; amos_amal_tick() iterates channels 0..15;
              Wait suspends until next synchro event; max 16 channels supported
  Level: 2 (subsystem)
  Source: Blueprint S5 "Synchro System"
  Status: IMPL (AMOS_MAX_AMAL_CHANNELS = 16; amos_amal_tick iterates all)
```

```
REQ-AML-061: Synchro Off mode -- manual, max 64 channels
  Acceptance: When Synchro Off, BASIC code must call Synchro command manually to
              advance AMAL; max 64 channels supported in this mode
  Level: 2 (subsystem)
  Source: Blueprint S5 "Synchro System"
  Status: PARTIAL (amos_amal_synchro() exists but max channels is 16 not 64;
          no Synchro On/Off toggle mechanism)
```

### Move Fixed-Point Accuracy

```
REQ-AML-065: Move -- 16.16 fixed-point accumulation
  Acceptance: Move 1,0,3 over 3 steps moves target X by exactly 1 total;
              fractional accumulation ensures no drift; after all steps,
              total displacement equals requested dx/dy exactly
  Level: 2 (subsystem)
  Source: Blueprint S5 "Channel Data Structure" -- "16.16 fixed-point deltas"
  Status: IMPL
```

### Comparison Operators

```
REQ-AML-070: Expression -- equality returns AMOS-style TRUE (-1)
  Acceptance: Expression "5=5" evaluates to -1 (AMOS TRUE);
              "5=6" evaluates to 0 (AMOS FALSE)
  Level: 1 (unit)
  Source: Blueprint S2 "Notable Quirks" -- "comparison is TRUE (-1), not 1"
  Status: IMPL (AMAL_EXPR_EQ returns -1 or 0)
```

```
REQ-AML-071: Expression -- all comparison operators return -1/0
  Acceptance: <, >, <=, >=, <> all return -1 for true, 0 for false
  Level: 1 (unit)
  Source: Blueprint S2 "Notable Quirks"
  Status: IMPL
```

### Expression Evaluator

```
REQ-AML-075: Expression -- operator precedence
  Acceptance: "2+3*4" evaluates to 14 (multiply before add);
              "2*3+4" evaluates to 10; parentheses override: "(2+3)*4" = 20
  Level: 1 (unit)
  Source: Blueprint S5 (implied by expression grammar)
  Status: IMPL (recursive descent: factor handles *, term handles +)
```

```
REQ-AML-076: Expression -- unary negation
  Acceptance: "-5" compiles to NUM(5), EXPR_NEG; evaluating returns -5
  Level: 1 (unit)
  Source: Blueprint S5 (implied)
  Status: IMPL
```

```
REQ-AML-077: Expression -- global register read
  Acceptance: In expression, a bare uppercase letter (e.g., "B") compiles to
              AMAL_EXPR_GREG + index; evaluating reads state->amal_global_regs[index]
  Level: 1 (unit)
  Source: Blueprint S5 "Registers" -- "RA-RZ: 26 global shared"
  Status: IMPL
```

---

## Editor System (REQ-EDT)

### AMOS 1.3 Palette

```
REQ-EDT-001: AMOS 1.3 palette -- index 0 = $000 (Black)
  Acceptance: editor_set_palette() sets palette[0] = RGBA(0x00,0x00,0x00,0xFF)
  Level: 1 (unit)
  Source: Blueprint S6 "AMOS 1.3 Editor -- 4-color Palette"
  Status: IMPL
```

```
REQ-EDT-002: AMOS 1.3 palette -- index 1 = $017 (Dark Blue)
  Acceptance: palette[1] = RGBA(0x00,0x22,0x88,0xFF) -- boosted from raw $017
              for modern displays; used as text background
  Level: 1 (unit)
  Source: Blueprint S6 "AMOS 1.3 Editor -- 4-color Palette"
  Status: IMPL
```

```
REQ-EDT-003: AMOS 1.3 palette -- index 2 = $0EC (Cyan)
  Acceptance: palette[2] = RGBA(0x00,0xEE,0xCC,0xFF); used for keywords
  Level: 1 (unit)
  Source: Blueprint S6 "AMOS 1.3 Editor -- 4-color Palette"
  Status: IMPL
```

```
REQ-EDT-004: AMOS 1.3 palette -- index 3 = $C40 (Orange)
  Acceptance: palette[3] = RGBA(0xCC,0x44,0x00,0xFF); used for user text
  Level: 1 (unit)
  Source: Blueprint S6 "AMOS 1.3 Editor -- 4-color Palette"
  Status: IMPL
```

### AMOS Pro Palette

```
REQ-EDT-010: AMOS Pro palette -- index 0 = $000 (Black)
  Acceptance: palette[0] = RGBA(0x00,0x00,0x00,0xFF)
  Level: 1 (unit)
  Source: Blueprint S6 "AMOS Pro Editor -- 8-color Palette"
  Status: IMPL
```

```
REQ-EDT-011: AMOS Pro palette -- index 1 = $06F (Blue)
  Acceptance: palette[1] = RGBA(0x00,0x66,0xFF,0xFF)
  Level: 1 (unit)
  Source: Blueprint S6 "AMOS Pro Editor -- 8-color Palette"
  Status: IMPL
```

```
REQ-EDT-012: AMOS Pro palette -- index 2 = $077 (Teal)
  Acceptance: palette[2] = RGBA(0x00,0x77,0x77,0xFF); used as text background
  Level: 1 (unit)
  Source: Blueprint S6 "AMOS Pro Editor -- 8-color Palette"
  Status: IMPL
```

```
REQ-EDT-013: AMOS Pro palette -- index 3 = $EEE (Light Gray)
  Acceptance: palette[3] = RGBA(0xEE,0xEE,0xEE,0xFF); used for normal text
  Level: 1 (unit)
  Source: Blueprint S6 "AMOS Pro Editor -- 8-color Palette"
  Status: IMPL
```

```
REQ-EDT-014: AMOS Pro palette -- index 4 = $F00 (Red)
  Acceptance: palette[4] = RGBA(0xFF,0x00,0x00,0xFF); used for errors
  Level: 1 (unit)
  Source: Blueprint S6 "AMOS Pro Editor -- 8-color Palette"
  Status: IMPL
```

```
REQ-EDT-015: AMOS Pro palette -- index 5 = $0DD (Bright Cyan)
  Acceptance: palette[5] = RGBA(0x00,0xDD,0xDD,0xFF); used for highlights/keywords
  Level: 1 (unit)
  Source: Blueprint S6 "AMOS Pro Editor -- 8-color Palette"
  Status: IMPL
```

```
REQ-EDT-016: AMOS Pro palette -- index 6 = $0AA (Medium Cyan)
  Acceptance: palette[6] = RGBA(0x00,0xAA,0xAA,0xFF); used for comments/secondary
  Level: 1 (unit)
  Source: Blueprint S6 "AMOS Pro Editor -- 8-color Palette"
  Status: IMPL
```

```
REQ-EDT-017: AMOS Pro palette -- index 7 = $FF3 (Yellow)
  Acceptance: palette[7] = RGBA(0xFF,0xFF,0x33,0xFF); used for string literals/accent
  Level: 1 (unit)
  Source: Blueprint S6 "AMOS Pro Editor -- 8-color Palette"
  Status: IMPL
```

### Editor Layout (AMOS Pro)

```
REQ-EDT-020: Pro layout -- screen 640x256, 8 colors
  Acceptance: Editor opens with screen dimensions suitable for 8-color display;
              blueprint specifies 640x256
  Level: 2 (subsystem)
  Source: Blueprint S6 "Editor Layout (AMOS Pro)"
  Status: PARTIAL (current implementation uses 320x256 not 640x256;
          EDITOR_SCREEN_W = 320 in editor.h)
```

```
REQ-EDT-021: Pro layout -- title bar 16px tall
  Acceptance: Title bar occupies 2 rows of 8px = 16px total;
              EDITORPRO_TITLE_ROWS = 2
  Level: 1 (unit)
  Source: Blueprint S6 "Editor Layout" -- "Title bar: 16px tall"
  Status: IMPL
```

```
REQ-EDT-022: Pro layout -- Topaz 8x8 font
  Acceptance: Editor uses Topaz 8x8 bitmap font; EDITOR_CHAR_W=8, EDITOR_CHAR_H=8;
              character drawing uses topaz_font_8x8[][] lookup
  Level: 1 (unit)
  Source: Blueprint S6 "Editor Layout" -- "Topaz 8x8 font"
  Status: IMPL
```

```
REQ-EDT-023: Pro layout -- code area rows 2-28
  Acceptance: EDITORPRO_CODE_TOP=2, EDITORPRO_CODE_BOTTOM=28;
              visible lines = 27
  Level: 1 (unit)
  Source: Blueprint S6 "Editor Layout"
  Status: IMPL
```

```
REQ-EDT-024: Pro layout -- status bar below code area
  Acceptance: EDITORPRO_STATUS_ROW=29; shows window#, line, column, free bytes, edit mode
  Level: 1 (unit)
  Source: Blueprint S6 "Editor Layout"
  Status: IMPL (shows W1, filename, line, column, INS/OVR)
```

### Syntax Highlighting

```
REQ-EDT-030: Syntax highlighting -- tokenized storage, detokenized display
  Acceptance: Original AMOS stores source tokenized and produces colored display
              by detokenizing; AMOS Reborn stores source as plain text and applies
              keyword matching for color; keywords rendered in keyword color,
              user text in default text color
  Level: 2 (subsystem)
  Source: Blueprint S6 "Syntax Highlighting"
  Status: IMPL (uses keyword-match approach rather than tokenized storage;
          functionally equivalent for display purposes)
```

```
REQ-EDT-031: Syntax highlighting -- AMOS 1.3 colors
  Acceptance: Keywords rendered in cyan ($0EC), user text in orange ($C40),
              comments (Rem) rendered entirely in cyan, strings in orange,
              all on dark blue ($017) background
  Level: 2 (subsystem)
  Source: Blueprint S6 "AMOS 1.3 Editor -- 4-color Palette"
  Status: IMPL
```

```
REQ-EDT-032: Syntax highlighting -- AMOS Pro colors
  Acceptance: Keywords rendered in bright cyan ($0DD), normal text in light gray ($EEE),
              comments in medium cyan ($0AA), strings in yellow ($FF3),
              all on teal ($077) background
  Level: 2 (subsystem)
  Source: Blueprint S6 "AMOS Pro Editor -- 8-color Palette"
  Status: IMPL
```

### Key Bindings

```
REQ-EDT-040: Key binding -- Cursor keys move cursor
  Acceptance: Up/Down move cursor_y by 1; Left/Right move cursor_x by 1;
              wrapping at line boundaries
  Level: 2 (subsystem)
  Source: Blueprint S6 "Key Bindings (AMOS Pro)"
  Status: IMPL
```

```
REQ-EDT-041: Key binding -- Shift+Up/Down = Page up/down
  Acceptance: Shift+Up moves cursor_y by -visible_lines;
              Shift+Down moves cursor_y by +visible_lines
  Level: 2 (subsystem)
  Source: Blueprint S6 "Key Bindings"
  Status: IMPL
```

```
REQ-EDT-042: Key binding -- Shift+Left/Right = Word left/right
  Acceptance: Shift+Left moves cursor to start of previous word (skip spaces, then non-spaces);
              Shift+Right moves cursor to start of next word
  Level: 2 (subsystem)
  Source: Blueprint S6 "Key Bindings"
  Status: IMPL
```

```
REQ-EDT-043: Key binding -- Ctrl+Left/Right = Start/End of line
  Acceptance: Ctrl+Left sets cursor_x = 0;
              Ctrl+Right sets cursor_x = strlen(current_line)
  Level: 2 (subsystem)
  Source: Blueprint S6 "Key Bindings"
  Status: IMPL
```

```
REQ-EDT-044: Key binding -- Ctrl+Shift+Up/Down = Top/Bottom of text
  Acceptance: Ctrl+Home goes to line 0, column 0;
              Ctrl+End goes to last line, end of line
  Level: 2 (subsystem)
  Source: Blueprint S6 "Key Bindings"
  Status: PARTIAL (Ctrl+Home/End implemented but not Ctrl+Shift+Up/Down specifically)
```

```
REQ-EDT-045: Key binding -- Return = New line / execute
  Acceptance: In edit mode, Return splits current line at cursor;
              in direct mode, Return executes the direct mode command
  Level: 2 (subsystem)
  Source: Blueprint S6 "Key Bindings"
  Status: IMPL
```

```
REQ-EDT-046: Key binding -- ESC = Direct mode toggle
  Acceptance: Pressing ESC enters direct mode (g_editor.direct_mode = true);
              pressing ESC again exits direct mode
  Level: 2 (subsystem)
  Source: Blueprint S6 "Key Bindings"
  Status: IMPL
```

```
REQ-EDT-047: Key binding -- Ctrl+B = Toggle block mark
  Acceptance: Ctrl+B marks/unmarks the start of a block selection for cut/copy/paste
  Level: 2 (subsystem)
  Source: Blueprint S6 "Key Bindings"
  Status: TODO (no block mark implementation in editor.c)
```

```
REQ-EDT-048: Key binding -- Ctrl+C = Cut block
  Acceptance: Ctrl+C cuts the marked block (removes from source, copies to clipboard)
  Level: 2 (subsystem)
  Source: Blueprint S6 "Key Bindings"
  Status: TODO (no block cut implementation)
```

```
REQ-EDT-049: Key binding -- Ctrl+P = Paste block
  Acceptance: Ctrl+P pastes the previously cut/copied block at cursor position
  Level: 2 (subsystem)
  Source: Blueprint S6 "Key Bindings"
  Status: TODO (no block paste implementation)
```

```
REQ-EDT-050: Key binding -- Ctrl+U / Shift+U = Undo/Redo
  Acceptance: Ctrl+U undoes the last editing operation;
              Shift+U redoes the last undone operation
  Level: 2 (subsystem)
  Source: Blueprint S6 "Key Bindings"
  Status: TODO (no undo/redo system)
```

```
REQ-EDT-051: Key binding -- Amiga+L/S = Load/Save
  Acceptance: Meta+L opens load dialog; Meta+S saves current program
  Level: 2 (subsystem)
  Source: Blueprint S6 "Key Bindings"
  Status: PARTIAL (F5 saves; no load key binding; no Amiga/Meta key mapping)
```

```
REQ-EDT-052: Key binding -- Amiga+F/N = Search/Next
  Acceptance: Meta+F opens search input; Meta+N finds next occurrence
  Level: 2 (subsystem)
  Source: Blueprint S6 "Key Bindings"
  Status: TODO (no search functionality)
```

```
REQ-EDT-053: Key binding -- Amiga+G = Goto line
  Acceptance: Meta+G prompts for line number and jumps cursor to that line
  Level: 2 (subsystem)
  Source: Blueprint S6 "Key Bindings"
  Status: TODO (no goto-line functionality)
```

```
REQ-EDT-054: Key binding -- F1 = Run
  Acceptance: F1 builds source from all editor lines, loads it into interpreter,
              deactivates editor, and runs the program
  Level: 2 (subsystem)
  Source: Blueprint S6 "Key Bindings"
  Status: IMPL
```

```
REQ-EDT-055: Key binding -- F2 = Test (syntax check)
  Acceptance: F2 performs syntax check of current source without executing
  Level: 2 (subsystem)
  Source: Blueprint S6 "Key Bindings"
  Status: TODO (F2 key not handled in editor_handle_key)
```

```
REQ-EDT-056: Key binding -- F8 = Insert/Overwrite toggle
  Acceptance: F8 toggles insert_mode between true and false;
              status message shows "Insert mode" or "Overwrite mode"
  Level: 2 (subsystem)
  Source: Blueprint S6 "Key Bindings"
  Status: IMPL
```

```
REQ-EDT-057: Key binding -- F9 = Open/Close procedure fold
  Acceptance: F9 toggles the fold state of the procedure at cursor position
  Level: 2 (subsystem)
  Source: Blueprint S6 "Key Bindings"
  Status: TODO (F9 key not handled; no folding system)
```

### Procedure Folding

```
REQ-EDT-060: Procedure folding -- bit 7 of byte 10
  Acceptance: In tokenized procedure header, bit 7 of byte 10 indicates fold state;
              when set (closed), detokenizer skips all lines between Procedure and End Proc
              using the body length at offset 4
  Level: 2 (subsystem)
  Source: Blueprint S6 "Procedure Folding"
  Status: TODO (no tokenized storage or folding mechanism in editor)
```

### Direct Mode

```
REQ-EDT-065: Direct mode -- ESC triggers entry
  Acceptance: Pressing ESC sets direct_mode = true; focus moves to direct mode bar;
              further character input goes into direct_line
  Level: 2 (subsystem)
  Source: Blueprint S6 "Direct Mode"
  Status: IMPL
```

```
REQ-EDT-066: Direct mode -- 160 chars wide
  Acceptance: EDITOR_DIRECT_LEN >= 160; direct_line buffer accepts up to 160 characters
  Level: 1 (unit)
  Source: Blueprint S6 "Direct Mode" -- "160 chars wide"
  Status: IMPL (EDITOR_DIRECT_LEN = 256, exceeds requirement)
```

```
REQ-EDT-067: Direct mode -- commands executed via interpreter
  Acceptance: Pressing Return in direct mode calls editor_execute_direct() which
              loads direct_line into interpreter via amos_load_text(), sets
              state->direct_mode = true, and runs until completion
  Level: 2 (subsystem)
  Source: Blueprint S6 "Direct Mode" -- "Ed_RunDirect -> L_Prg_RunIt"
  Status: IMPL
```

### Editor Initialization

```
REQ-EDT-070: AMOS 1.3 editor init
  Acceptance: amos_editor_init() sets dialect = AMOS_DIALECT_13, opens screen,
              sets 4-color palette, initializes single empty line buffer
  Level: 2 (subsystem)
  Source: Blueprint S6
  Status: IMPL
```

```
REQ-EDT-071: AMOS Pro editor init
  Acceptance: amos_editor_init_pro() sets dialect = AMOS_DIALECT_PRO, opens screen,
              sets 8-color palette, initializes single empty line buffer
  Level: 2 (subsystem)
  Source: Blueprint S6
  Status: IMPL
```

```
REQ-EDT-072: Editor dialect toggle
  Acceptance: Ctrl+D toggles between AMOS_DIALECT_13 and AMOS_DIALECT_PRO;
              palette and rendering path switch accordingly
  Level: 2 (subsystem)
  Source: Implementation (bonus feature)
  Status: IMPL
```

### File I/O

```
REQ-EDT-075: Load .AMOS binary files
  Acceptance: Editor detects "AMOS Basic" or "AMOS Pro" header in first 16 bytes;
              if detected, reads tokenized source at offset 0x14, calls amos_detokenize(),
              loads resulting text into editor lines; sets dialect from header
  Level: 2 (subsystem)
  Source: Blueprint S7 ".AMOS File Structure"
  Status: IMPL
```

```
REQ-EDT-076: Load plain text files
  Acceptance: If file does not have .AMOS binary header, loads as plain text;
              splits on newlines, strips trailing CR/LF
  Level: 2 (subsystem)
  Source: Implementation
  Status: IMPL
```

```
REQ-EDT-077: Save as plain text
  Acceptance: F5 saves all editor lines joined with newlines to disk;
              clears modified flag; shows "Saved: filename" status
  Level: 2 (subsystem)
  Source: Implementation
  Status: IMPL
```

### Cursor and Display

```
REQ-EDT-080: Cursor blink at ~2Hz (500ms period)
  Acceptance: Cursor toggles visibility every 25 frames at 50Hz;
              blink_counter resets on any keypress
  Level: 1 (unit)
  Source: Implementation (authentic AMOS behavior)
  Status: IMPL
```

```
REQ-EDT-081: Block cursor style
  Acceptance: Cursor is a filled block (8x8 pixels) with the character under
              cursor drawn inverted (foreground/background swapped)
  Level: 2 (subsystem)
  Source: Implementation (authentic AMOS behavior)
  Status: IMPL
```

```
REQ-EDT-082: Insert vs Overwrite mode
  Acceptance: In insert mode, typing shifts characters right;
              in overwrite mode, typing replaces character at cursor;
              default is insert mode
  Level: 2 (subsystem)
  Source: Blueprint S6 "Key Bindings" -- F8
  Status: IMPL
```

---

## Summary

| Category | Total | IMPL | PARTIAL | TODO |
|----------|-------|------|---------|------|
| AMAL Bytecodes (REQ-AML-001..036) | 36 | 30 | 3 | 3 |
| AMAL Channel Data (REQ-AML-040..047) | 8 | 7 | 0 | 1 |
| AMAL Registers (REQ-AML-050..052) | 3 | 3 | 0 | 0 |
| AMAL Jump Limit (REQ-AML-055..056) | 2 | 2 | 0 | 0 |
| AMAL Synchro (REQ-AML-060..061) | 2 | 1 | 1 | 0 |
| AMAL Move (REQ-AML-065) | 1 | 1 | 0 | 0 |
| AMAL Expressions (REQ-AML-070..077) | 5 | 5 | 0 | 0 |
| Editor Palette 1.3 (REQ-EDT-001..004) | 4 | 4 | 0 | 0 |
| Editor Palette Pro (REQ-EDT-010..017) | 8 | 8 | 0 | 0 |
| Editor Layout (REQ-EDT-020..024) | 5 | 4 | 1 | 0 |
| Editor Syntax (REQ-EDT-030..032) | 3 | 3 | 0 | 0 |
| Editor Keys (REQ-EDT-040..057) | 18 | 8 | 3 | 7 |
| Editor Folding (REQ-EDT-060) | 1 | 0 | 0 | 1 |
| Editor Direct Mode (REQ-EDT-065..067) | 3 | 3 | 0 | 0 |
| Editor Init (REQ-EDT-070..072) | 3 | 3 | 0 | 0 |
| Editor File I/O (REQ-EDT-075..077) | 3 | 3 | 0 | 0 |
| Editor Display (REQ-EDT-080..082) | 3 | 3 | 0 | 0 |
| **TOTAL** | **108** | **88** | **8** | **12** |

### Key Gaps (TODO items)

1. **REQ-AML-019** -- =Col(n) collision detection in AMAL expressions
2. **REQ-AML-021** -- Explicit Exit opcode for autotest blocks
3. **REQ-AML-040** -- Linked list channel structure (using flat array instead)
4. **REQ-EDT-047/048/049** -- Block mark, cut, paste (Ctrl+B/C/P)
5. **REQ-EDT-050** -- Undo/Redo system
6. **REQ-EDT-052** -- Search/Find Next
7. **REQ-EDT-053** -- Goto Line
8. **REQ-EDT-055** -- F2 syntax check
9. **REQ-EDT-057/060** -- Procedure folding (F9 + tokenized fold bit)
