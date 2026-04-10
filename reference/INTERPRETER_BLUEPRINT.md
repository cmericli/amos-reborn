# AMOS Interpreter Core -- Technical Blueprint for Reimplementation

Based on analysis of AMOS 1.3 source (TOKEN.S, TOKTAB.S, EVALUE.S, B.S, BRANCH.S, VERIF.S) and AMOS Professional (+Lib.s, +Equ.s).

---

## 1. Token Format

### 1.1 Tokenized Line Layout

Each tokenized program line has this structure:

```
Offset 0:   byte   Line length / 2 (0 = end of program)
Offset 1:   byte   Indentation (TAB count, max 127)
Offset 2+:  words  Token stream (variable-length)
Last:       word   $0000 (end-of-line sentinel)
```

The line length is stored as a byte at offset 0, representing the total line size divided by 2. A zero-length line marks the end of the program. Lines are always word-aligned.

### 1.2 Token Table Entry Format (TOKTAB.S)

Each token definition in the token table has this structure:

```
dc.w  InstructionAddress - Tk    ; Instruction handler offset
dc.w  FunctionAddress - Tk       ; Function (expression) handler offset
dc.b  "keyword",$80              ; Keyword text, last char OR'd with $80
dc.b  "parameter_spec"           ; Parameter specification string
dc.b  -1 or -2 or -3             ; Terminator (-1 = last variant, -2/-3 = more variants follow)
```

All offsets are relative to the `Tk` label, which serves as the base address for the entire dispatch system. This is the critical insight: `Tk` is both a token table entry AND the base for all jump calculations.

### 1.3 Token Word Encoding

A token word in the program stream is the offset from `Tk` to the token table entry. For example, `TkVar-Tk` is the token for a variable reference. This makes dispatch trivially fast -- the token IS the table offset.

**Key token IDs (all relative to Tk):**
- `TkVar-Tk` -- Variable reference
- `TkLab-Tk` -- Label definition  
- `TkLGo-Tk` -- Label goto reference
- `TkPro-Tk` -- Procedure label
- `TkExt-Tk` -- Extension token (see 1.4)
- `TkEnt-Tk` -- Integer constant
- `TkFl-Tk` -- Float constant
- `TkCh1-Tk` / `TkCh2-Tk` -- String constants (double/single quoted)
- `TkBin-Tk` / `TkHex-Tk` -- Binary/hex constants
- `TkDP-Tk` -- Colon (statement separator)
- `TkRem1-Tk` / `TkRem2-Tk` -- REM / apostrophe comment

### 1.4 Variable Token Encoding

When a variable is tokenized, the stream contains:

```
word    TkVar-Tk                  ; Variable token marker
word    offset                    ; Offset into variable area (positive=local, negative=global)
byte    name_skip_length          ; Signed: bytes to skip over the embedded name
byte    type_flags                ; Bits 0-3: type (0=int, 1=float, 2=string)
                                  ; Bit 6: array flag
string  variable_name             ; The actual name (padded to even)
```

For global variables, the offset word is stored as `-(offset+1)`, detected by the sign bit. The interpreter checks `bpl` for local and reconstructs global offsets via `addq.w #1,d0; neg.w d0`.

### 1.5 Extension Token Encoding

Extension commands (from loaded .lib files) use an indirect format:

```
word    TkExt-Tk                  ; Extension marker
byte    extension_number          ; Which extension (0-25, index into AdTokens array)
byte    parameter_count           ; Number of parameters
word    token_offset              ; Offset within extension's token table
```

### 1.6 Constants in Token Stream

Constants are stored inline after their token word:
- **Integer**: `word TkEnt-Tk` + `long value` (4 bytes)
- **Float**: `word TkFl-Tk` + `long FFP_value` (4 bytes, Motorola Fast Floating Point)
- **String**: `word TkCh1-Tk` + `word length` + `bytes data` (padded to even)
- **Binary/Hex**: `word TkBin/TkHex-Tk` + `long value`

### 1.7 Tokenizer Process (TOKEN.S)

The tokenizer (`Tokenise` routine) processes one editor line at a time:

1. **Count leading spaces** into indentation byte (max 127).
2. **Check for line-number prefix** -- sets `TkVar` with label flag bit 4.
3. **Main loop** processes characters with state bits in D5:
   - Bit 0: Inside string literal
   - Bit 1: Inside variable name
   - Bit 3: Past beginning-of-line (no more labels)
   - Bit 4: Line number in progress
   - Bit 5: Inside REM comment
4. **Keyword matching** uses a two-tier lookup: a fast path via precomputed per-first-letter index tables (built at init in `InitTok`), and a slow linear scan fallback. The fast tables map each starting letter to candidate token entries, avoiding full scans of the ~300 keyword table.
5. **Variable name termination**: `$` suffix sets type=2 (string), `#` suffix sets type=1 (float), no suffix sets type=0 (integer).
6. **Line finalization**: appends `$0000` sentinel, stores `length/2` in byte 0. Maximum tokenized line length: 510 bytes.

---

## 2. Expression Evaluator (EVALUE.S)

### 2.1 Architecture

The expression evaluator is a recursive-descent operator-precedence parser. The core is two mutually recursive routines:

- **`Evalue`** -- Parse an expression with precedence threshold
- **`Operande`** -- Parse a single operand (constant, variable, function call, parenthesized subexpr)

### 2.2 Precedence via Token Ordering

Operator precedence is encoded directly in the token word values. The token table is ordered so that lower-precedence operators have lower token IDs. The evaluator reads the next token word as a raw comparison value:

```asm
Evalue: move.w  #$7FFF,d0       ; Initial precedence = max (accept everything)
Eva1:   move.w  d0,-(sp)        ; Push current precedence threshold
        bsr     Operande        ; Get left operand -> D3/D2
EvaRet: move.w  (a6)+,d0       ; Read next token
        cmp.w   (sp),d0         ; Compare with threshold
        bhi.s   Eva0            ; Higher = tighter binding, recurse
        subq.l  #2,a6           ; Put it back
        ...return...
Eva0:   move.l  d3,-(sp)        ; Save left operand
        move.w  d2,-(sp)        ; Save left type
        ...recurse for right operand, then dispatch operator...
```

This means the operator token ordering in TOKTAB.S IS the precedence table (from low to high):
1. `xor`, `or`, `and` (logical, lowest)
2. `<>`, `<=`, `>=`, `=`, `<`, `>` (comparison)
3. `+`, `-` (additive)
4. `mod` (modulo)
5. `*`, `/` (multiplicative)
6. `^` (power, highest)

The "O" codes in the parameter spec (e.g., `"O00"`, `"O20"`, `"O22"`) encode the operator type for the verifier, not for runtime.

### 2.3 Type System

Three types, encoded in register D2:
- `0` = Integer (32-bit signed, in D3)
- `1` = Float (Motorola FFP format, in D3)  
- `2` = String (pointer to length-prefixed string, in D3)

**Type coercion rules:**
- Arithmetic operators call `Compat` which promotes integers to float if either operand is float.
- `IntToFl` / `FlToInt` use the Amiga mathffp.library (`SPFlt`/`SPFix`).
- String `+` concatenates; string `-` performs substring removal (exclusive subtraction).
- Mixed numeric/string operations raise type mismatch errors.

### 2.4 Register Conventions

| Register | Role |
|----------|------|
| A5 | Data zone base pointer (all interpreter state accessed as offsets from A5) |
| A6 | Program counter -- points into tokenized program stream |
| A4 | Saved position (beginning of current line, used for error reporting) |
| A3 | Loop/control flow stack pointer (grows downward) |
| D3 | Current expression value |
| D2 | Current expression type (0/1/2) |
| D5 | Previous operand type (in operator handlers) |
| D6 | Previous operand value (in operator handlers) |

### 2.5 Operand Dispatch

`Operande` reads a token word and uses it as an index into the token table's second word (function address):

```asm
Operande:
    clr.w   -(sp)           ; Sign flag (for unary minus)
Ope0:
    move.w  (a6)+,d0        ; Read token
    lea     Tk(pc),a0       ; Token table base
    move.w  2(a0,d0.w),d0   ; Get function handler offset (2nd word in entry)
    jmp     0(a0,d0.w)      ; Jump to handler
```

Each operand handler must return with the value in D3 and type in D2, then branch to `OpeRet`. `OpeRet` checks the sign flag and negates if unary minus was seen.

### 2.6 Parameter Collection Helpers

The evaluator provides several typed parameter-collection routines:
- `FnEval` -- Skip opening paren, evaluate one expression
- `ExpEntier` -- Evaluate and force to integer
- `FnFloat` -- Evaluate and force to float  
- `ExpAlpha` -- Evaluate and expect string (returns A2=data, D2=length)
- `FnEnt1`/`FnEnt2` -- Evaluate 1/2 integers, push onto A3 stack
- `FnGetP`/`GetPar` -- Evaluate comma-separated parameters, push all onto A3
- `FnParD0` -- Evaluate D0 parameters, push onto A3

---

## 3. Command Dispatch (Main Interpreter Loop)

### 3.1 The ChrGet/InsRet Loop

The interpreter's inner loop at labels `ChrGet` and `InsRet` is extremely tight:

```asm
ChrGet: move.w  (a6)+,d0       ; Read token (skips line-header/end-of-line zeroes)
        beq.s   ChrGet          ; Zero = end-of-line, skip to next line
InsRet: move.w  (a6)+,d0       ; Read instruction token  
        beq.s   ChrGet          ; Zero = end-of-line
        move.l  a6,a4           ; Save position for error reporting
InsMen: lea     Tk(pc),a0       ; Token table base
        move.w  0(a0,d0.w),d0   ; Get instruction handler offset (1st word)
        jmp     0(a0,d0.w)      ; Jump to handler
```

**Key distinction**: `ChrGet` is the entry point after line boundaries (skips zero words), while `InsRet` is the return point after each instruction (also checks for zero). Every instruction handler ends with `bra InsRet` to continue execution.

The `GFolow`/`CFol1`/`CFol2` mechanism at the `jmp` instruction is a hook point for the FOLLOW debugger mode -- it can be patched to redirect through `Follow` for single-stepping.

### 3.2 Extension Dispatch

Extension instructions and functions use separate handlers:

```asm
InExt:  ; Instruction from extension
    move.b  (a6)+,d1        ; Extension number
    move.b  (a6)+,d0        ; Parameter count
    move.w  (a6)+,d2        ; Token offset within extension
    ; ... look up in AdTokens[d1], get routine address, call it ...
    jsr     (a0)
    bra     InsRet

FnExt:  ; Function from extension  
    ; Same lookup but uses 2nd word (function offset) instead of 1st
    jsr     (a0)
    bra     OpeRet
```

Extensions are loaded at startup from .lib files. Up to 26 extensions are supported (one per slot in the `AdTokens` array). Each extension's code is relocated in-place using a custom relocation format with instruction tags (`C_Code1`, `C_Code2`) that handle jump/branch/data fixups.

### 3.3 Interrupt Testing

Between instructions, the `T_Actualise` flag byte (set by the VBL interrupt) triggers periodic checks via `Tester`. The flag is tested at the top of every blocking control flow instruction (FOR, NEXT, WHILE, GOSUB, etc.) with the pattern:

```asm
TForNxt: bsr    Tester       ; Check interrupts
Next:    tst.b  T_Actualise(a5)
         bmi.s  TForNxt      ; Retry if interrupt pending
         ... actual logic ...
```

`Tester` handles: CTRL-C break detection, screen/copper updates, bob animation, sprite updates, extension periodic callbacks, and the EVERY timer.

---

## 4. Control Flow

### 4.1 The A3 Loop Stack

All control flow structures use A3 as a downward-growing stack (separate from the 68000 SP hardware stack). Each frame starts with a word containing the frame size, enabling unwinding:

```
A3 ->  [word: frame_size]     ; Size of this frame in bytes
       [long: exit_address]   ; Address after the loop (token stream position)
       [long: loop_address]   ; Address of loop start  
       ... structure-specific data ...
```

`PLoop(a5)` always points to the current top of the A3 stack. `BasA3(a5)` marks the bottom boundary for the current scope (used by GOSUB/PROCEDURE to isolate frames). `MinLoop(a5)` is the stack overflow guard.

### 4.2 FOR/NEXT (24 bytes)

```
A3 ->  [word: 24]             ; Frame size = TForNxt
       [long: exit_addr]      ; Past the NEXT
       [long: loop_addr]      ; Start of loop body
       [word: type]           ; 0=integer, 1=float
       [long: step]           ; Step value
       [long: limit]          ; End value
       [long: var_addr]       ; Pointer to loop variable
```

NEXT adds step to the variable, compares with limit (respecting step sign), and either jumps back to `loop_addr` or pops the frame. Float loops use mathffp.library for arithmetic and comparison.

### 4.3 REPEAT/UNTIL, WHILE/WEND, DO/LOOP (10 bytes each)

```
A3 ->  [word: 10]             ; Frame size = TRptUnt / TWhlWnd / TDoLoop
       [long: exit_addr]      ; Past the closing keyword
       [long: loop_addr]      ; Start of loop body
```

- UNTIL evaluates expression: false = loop, true = exit
- WEND evaluates the WHILE condition (jumps back to read it): true = loop, false = exit
- LOOP always jumps back (infinite loop, use EXIT to break)
- DO is simply an alias for REPEAT

### 4.4 IF/THEN/ELSE

IF does not use the A3 stack. Instead, the tokenizer embeds relative offsets:

```
Token stream: [IF] [word: offset_to_else] [expression] [THEN] ... [ELSE] [word: offset_past_endif] ...
```

IF evaluates the expression. If false, it adds the embedded offset to A6 to skip to ELSE. ELSE similarly jumps past END IF. Both use `LGoto` for stack cleanup when jumping.

### 4.5 GOSUB/RETURN (12 bytes)

```
A3 ->  [long: "Gosb"]        ; Anti-crash magic cookie
       [long: return_addr]    ; A6 to restore on RETURN
       [long: saved_BasA3]    ; Previous BasA3 value
```

GOSUB pushes a frame with the magic value `"Gosb"` (literally ASCII `$476F7362`), sets `BasA3` to the new frame for scope isolation. RETURN scans for the magic cookie, restores A6 and BasA3.

### 4.6 PROCEDURE/END PROC (44+ bytes)

Procedures create a full scope frame:

```
A3 ->  [long: "Proc"]        ; Magic cookie
       [long: return_addr]    ; A6 to restore (null if called from menu/direct)
       [long: DProc]          ; Saved DATA procedure base
       [long: AData]          ; Saved DATA pointer
       [long: PData]          ; Saved DATA start
       [word: ErrorOn]        ; Saved error handling state
       [long: ErrorChr]       ; Saved error character
       [long: OnErrLine]      ; Saved ON ERROR line
       [long: TabBas]         ; Saved array base
       [long: VarLoc]         ; Saved local variable base
       [long: BasA3]          ; Saved previous BasA3
```

Procedure call:
1. Allocates a new local variable area below `TabBas`
2. Copies parameter values from caller expressions into the new local variables
3. Pushes the full frame above
4. Sets `VarLoc` and `TabBas` to the new local area
5. Resets `OnErrLine`, `PData`, `AData` for the new scope

END PROC reverses everything, restoring all saved state. The `"Proc"` magic cookie enables anti-crash scanning (`EPro2` loop) if the stack becomes corrupted.

### 4.7 LGoto Stack Unwinding

When GOTO jumps to a destination, `LGoto` walks the A3 stack upward, popping frames whose `[exit_addr, loop_addr]` range does not contain the target. This ensures loops are properly exited when jumping out of them.

### 4.8 EXIT / EXIT IF

EXIT reads two words: a relative offset for A6 (to skip past the loop's closing keyword) and a stack-frame offset for A3 (to pop the right number of frames). EXIT IF evaluates a condition first.

---

## 5. Variable Storage

### 5.1 Memory Layout

Variables are stored in a flat area accessed via two base pointers:
- `VarGlo(a5)` -- Base of global variable area
- `VarLoc(a5)` -- Base of local variable area (equals VarGlo when not in a procedure)

Each variable occupies exactly **6 bytes**:

```
Offset 0:  long   Value (integer, FFP float, or string pointer)
Offset 4:  word   Type/flags (bits 0-3: type, bit 6: array flag)
```

### 5.2 Variable Resolution (No Hash Table)

Variable lookup uses **direct offset addressing** -- there is NO hash table and NO runtime name lookup. The verifier pass (`VERIF.S` / `PTest`) runs before execution and:

1. Scans all variable references in the token stream
2. Assigns each unique variable name an offset in the variable area
3. Patches the token stream in-place: the variable's offset word is written directly into the tokenized program

At runtime, `FnVar` simply does:
```asm
move.l  VarLoc(a5),a0      ; Base pointer
move.w  (a6)+,d0            ; Pre-computed offset
add.w   d0,a0               ; Direct address = base + offset
move.l  (a0),d3             ; Read value
```

### 5.3 Global vs Local Discrimination

The sign bit of the offset word distinguishes global from local variables. A negative offset means global: the runtime inverts it (`addq.w #1; neg.w`) and uses `VarGlo(a5)` instead of `VarLoc(a5)`.

### 5.4 Variable Names

Variable names are embedded in the token stream (after the offset and type bytes) but are only used by the editor for detokenization and by the verifier for initial allocation. At runtime, names are skipped over via the `name_skip_length` byte.

### 5.5 Type Suffixes

Following BASIC convention:
- No suffix: integer (type 0)
- `#` suffix: float (type 1)
- `$` suffix: string (type 2)

The type byte value is stored both in the token stream AND in the variable's word at offset 4.

---

## 6. Key Data Structures

### 6.1 The A5 Data Zone

All interpreter state is accessed as fixed offsets from A5. This is a large structure (thousands of bytes) containing:

| Field | Purpose |
|-------|---------|
| `VarGlo(a5)` | Global variable area base |
| `VarLoc(a5)` | Local variable area base |
| `TabBas(a5)` | Bottom of array storage (grows down) |
| `HiChaine(a5)` | Top of string heap (grows up) |
| `PLoop(a5)` | Current top of A3 loop stack |
| `MinLoop(a5)` | Stack overflow guard for A3 |
| `BasA3(a5)` | Base of current scope's stack frames |
| `HoLoop(a5)` | Top of A3 stack area |
| `BaLoop(a5)` | Bottom of A3 stack area |
| `StBas(a5)` | Start of tokenized program |
| `StMini(a5)` | Start of string storage area |
| `ChVide(a5)` | Pointer to the canonical empty string |
| `FloatBase(a5)` | mathffp.library base |
| `DProc(a5)` | Current procedure's DATA base |
| `PData(a5)` | Current DATA read pointer |
| `AData(a5)` | Current DATA item pointer |
| `OnErrLine(a5)` | ON ERROR GOTO target |
| `ErrorOn(a5)` | Error handling active flag |
| `Direct(a5)` | Non-zero when running in direct mode |
| `AdTokens(a5)` | Array of 26 extension token table pointers |
| `T_Actualise(a5)` | VBL interrupt flag (set by interrupt, cleared by Tester) |
| `ActuMask(a5)` | Bitmask of which interrupt checks are enabled |
| `LabBas(a5)` | Start of label table |
| `LabHaut(a5)` | Label address lookup table |

### 6.2 Memory Map (Runtime)

```
Low memory
  |  Program code (tokenized lines)        StBas(a5)
  |  ...
  |  String heap (grows UP)                -> HiChaine(a5)
  |  
  |  [free space]
  |
  |  Array storage (grows DOWN)            <- TabBas(a5)
  |  Local variable area                   <- VarLoc(a5)
  |  Global variable area                  <- VarGlo(a5)
  |  Label table                           <- LabBas(a5)
  |
  |  [A3 loop/control stack area]          BaLoop..HoLoop
High memory
```

Strings and arrays compete for the same free space in the middle. When they collide, the garbage collector (`Menage`) runs to compact strings.

### 6.3 String Storage

Strings are stored as:
```
word    length          ; Character count
bytes   data            ; Characters (padded to even alignment)
```

String pointers in variables point to the length word. The empty string has its own canonical instance at `ChVide(a5)`. String concatenation allocates new space at `HiChaine` and copies both strings.

### 6.4 Array Storage

Arrays are stored below `TabBas(a5)`, growing downward. Each array has:

```
word    num_dimensions
[for each dimension:]
  word  max_index       ; (0-based limit)
  word  multiplier      ; product of subsequent dimension sizes
[data:]
  long  elements[...]   ; 4 bytes each, regardless of type
```

The `GetTablo` routine computes the flat index by walking the dimension table, multiplying and accumulating.

### 6.5 Label Table

Labels found during verification are stored in a linear table at `LabBas(a5)`. Each entry:

```
byte    name_length     ; Including padding
long    target_address  ; Pointer into token stream
[padding to 8 bytes]
bytes   name            ; Lowercase, padded to even
```

Lookup is linear scan (`GetLabel` / `GLab2..GLab3` loop). For procedure calls, a separate indexed table at `LabHaut(a5)` provides O(1) lookup by pre-computed index.

---

## 7. Quirks and Undocumented Behaviors

1. **Anti-crash magic cookies**: GOSUB frames contain `"Gosb"` ($476F7362) and PROC frames contain `"Proc"` ($50726F63). If the stack is corrupted, the interpreter scans upward looking for these cookies to recover. This is explicitly noted in the source: `"Protection anti crash!!!"`.

2. **POP bug**: The source contains the comment `"BUG si POP au milieu d'une boucle"` (bug if POP in the middle of a loop). POP directly uses `BasA3` without walking the stack, so it only works correctly when the GOSUB frame is at the scope boundary.

3. **String subtraction**: The `-` operator on strings performs exclusive subtraction -- it removes all occurrences of the right string from the left string. This is unusual and not well-documented.

4. **The Tk base address trick**: The entire dispatch system depends on token words being literal byte offsets from the `Tk` label. This means the token table must be position-independent and all entries must be reachable within a 16-bit signed offset range.

5. **Two-pass verification**: Before execution, `PTest` runs two passes -- Phase 0 finds procedures and global variables, Phase 1 processes each procedure's local scope. This pre-resolves all variable offsets, eliminating runtime name lookup entirely.

6. **Extension relocation**: Extensions use a custom bytecode for relocation (`C_Code1`/`C_Code2` tags) rather than standard Amiga relocations. This allows jumps into the main AMOS interpreter via an `AMOSJmps` table, enabling extensions to call core routines like `Evalue`, `InsRet`, `OpeRet`, etc.

7. **Direct mode strings**: When running in direct mode (`Direct(a5)` non-zero), string constants are copied to the heap rather than referenced in-place from the token stream, because the token buffer is volatile.

8. **Follow mode patching**: The interpreter loop's final jump instruction can be dynamically patched between `jmp 0(a0,d0.w)` (normal) and `jmp Follow` (debug) via the `SetFolow` routine, enabling single-step tracing without per-instruction overhead.
