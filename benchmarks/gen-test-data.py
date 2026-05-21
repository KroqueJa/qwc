#!/usr/bin/env python3
"""
Generate benchmark test data according to benchmarks/bench-spec.yml.

Outputs:
  benchmarks/test-data/big.bin          – one large file (~1 GB)
  benchmarks/test-data/small_NNNN.bin   – 200 small files (500 KB – 2 MB each)
  benchmarks/bench-spec.csv             – manifest with exact newline counts
"""

import csv
import os
import random
import sys

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT    = os.path.dirname(SCRIPT_DIR)
TEST_DATA_DIR = os.path.join(SCRIPT_DIR, "test-data")
CSV_OUTPUT   = os.path.join(SCRIPT_DIR, "bench-spec.csv")

# ---------------------------------------------------------------------------
# Spec (mirrored from bench-spec.yml so the script is self-contained)
# ---------------------------------------------------------------------------
BIG_BYTES        = 1_000_000_000   # 1 GB
BIG_NL_RATIO     = 0.23

SMALL_COUNT      = 200
SMALL_NL_RATIO   = 0.50
SMALL_MIN_BYTES  = 500 * 1024       # 500 KB
SMALL_MAX_BYTES  = 2 * 1024 * 1024  # 2 MB

# ---------------------------------------------------------------------------
# Template generation
# ---------------------------------------------------------------------------
TEMPLATE_SIZE   = 100_000           # 100 KB – long enough to avoid obvious repetition
WRITE_BUF_TILES = 640               # 640 × 100 KB = 64 MB write buffer


def _make_template(newline_ratio: float) -> bytes:
    """Return a TEMPLATE_SIZE-byte block with exactly the requested newline ratio."""
    n_nl    = round(TEMPLATE_SIZE * newline_ratio)
    n_other = TEMPLATE_SIZE - n_nl

    # Generate non-newline random bytes: strip any accidental 0x0A bytes.
    strip_nl = bytes.maketrans(b"\n", b"\x0b")
    raw = b""
    while len(raw) < n_other:
        raw += os.urandom(n_other - len(raw) + 32).translate(strip_nl)
    raw = raw[:n_other]

    block = bytearray(raw) + bytearray(b"\n" * n_nl)
    random.shuffle(block)   # Fisher-Yates in C; 100 K iterations is negligible
    return bytes(block)


# ---------------------------------------------------------------------------
# File generation
# ---------------------------------------------------------------------------
def generate_file(path: str, total_bytes: int, newline_ratio: float) -> int:
    """
    Write *total_bytes* of random-ish data to *path*, with approximately
    *newline_ratio* of bytes being newlines.

    Returns the exact number of newline bytes written.
    """
    template    = _make_template(newline_ratio)
    tmpl_nl     = template.count(b"\n")

    # Pre-build a 64 MB write buffer (C-level string multiplication – fast).
    write_buf    = template * WRITE_BUF_TILES
    write_buf_nl = tmpl_nl  * WRITE_BUF_TILES

    total_nl = 0
    remaining = total_bytes

    with open(path, "wb") as f:
        # Full 64 MB chunks
        while remaining >= len(write_buf):
            f.write(write_buf)
            total_nl  += write_buf_nl
            remaining -= len(write_buf)

        # Full template tiles in the tail
        full_tiles = remaining // TEMPLATE_SIZE
        if full_tiles:
            chunk = template * full_tiles
            f.write(chunk)
            total_nl  += tmpl_nl * full_tiles
            remaining -= TEMPLATE_SIZE * full_tiles

        # Sub-template leftover
        if remaining:
            tail = template[:remaining]
            f.write(tail)
            total_nl += tail.count(b"\n")

    return total_nl


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main() -> None:
    os.makedirs(TEST_DATA_DIR, exist_ok=True)

    results: list[tuple[str, int]] = []   # (relative_path, newline_count)

    # --- Big file ---
    big_abs  = os.path.join(TEST_DATA_DIR, "big.bin")
    big_rel  = os.path.relpath(big_abs, REPO_ROOT)
    print(f"Generating big file ({BIG_BYTES / 1e9:.1f} GB) …", flush=True)
    big_nl = generate_file(big_abs, BIG_BYTES, BIG_NL_RATIO)
    print(f"  {big_nl:,} newlines written to {big_rel}")
    results.append((big_rel, big_nl))

    # --- Small files ---
    print(f"\nGenerating {SMALL_COUNT} small files …", flush=True)
    for i in range(SMALL_COUNT):
        size    = random.randint(SMALL_MIN_BYTES, SMALL_MAX_BYTES)
        abs_path = os.path.join(TEST_DATA_DIR, f"small_{i:04d}.bin")
        rel_path = os.path.relpath(abs_path, REPO_ROOT)
        nl       = generate_file(abs_path, size, SMALL_NL_RATIO)
        results.append((rel_path, nl))
        if (i + 1) % 50 == 0 or (i + 1) == SMALL_COUNT:
            print(f"  {i + 1}/{SMALL_COUNT}", flush=True)

    # --- CSV manifest ---
    with open(CSV_OUTPUT, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["file", "newlines"])
        for rel_path, nl_count in results:
            writer.writerow([rel_path, nl_count])

    csv_rel = os.path.relpath(CSV_OUTPUT, REPO_ROOT)
    print(f"\nManifest written to {csv_rel}  ({len(results)} files)")


if __name__ == "__main__":
    sys.exit(main())
