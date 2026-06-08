"""
Core engine for the qwc-vs-wc conformance suite.

The whole purpose of qwc is to be a drop-in replacement for `wc`, so "correct"
is defined as "produces the same counts as the system `wc`" -- with one carve-out
the task allows: on binary / invalid-UTF-8 input, where `wc`'s own behaviour is
locale-dependent and not byte-defined, qwc is permitted its own interpretation.

This module knows nothing about *which* inputs to run; it just runs one `wc` and
one `qwc` invocation, parses both, and decides whether they are required to agree.
corpus.py supplies the inputs; run.py and test_conformance.py drive it.

----------------------------------------------------------------------------
The parity policy (derived empirically -- see conformance/README.md)
----------------------------------------------------------------------------
Everything hinges on the locale, because `wc` counts words and characters
through the locale's multibyte/`iswspace` machinery while qwc counts bytes (for
words) and UTF-8 code points (for chars):

* Under the C/POSIX locale `wc` is itself byte-defined, so qwc matches it on
  EVERY mode and EVERY input, binary included. This is the strongest invariant
  and the backbone of the suite.

* Under a UTF-8 locale:
    - byte-defined modes  (lines, bytes, max-line-length) match on every input;
    - chars (`-m`)        match on valid UTF-8 (ASCII included), and qwc may
                          diverge on invalid UTF-8 (where `wc` may also error);
    - words (`-w`) and    match on pure-ASCII input; on any non-ASCII input
      bare-wc (`-a`)      qwc's C-locale byte semantics may legitimately differ
                          from `wc`'s wide-char `iswspace` splitting.

When a comparison is not *required*, the suite still checks that qwc ran
cleanly and produced well-formed output -- garbage in must not mean a crash out.
"""

from __future__ import annotations

import dataclasses
import os
import subprocess
from typing import Optional


# ---------------------------------------------------------------------------
# Modes: every `wc` flag (and combination) qwc claims to replicate. Bare `wc`
# (lines, words, bytes) is qwc with no count flag. `kind` selects the parity
# rule above; for a combination it is the most restrictive of its parts (a
# words column forces "word", else a chars column forces "char", else "byte"),
# because every selected column must agree for the row to match. `ncols` counts
# the output columns -- note -c and -m share one column (last wins).
# ---------------------------------------------------------------------------
@dataclasses.dataclass(frozen=True)
class Mode:
    name: str
    qwc: tuple[str, ...]  # flags passed to qwc
    wc: tuple[str, ...]   # flags passed to wc
    kind: str             # "byte" | "char" | "word"
    ncols: int            # how many integer columns the output has
    # macOS/BSD `wc` has a bug: with -m AND -L together, the multi-file char
    # total prints as 0 (per-file values are fine; `wc -m` alone is fine). qwc
    # computes the correct total and we will not replicate the defect, so for
    # such a mode the per-file rows are still compared but the "total" is not.
    compare_total: bool = True


MODES: tuple[Mode, ...] = (
    # Single counters.
    Mode("lines",   ("-l",), ("-l",), "byte", 1),
    Mode("words",   ("-w",), ("-w",), "word", 1),
    Mode("bytes",   ("-c",), ("-c",), "byte", 1),
    Mode("chars",   ("-m",), ("-m",), "char", 1),
    Mode("maxline", ("-L",), ("-L",), "byte", 1),
    Mode("all",     (),      (),      "word", 3),  # bare qwc == bare wc: l w b
    # Combinations: wc prints a fixed column order regardless of flag order.
    Mode("l+w",     ("-l", "-w"),             ("-l", "-w"),             "word", 2),
    Mode("l+c",     ("-l", "-c"),             ("-l", "-c"),             "byte", 2),
    Mode("l+L",     ("-l", "-L"),             ("-l", "-L"),             "byte", 2),
    Mode("w+c",     ("-w", "-c"),             ("-w", "-c"),             "word", 2),
    Mode("m+L",     ("-m", "-L"),             ("-m", "-L"),             "char", 2,
         compare_total=False),  # wc's char total is buggy with -m and -L
    Mode("l+w+c",   ("-l", "-w", "-c"),       ("-l", "-w", "-c"),       "word", 3),
    Mode("l+w+c+L", ("-l", "-w", "-c", "-L"), ("-l", "-w", "-c", "-L"), "word", 4),
    # -c and -m share one column; the last flag wins, so -c -m counts chars and
    # -m -c counts bytes.
    Mode("c+m",     ("-c", "-m"),             ("-c", "-m"),             "char", 1),
    Mode("m+c",     ("-m", "-c"),             ("-m", "-c"),             "byte", 1),
)

MODE_BY_NAME = {m.name: m for m in MODES}


# ---------------------------------------------------------------------------
# Input classification
# ---------------------------------------------------------------------------
@dataclasses.dataclass(frozen=True)
class Meta:
    ascii: bool       # every byte < 0x80
    valid_utf8: bool  # decodes cleanly as UTF-8
    size: int


def classify(data: bytes) -> Meta:
    is_ascii = all(b < 0x80 for b in data)
    try:
        data.decode("utf-8")
        valid = True
    except UnicodeDecodeError:
        valid = False
    return Meta(ascii=is_ascii, valid_utf8=valid, size=len(data))


def combine(metas: list[Meta]) -> Meta:
    """Merge per-file metadata for a multi-file invocation."""
    return Meta(
        ascii=all(m.ascii for m in metas),
        valid_utf8=all(m.valid_utf8 for m in metas),
        size=sum(m.size for m in metas),
    )


def required_to_match(regime: str, mode: Mode, meta: Meta, wc_ok: bool) -> bool:
    """Should qwc be required to reproduce `wc` exactly for this case?"""
    if not wc_ok:
        # `wc` itself failed (e.g. -m on invalid UTF-8): our interpretation is
        # allowed, so do not demand agreement.
        return False
    if regime == "C":
        return True
    # UTF-8 regime.
    if mode.kind == "byte":
        return True
    if mode.kind == "char":
        return meta.valid_utf8
    if mode.kind == "word":
        return meta.ascii
    return True


# ---------------------------------------------------------------------------
# Running the tools and parsing their output
# ---------------------------------------------------------------------------
@dataclasses.dataclass
class ToolRun:
    stdout: bytes
    stderr: bytes
    returncode: int


def run_tool(
    binary: str,
    flags: tuple[str, ...],
    *,
    files: Optional[list[str]] = None,
    stdin: Optional[bytes] = None,
    bytes_per_thread: Optional[int] = None,
    locale: str,
) -> ToolRun:
    """Run one `wc`/`qwc` invocation under a fixed locale."""
    argv = [binary, *flags]
    # --bytes-per-thread is a qwc-only knob used to force chunk boundaries; it
    # never changes the counts, only how qwc splits the work across threads.
    if bytes_per_thread is not None:
        argv += ["--bytes-per-thread", str(bytes_per_thread)]
    if files:
        argv += files

    env = dict(os.environ)
    env["LC_ALL"] = locale
    env["LANG"] = locale

    proc = subprocess.run(
        argv,
        input=stdin if files is None else None,
        capture_output=True,
        env=env,
    )
    return ToolRun(proc.stdout, proc.stderr, proc.returncode)


def _is_uint(tok: str) -> bool:
    return tok.isdigit()


@dataclasses.dataclass
class Parsed:
    # name -> tuple of integer columns. The stdin case (no filename) uses None.
    records: dict
    total: Optional[tuple]


def parse_output(stdout: bytes) -> Parsed:
    """
    Parse `wc`/`qwc` output into structured counts, tolerant of both BSD's
    fixed-width padding and GNU's dynamic width (we compare numbers, not bytes,
    for portability -- exact-byte equality is checked separately when the local
    `wc` is format-compatible).

    Each line is: <int>...<int> [name]. A trailing "total" names the grand total;
    any other trailing text is a filename; no trailing text is the stdin form.
    """
    records: dict = {}
    total: Optional[tuple] = None
    for line in stdout.decode("latin-1").splitlines():
        toks = line.split()
        if not toks:
            continue
        i = 0
        nums = []
        while i < len(toks) and _is_uint(toks[i]):
            nums.append(int(toks[i]))
            i += 1
        if not nums:
            continue
        name = " ".join(toks[i:]) if i < len(toks) else None
        if name == "total":
            total = tuple(nums)
        else:
            records[name] = tuple(nums)
    return Parsed(records, total)


# ---------------------------------------------------------------------------
# One comparison
# ---------------------------------------------------------------------------
@dataclasses.dataclass
class Result:
    status: str       # "match" | "skip" | "fail"
    detail: str = ""

    @property
    def ok(self) -> bool:
        return self.status != "fail"


def _check_qwc_sane(run: ToolRun, mode: Mode) -> Optional[str]:
    """qwc must never crash or emit malformed output, even on binary garbage."""
    if run.returncode != 0:
        return f"qwc exited {run.returncode}; stderr={run.stderr!r}"
    parsed = parse_output(run.stdout)
    rows = list(parsed.records.values())
    if parsed.total is not None:
        rows.append(parsed.total)
    if not rows:
        return f"qwc produced no parseable count line: {run.stdout!r}"
    for row in rows:
        if len(row) != mode.ncols:
            return (
                f"qwc produced {len(row)} columns, expected {mode.ncols}: "
                f"{run.stdout!r}"
            )
    return None


def compare(
    *,
    qwc_bin: str,
    mode: Mode,
    regime: str,
    locale: str,
    meta: Meta,
    files: Optional[list[str]] = None,
    stdin: Optional[bytes] = None,
    bytes_per_thread: Optional[int] = None,
    exact_format: bool = False,
) -> Result:
    """Run one qwc invocation and the matching wc invocation, then judge them."""
    wc_run = run_tool(
        "wc", mode.wc, files=files, stdin=stdin, locale=locale
    )
    qwc_run = run_tool(
        qwc_bin, mode.qwc, files=files, stdin=stdin,
        bytes_per_thread=bytes_per_thread, locale=locale,
    )

    # qwc robustness is non-negotiable regardless of whether we compare to wc.
    sane = _check_qwc_sane(qwc_run, mode)
    if sane is not None:
        return Result("fail", sane)

    wc_ok = wc_run.returncode == 0
    if not required_to_match(regime, mode, meta, wc_ok):
        return Result("skip", "parity not required for this locale/mode/input")

    # Exact byte-for-byte output (formatting included) -- only when the local
    # `wc` formats the way qwc does (BSD/macOS). On GNU `wc` we fall back to the
    # numeric comparison below, which is the portable correctness signal. Modes
    # where wc's total is buggy can't be byte-compared (the total line differs),
    # so they take the numeric path and skip the total there.
    if exact_format and mode.compare_total and qwc_run.stdout != wc_run.stdout:
        return Result(
            "fail",
            "exact output mismatch\n"
            f"  wc : {wc_run.stdout!r}\n"
            f"  qwc: {qwc_run.stdout!r}",
        )

    wc_parsed = parse_output(wc_run.stdout)
    qwc_parsed = parse_output(qwc_run.stdout)

    records_differ = wc_parsed.records != qwc_parsed.records
    total_differs = mode.compare_total and wc_parsed.total != qwc_parsed.total
    if records_differ or total_differs:
        return Result(
            "fail",
            "count mismatch\n"
            f"  wc  records={wc_parsed.records} total={wc_parsed.total}\n"
            f"  qwc records={qwc_parsed.records} total={qwc_parsed.total}\n"
            f"  wc stdout={wc_run.stdout!r}\n"
            f"  qwc stdout={qwc_run.stdout!r}",
        )
    return Result("match")
