# AMOS Reborn Oracle Pipeline

Automated pipeline that runs AMOS programs on real AMOS Professional (via FS-UAE Amiga emulator), captures screenshots, and compares them against AMOS Reborn output for visual verification.

## Architecture

```
oracle/
  config/
    oracle.conf       Master configuration (paths, timeouts)
    oracle.fs-uae     FS-UAE emulator configuration
  disks/
    workbench.hdf     A1200 Workbench boot disk (DH0:) — symlink
    amos-pro.hdf      AMOS Professional disk (DH1:) — symlink
    cetin-1.adf       Personal AMOS disk from the 90s — symlink
  roms/
    kick31.rom        Kickstart 3.1 ROM — symlink
    kick13.rom        Kickstart 1.3 ROM — symlink
  programs/
    *.amos            Test programs (one per requirement)
  staging/            Temporary mount point for FS-UAE (DH2:)
  scripts/
    fsuae_capture.sh  Capture a single test program via FS-UAE
    setup_check.sh    Prerequisite checker
  output/
    oracle/           Screenshots from real AMOS Professional
    reborn/           Screenshots from AMOS Reborn
    diffs/            Pixel diff images
  reference/          Committed golden reference images
```

## Prerequisites

- FS-UAE 3.x installed at `/Applications/FS-UAE.app`
- Kickstart 3.1 ROM (512 KB)
- Workbench 3.1 HDF boot disk
- AMOS Professional HDF with AMOS Pro installed
- Python 3 with Pillow
- `oracle-capture` binary (built from AMOS Reborn)

Run the setup check:

```bash
make -f oracle/Makefile oracle-setup
```

## Quick Start

```bash
# 1. Verify prerequisites
make -f oracle/Makefile oracle-setup

# 2. Build the oracle-capture tool
make -f oracle/Makefile oracle-capture-build

# 3. Capture a single test program (manual)
./oracle/scripts/fsuae_capture.sh oracle/programs/REQ-GFX-001_default_screen.amos

# 4. Generate all oracle reference screenshots
make -f oracle/Makefile oracle-generate

# 5. Run comparison test
make -f oracle/Makefile oracle-test

# 6. View report
make -f oracle/Makefile oracle-report
```

## How It Works

### Capture Flow

1. Test `.amos` program is copied to `oracle/staging/` (mounted as DH2: in the Amiga)
2. FS-UAE boots an A1200 with Workbench 3.1 (DH0:) and AMOS Pro (DH1:)
3. A startup script launches AMOS Pro and loads the test program from DH2:
4. After configurable boot + execution timeout, a screenshot is captured
5. FS-UAE is killed and the screenshot is saved as a PNG

### Comparison

The pipeline compares oracle screenshots (real AMOS Pro) against reborn screenshots (our reimplementation) using pixel-level difference. A test passes if less than 5% of pixels differ beyond a threshold.

### Disk Mount Strategy

| Drive | Volume    | Source              | Mode       |
|-------|-----------|---------------------|------------|
| DH0:  | Workbench | workbench.hdf       | Boot       |
| DH1:  | AMOSPro   | amos-pro.hdf        | Read-only  |
| DH2:  | Staging   | oracle/staging/     | Read-write |

The staging directory approach (DH2: mounted as a native directory) avoids any need to manipulate ADF/HDF files directly. Test programs are simply copied to the staging folder on the host filesystem.

## Writing Test Programs

Test programs live in `oracle/programs/` and follow the naming convention:

```
REQ-XXX-NNN_description.amos
```

Each program should:
- Include a `Rem` line stating the requirement being tested
- Include a `Rem` line stating the expected output
- Print "PASS" if the test condition is met
- Be self-contained (no external dependencies beyond AMOS Pro)

Example:

```basic
Rem REQ-GFX-001: Default screen is 320x256 with standard palette
Rem Expected: Default screen opens, colors drawn, dimensions printed
Screen Open 0,320,256,32,Lowres
Cls 0
Ink 1 : Draw 0,0 To 319,0
Print "W=";Screen Width;" H=";Screen Height
Print "PASS"
```

## Troubleshooting

### FS-UAE window hidden but still needs display
On macOS, `window_hidden = 1` hides the window but still requires a display server. This works fine in a normal desktop session. For CI, you would need a virtual framebuffer (Xvfb) which is not applicable on macOS.

### No screenshot captured
1. Verify FS-UAE launches manually: `fs-uae oracle/config/oracle.fs-uae`
2. Increase `BOOT_TIMEOUT` in `oracle/config/oracle.conf` if the Amiga is slow to boot
3. Install `cliclick` (`brew install cliclick`) for reliable keystroke injection
4. Check that the ROM and HDF symlinks resolve to valid files

### AMOS Pro does not auto-run the test
The current startup-sequence approach requires AMOS Pro to be in the Workbench startup. If your Workbench HDF does not auto-launch AMOS Pro, you may need to:
1. Boot FS-UAE interactively (without `window_hidden`)
2. Add AMOS Pro to the WBStartup drawer
3. Save the modified HDF

## Makefile Targets

| Target                | Description                                          |
|-----------------------|------------------------------------------------------|
| `oracle-setup`        | Run setup_check.sh to verify prerequisites           |
| `oracle-capture-build`| Build the oracle-capture tool via cmake              |
| `oracle-generate`     | Run FS-UAE for all test programs, save reference PNGs|
| `oracle-test`         | Run reborn for all test programs, compare references |
| `oracle-report`       | Generate summary of what has been captured/compared  |
| `oracle-clean`        | Remove output/ screenshots (preserves reference/)    |
| `oracle-help`         | List targets and discovered test programs            |
