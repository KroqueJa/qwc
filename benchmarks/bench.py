#!/usr/bin/env python3
"""qwc benchmark orchestrator.

Runs hyperfine across a matrix of wc flags, comparing the branch qwc against:
  - qwc @ main   (the "did this branch improve?" signal; optional)
  - uu-wc        (uutils coreutils, the closest competitor)
  - GNU wc       (the headline number)

Renders a markdown table of mean times plus relative-speedup columns. Prints to
stdout and, when running under GitHub Actions, appends to $GITHUB_STEP_SUMMARY.

Speed only -- correctness/parity with wc is the conformance suite's job (and
qwc's -L intentionally diverges from GNU wc on wide characters).
"""
import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile

# Default flag matrix. '' is the bare invocation (wc prints lines/words/bytes).
# -c is kept for completeness; all three tools take the fstat fast path for it,
# so it mostly measures process startup.
DEFAULT_FLAGS = ["", "-l", "-w", "-c", "-m", "-L", "-L -m"]


def have(cmd: str) -> bool:
    """Is the first token of a command string an executable on PATH?"""
    if not cmd:
        return False
    return shutil.which(shlex.split(cmd)[0]) is not None


def run_hyperfine(commands, warmup: int, runs: int) -> list:
    """Run one hyperfine invocation over `commands` (list of (label, cmdline));
    return the per-command mean times in seconds, in the same order."""
    with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tf:
        json_path = tf.name
    # --shell=none: hyperfine parses the command itself and execs directly, so
    # there is no per-run shell startup polluting the (often sub-10ms) timings.
    # Every command prefix here is a real executable, so this is safe.
    argv = ["hyperfine", "--shell=none", "--warmup", str(warmup),
            "--runs", str(runs), "--export-json", json_path]
    for label, cmdline in commands:
        argv += ["-n", label, cmdline]
    subprocess.run(argv, check=True, stdout=subprocess.DEVNULL)
    with open(json_path) as f:
        data = json.load(f)
    os.unlink(json_path)
    return [r["mean"] for r in data["results"]]


def fmt_ratio(slower: float, faster: float) -> str:
    """Speedup of `faster` relative to `slower` (>1 means faster is quicker)."""
    if faster <= 0:
        return "-"
    return f"{slower / faster:.2f}x"


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--qwc", required=True, help="branch qwc binary")
    ap.add_argument("--qwc-main", help="main qwc binary (omit when running on main)")
    ap.add_argument("--uuwc", default="coreutils wc", help="uutils invocation")
    ap.add_argument("--gwc", default="wc", help="GNU wc invocation")
    ap.add_argument("--data", required=True, help="corpus file to count")
    ap.add_argument("--warmup", type=int, default=3)
    ap.add_argument("--runs", type=int, default=10)
    ap.add_argument("--flags", help="comma-separated flag sets (overrides default)")
    ap.add_argument("--json-out", help="also write {flag: {tool: mean_sec}} JSON here")
    args = ap.parse_args()

    flags = ([f.strip() for f in args.flags.split(",")] if args.flags
             else DEFAULT_FLAGS)
    data_q = shlex.quote(args.data)

    # Build the column set, skipping any tool that isn't available.
    columns = [("qwc", args.qwc)]
    if args.qwc_main:
        columns.append(("main", args.qwc_main))
    if have(args.uuwc):
        columns.append(("uu-wc", args.uuwc))
    else:
        print(f"note: '{args.uuwc}' not found; skipping uu-wc column",
              file=sys.stderr)
    if have(args.gwc):
        columns.append(("GNU wc", args.gwc))
    else:
        print(f"note: '{args.gwc}' not found; skipping GNU wc column",
              file=sys.stderr)

    headers = [c[0] for c in columns]
    rows = []  # each: {header: mean_seconds}
    for flag in flags:
        commands = []
        for header, prefix in columns:
            cmdline = f"{prefix} {flag} {data_q}".replace("  ", " ").strip()
            commands.append((header, cmdline))
        means = run_hyperfine(commands, args.warmup, args.runs)
        rows.append(dict(zip(headers, means)))

    if args.json_out:
        payload = {(flag or "(default)"): row for flag, row in zip(flags, rows)}
        with open(args.json_out, "w") as f:
            json.dump(payload, f, indent=2)

    render(flags, headers, rows)


def render(flags, headers, rows) -> None:
    has_main = "main" in headers
    has_uu = "uu-wc" in headers
    has_gwc = "GNU wc" in headers

    cols = ["flag"] + [f"{h} (ms)" for h in headers]
    if has_main:
        cols.append("vs main")
    if has_uu:
        cols.append("vs uu-wc")
    if has_gwc:
        cols.append("vs GNU wc")

    lines = ["| " + " | ".join(cols) + " |",
             "|" + "|".join(["---"] * len(cols)) + "|"]
    for flag, row in zip(flags, rows):
        label = flag if flag else "(default)"
        cells = [label] + [f"{row[h] * 1000:.1f}" for h in headers]
        qwc = row["qwc"]
        if has_main:
            cells.append(fmt_ratio(row["main"], qwc))
        if has_uu:
            cells.append(fmt_ratio(row["uu-wc"], qwc))
        if has_gwc:
            cells.append(fmt_ratio(row["GNU wc"], qwc))
        lines.append("| " + " | ".join(cells) + " |")

    table = "\n".join(lines)
    print(table)

    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a") as f:
            f.write("## qwc benchmark\n\n")
            f.write("Speedup columns are relative to qwc (>1.00x means qwc is "
                    "faster). Measured with hyperfine; runner noise applies.\n\n")
            f.write(table + "\n")


if __name__ == "__main__":
    main()
