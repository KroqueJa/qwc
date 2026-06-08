#!/usr/bin/env python3
"""
Benchmark qwc vs wc -l on benchmarks/test-data/*.

Runs both commands against the same file list, times them, parses their
output, and verifies that every per-file count and the grand total agree.
Results are appended to benchmarks/benchmarks.log (pipe-separated).

Usage:
    python3 benchmarks/bench.py          # from the repo root
    cmake --build build --target bench   # equivalent shortcut
"""

import datetime
import glob
import os
import subprocess
import sys
import time

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
SCRIPT_DIR    = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT     = os.path.dirname(SCRIPT_DIR)
TEST_DATA_DIR = os.path.join(SCRIPT_DIR, "test-data")
QWC_BIN       = os.path.join(REPO_ROOT, "qwc")
LOG_FILE      = os.path.join(SCRIPT_DIR, "benchmarks.log")

LOG_HEADER = (
    "timestamp|commit_sha|file_count|total_bytes"
    "|wc_time_s|wc_mbs|qwc_time_s|qwc_mbs"
    "|total_lines|match|qwc_faster_pct"
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def find_test_files() -> list[str]:
    files = sorted(glob.glob(os.path.join(TEST_DATA_DIR, "*")))
    if not files:
        sys.exit(
            f"No files found in {TEST_DATA_DIR}\n"
            "Run 'cmake --build build --target gen-test-data' first."
        )
    return [os.path.abspath(f) for f in files]


def run_cmd(cmd: list[str]) -> str:
    """Run *cmd* once (unmeasured); return stdout. Exits on failure."""
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(
            f"Command failed: {' '.join(cmd)}\n"
            f"{result.stderr.strip()}"
        )
    return result.stdout


def run_timed(cmd: list[str]) -> tuple[str, float]:
    """Run *cmd*, return (stdout, wall-clock seconds)."""
    t0 = time.perf_counter()
    stdout = run_cmd(cmd)
    elapsed = time.perf_counter() - t0
    return stdout, elapsed


def git_sha() -> str:
    """Return the current HEAD commit SHA (short), or 'unknown'."""
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            capture_output=True, text=True, cwd=REPO_ROOT,
        )
        return result.stdout.strip() if result.returncode == 0 else "unknown"
    except FileNotFoundError:
        return "unknown"


def append_log(
    sha: str,
    file_count: int,
    total_bytes: int,
    wc_time: float,
    qwc_time: float,
    total_lines: int,
    match: bool,
) -> None:
    """Append one pipe-separated result line to LOG_FILE."""
    write_header = not os.path.exists(LOG_FILE)
    ts      = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    wc_mbs  = total_bytes / wc_time  / 1e6
    qwc_mbs = total_bytes / qwc_time / 1e6
    qwc_faster_pct = (wc_time - qwc_time) / wc_time * 100   # negative = qwc slower

    row = "|".join([
        ts,
        sha,
        str(file_count),
        str(total_bytes),
        f"{wc_time:.4f}",
        f"{wc_mbs:.1f}",
        f"{qwc_time:.4f}",
        f"{qwc_mbs:.1f}",
        str(total_lines),
        "yes" if match else "no",
        f"{qwc_faster_pct:.2f}",
    ])

    with open(LOG_FILE, "a") as f:
        if write_header:
            f.write(LOG_HEADER + "\n")
        f.write(row + "\n")

    print(f"\nAppended to {os.path.relpath(LOG_FILE, REPO_ROOT)}")


def parse_wc(stdout: str) -> tuple[dict[str, int], int]:
    """
    Parse `wc -l` output.

    Format (multiple files):
        <count> <path>
        ...
        <total> total

    Returns ({abspath: count}, total).
    """
    counts: dict[str, int] = {}
    total: int | None = None

    for line in stdout.splitlines():
        parts = line.split(None, 1)
        if len(parts) < 1:
            continue
        count = int(parts[0])
        if len(parts) == 1 or parts[1].strip() == "total":
            total = count
        else:
            counts[os.path.abspath(parts[1].strip())] = count

    # Single-file mode: wc -l prints just the count and path on one line,
    # no separate "total" line.
    if total is None:
        total = sum(counts.values())

    return counts, total


def parse_qwc(stdout: str) -> tuple[dict[str, int], int]:
    """
    Parse `qwc` output.

    Format (multiple files):
        <count> <path>
        ...
        <total>           ← bare number, no label

    Returns ({abspath: count}, total).
    """
    counts: dict[str, int] = {}
    total: int | None = None

    for line in stdout.splitlines():
        parts = line.split(None, 1)
        if len(parts) < 1:
            continue
        count = int(parts[0])
        if len(parts) == 1:
            total = count          # bare total line
        else:
            counts[os.path.abspath(parts[1].strip())] = count

    if total is None:
        total = sum(counts.values())

    return counts, total


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main() -> int:
    if not os.path.isfile(QWC_BIN):
        sys.exit(
            f"qwc binary not found at {QWC_BIN}\n"
            "Run 'cmake -S . -B build && cmake --build build' first."
        )

    files = find_test_files()
    total_bytes = sum(os.path.getsize(f) for f in files)
    print(
        f"Benchmarking on {len(files)} file(s) "
        f"({total_bytes / 1e9:.2f} GB total)\n"
    )

    sha = git_sha()
    print(f"Commit        : {sha}\n")

    # Warmup pass: prime the OS page cache and branch predictor so the
    # timed runs reflect steady-state throughput, not cold-start overhead.
    print("Warming up wc -l …", end="  ", flush=True)
    run_cmd(["wc", "-l"] + files)
    print("done")

    print("Warming up qwc   …", end="  ", flush=True)
    run_cmd([QWC_BIN, "-l"] + files)
    print("done\n")

    # Timed runs (both with warm cache). Bare qwc now prints lines, words and
    # bytes like wc, so benchmark line counting explicitly with `qwc -l`.
    print("Running wc -l …", end="  ", flush=True)
    wc_out,  wc_time  = run_timed(["wc", "-l"] + files)
    print(f"{wc_time:.3f} s")

    print("Running qwc   …", end="  ", flush=True)
    qwc_out, qwc_time = run_timed([QWC_BIN, "-l"] + files)
    print(f"{qwc_time:.3f} s")

    # ---------------------------------------------------------------------------
    # Parse
    # ---------------------------------------------------------------------------
    wc_counts,  wc_total  = parse_wc(wc_out)
    qwc_counts, qwc_total = parse_qwc(qwc_out)

    # ---------------------------------------------------------------------------
    # Verify totals
    # ---------------------------------------------------------------------------
    print()
    if qwc_total == wc_total:
        print(f"Grand total : {qwc_total:,} lines  [MATCH]")
    else:
        print(
            f"GRAND TOTAL MISMATCH\n"
            f"  wc -l : {wc_total:,}\n"
            f"  qwc   : {qwc_total:,}",
            file=sys.stderr,
        )
        append_log(
            sha=sha, file_count=len(files), total_bytes=total_bytes,
            wc_time=wc_time, qwc_time=qwc_time,
            total_lines=qwc_total, match=False,
        )
        return 1

    # ---------------------------------------------------------------------------
    # Verify per-file counts
    # ---------------------------------------------------------------------------
    mismatches: list[tuple[str, int | None, int | None]] = []
    for path in files:
        wc_n  = wc_counts.get(path)
        qwc_n = qwc_counts.get(path)
        if wc_n != qwc_n:
            mismatches.append((path, wc_n, qwc_n))

    if mismatches:
        print(f"\nPer-file mismatches ({len(mismatches)}):", file=sys.stderr)
        for path, wc_n, qwc_n in mismatches[:20]:
            print(
                f"  {os.path.basename(path)}: wc -l={wc_n}  qwc={qwc_n}",
                file=sys.stderr,
            )
        if len(mismatches) > 20:
            print(f"  … and {len(mismatches) - 20} more", file=sys.stderr)
        append_log(
            sha=sha, file_count=len(files), total_bytes=total_bytes,
            wc_time=wc_time, qwc_time=qwc_time,
            total_lines=qwc_total, match=False,
        )
        return 1

    print(f"Per-file    : all {len(files)} counts match  [MATCH]")

    # ---------------------------------------------------------------------------
    # Timing report
    # ---------------------------------------------------------------------------
    print()
    print(f"  {'command':<8}  {'time':>8}  {'MB/s':>8}")
    print(f"  {'-------':<8}  {'----':>8}  {'----':>8}")
    for label, t in [("wc -l", wc_time), ("qwc", qwc_time)]:
        mbs = total_bytes / t / 1e6
        print(f"  {label:<8}  {t:>7.3f}s  {mbs:>7.0f}")

    print()
    if qwc_time < wc_time:
        pct = (wc_time - qwc_time) / wc_time * 100
        print(f"qwc is {pct:.1f} % faster than wc -l")
    else:
        pct = (qwc_time - wc_time) / wc_time * 100
        print(f"qwc is {pct:.1f} % slower than wc -l")

    # ---------------------------------------------------------------------------
    # Append to log
    # ---------------------------------------------------------------------------
    append_log(
        sha=sha,
        file_count=len(files),
        total_bytes=total_bytes,
        wc_time=wc_time,
        qwc_time=qwc_time,
        total_lines=qwc_total,
        match=True,
    )

    return 0


if __name__ == "__main__":
    sys.exit(main())
