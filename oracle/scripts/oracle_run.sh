#!/bin/bash
# oracle_run.sh — AMOS Reborn oracle pipeline orchestrator
#
# Usage:
#   oracle_run.sh [--test REQ-INT-005] [--reborn-only] [--generate-reference] [--compare]
#
# Modes:
#   --generate-reference   Run FS-UAE, save outputs as golden reference
#   --reborn-only          Run reborn only, compare against saved reference (default)
#   --compare              Run both FS-UAE and reborn, compare
#
# Options:
#   --test PATTERN         Only run tests matching PATTERN (substring match)
#   --tolerance N          Per-channel color tolerance (default: 5)
#   --threshold F          Match fraction threshold (default: 0.98)
#   --verbose              Show per-channel stats
#   --diff                 Generate diff images in oracle/output/diffs/

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ORACLE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_DIR="$(cd "$ORACLE_DIR/.." && pwd)"

PROGRAMS_DIR="$ORACLE_DIR/programs"
REFERENCE_DIR="$ORACLE_DIR/reference"
REBORN_OUTPUT="$ORACLE_DIR/output/reborn"
ORACLE_OUTPUT="$ORACLE_DIR/output/oracle"
DIFFS_DIR="$ORACLE_DIR/output/diffs"
COMPARE="$SCRIPT_DIR/compare.py"
ORACLE_CAPTURE="$REPO_DIR/build/oracle-capture"

# Defaults
MODE="reborn-only"
TEST_FILTER=""
TOLERANCE=5
THRESHOLD=0.98
VERBOSE=""
GEN_DIFFS=""

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --generate-reference)
            MODE="generate-reference"
            shift
            ;;
        --reborn-only)
            MODE="reborn-only"
            shift
            ;;
        --compare)
            MODE="compare"
            shift
            ;;
        --test)
            TEST_FILTER="$2"
            shift 2
            ;;
        --tolerance)
            TOLERANCE="$2"
            shift 2
            ;;
        --threshold)
            THRESHOLD="$2"
            shift 2
            ;;
        --verbose)
            VERBOSE="--verbose"
            shift
            ;;
        --diff)
            GEN_DIFFS="yes"
            shift
            ;;
        -h|--help)
            head -20 "$0" | grep '^#' | sed 's/^# *//'
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

# Ensure output directories exist
mkdir -p "$REBORN_OUTPUT" "$ORACLE_OUTPUT" "$DIFFS_DIR"

# Collect test programs
PROGRAMS=()
for prog in "$PROGRAMS_DIR"/*.amos; do
    [[ -f "$prog" ]] || continue
    name="$(basename "$prog" .amos)"
    if [[ -n "$TEST_FILTER" && "$name" != *"$TEST_FILTER"* ]]; then
        continue
    fi
    PROGRAMS+=("$prog")
done

if [[ ${#PROGRAMS[@]} -eq 0 ]]; then
    echo "No test programs found matching filter: '$TEST_FILTER'"
    exit 1
fi

echo "=========================================="
echo "  AMOS Reborn Oracle Pipeline"
echo "  Mode: $MODE"
echo "  Tests: ${#PROGRAMS[@]}"
echo "  Tolerance: $TOLERANCE  Threshold: $THRESHOLD"
echo "=========================================="
echo ""

# --- Generate Reference Mode ---
if [[ "$MODE" == "generate-reference" ]]; then
    echo "ERROR: --generate-reference requires FS-UAE integration (not yet implemented)"
    echo "       Place reference PNGs manually in: $REFERENCE_DIR/"
    exit 1
fi

# --- Compare Mode (both) ---
if [[ "$MODE" == "compare" ]]; then
    echo "ERROR: --compare requires FS-UAE integration (not yet implemented)"
    echo "       Use --reborn-only to compare against saved references."
    exit 1
fi

# --- Reborn-Only Mode (default) ---
# Check oracle-capture binary
if [[ ! -x "$ORACLE_CAPTURE" ]]; then
    echo "WARNING: oracle-capture binary not found at: $ORACLE_CAPTURE"
    echo "         Build with: cd $REPO_DIR && cmake --build build"
    echo "         Proceeding with comparison of existing outputs only..."
    SKIP_CAPTURE=1
else
    SKIP_CAPTURE=0
fi

# Results tracking
TOTAL=0
PASSED=0
FAILED=0
SKIPPED=0
ERRORS=0

# Width for formatting
NAME_WIDTH=40
STATUS_WIDTH=8
RATIO_WIDTH=12

# Print table header
printf "%-${NAME_WIDTH}s %-${STATUS_WIDTH}s %-${RATIO_WIDTH}s %s\n" \
    "TEST" "STATUS" "MATCH" "DETAILS"
printf "%0.s-" {1..80}
echo ""

for prog in "${PROGRAMS[@]}"; do
    name="$(basename "$prog" .amos)"
    reborn_png="$REBORN_OUTPUT/${name}.png"
    ref_png="$REFERENCE_DIR/${name}.png"
    diff_png="$DIFFS_DIR/${name}_diff.png"
    TOTAL=$((TOTAL + 1))

    # Step 1: Run oracle-capture if available
    if [[ "$SKIP_CAPTURE" -eq 0 ]]; then
        capture_output=$("$ORACLE_CAPTURE" "$prog" --screenshot "$reborn_png" 2>&1) || {
            printf "%-${NAME_WIDTH}s %-${STATUS_WIDTH}s %-${RATIO_WIDTH}s %s\n" \
                "$name" "ERROR" "-" "oracle-capture failed"
            ERRORS=$((ERRORS + 1))
            continue
        }
    fi

    # Step 2: Check for required files
    if [[ ! -f "$reborn_png" ]]; then
        printf "%-${NAME_WIDTH}s %-${STATUS_WIDTH}s %-${RATIO_WIDTH}s %s\n" \
            "$name" "SKIP" "-" "No reborn output"
        SKIPPED=$((SKIPPED + 1))
        continue
    fi

    if [[ ! -f "$ref_png" ]]; then
        printf "%-${NAME_WIDTH}s %-${STATUS_WIDTH}s %-${RATIO_WIDTH}s %s\n" \
            "$name" "SKIP" "-" "No reference image"
        SKIPPED=$((SKIPPED + 1))
        continue
    fi

    # Step 3: Run comparison
    DIFF_ARG=""
    if [[ -n "$GEN_DIFFS" ]]; then
        DIFF_ARG="--diff $diff_png"
    fi

    result=$(python3 "$COMPARE" "$ref_png" "$reborn_png" \
        --tolerance "$TOLERANCE" \
        --threshold "$THRESHOLD" \
        $DIFF_ARG $VERBOSE 2>&1) || true

    # Parse result: last line of stdout is PASS/FAIL, stderr has JSON
    status_line=$(echo "$result" | grep -E '^(PASS|FAIL)$' | tail -1)
    json_line=$(echo "$result" | grep -E '^\{' | head -1)

    match_ratio="-"
    detail=""
    if [[ -n "$json_line" ]]; then
        match_ratio=$(echo "$json_line" | python3 -c "
import sys, json
d = json.load(sys.stdin)
print(d.get('match_ratio', '-'))
" 2>/dev/null || echo "-")

        mismatched=$(echo "$json_line" | python3 -c "
import sys, json
d = json.load(sys.stdin)
m = d.get('mismatched', d.get('mismatched_lines', 0))
w = d.get('warning', '')
parts = []
if m: parts.append(f'{m} px differ')
if w: parts.append(w)
print('; '.join(parts) if parts else '')
" 2>/dev/null || echo "")
        detail="$mismatched"
    fi

    if [[ "$status_line" == "PASS" ]]; then
        printf "%-${NAME_WIDTH}s \033[32m%-${STATUS_WIDTH}s\033[0m %-${RATIO_WIDTH}s %s\n" \
            "$name" "PASS" "$match_ratio" "$detail"
        PASSED=$((PASSED + 1))
    elif [[ "$status_line" == "FAIL" ]]; then
        printf "%-${NAME_WIDTH}s \033[31m%-${STATUS_WIDTH}s\033[0m %-${RATIO_WIDTH}s %s\n" \
            "$name" "FAIL" "$match_ratio" "$detail"
        FAILED=$((FAILED + 1))
    else
        printf "%-${NAME_WIDTH}s %-${STATUS_WIDTH}s %-${RATIO_WIDTH}s %s\n" \
            "$name" "ERROR" "-" "Unexpected output"
        ERRORS=$((ERRORS + 1))
    fi
done

# Summary
echo ""
printf "%0.s=" {1..80}
echo ""
echo "TOTAL: $TOTAL  PASSED: $PASSED  FAILED: $FAILED  SKIPPED: $SKIPPED  ERRORS: $ERRORS"

if [[ $FAILED -gt 0 || $ERRORS -gt 0 ]]; then
    echo ""
    echo "RESULT: FAIL"
    exit 1
elif [[ $PASSED -eq 0 ]]; then
    echo ""
    echo "RESULT: NO TESTS RAN (all skipped)"
    exit 1
else
    echo ""
    echo "RESULT: PASS"
    exit 0
fi
