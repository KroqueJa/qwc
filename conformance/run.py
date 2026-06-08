#!/usr/bin/env python3
"""
Standalone conformance runner: checks that qwc reproduces the system `wc` across
a curated + fuzzed corpus, under both the C and a UTF-8 locale, in single-file,
multi-file and stdin forms, and with the thread-chunking knob cranked so the
parallel boundary-stitch paths are exercised against wc's ground truth.

Pure standard library -- no third-party deps -- so any CI can run it with just a
Python interpreter:

    python3 conformance/run.py                 # default (≈250 fuzz cases)
    python3 conformance/run.py --quick         # fast smoke run
    python3 conformance/run.py --fuzz 2000     # deep run
    QWC_BIN=./qwc python3 conformance/run.py   # explicit binary

Exits non-zero if any required wc/qwc comparison fails or qwc misbehaves.
A prebuilt `qwc` is required (cmake --build <dir> --target qwc).
"""

from __future__ import annotations

import argparse
import dataclasses
import os
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from typing import Iterator, Optional

import corpus
import wc_harness as H
from session import build_session, Session


# A bytes-per-thread of 1 forces qwc to split work into as many chunks as it has
# hardware threads, landing chunk boundaries inside words / lines / multibyte
# sequences -- the parallel stitch logic must still match wc. None == default.
BPT_STRESS = (None, 1)
BPT_DEFAULT_ONLY = (None,)

# Below this size, chunking is a no-op, so skip the stress variant.
MIN_SIZE_FOR_BPT_STRESS = 16


@dataclasses.dataclass
class Scenario:
    label: str
    mode: H.Mode
    regime: str
    locale: str
    meta: H.Meta
    files: Optional[list[str]]
    stdin: Optional[bytes]
    bpt: Optional[int]
    exact_format: bool


@dataclasses.dataclass
class Input:
    cid: str
    data: bytes
    path: str
    meta: H.Meta


def _materialize(inputs, tmpdir: str) -> list[Input]:
    out = []
    for i, (cid, data) in enumerate(inputs):
        path = os.path.join(tmpdir, f"in_{i:05d}_{_safe(cid)}")
        with open(path, "wb") as fh:
            fh.write(data)
        out.append(Input(cid, data, path, H.classify(data)))
    return out


def _safe(name: str) -> str:
    return "".join(c if c.isalnum() or c in "-._" else "_" for c in name)[:40]


def _multifile_groups(by_id: dict[str, Input]) -> list[tuple[str, list[Input]]]:
    """A few multi-file groupings to exercise per-file lines + the total line."""
    wanted = [
        ("mf-ascii-mix", ["words-multi-line", "many-short-lines", "single-char-nl"]),
        ("mf-diff-magnitude", ["long-line", "single-char-nl"]),
        ("mf-with-empty", ["empty", "words-one-line", "empty"]),
        ("mf-utf8-mix", ["utf8-accent", "utf8-cjk", "utf8-mixed"]),
        ("mf-maxline", ["long-line", "many-short-lines", "words-one-line"]),
    ]
    groups = []
    for gid, ids in wanted:
        members = [by_id[i] for i in ids if i in by_id]
        if len(members) >= 2:
            groups.append((gid, members))
    return groups


def scenarios(
    session: Session,
    single: list[Input],
    multi: list[tuple[str, list[Input]]],
    bpt_stress: bool,
    stdin_inputs: list[Input],
) -> Iterator[Scenario]:
    ef = session.exact_format
    for regime, locale in session.regimes():
        for mode in H.MODES:
            # Single file, on disk (with and without chunk-stress).
            for inp in single:
                variants = (
                    BPT_STRESS
                    if bpt_stress and inp.meta.size >= MIN_SIZE_FOR_BPT_STRESS
                    else BPT_DEFAULT_ONLY
                )
                for bpt in variants:
                    tag = "" if bpt is None else f" bpt={bpt}"
                    yield Scenario(
                        f"[{regime}] {mode.name} file {inp.cid}{tag}",
                        mode, regime, locale, inp.meta, [inp.path], None, bpt, ef,
                    )
            # stdin (a distinct qwc code path; bpt does not apply).
            for inp in stdin_inputs:
                yield Scenario(
                    f"[{regime}] {mode.name} stdin {inp.cid}",
                    mode, regime, locale, inp.meta, None, inp.data, None, ef,
                )
            # Multiple files: per-file lines + the grand total / max line.
            for gid, members in multi:
                meta = H.combine([m.meta for m in members])
                paths = [m.path for m in members]
                variants = BPT_STRESS if bpt_stress else BPT_DEFAULT_ONLY
                for bpt in variants:
                    tag = "" if bpt is None else f" bpt={bpt}"
                    yield Scenario(
                        f"[{regime}] {mode.name} multi {gid}{tag}",
                        mode, regime, locale, meta, paths, None, bpt, ef,
                    )


def run_one(session: Session, sc: Scenario) -> tuple[str, H.Result]:
    res = H.compare(
        qwc_bin=session.qwc_bin,
        mode=sc.mode,
        regime=sc.regime,
        locale=sc.locale,
        meta=sc.meta,
        files=sc.files,
        stdin=sc.stdin,
        bytes_per_thread=sc.bpt,
        exact_format=sc.exact_format,
    )
    return sc.label, res


def main() -> int:
    ap = argparse.ArgumentParser(description="qwc vs wc conformance suite")
    ap.add_argument("--fuzz", type=int,
                    default=int(os.environ.get("QWC_CONF_FUZZ", "250")),
                    help="number of fuzz inputs (default 250)")
    ap.add_argument("--seed", type=int,
                    default=int(os.environ.get("QWC_CONF_SEED", "1234")),
                    help="fuzz RNG seed (default 1234)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4,
                    help="parallel worker threads")
    ap.add_argument("--quick", action="store_true",
                    help="small/fast run (40 fuzz cases, no chunk-stress)")
    ap.add_argument("--no-bpt-stress", action="store_true",
                    help="skip the bytes-per-thread chunk-boundary stress variant")
    ap.add_argument("--max-failures", type=int, default=25,
                    help="how many failures to print in detail")
    args = ap.parse_args()

    session = build_session()
    fuzz_n = 40 if args.quick else args.fuzz
    bpt_stress = not (args.quick or args.no_bpt_stress)

    print("qwc conformance suite")
    print(f"  qwc binary    : {session.qwc_bin}")
    print(f"  C locale      : {session.c_locale}")
    print(f"  UTF-8 locale  : {session.utf8_locale or '(none available -- UTF-8 regime skipped)'}")
    print(f"  exact format  : {session.exact_format} "
          f"({'byte-for-byte vs wc' if session.exact_format else 'numeric compare (GNU wc?)'})")
    print(f"  fuzz cases    : {fuzz_n} (seed {args.seed})")
    print(f"  chunk-stress  : {bpt_stress}")
    print(f"  jobs          : {args.jobs}")
    print()

    with tempfile.TemporaryDirectory(prefix="qwc_conformance_") as tmp:
        inputs = corpus.curated() + corpus.fuzz(args.seed, fuzz_n)
        single = _materialize(inputs, tmp)
        by_id = {inp.cid: inp for inp in single}
        multi = _multifile_groups(by_id)
        # stdin: skip the multi-MB blobs to keep the pipe cheap; they are covered
        # in file mode (where chunking actually matters anyway).
        stdin_inputs = [i for i in single if i.meta.size <= 256 * 1024]

        all_scenarios = list(
            scenarios(session, single, multi, bpt_stress, stdin_inputs)
        )
        print(f"Running {len(all_scenarios):,} comparisons "
              f"({len(single)} inputs, {len(multi)} multi-file groups)...\n")

        matched = skipped = 0
        failures: list[tuple[str, H.Result]] = []

        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            for label, res in pool.map(
                lambda sc: run_one(session, sc), all_scenarios
            ):
                if res.status == "match":
                    matched += 1
                elif res.status == "skip":
                    skipped += 1
                else:
                    failures.append((label, res))

    print(f"  matched : {matched:,}")
    print(f"  skipped : {skipped:,}  (parity not required for that locale/mode/input)")
    print(f"  failed  : {len(failures):,}")

    if failures:
        print("\nFAILURES:")
        for label, res in failures[: args.max_failures]:
            print(f"\n• {label}\n  {res.detail}")
        if len(failures) > args.max_failures:
            print(f"\n... and {len(failures) - args.max_failures} more failures")
        return 1

    print("\nAll required wc/qwc comparisons agree. qwc is conformant. ✓")
    return 0


if __name__ == "__main__":
    # Allow `python3 conformance/run.py` from the repo root by making sibling
    # modules importable regardless of the working directory.
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    sys.exit(main())
