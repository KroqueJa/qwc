#!/usr/bin/env python3
"""
Generate benchmark test data according to benchmarks/bench-spec.yml.

Outputs:
  benchmarks/test-data/big.bin          – one large file (~1 GB)
  benchmarks/test-data/medium_NNNN.bin  – medium files (20 MB – 100 MB each)
  benchmarks/test-data/small_NNNN.bin   – 200 small files (500 KB – 2 MB each)
  benchmarks/bench-spec.csv             – manifest with exact newline counts

Design notes
------------
Newlines are placed at uniformly-random positions across the *entire* file,
not by tiling a single shuffled template. The file is written in chunks to
bound memory; the number of newlines per chunk is drawn from a hypergeometric
distribution so that:

  * placement is uniform over the whole file (no 100 KB periodicity, so the
    high-ratio tiers stay genuinely branch-hostile for a scalar reference loop);
  * the random fill is regenerated per chunk (no repeated byte pattern at all);
  * the per-file newline total is EXACT by construction, so the CSV manifest
    is ground truth rather than an approximation.

Requires numpy. Pass --seed for a reproducible corpus.
"""

import argparse
import csv
import os
import sys

import numpy as np
import yaml

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
SCRIPT_DIR    = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT     = os.path.dirname(SCRIPT_DIR)
TEST_DATA_DIR = os.path.join(SCRIPT_DIR, "test-data")
CSV_OUTPUT    = os.path.join(SCRIPT_DIR, "bench-spec.csv")
SPEC_FILE     = os.path.join(SCRIPT_DIR, "bench-spec.yml")

# Chunk size for streaming a file to disk. 16 MiB keeps the transient
# index/permutation allocations modest while still amortizing write cost.
CHUNK_BYTES = 16 * 1024 * 1024

NEWLINE = 0x0A


# ---------------------------------------------------------------------------
# Chunk generation
# ---------------------------------------------------------------------------
def _fill_non_newline(rng: np.random.Generator, n: int) -> np.ndarray:
    """Return n uniform random bytes drawn from the 255 non-newline values.

    Generate 0..254 then shift everything >= 0x0A up by one, which maps the
    range onto 0..255 with 0x0A skipped — uniform over all non-newline bytes
    and incapable of producing a stray newline.
    """
    fill = rng.integers(0, 255, size=n, dtype=np.uint8)
    fill += (fill >= NEWLINE).astype(np.uint8)
    return fill


def generate_file(rng: np.random.Generator, path: str,
                  total_bytes: int, newline_ratio: float) -> int:
    """
    Write *total_bytes* of random data to *path* with exactly round(
    total_bytes * newline_ratio) newline bytes placed at uniformly random
    positions across the whole file.

    Returns the exact number of newline bytes written.
    """
    total_nl = round(total_bytes * newline_ratio)

    remaining_bytes = total_bytes      # positions not yet emitted
    remaining_nl    = total_nl         # newlines not yet emitted
    written_nl      = 0

    with open(path, "wb") as f:
        while remaining_bytes > 0:
            c = min(CHUNK_BYTES, remaining_bytes)

            # How many of this chunk's c positions are newlines, given a
            # uniform random placement of remaining_nl newlines among the
            # remaining_bytes positions: multivariate-hypergeometric, sampled
            # sequentially so the totals close exactly.
            ngood = remaining_nl
            nbad  = remaining_bytes - remaining_nl
            if ngood == 0:
                k = 0
            elif nbad == 0:
                k = c
            else:
                k = int(rng.hypergeometric(ngood, nbad, c))

            chunk = _fill_non_newline(rng, c)
            if k:
                idx = rng.choice(c, size=k, replace=False)
                chunk[idx] = NEWLINE

            f.write(chunk.tobytes())

            written_nl      += k
            remaining_nl    -= k
            remaining_bytes -= c

    assert written_nl == total_nl and remaining_nl == 0, "newline accounting drift"
    return written_nl


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main() -> None:
    parser = argparse.ArgumentParser(description="Generate benchmark test data.")
    parser.add_argument("--seed", type=int, default=None,
                        help="Seed for a reproducible corpus (default: random).")
    args = parser.parse_args()

    rng = np.random.default_rng(args.seed)
    if args.seed is not None:
        print(f"Using seed {args.seed}")

    with open(SPEC_FILE) as f:
        spec = yaml.safe_load(f)

    os.makedirs(TEST_DATA_DIR, exist_ok=True)

    results: list[tuple[str, int]] = []

    # --- Big file ---
    big_spec = spec["big"]
    big_abs  = os.path.join(TEST_DATA_DIR, "big.bin")
    big_rel  = os.path.relpath(big_abs, REPO_ROOT)
    print(f"Generating big file ({big_spec['bytes'] / 1e9:.1f} GB) …", flush=True)
    big_nl = generate_file(rng, big_abs, big_spec["bytes"], big_spec["ratio-newlines"])
    print(f"  {big_nl:,} newlines written to {big_rel}")
    results.append((big_rel, big_nl))

    # --- Medium files ---
    med_spec  = spec["medium"]
    med_count = med_spec["number-of-files"]
    print(f"\nGenerating {med_count} medium files …", flush=True)
    for i in range(med_count):
        size     = int(rng.integers(med_spec["min-bytes"], med_spec["max-bytes"] + 1))
        abs_path = os.path.join(TEST_DATA_DIR, f"medium_{i:04d}.bin")
        rel_path = os.path.relpath(abs_path, REPO_ROOT)
        nl       = generate_file(rng, abs_path, size, med_spec["ratio-newlines"])
        results.append((rel_path, nl))
        if (i + 1) % 5 == 0 or (i + 1) == med_count:
            print(f"  {i + 1}/{med_count}", flush=True)

    # --- Small files ---
    sml_spec  = spec["small"]
    sml_count = sml_spec["number-of-files"]
    print(f"\nGenerating {sml_count} small files …", flush=True)
    for i in range(sml_count):
        size     = int(rng.integers(sml_spec["min-bytes"], sml_spec["max-bytes"] + 1))
        abs_path = os.path.join(TEST_DATA_DIR, f"small_{i:04d}.bin")
        rel_path = os.path.relpath(abs_path, REPO_ROOT)
        nl       = generate_file(rng, abs_path, size, sml_spec["ratio-newlines"])
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