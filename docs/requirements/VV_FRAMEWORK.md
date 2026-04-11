# AMOS Reborn V&V Test Framework

## Overview

The V&V (Verification & Validation) framework provides structured, requirement-traceable testing for the AMOS Reborn interpreter. Every test is tagged with a requirement ID, enabling automated traceability from specification to test to source location.

## Test Levels

### Level 1: Unit Tests
Individual function tests. These test tokenizer, parser, and executor internals directly. Already implemented in `test_tokenizer.c`, `test_parser.c`, etc. using the `ASSERT()` macro pattern.

### Level 2: Subsystem Tests (V&V Framework)
These test the interpreter as a black box by running AMOS Basic snippets and checking resulting state. This is where the V&V framework adds the most value.

Pattern:
```c
VV_TEST("REQ-INT-005: TRUE equals -1") {
    amos_state_t *s = vv_create();
    vv_run(s, "X = (1=1)");
    VV_ASSERT_INT(s, "X", -1);
    vv_destroy(s);
}
```

### Level 3: Golden Tests (Future)
Full program execution compared against reference output. Structure:
```
tests/golden/
    programs/          # .amos source files
    expected/          # .txt expected stdout output
    screens/           # .png expected screen captures
    golden_runner.c    # Runs each program, diffs output
```

The golden runner will:
1. Iterate `tests/golden/programs/*.amos`
2. Run each program headlessly (using `vv_create()` / `vv_run()`)
3. Capture stdout output and screen state
4. Compare against `tests/golden/expected/<name>.txt`
5. Report mismatches with diff context

Not implemented yet. The V&V framework helpers (`vv_create`, `vv_run`, `vv_destroy`) are designed to support this when ready.

## Requirement ID Format

```
REQ-<SUBSYSTEM>-<NNN>
```

| Prefix | Subsystem |
|--------|-----------|
| REQ-INT | Interpreter core (variables, expressions, control flow) |
| REQ-GFX | Graphics (screens, drawing, palette, sprites) |
| REQ-AUD | Audio (Paula emulation, tracker, effects) |
| REQ-IO | File I/O |
| REQ-STR | String functions |
| REQ-SPR | Sprites and bobs |
| REQ-AML | AMAL animation language |
| REQ-BNK | Memory banks |

## API Reference

### Test Declaration

```c
VV_TEST("REQ-INT-001: Integer variable assignment") {
    // test body
}
```

The `VV_TEST` macro:
- Defines a static test function
- Registers it via a constructor attribute (runs before `main()`)
- Extracts the requirement ID from everything before the first `:`

### Interpreter Helpers

| Function | Description |
|----------|-------------|
| `vv_create()` | Create a fresh `amos_state_t` ready for testing |
| `vv_run(state, program)` | Load and execute AMOS Basic text to completion (step limit: 10,000) |
| `vv_destroy(state)` | Free the interpreter state |

### Assertion Macros

| Macro | Purpose |
|-------|---------|
| `VV_ASSERT(cond, msg)` | Generic boolean assertion |
| `VV_ASSERT_INT(state, varname, expected)` | Check integer variable value |
| `VV_ASSERT_FLOAT(state, varname, expected, tolerance)` | Check float variable within tolerance |
| `VV_ASSERT_STR(state, varname, expected)` | Check string variable contents |
| `VV_ASSERT_SCREEN_PIXEL(state, screen, x, y, rgba)` | Check pixel color on a screen |
| `VV_ASSERT_PALETTE(state, screen, index, rgba)` | Check palette entry |

### Test Filtering

Run only tests matching a requirement prefix:
```c
vv_set_filter("REQ-GFX");  // only graphics tests
vv_run_all();
```

Or set `VV_FILTER` environment variable (requires adding env check to runner).

### Traceability Matrix

Call `vv_print_traceability()` to print a table mapping:
```
Requirement       Description                                       Location
REQ-INT-001       Integer variable assignment                        vv_example_tests.c:18
REQ-INT-002       Arithmetic precedence (multiply before add)        vv_example_tests.c:24
...
```

## How to Add New Tests

1. Create a new `.c` file in `tests/` (or add to `vv_example_tests.c`)
2. Include `vv_framework.h`
3. Write tests using `VV_TEST("REQ-XXX-NNN: description") { ... }`
4. Add the `.c` file to `CMakeLists.txt` in the `amos-test` target
5. Build and run: `cd build && cmake .. && make amos-test && ./amos-test`

Tests auto-register via constructor attributes. No manual registration needed.

## Output Format

```
--- V&V Tests ---

  PASS  [REQ-INT-001] REQ-INT-001: Integer variable assignment
  PASS  [REQ-INT-002] REQ-INT-002: Arithmetic precedence (multiply before add)
  FAIL  [REQ-INT-005] REQ-INT-005: TRUE equals -1 (1 assertion failed)
    FAIL: X = 1, expected -1 (vv_example_tests.c:42)

  V&V Results: 14 passed, 1 failed
  Assertions: 28 total, 1 failed
```

## Integration with Existing Tests

The V&V suite is registered as a new suite entry in `test_main.c`:
```c
{"V&V Tests", test_vv_all},
```

It runs alongside the existing unit test suites. The `test_vv_all()` function is implemented in `vv_framework.c` and calls `vv_run_all()`.

## File Layout

```
tests/
    vv_framework.h       # Framework header (macros + declarations)
    vv_framework.c       # Framework implementation (registration, runner, helpers)
    vv_example_tests.c   # Example V&V tests demonstrating the framework
    test_main.c          # Test runner (includes V&V as a suite)
docs/
    requirements/
        VV_FRAMEWORK.md  # This document
```
