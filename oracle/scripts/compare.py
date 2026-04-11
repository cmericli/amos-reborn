#!/usr/bin/env python3
"""
Image comparison tool for the AMOS Reborn oracle pipeline.

Compares screenshots from amos-reborn against golden reference images
from FS-UAE (real Amiga emulator) with configurable pixel-level tolerance.

Usage:
    compare.py reference.png actual.png [options]

Exit codes:
    0 = PASS (match ratio >= threshold)
    1 = FAIL (match ratio < threshold or error)
"""

import argparse
import json
import os
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError:
    print("ERROR: Pillow is required. Install with: pip install Pillow>=9.0",
          file=sys.stderr)
    sys.exit(1)


def parse_crop(crop_str: str) -> tuple:
    """Parse 'X,Y,W,H' into (x, y, w, h) tuple."""
    parts = crop_str.split(",")
    if len(parts) != 4:
        raise ValueError(f"--crop requires X,Y,W,H format, got: {crop_str}")
    return tuple(int(p.strip()) for p in parts)


def load_image(path: str) -> Image.Image:
    """Load an image, converting to RGBA for consistent comparison."""
    img = Image.open(path)
    return img.convert("RGBA")


def crop_image(img: Image.Image, crop: tuple) -> Image.Image:
    """Crop image to (x, y, w, h) region."""
    x, y, w, h = crop
    return img.crop((x, y, x + w, y + h))


def compare_pixels(ref: Image.Image, actual: Image.Image,
                   tolerance: int, verbose: bool = False):
    """
    Compare two same-sized RGBA images pixel by pixel.

    Returns:
        dict with match_ratio, total_pixels, mismatched, per_channel stats,
        and a list of per-pixel error magnitudes for diff generation.
    """
    assert ref.size == actual.size, "Images must be same size for comparison"

    w, h = ref.size
    total = w * h
    mismatched = 0

    ref_px = ref.load()
    act_px = actual.load()

    # Per-pixel error magnitude (0.0 = match, 1.0 = max mismatch)
    errors = []

    # Per-channel statistics
    channel_diffs = {"R": [], "G": [], "B": [], "A": []}

    for i in range(total):
        x = i % w
        y = i // w
        rp = ref_px[x, y]
        ap = act_px[x, y]

        diffs = [abs(rp[c] - ap[c]) for c in range(4)]
        max_diff = max(diffs)

        for ci, name in enumerate(["R", "G", "B", "A"]):
            channel_diffs[name].append(diffs[ci])

        if max_diff > tolerance:
            mismatched += 1
            # Normalize error magnitude: how far beyond tolerance
            errors.append(min(1.0, max_diff / 255.0))
        else:
            errors.append(0.0)

    match_ratio = (total - mismatched) / total if total > 0 else 1.0

    stats = {
        "match_ratio": round(match_ratio, 6),
        "total_pixels": total,
        "mismatched": mismatched,
        "image_size": [w, h],
    }

    if verbose:
        for name in ["R", "G", "B", "A"]:
            vals = channel_diffs[name]
            stats[f"channel_{name}_mean_diff"] = round(sum(vals) / len(vals), 2)
            stats[f"channel_{name}_max_diff"] = max(vals)

    return stats, errors


def generate_diff_image(ref: Image.Image, errors: list,
                        tolerance: int) -> Image.Image:
    """
    Generate a diff visualization image.

    - Exact match (error == 0): transparent
    - Within tolerance but not exact: green with low alpha
    - Mismatch (beyond tolerance): red, brightness proportional to error
    """
    w, h = ref.size
    diff = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    pixels = diff.load()

    for i, err in enumerate(errors):
        x = i % w
        y = i // w

        if err == 0.0:
            pixels[x, y] = (0, 0, 0, 0)  # transparent
        else:
            # Mismatch: red with brightness proportional to error magnitude
            brightness = int(err * 255)
            pixels[x, y] = (255, max(0, 50 - brightness), 0,
                            min(255, 80 + brightness))

    return diff


def mark_within_tolerance(ref: Image.Image, actual: Image.Image,
                          diff: Image.Image, tolerance: int):
    """
    Second pass: mark pixels that differ but are within tolerance as green.
    """
    if tolerance == 0:
        return diff

    w, h = ref.size
    ref_px = ref.load()
    act_px = actual.load()
    pixels = diff.load()

    for y in range(h):
        for x in range(w):
            rp = ref_px[x, y]
            ap = act_px[x, y]
            max_diff = max(abs(rp[c] - ap[c]) for c in range(4))

            if 0 < max_diff <= tolerance:
                alpha = int((max_diff / tolerance) * 120)
                pixels[x, y] = (0, 200, 0, max(30, alpha))

    return diff


def compare_text_files(ref_path: str, actual_path: str):
    """
    Compare two text files line by line.

    Returns stats dict and line-by-line diff info.
    """
    def read_lines(path):
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return f.readlines()

    ref_lines = read_lines(ref_path)
    act_lines = read_lines(actual_path)

    max_lines = max(len(ref_lines), len(act_lines))
    matched = 0
    diffs = []

    for i in range(max_lines):
        ref_line = ref_lines[i].rstrip("\n") if i < len(ref_lines) else "<missing>"
        act_line = act_lines[i].rstrip("\n") if i < len(act_lines) else "<missing>"

        if ref_line == act_line:
            matched += 1
        else:
            diffs.append({
                "line": i + 1,
                "reference": ref_line,
                "actual": act_line,
            })

    match_ratio = matched / max_lines if max_lines > 0 else 1.0

    stats = {
        "match_ratio": round(match_ratio, 6),
        "total_lines": max_lines,
        "mismatched_lines": max_lines - matched,
        "reference_lines": len(ref_lines),
        "actual_lines": len(act_lines),
    }

    return stats, diffs


def main():
    parser = argparse.ArgumentParser(
        description="AMOS Reborn oracle image comparison tool",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  compare.py ref.png actual.png
  compare.py ref.png actual.png --tolerance 10 --threshold 0.95 --diff out.png
  compare.py ref.png actual.png --crop 0,0,320,200 --verbose
  compare.py ref.txt actual.txt --text-mode
        """,
    )
    parser.add_argument("reference", help="Golden reference image (from FS-UAE oracle)")
    parser.add_argument("actual", help="Output from amos-reborn oracle-capture tool")
    parser.add_argument("--tolerance", type=int, default=5,
                        help="Per-channel color tolerance 0-255 (default: 5)")
    parser.add_argument("--threshold", type=float, default=0.98,
                        help="Fraction of pixels that must match 0.0-1.0 (default: 0.98)")
    parser.add_argument("--diff", metavar="FILE",
                        help="Output diff image path (green=within tolerance, red=mismatch)")
    parser.add_argument("--crop", metavar="X,Y,W,H",
                        help="Only compare a sub-region")
    parser.add_argument("--text-mode", action="store_true",
                        help="Compare extracted text lines instead of pixels")
    parser.add_argument("--verbose", action="store_true",
                        help="Print per-channel statistics")

    args = parser.parse_args()

    # --- Validate files exist ---
    if not os.path.isfile(args.reference):
        stats = {"error": f"Reference file not found: {args.reference}"}
        print(json.dumps(stats), file=sys.stderr)
        print("FAIL")
        sys.exit(1)

    if not os.path.isfile(args.actual):
        stats = {"error": f"Actual file not found: {args.actual}"}
        print(json.dumps(stats), file=sys.stderr)
        print("FAIL")
        sys.exit(1)

    # --- Text mode ---
    if args.text_mode:
        stats, diffs = compare_text_files(args.reference, args.actual)
        print(json.dumps(stats), file=sys.stderr)

        if args.verbose and diffs:
            for d in diffs:
                print(f"  Line {d['line']}: ref={d['reference']!r} "
                      f"act={d['actual']!r}", file=sys.stderr)

        if stats["match_ratio"] >= args.threshold:
            print("PASS")
            sys.exit(0)
        else:
            print("FAIL")
            sys.exit(1)

    # --- Image mode ---
    crop = parse_crop(args.crop) if args.crop else None

    try:
        ref = load_image(args.reference)
        actual = load_image(args.actual)
    except Exception as e:
        stats = {"error": f"Failed to load images: {e}"}
        print(json.dumps(stats), file=sys.stderr)
        print("FAIL")
        sys.exit(1)

    # Apply crop if specified
    if crop:
        try:
            ref = crop_image(ref, crop)
            actual = crop_image(actual, crop)
        except Exception as e:
            stats = {"error": f"Crop failed: {e}"}
            print(json.dumps(stats), file=sys.stderr)
            print("FAIL")
            sys.exit(1)

    # Handle size mismatch
    resized = False
    if ref.size != actual.size:
        print(json.dumps({
            "warning": "Size mismatch",
            "reference_size": list(ref.size),
            "actual_size": list(actual.size),
        }), file=sys.stderr)
        resized = True
        # Resize actual to match reference for comparison
        actual = actual.resize(ref.size, Image.Resampling.NEAREST)

    # Run pixel comparison
    stats, errors = compare_pixels(ref, actual, args.tolerance, args.verbose)

    if resized:
        stats["warning"] = "Images were different sizes; actual was resized for comparison"

    # Generate diff image if requested
    if args.diff:
        diff_img = generate_diff_image(ref, errors, args.tolerance)
        diff_img = mark_within_tolerance(ref, actual, diff_img, args.tolerance)
        diff_dir = os.path.dirname(args.diff)
        if diff_dir:
            os.makedirs(diff_dir, exist_ok=True)
        diff_img.save(args.diff)
        stats["diff_image"] = args.diff

    # Output results
    print(json.dumps(stats), file=sys.stderr)

    if stats["match_ratio"] >= args.threshold:
        print("PASS")
        sys.exit(0)
    else:
        print("FAIL")
        sys.exit(1)


if __name__ == "__main__":
    main()
