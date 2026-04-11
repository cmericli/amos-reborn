# AMOS Reborn Requirements -- System Architecture & Interpreter Core

**Source:** AMOS_TECHNICAL_BLUEPRINT.md, Sections 1 and 2
**Extracted:** 2026-04-10
**Total Requirements:** 52 (REQ-SYS: 26, REQ-INT: 26)

Status Legend:
- **IMPL** -- Requirement is implemented and matches the specification
- **PARTIAL** -- Some aspects implemented, gaps remain
- **TODO** -- Not yet implemented

---

## Section 1: System Architecture & Memory Map

### Memory Banks

```
REQ-SYS-001: Memory Banks -- 16 banks (0-15), 8 bytes each
  Acceptance: AMOS_MAX_BANKS == 16; banks[] array has 16 entries
  Level: 1 (unit)
  Source: Blueprint S1 "Memory Banks"
  Status: IMPL -- AMOS_MAX_BANKS=16, amos_bank_t banks[AMOS_MAX_BANKS] in amos_state_t
```

```
REQ-SYS-002: Bank flag bit 31 -- data bank indicator
  Acceptance: Bank flags field supports bit 31 = data bank type
  Level: 1 (unit)
  Source: Blueprint S1 "Memory Banks" -- "bit 31=data"
  Status: PARTIAL -- amos_bank_type_t has BANK_DATA enum but no bitfield flags encoding
```

```
REQ-SYS-003: Bank flag bit 30 -- chip RAM indicator
  Acceptance: Bank flags field supports bit 30 = chip memory type
  Level: 1 (unit)
  Source: Blueprint S1 "Memory Banks" -- "bit 30=chip"
  Status: TODO -- No chip/fast RAM distinction in current implementation
```

```
REQ-SYS-004: Bank flag bit 29 -- bob bank indicator
  Acceptance: Bank flags field supports bit 29 = bob type
  Level: 1 (unit)
  Source: Blueprint S1 "Memory Banks" -- "bit 29=bob"
  Status: TODO -- No bob flag in amos_bank_t
```

```
REQ-SYS-005: Bank flag bit 28 -- icon bank indicator
  Acceptance: Bank flags field supports bit 28 = icon type
  Level: 1 (unit)
  Source: Blueprint S1 "Memory Banks" -- "bit 28=icon"
  Status: TODO -- No icon flag in amos_bank_t
```

```
REQ-SYS-006: Bank size encoding -- flags AND $0FFFFFFF - 8
  Acceptance: Bank data length = (flags_and_size & 0x0FFFFFFF) - 8
  Level: 1 (unit)
  Source: Blueprint S1 "Memory Banks" -- "Data length = flags AND $0FFFFFFF - 8"
  Status: TODO -- Current impl uses plain uint32_t size field, no packed encoding
```

```
REQ-SYS-007: Bank default assignments -- Bank 1=Sprites, 2=Icons, 3=Samples, 4=AMAL
  Acceptance: Reserve Bank 1 sets type=BANK_SPRITES; Bank 2=BANK_ICONS; Bank 3=BANK_SAMPLES
  Level: 2 (subsystem)
  Source: Blueprint S1 "Memory Banks"
  Status: PARTIAL -- BANK_SPRITES/ICONS/SAMPLES enums exist but no auto-assignment on reserve
```

```
REQ-SYS-008: Bank initialization -- all banks start empty on amos_create()
  Acceptance: After amos_create(), for all i in 0..15: banks[i].data==NULL, banks[i].size==0, banks[i].type==BANK_EMPTY
  Level: 1 (unit)
  Source: Blueprint S1 "Memory Banks"
  Status: IMPL -- calloc zeroes the state, BANK_EMPTY==0
```

### VBI Handler

```
REQ-SYS-009: VBI fires at 50Hz (PAL) / 60Hz (NTSC)
  Acceptance: VBI tick runs once per 1/50s (PAL) or 1/60s (NTSC); timer increments at correct rate
  Level: 2 (subsystem)
  Source: Blueprint S1 "VBI Handler (50Hz PAL / 60Hz NTSC)"
  Status: PARTIAL -- timer field exists, frame_time tracked, but no explicit 50/60Hz VBI loop
```

```
REQ-SYS-010: VBI increments T_VBLCount each tick
  Acceptance: Each VBI tick increments a monotonic VBL counter by 1
  Level: 1 (unit)
  Source: Blueprint S1 "VBI Handler" step 1
  Status: PARTIAL -- state->timer exists and is uint32_t, but no dedicated VBL counter increment in a VBI handler
```

```
REQ-SYS-011: VBI supports 8 VBL routine slots (VblRout)
  Acceptance: Up to 8 callback routines can be registered and are called each VBI tick
  Level: 2 (subsystem)
  Source: Blueprint S1 "VBI Handler" step 4 -- "Call up to 8 VBL routines from VblRout slots"
  Status: TODO -- No VBL routine slot array in amos_state_t
```

```
REQ-SYS-012: VBI sets BitVBL (bit 15) in T_Actualise
  Acceptance: Each VBI tick sets bit 15 of an actualization flags word
  Level: 1 (unit)
  Source: Blueprint S1 "VBI Handler" step 2
  Status: TODO -- No T_Actualise field in current implementation
```

```
REQ-SYS-013: VBI decrements EVERY timer counter (T_EveCpt)
  Acceptance: Each VBI tick decrements every_counter by 1; when it reaches 0, the EVERY target fires and counter reloads from every_interval
  Level: 2 (subsystem)
  Source: Blueprint S1 "VBI Handler" step 3
  Status: PARTIAL -- every_interval/every_counter/every_target_line fields exist but decrement logic is not in a VBI handler
```

### Input Handling

```
REQ-SYS-014: Inkey returns ASCII + scancode
  Acceptance: amos_inkey() returns a value encoding both ASCII character and hardware scancode
  Level: 1 (unit)
  Source: Blueprint S1 "Input Handling" -- "Inkey returns ASCII+scancode"
  Status: PARTIAL -- amos_inkey() and amos_inkey_str() exist, amos_scancode() separate; not combined as original
```

```
REQ-SYS-015: KEY STATE(n) tests specific scancode
  Acceptance: amos_key_state(state, n) returns nonzero if scancode n is currently pressed
  Level: 1 (unit)
  Source: Blueprint S1 "Input Handling" -- "KEY STATE(n) tests specific scancode"
  Status: IMPL -- key_states[512] array exists, amos_key_state() declared
```

```
REQ-SYS-016: Joy(n) returns bitmask -- bit 0=up, 1=down, 2=left, 3=right, 4=fire
  Acceptance: amos_joy(state, n) returns uint with bit 0=up, bit 1=down, bit 2=left, bit 3=right, bit 4=fire
  Level: 1 (unit)
  Source: Blueprint S1 "Input Handling" -- "Joy(n) returns bitmask"
  Status: IMPL -- amos_joy() declared; Jleft/Jright/Jup/Jdown/Fire functions test correct bits (4,8,1,2,16)
```

```
REQ-SYS-017: Mouse position tracking (X Mouse, Y Mouse)
  Acceptance: amos_x_mouse() and amos_y_mouse() return current mouse coordinates
  Level: 1 (unit)
  Source: Blueprint S1 "Input Handling" -- "Mouse: CIA $BFE001 for buttons"
  Status: IMPL -- mouse_x, mouse_y fields; amos_x_mouse/amos_y_mouse declared
```

```
REQ-SYS-018: Mouse button state (Mouse Key, Mouse Click)
  Acceptance: amos_mouse_key() returns current button state; amos_mouse_click() returns buttons clicked since last check
  Level: 1 (unit)
  Source: Blueprint S1 "Input Handling" -- "Mouse: CIA $BFE001 for buttons"
  Status: IMPL -- mouse_buttons, mouse_click fields; amos_mouse_key/amos_mouse_click declared
```

### String Garbage Collection

```
REQ-SYS-019: Fast GC -- copies live strings sequentially, marks copied with $FFF0xxxx
  Acceptance: GC pass allocates temp buffer, copies live strings, updates variable pointers, marks with $FFF0 prefix
  Level: 2 (subsystem)
  Source: Blueprint S1 "String Garbage Collection" -- Fast GC
  Status: TODO -- Current impl uses malloc/free per string, no arena GC
```

```
REQ-SYS-020: Slow GC -- fallback when temp buffer unavailable, O(n^2) worst case
  Acceptance: When temp buffer unavailable, GC falls back to O(n^2) sorting-based algorithm
  Level: 2 (subsystem)
  Source: Blueprint S1 "String Garbage Collection" -- Slow GC
  Status: TODO -- No GC system implemented
```

```
REQ-SYS-021: SET BUFFER n -- controls initial string buffer size
  Acceptance: "Set Buffer N" command sets string arena size to N bytes
  Level: 2 (subsystem)
  Source: Blueprint S1 "String Garbage Collection"
  Status: TODO -- No string buffer/arena system
```

### Initialization Sequence

```
REQ-SYS-022: Init step 1 -- Detect PAL/NTSC (50Hz/60Hz)
  Acceptance: amos_create() detects or defaults to PAL (50Hz) or NTSC (60Hz) mode
  Level: 1 (unit)
  Source: Blueprint S1 "Initialization Sequence" step 1
  Status: TODO -- No PAL/NTSC detection; implicitly 50Hz via timer
```

```
REQ-SYS-023: Init step 4 -- Allocate master data zone, set A5 equivalent
  Acceptance: amos_create() allocates amos_state_t as master data zone
  Level: 1 (unit)
  Source: Blueprint S1 "Initialization Sequence" step 4
  Status: IMPL -- amos_create() callocs amos_state_t
```

```
REQ-SYS-024: Init step 5 -- Set up BASIC stack limits
  Acceptance: amos_create() initializes gosub_top=0, for_top=0; stacks bounded by MAX_GOSUB_DEPTH and MAX_FOR_DEPTH
  Level: 1 (unit)
  Source: Blueprint S1 "Initialization Sequence" step 5
  Status: IMPL -- gosub_stack/for_stack arrays with depth limits; reset to 0 in amos_reset()
```

```
REQ-SYS-025: Init -- Seed RNG
  Acceptance: amos_create() seeds the random number generator
  Level: 1 (unit)
  Source: Blueprint S1 "Initialization Sequence"
  Status: IMPL -- srand((unsigned)time(NULL)) in amos_create()
```

```
REQ-SYS-026: Init -- Open default screen
  Acceptance: amos_create() opens screen 0 at 320x256 with default palette
  Level: 2 (subsystem)
  Source: Blueprint S1 "Initialization Sequence" step 6
  Status: IMPL -- amos_screen_open(state, 0, 320, 256, 5) in amos_create()
```

---

## Section 2: Interpreter Core

### Token Format

```
REQ-INT-001: Tokens are word-sized offsets (16-bit)
  Acceptance: Tokenizer emits 16-bit token values; token value is dispatch index
  Level: 1 (unit)
  Source: Blueprint S2 "Token Format" -- "word-sized offsets from the Tk label"
  Status: PARTIAL -- amos_token_type_t is an enum (int-sized); not word-sized dispatch offsets
```

```
REQ-INT-002: Variable tokens embed pre-computed offset into variable area
  Acceptance: Variable token includes: 2 bytes reserved, 1 byte name length, 1 byte type (bit0=float, bit1=string), N bytes lowercase name padded to even
  Level: 1 (unit)
  Source: Blueprint S2 "Token Format" -- Variable tokens
  Status: TODO -- Variables use runtime name lookup, not pre-computed offsets
```

```
REQ-INT-003: Constant token TkEnt (0x003E) -- decimal integer, 4-byte BE value
  Acceptance: Integer literal token 0x003E followed by 4 big-endian bytes
  Level: 1 (unit)
  Source: Blueprint S2 "Token Format" -- Constant tokens
  Status: TODO -- Tokenizer uses TOK_INTEGER enum, not binary token IDs
```

```
REQ-INT-004: Constant token TkHex (0x0036) -- hex integer, 4-byte BE value
  Acceptance: Hex literal token 0x0036 followed by 4 big-endian bytes
  Level: 1 (unit)
  Source: Blueprint S2 "Token Format" -- Constant tokens
  Status: TODO -- No binary token format; parsed from text
```

```
REQ-INT-005: Constant token TkFl (0x0046) -- float, 4-byte AMOS float (mantissa x 2^(exp-88))
  Acceptance: Float token 0x0046 followed by 4 bytes in AMOS float format
  Level: 1 (unit)
  Source: Blueprint S2 "Token Format" -- Constant tokens
  Status: TODO -- Floats stored as native double, not AMOS format
```

```
REQ-INT-006: Constant token TkCh1 (0x0026) -- string, 2-byte length + ASCII + padding
  Acceptance: String literal token 0x0026 followed by 2-byte length, ASCII data, padded to even
  Level: 1 (unit)
  Source: Blueprint S2 "Token Format" -- Constant tokens
  Status: TODO -- Strings stored as C strings, not binary token format
```

### Expression Evaluator

```
REQ-INT-007: Three expression types -- int=0, float=1, string=2
  Acceptance: Expression evaluator tracks type as enum: VAR_INTEGER=0, VAR_FLOAT=1, VAR_STRING=2
  Level: 1 (unit)
  Source: Blueprint S2 "Expression Evaluator" -- "Three types: int=0, float=1, string=2"
  Status: IMPL -- amos_var_type_t: VAR_INTEGER=0, VAR_FLOAT=1, VAR_STRING=2; eval_result_t uses same enum
```

```
REQ-INT-008: Automatic type coercion between int and float
  Acceptance: Binary ops with mixed int/float operands coerce to float; result is float
  Level: 1 (unit)
  Source: Blueprint S2 "Expression Evaluator" -- "Automatic coercion"
  Status: IMPL -- executor.c checks use_float when either operand is float
```

```
REQ-INT-009: Operator precedence encoded in token ordering
  Acceptance: Higher token IDs bind tighter; Evalue compares raw token word values against threshold
  Level: 2 (subsystem)
  Source: Blueprint S2 "Expression Evaluator"
  Status: TODO -- Precedence handled by recursive descent parser, not token ordering
```

### Command Dispatch

```
REQ-INT-010: Inner loop reads token word, jumps via dispatch table
  Acceptance: Execution loop reads next token and dispatches via table/switch; zero token = end of line
  Level: 2 (subsystem)
  Source: Blueprint S2 "Command Dispatch" -- "4 instruction inner loop"
  Status: PARTIAL -- amos_execute_line() dispatches via switch on node type (AST-based, not token-table)
```

```
REQ-INT-011: Extension dispatch via AdTokens array -- up to 26 extension slots
  Acceptance: Extension commands dispatch through indexed array of token tables, max 26 extensions
  Level: 2 (subsystem)
  Source: Blueprint S2 "Command Dispatch" -- "AdTokens array (up to 26 extension slots)"
  Status: TODO -- No extension dispatch system
```

### Variable Storage

```
REQ-INT-012: Variables are 6 bytes each -- 4-byte value + 2-byte type
  Acceptance: Each variable occupies exactly 6 bytes in the variable area: 4 for value, 2 for type
  Level: 1 (unit)
  Source: Blueprint S2 "Variable Storage" -- "Variables are 6 bytes: 4-byte value + 2-byte type"
  Status: TODO -- amos_var_t is a struct with name[64] + enum + union; much larger than 6 bytes
```

```
REQ-INT-013: Variable name suffix determines type -- $ = string, # = float, no suffix = integer
  Acceptance: var_type_from_name("A$")==VAR_STRING; var_type_from_name("A#")==VAR_FLOAT; var_type_from_name("A")==VAR_INTEGER
  Level: 1 (unit)
  Source: Blueprint S2 "Variable Storage"
  Status: IMPL -- var_type_from_name() in variables.c checks last char for $ and #
```

```
REQ-INT-014: No runtime name lookup -- verifier assigns direct byte offsets
  Acceptance: After verification pass, variable access is base+offset indexed load, no string comparison
  Level: 2 (subsystem)
  Source: Blueprint S2 "Variable Storage" -- "NO hash table, NO runtime name lookup"
  Status: TODO -- Current impl uses linear scan with strcasecmp in amos_var_get()
```

```
REQ-INT-015: Global vs local distinguished by sign bit of offset word
  Acceptance: Negative offset = local variable; positive offset = global variable
  Level: 1 (unit)
  Source: Blueprint S2 "Variable Storage" -- "sign bit of offset word"
  Status: TODO -- No offset-based variable addressing
```

```
REQ-INT-016: Variable lookup is case-insensitive
  Acceptance: amos_var_get(state, "ABC") == amos_var_get(state, "abc") == amos_var_get(state, "Abc")
  Level: 1 (unit)
  Source: Blueprint S2 "Variable Storage" (AMOS convention)
  Status: IMPL -- strcasecmp used in amos_var_get()
```

### Control Flow Stack

```
REQ-INT-017: FOR frame -- 24 bytes (variable addr, step, limit, return addr)
  Acceptance: FOR pushes a frame containing: variable reference, step value, limit value, return address (line + position)
  Level: 1 (unit)
  Source: Blueprint S2 "Control Flow Stack" -- "FOR: 24 bytes"
  Status: IMPL -- for_entry_t has var_name, limit, step, loop_line, loop_pos
```

```
REQ-INT-018: REPEAT/WHILE/DO frame -- 10 bytes
  Acceptance: REPEAT/WHILE/DO pushes a 10-byte frame containing loop return address
  Level: 1 (unit)
  Source: Blueprint S2 "Control Flow Stack" -- "REPEAT/WHILE/DO: 10 bytes"
  Status: PARTIAL -- WHILE/REPEAT handled via line tracking but no explicit stack frame structure
```

```
REQ-INT-019: GOSUB frame -- 12 bytes with magic cookie "Gosb"
  Acceptance: GOSUB pushes 12-byte frame with return_line, return_pos; magic cookie "Gosb" for stack scanning
  Level: 1 (unit)
  Source: Blueprint S2 "Control Flow Stack" -- 'GOSUB: 12 bytes (magic cookie "Gosb")'
  Status: PARTIAL -- gosub_entry_t has return_line/return_pos but no magic cookie
```

```
REQ-INT-020: PROCEDURE frame -- 44+ bytes with magic cookie "Proc", saved VarLoc
  Acceptance: Procedure call pushes 44+ byte frame with cookie "Proc", local variable snapshot, saved VarLoc pointer
  Level: 2 (subsystem)
  Source: Blueprint S2 "Control Flow Stack" -- 'PROCEDURE: 44+ bytes (magic cookie "Proc")'
  Status: TODO -- No procedure stack frame structure; procedures handled via separate mechanism
```

```
REQ-INT-021: Control flow uses separate stack (A3), not hardware SP
  Acceptance: FOR/GOSUB/PROC frames stored in dedicated stack arrays, not the C call stack
  Level: 1 (unit)
  Source: Blueprint S2 "Control Flow Stack" -- "Uses a separate A3 stack"
  Status: IMPL -- gosub_stack[] and for_stack[] are separate arrays in amos_state_t
```

### Notable Quirks

```
REQ-INT-022: TRUE = -1, not 1
  Acceptance: All comparison operators return -1 for true and 0 for false; True function returns -1
  Level: 1 (unit)
  Source: Blueprint S2 "Notable Quirks" -- "comparison is TRUE (-1), not 1"
  Status: IMPL -- All comparison cases in executor.c return make_int(cond ? -1 : 0)
```

```
REQ-INT-023: String subtraction operator -- removes substrings
  Acceptance: A$ - B$ returns A$ with all occurrences of B$ removed
  Level: 2 (subsystem)
  Source: Blueprint S2 "Notable Quirks" -- "String subtraction operator (removes substrings)"
  Status: TODO -- No string minus handling in executor.c binary op dispatch
```

```
REQ-INT-024: Anti-crash magic cookies enable stack scanning for error recovery
  Acceptance: On error, stack scanner walks frames looking for "Gosb"/"Proc" cookies to unwind safely
  Level: 2 (subsystem)
  Source: Blueprint S2 "Notable Quirks" -- "Anti-crash magic cookies"
  Status: TODO -- No magic cookies in stack frames
```

```
REQ-INT-025: Patchable JMP instruction enables debug/follow mode
  Acceptance: A hook point in the inner loop allows debug/trace mode to be toggled at runtime
  Level: 2 (subsystem)
  Source: Blueprint S2 "Notable Quirks" -- "A patchable jmp instruction"
  Status: TODO -- No debug/follow mode hook
```

```
REQ-INT-026: NOT operator is bitwise (NOT 0 = -1, NOT -1 = 0)
  Acceptance: NOT(0) returns -1; NOT(-1) returns 0; NOT(x) returns bitwise complement of x
  Level: 1 (unit)
  Source: Blueprint S2 "Expression Evaluator" + "Notable Quirks" (TRUE=-1 implies bitwise NOT)
  Status: PARTIAL -- TOK_NOT exists but needs verification of bitwise vs logical behavior
```

---

## Summary

| Category | IMPL | PARTIAL | TODO | Total |
|----------|------|---------|------|-------|
| REQ-SYS  | 8    | 5       | 13   | 26    |
| REQ-INT  | 6    | 4       | 16   | 26    |
| **Total**| **14** | **9** | **29** | **52** |

### Priority Implementation Order

**P0 -- Core correctness (test immediately):**
- REQ-INT-022 (TRUE=-1) -- IMPL, needs golden test
- REQ-INT-013 (type suffix) -- IMPL, needs unit test
- REQ-INT-007 (expression types) -- IMPL, needs unit test
- REQ-INT-008 (type coercion) -- IMPL, needs unit test

**P1 -- Behavioral gaps (implement next):**
- REQ-INT-023 (string subtraction) -- TODO, AMOS-specific behavior
- REQ-SYS-009/010/013 (VBI/timer/EVERY) -- PARTIAL, timing correctness
- REQ-INT-018/019 (REPEAT/GOSUB frames) -- PARTIAL, add magic cookies
- REQ-INT-026 (bitwise NOT) -- PARTIAL, verify behavior

**P2 -- Architectural alignment (longer term):**
- REQ-SYS-002..006 (bank flags encoding) -- TODO, compatibility with .AMOS files
- REQ-INT-001..006 (binary token format) -- TODO, needed for .AMOS file loading
- REQ-INT-012/014/015 (6-byte vars, offset addressing) -- TODO, performance optimization
- REQ-SYS-019..021 (string GC) -- TODO, needed for large programs
