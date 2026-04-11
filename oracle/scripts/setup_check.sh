#!/usr/bin/env bash
# setup_check.sh — Verify all prerequisites for the AMOS Reborn oracle pipeline.
#
# Usage: ./oracle/scripts/setup_check.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Load configuration
CONF="$PROJECT_ROOT/oracle/config/oracle.conf"
if [[ ! -f "$CONF" ]]; then
    echo "ERROR: Configuration file not found: $CONF" >&2
    exit 1
fi
source "$CONF"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m'  # No Color

PASS_COUNT=0
FAIL_COUNT=0
WARN_COUNT=0

check_pass() {
    printf "  ${GREEN}[OK]${NC}  %s\n" "$1"
    PASS_COUNT=$((PASS_COUNT + 1))
}

check_fail() {
    printf "  ${RED}[XX]${NC}  %s\n" "$1"
    FAIL_COUNT=$((FAIL_COUNT + 1))
}

check_warn() {
    printf "  ${YELLOW}[!!]${NC}  %s\n" "$1"
    WARN_COUNT=$((WARN_COUNT + 1))
}

echo "================================================================"
echo "  AMOS Reborn — Oracle Pipeline Setup Check"
echo "================================================================"
echo ""

# ─── FS-UAE Binary ────────────────────────────────────────────────────────────

echo "FS-UAE Emulator:"
if [[ -x "$FSUAE_BIN" ]]; then
    VERSION=$("$FSUAE_BIN" --version 2>&1 | head -1 || echo "unknown")
    check_pass "FS-UAE binary found: $FSUAE_BIN ($VERSION)"
else
    check_fail "FS-UAE binary not found: $FSUAE_BIN"
    echo "         Install from: https://fs-uae.net/ or 'brew install fs-uae'"
fi

# ─── Kickstart ROM ────────────────────────────────────────────────────────────

echo ""
echo "Kickstart ROM:"
ROM_PATH="$PROJECT_ROOT/$KICKSTART_ROM"
if [[ -f "$ROM_PATH" ]] || [[ -L "$ROM_PATH" && -f "$(readlink -f "$ROM_PATH" 2>/dev/null || readlink "$ROM_PATH")" ]]; then
    # Resolve symlink for size check
    REAL_ROM="$(readlink -f "$ROM_PATH" 2>/dev/null || readlink "$ROM_PATH" 2>/dev/null || echo "$ROM_PATH")"
    ROM_SIZE=$(stat -f%z "$REAL_ROM" 2>/dev/null || stat -c%s "$REAL_ROM" 2>/dev/null || echo 0)
    if [[ "$ROM_SIZE" -eq 524288 ]]; then
        check_pass "Kickstart 3.1 ROM: $KICKSTART_ROM (512 KB)"
    elif [[ "$ROM_SIZE" -eq 262144 ]]; then
        check_pass "Kickstart ROM: $KICKSTART_ROM (256 KB — Kickstart 1.3?)"
    else
        check_warn "Kickstart ROM exists but unexpected size: ${ROM_SIZE} bytes (expected 524288)"
    fi
else
    check_fail "Kickstart ROM not found: $KICKSTART_ROM"
    echo "         Symlink your Kickstart 3.1 ROM to oracle/roms/kick31.rom"
fi

# ─── HDF Disk Images ─────────────────────────────────────────────────────────

echo ""
echo "Hard Drive Images:"
for HDF_VAR in BOOT_HDF AMOS_HDF; do
    HDF_PATH="$PROJECT_ROOT/${!HDF_VAR}"
    if [[ -f "$HDF_PATH" ]] || [[ -L "$HDF_PATH" && -f "$(readlink -f "$HDF_PATH" 2>/dev/null || readlink "$HDF_PATH")" ]]; then
        REAL_HDF="$(readlink -f "$HDF_PATH" 2>/dev/null || readlink "$HDF_PATH" 2>/dev/null || echo "$HDF_PATH")"
        HDF_SIZE=$(stat -f%z "$REAL_HDF" 2>/dev/null || stat -c%s "$REAL_HDF" 2>/dev/null || echo 0)
        HDF_MB=$((HDF_SIZE / 1048576))
        check_pass "${HDF_VAR}: ${!HDF_VAR} (${HDF_MB} MB)"
    else
        check_fail "${HDF_VAR}: ${!HDF_VAR} — not found"
    fi
done

# Check for Cetin's personal disk (optional)
CETIN_ADF="$PROJECT_ROOT/oracle/disks/cetin-1.adf"
if [[ -f "$CETIN_ADF" ]] || [[ -L "$CETIN_ADF" ]]; then
    check_pass "Personal disk: cetin-1.adf (optional, present)"
else
    check_warn "Personal disk: cetin-1.adf (optional, not present)"
fi

# ─── Python + Pillow ──────────────────────────────────────────────────────────

echo ""
echo "Python Environment:"
if command -v python3 &>/dev/null; then
    PY_VERSION=$(python3 --version 2>&1)
    check_pass "Python3: $PY_VERSION"
else
    check_fail "Python3 not found"
fi

if python3 -c "import PIL; print(PIL.__version__)" 2>/dev/null; then
    PIL_VERSION=$(python3 -c "import PIL; print(PIL.__version__)" 2>/dev/null)
    check_pass "Pillow: $PIL_VERSION"
else
    check_fail "Pillow not installed (pip3 install Pillow)"
fi

# ─── oracle-capture binary ────────────────────────────────────────────────────

echo ""
echo "Reborn Build:"
CAPTURE_BIN="$PROJECT_ROOT/$REBORN_BIN"
if [[ -x "$CAPTURE_BIN" ]]; then
    check_pass "oracle-capture binary: $REBORN_BIN"
else
    check_warn "oracle-capture binary not built: $REBORN_BIN"
    echo "         Run: cd build && cmake .. && make oracle-capture"
fi

# ─── Directory structure ──────────────────────────────────────────────────────

echo ""
echo "Directory Structure:"
for DIR_VAR in PROGRAMS_DIR STAGING_DIR OUTPUT_ORACLE_DIR OUTPUT_REBORN_DIR OUTPUT_DIFFS_DIR REFERENCE_DIR; do
    DIR_PATH="$PROJECT_ROOT/${!DIR_VAR}"
    if [[ -d "$DIR_PATH" ]]; then
        COUNT=$(find "$DIR_PATH" -maxdepth 1 -type f 2>/dev/null | wc -l | tr -d ' ')
        check_pass "${DIR_VAR}: ${!DIR_VAR} ($COUNT files)"
    else
        check_warn "${DIR_VAR}: ${!DIR_VAR} — will be created on first run"
    fi
done

# ─── Test programs ────────────────────────────────────────────────────────────

echo ""
echo "Test Programs:"
AMOS_COUNT=$(find "$PROJECT_ROOT/$PROGRAMS_DIR" -name "*.amos" 2>/dev/null | wc -l | tr -d ' ')
if [[ "$AMOS_COUNT" -gt 0 ]]; then
    check_pass "$AMOS_COUNT test program(s) found:"
    find "$PROJECT_ROOT/$PROGRAMS_DIR" -name "*.amos" -exec basename {} \; 2>/dev/null | sort | while read -r f; do
        echo "           $f"
    done
else
    check_warn "No test programs found in $PROGRAMS_DIR/"
fi

# ─── macOS screenshot tools ──────────────────────────────────────────────────

echo ""
echo "Screenshot Tools:"
if command -v screencapture &>/dev/null; then
    check_pass "screencapture: available (macOS built-in)"
else
    check_warn "screencapture: not available"
fi

if command -v cliclick &>/dev/null; then
    check_pass "cliclick: available (for sending keystrokes)"
else
    check_warn "cliclick: not installed (brew install cliclick — optional)"
fi

# ─── Summary ─────────────────────────────────────────────────────────────────

echo ""
echo "================================================================"
printf "  Summary: ${GREEN}%d passed${NC}" "$PASS_COUNT"
if [[ $WARN_COUNT -gt 0 ]]; then
    printf ", ${YELLOW}%d warnings${NC}" "$WARN_COUNT"
fi
if [[ $FAIL_COUNT -gt 0 ]]; then
    printf ", ${RED}%d failed${NC}" "$FAIL_COUNT"
fi
echo ""
echo "================================================================"

if [[ $FAIL_COUNT -gt 0 ]]; then
    echo ""
    echo "Fix the failed checks above before running the oracle pipeline."
    exit 1
fi

exit 0
