#!/usr/bin/env python3
"""
Generate benchmark test data according to benchmarks/bench-spec.yml.

Outputs:
  benchmarks/test-data/big.bin          – one large file (~1 GB)
  benchmarks/test-data/medium_NNNN.bin  – medium files (20 MB – 100 MB each)
  benchmarks/test-data/small_NNNN.bin   – 200 small files (500 KB – 2 MB each)
  benchmarks/bench-spec.csv             – manifest with exact newline counts
"""

import csv
import os
import random
import sys
import yaml

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
SCRIPT_DIR    = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT     = os.path.dirname(SCRIPT_DIR)
TEST_DATA_DIR = os.path.join(SCRIPT_DIR, "test-data")
CSV_OUTPUT    = os.path.join(SCRIPT_DIR, "bench-spec.csv")
SPEC_FILE     = os.path.join(SCRIPT_DIR, "bench-spec.yml")

# ---------------------------------------------------------------------------
# Template generation
# ---------------------------------------------------------------------------
TEMPLATE_SIZE   = 100_000
WRITE_BUF_TILES = 640


def _make_template(newline_ratio: float) -> bytes:
    """Return a TEMPLATE_SIZE-byte block with exactly the requested newline ratio."""
    n_nl    = round(TEMPLATE_SIZE * newline_ratio)
    n_other = TEMPLATE_SIZE - n_nl

    strip_nl = bytes.maketrans(b"\n", b"\x0b")
    raw = b""
    while len(raw) < n_other:
        raw += os.urandom(n_other - len(raw) + 32).translate(strip_nl)
    raw = raw[:n_other]

    block = bytearray(raw) + bytearray(b"\n" * n_nl)
    random.shuffle(block)
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
    template     = _make_template(newline_ratio)
    tmpl_nl      = template.count(b"\n")

    write_buf    = template * WRITE_BUF_TILES
    write_buf_nl = tmpl_nl  * WRITE_BUF_TILES

    total_nl  = 0
    remaining = total_bytes

    with open(path, "wb") as f:
        while remaining >= len(write_buf):
            f.write(write_buf)
            total_nl  += write_buf_nl
            remaining -= len(write_buf)

        full_tiles = remaining // TEMPLATE_SIZE
        if full_tiles:
            chunk = template * full_tiles
            f.write(chunk)
            total_nl  += tmpl_nl * full_tiles
            remaining -= TEMPLATE_SIZE * full_tiles

        if remaining:
            tail = template[:remaining]
            f.write(tail)
            total_nl += tail.count(b"\n")

    return total_nl


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main() -> None:
    with open(SPEC_FILE) as f:
        spec = yaml.safe_load(f)

    os.makedirs(TEST_DATA_DIR, exist_ok=True)

    results: list[tuple[str, int]] = []

    # --- Big file ---
    big_spec = spec["big"]
    big_abs  = os.path.join(TEST_DATA_DIR, "big.bin")
    big_rel  = os.path.relpath(big_abs, REPO_ROOT)
    print(f"Generating big file ({big_spec['bytes'] / 1e9:.1f} GB) …", flush=True)
    big_nl = generate_file(big_abs, big_spec["bytes"], big_spec["ratio-newlines"])
    print(f"  {big_nl:,} newlines written to {big_rel}")
    results.append((big_rel, big_nl))

    # --- Medium files ---
    med_spec  = spec["medium"]
    med_count = med_spec["number-of-files"]
    print(f"\nGenerating {med_count} medium files …", flush=True)
    for i in range(med_count):
        size     = random.randint(med_spec["min-bytes"], med_spec["max-bytes"])
        abs_path = os.path.join(TEST_DATA_DIR, f"medium_{i:04d}.bin")
        rel_path = os.path.relpath(abs_path, REPO_ROOT)
        nl       = generate_file(abs_path, size, med_spec["ratio-newlines"])
        results.append((rel_path, nl))
        if (i + 1) % 5 == 0 or (i + 1) == med_count:
            print(f"  {i + 1}/{med_count}", flush=True)

    # --- Small files ---
    sml_spec  = spec["small"]
    sml_count = sml_spec["number-of-files"]
    print(f"\nGenerating {sml_count} small files …", flush=True)
    for i in range(sml_count):
        size     = random.randint(sml_spec["min-bytes"], sml_spec["max-bytes"])
        abs_path = os.path.join(TEST_DATA_DIR, f"small_{i:04d}.bin")
        rel_path = os.path.relpath(abs_path, REPO_ROOT)
        nl       = generate_file(abs_path, size, sml_spec["ratio-newlines"])
        results.append((rel_path, nl))
        if (i + 1) % 50 == 0 or (i + 1) == sml_count:
            print(f"  {i + 1}/{sml_count}", flush=True)

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