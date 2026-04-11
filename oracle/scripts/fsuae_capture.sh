#!/usr/bin/env bash
# fsuae_capture.sh — Run an AMOS program on real AMOS Professional via FS-UAE,
# capture a screenshot, and save as a reference PNG.
#
# Usage: ./oracle/scripts/fsuae_capture.sh <program.amos> [--no-kill]
#
# The script:
#   1. Copies the .amos file to the staging directory (DH2:)
#   2. Creates an AmigaDOS startup-sequence that launches AMOS Pro and runs the program
#   3. Launches FS-UAE with the oracle config
#   4. Waits for boot + execution (configurable timeout)
#   5. Captures screenshot via FS-UAE's F12+S mechanism (xdotool/cliclick)
#   6. Kills FS-UAE
#   7. Moves screenshot to oracle/output/oracle/NAME.png

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

# ─── Arguments ────────────────────────────────────────────────────────────────

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <program.amos> [--no-kill]"
    echo ""
    echo "  program.amos    Path to AMOS program file (or just the basename)"
    echo "  --no-kill       Leave FS-UAE running after capture (for debugging)"
    exit 1
fi

PROGRAM_ARG="$1"
NO_KILL=0
if [[ "${2:-}" == "--no-kill" ]]; then
    NO_KILL=1
fi

# Resolve program path
if [[ -f "$PROGRAM_ARG" ]]; then
    PROGRAM_PATH="$PROGRAM_ARG"
elif [[ -f "$PROJECT_ROOT/$PROGRAMS_DIR/$PROGRAM_ARG" ]]; then
    PROGRAM_PATH="$PROJECT_ROOT/$PROGRAMS_DIR/$PROGRAM_ARG"
elif [[ -f "$PROJECT_ROOT/$PROGRAMS_DIR/${PROGRAM_ARG}.amos" ]]; then
    PROGRAM_PATH="$PROJECT_ROOT/$PROGRAMS_DIR/${PROGRAM_ARG}.amos"
else
    echo "ERROR: Program not found: $PROGRAM_ARG" >&2
    echo "Searched in: $PROJECT_ROOT/$PROGRAMS_DIR/" >&2
    exit 1
fi

PROGRAM_NAME="$(basename "$PROGRAM_PATH" .amos)"
echo "=== Oracle Capture: $PROGRAM_NAME ==="
echo "  Program: $PROGRAM_PATH"

# ─── Prepare staging directory ────────────────────────────────────────────────

STAGING="$PROJECT_ROOT/$STAGING_DIR"
mkdir -p "$STAGING/s"  # s/ subdirectory for startup-sequence

# Copy program to staging
cp "$PROGRAM_PATH" "$STAGING/${PROGRAM_NAME}.amos"
echo "  Staged: $STAGING/${PROGRAM_NAME}.amos"

# Create AmigaDOS startup-sequence for the staging volume
# This tells AMOS Pro to load and run the test program, then wait for screenshot.
#
# Strategy: We create an ARexx script that AMOS Pro can execute.
# The startup-sequence on DH2: will:
#   1. Wait for AMOS Pro to be available
#   2. Send ARexx command to load and run the program
#   3. Wait for screenshot delay
#
# Note: AMOS Pro must already be in the WB startup-sequence on DH0.
# If not, we create a minimal script that launches it.

cat > "$STAGING/s/startup-sequence" << 'AMIGAEOF'
; Oracle pipeline startup-sequence (DH2:)
; This is NOT the boot startup-sequence — DH0: boots Workbench.
; This script is called from DH0:'s User-Startup or manually.

Wait 5
; Launch AMOS Professional from DH1:
DH1:AMOSPro/AMOSPro
AMIGAEOF

# Create an ARexx script to load and run the test program
cat > "$STAGING/run_test.rexx" << REXXEOF
/* ARexx script to run AMOS test program */
/* Called after AMOS Pro is loaded */

ADDRESS 'AMOSPRO'
'LOAD "DH2:${PROGRAM_NAME}.amos"'
'RUN'

EXIT
REXXEOF

echo "  Startup scripts created"

# ─── Prepare output directory ─────────────────────────────────────────────────

OUTPUT_DIR="$PROJECT_ROOT/$OUTPUT_ORACLE_DIR"
mkdir -p "$OUTPUT_DIR"

# ─── Launch FS-UAE ────────────────────────────────────────────────────────────

FSUAE_CONF="$PROJECT_ROOT/oracle/config/oracle.fs-uae"
if [[ ! -x "$FSUAE_BIN" ]]; then
    echo "ERROR: FS-UAE binary not found or not executable: $FSUAE_BIN" >&2
    exit 1
fi

echo "  Launching FS-UAE..."
echo "  Boot timeout: ${BOOT_TIMEOUT}s, Test timeout: ${TEST_TIMEOUT}s, Screenshot delay: ${SCREENSHOT_DELAY}s"

# Total time to wait before screenshot
TOTAL_WAIT=$((BOOT_TIMEOUT + TEST_TIMEOUT))

# Launch FS-UAE in background
cd "$PROJECT_ROOT"
"$FSUAE_BIN" "$FSUAE_CONF" &
FSUAE_PID=$!
echo "  FS-UAE PID: $FSUAE_PID"

# Function to clean up on exit
cleanup() {
    if [[ $NO_KILL -eq 0 ]] && kill -0 "$FSUAE_PID" 2>/dev/null; then
        echo "  Killing FS-UAE (PID $FSUAE_PID)..."
        kill "$FSUAE_PID" 2>/dev/null || true
        wait "$FSUAE_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# Wait for boot + execution
echo "  Waiting ${TOTAL_WAIT}s for boot + execution..."
sleep "$TOTAL_WAIT"

# ─── Capture screenshot ──────────────────────────────────────────────────────

echo "  Waiting ${SCREENSHOT_DELAY}s before screenshot..."
sleep "$SCREENSHOT_DELAY"

# Method 1: Use FS-UAE's built-in screenshot (F12+S sends screenshot)
# On macOS, we use cliclick to send the key combo to the FS-UAE window
# F12+S is the default FS-UAE screenshot hotkey
if command -v cliclick &>/dev/null; then
    # cliclick can send keystrokes to the frontmost app
    # F12 key code: we use the key name directly
    echo "  Sending screenshot key (F12+S via cliclick)..."
    cliclick kd:fn kp:f12 ku:fn
    sleep 0.5
    cliclick kp:s
    sleep 1
elif command -v osascript &>/dev/null; then
    # Fallback: use AppleScript to send keystroke
    echo "  Sending screenshot key (F12+S via AppleScript)..."
    osascript -e '
        tell application "System Events"
            tell process "fs-uae"
                key code 111  -- F12
            end tell
        end tell
    ' 2>/dev/null || true
    sleep 0.5
    osascript -e '
        tell application "System Events"
            tell process "fs-uae"
                keystroke "s"
            end tell
        end tell
    ' 2>/dev/null || true
    sleep 1
fi

# Method 2: Fallback — use macOS screencapture on the FS-UAE window
# This always works regardless of FS-UAE's internal screenshot mechanism
echo "  Taking macOS screen capture as fallback..."
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
CAPTURE_FILE="$OUTPUT_DIR/${PROGRAM_NAME}_${TIMESTAMP}.png"

if command -v screencapture &>/dev/null; then
    # Capture the FS-UAE window by PID
    # -l flag captures a specific window; we find the window ID
    WINDOW_ID=$(osascript -e "
        tell application \"System Events\"
            try
                set w to first window of process \"fs-uae\"
                return id of w
            on error
                return \"\"
            end try
        end tell
    " 2>/dev/null || echo "")

    if [[ -n "$WINDOW_ID" ]]; then
        screencapture -l "$WINDOW_ID" "$CAPTURE_FILE" 2>/dev/null || true
    else
        # Fall back to full screen capture and crop later
        screencapture -x "$CAPTURE_FILE" 2>/dev/null || true
    fi
fi

# Check for FS-UAE's own screenshot output
FSUAE_SCREENSHOTS="$PROJECT_ROOT/oracle/output/oracle"
LATEST_SCREENSHOT=$(find "$FSUAE_SCREENSHOTS" -name "capture*.png" -newer "$PROGRAM_PATH" 2>/dev/null | sort | tail -1 || echo "")

# Determine final output file
FINAL_OUTPUT="$OUTPUT_DIR/${PROGRAM_NAME}.png"
if [[ -n "$LATEST_SCREENSHOT" && -f "$LATEST_SCREENSHOT" ]]; then
    mv "$LATEST_SCREENSHOT" "$FINAL_OUTPUT"
    echo "  Saved FS-UAE screenshot: $FINAL_OUTPUT"
elif [[ -f "$CAPTURE_FILE" ]]; then
    mv "$CAPTURE_FILE" "$FINAL_OUTPUT"
    echo "  Saved macOS capture: $FINAL_OUTPUT"
else
    echo "  WARNING: No screenshot captured" >&2
fi

# ─── Cleanup staging ─────────────────────────────────────────────────────────

echo "  Cleaning staging directory..."
rm -f "$STAGING/${PROGRAM_NAME}.amos"
rm -f "$STAGING/run_test.rexx"
rm -f "$STAGING/s/startup-sequence"

echo "=== Done: $PROGRAM_NAME ==="
if [[ -f "$FINAL_OUTPUT" ]]; then
    echo "  Output: $FINAL_OUTPUT"
    ls -la "$FINAL_OUTPUT"
fi
