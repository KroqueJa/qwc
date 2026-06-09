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
qwc is held to `wc` only where the count is *universal* -- the same for every
`wc` on every platform. The `wc` implementations themselves disagree once the
answer depends on the C library's locale tables: BSD `wc` (macOS), GNU `wc`
(Linux/glibc) and even GNU `wc` built against macOS libc all split non-ASCII
words and measure `-L` differently. qwc cannot (and should not) match all of
them, so for those cases it uses its own fast, locale-independent rules and the
suite does not demand agreement. The rule below is a single set that passes
against whatever `wc` is local -- BSD on a Mac, GNU in CI:

* lines (`-l`) and bytes (`-c`): pure byte counts, required on every input.
* chars (`-m`): bytes under the C locale (required everywhere); UTF-8 code
  points under a UTF-8 locale (required on valid UTF-8, allowed to differ on
  invalid input, where `wc` may also error).
* words (`-w`) and bare `wc`: required on ASCII input only. Non-ASCII word
  splitting is libc-defined and differs across every `wc`.
* longest line (`-L`): GNU `wc` expands tabs and uses display width while qwc
  and BSD `wc` count bytes, and GNU counts an unterminated final line the others
  ignore. So a `-L` column is required only on printable-ASCII, newline-
  terminated input (no tabs, control or high bytes, ending in '\n').

Counts are compared as parsed integers, so differing field widths between GNU
and BSD `wc` are irrelevant; column order, selection and the `total` row are
still enforced.

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
# the output columns.
# ---------------------------------------------------------------------------
@dataclasses.dataclass(frozen=True)
class Mode:
    name: str
    qwc: tuple[str, ...]  # flags passed to qwc
    wc: tuple[str, ...]   # flags passed to wc
    kind: str             # "byte" | "char" | "word"
    ncols: int            # how many integer columns the output has
    # True when the mode includes -L. GNU `wc` measures the longest line as a
    # display width (expanding tabs, applying wcwidth), whereas qwc and BSD `wc`
    # count raw bytes, so a -L column is only universally comparable on printable
    # ASCII input (see required_to_match).
    has_maxline: bool = False
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
    Mode("maxline", ("-L",), ("-L",), "byte", 1, has_maxline=True),
    Mode("all",     (),      (),      "word", 3),  # bare qwc == bare wc: l w b
    # Combinations: wc prints a fixed column order regardless of flag order.
    Mode("l+w",     ("-l", "-w"),             ("-l", "-w"),             "word", 2),
    Mode("l+c",     ("-l", "-c"),             ("-l", "-c"),             "byte", 2),
    Mode("l+L",     ("-l", "-L"),             ("-l", "-L"),             "byte", 2,
         has_maxline=True),
    Mode("w+c",     ("-w", "-c"),             ("-w", "-c"),             "word", 2),
    Mode("m+L",     ("-m", "-L"),             ("-m", "-L"),             "char", 2,
         has_maxline=True, compare_total=False),  # char total buggy with -m -L
    Mode("l+w+c",   ("-l", "-w", "-c"),       ("-l", "-w", "-c"),       "word", 3),
    Mode("l+w+c+L", ("-l", "-w", "-c", "-L"), ("-l", "-w", "-c", "-L"), "word", 4,
         has_maxline=True),
    # NB: -cm/-mc are intentionally absent. BSD `wc` (which qwc targets) collapses
    # -c and -m into one shared char/byte column on a last-flag-wins basis, while
    # GNU `wc` prints both a byte and a char column. That is a divergence in
    # column *count*, not formatting, so no single binary matches both `wc`s; the
    # differential harness cannot include them. qwc's single-column choice is
    # pinned directly by the CliCombined tests under tests/.
)

MODE_BY_NAME = {m.name: m for m in MODES}


# ---------------------------------------------------------------------------
# Input classification
# ---------------------------------------------------------------------------
@dataclasses.dataclass(frozen=True)
class Meta:
    ascii: bool          # every byte < 0x80
    valid_utf8: bool     # decodes cleanly as UTF-8
    ascii_print: bool    # only newlines + printable ASCII (0x20-0x7E); no tabs/ctrl
    nl_terminated: bool  # empty, or ends in '\n' (no unterminated final line)
    size: int


def classify(data: bytes) -> Meta:
    is_ascii = all(b < 0x80 for b in data)
    # Newlines (line separators) plus printable ASCII only -- the inputs on which
    # `-L` is universal. Tabs and other control bytes make GNU `wc` (display width
    # with tab expansion) diverge from qwc/BSD `wc` (raw byte count).
    is_ascii_print = all(b == 0x0A or 0x20 <= b <= 0x7E for b in data)
    # GNU `wc` counts an unterminated final line toward `-L`; BSD `wc` and qwc do
    # not. They agree only when every line ends in '\n'.
    nl_term = len(data) == 0 or data.endswith(b"\n")
    try:
        data.decode("utf-8")
        valid = True
    except UnicodeDecodeError:
        valid = False
    return Meta(
        ascii=is_ascii, valid_utf8=valid, ascii_print=is_ascii_print,
        nl_terminated=nl_term, size=len(data),
    )


def combine(metas: list[Meta]) -> Meta:
    """Merge per-file metadata for a multi-file invocation."""
    return Meta(
        ascii=all(m.ascii for m in metas),
        valid_utf8=all(m.valid_utf8 for m in metas),
        ascii_print=all(m.ascii_print for m in metas),
        nl_terminated=all(m.nl_terminated for m in metas),
        size=sum(m.size for m in metas),
    )


def required_to_match(regime: str, mode: Mode, meta: Meta, wc_ok: bool) -> bool:
    """Should qwc be required to reproduce `wc` exactly for this case?

    Parity is demanded only where the count is *universal* -- identical for every
    `wc` on every platform. Where the result depends on the C library's locale
    tables (word splitting, display width) or on undefined input, the various
    `wc`s disagree among themselves, so qwc's own fast, locale-independent answer
    is allowed to differ. The single rule set below holds against BSD `wc` (macOS)
    and GNU `wc` (Linux) alike, which is what lets the suite pass on both.
    """
    if not wc_ok:
        # `wc` itself failed (e.g. -m on invalid UTF-8): our interpretation is
        # allowed, so do not demand agreement.
        return False
    # -L (longest line): GNU `wc` expands tabs and measures display width (so it
    # only agrees with qwc/BSD's byte count on printable ASCII), and it counts an
    # unterminated final line that qwc/BSD ignore (so the input must end in '\n').
    if mode.has_maxline and not (meta.ascii_print and meta.nl_terminated):
        return False
    # Word splitting: every `wc` defers to libc whitespace classification for
    # non-ASCII bytes, and glibc, macOS libc and qwc all disagree there. Require
    # parity only on ASCII input.
    if mode.kind == "word" and not meta.ascii:
        return False
    # Characters (-m): bytes under the C locale (universal); code points under a
    # UTF-8 locale, which agree only on well-formed UTF-8.
    if mode.kind == "char" and regime != "C" and not meta.valid_utf8:
        return False
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
) -> Result:
    """Run one qwc invocation and the matching wc invocation, then judge them.

    Counts are compared numerically (parsed integers), so qwc's field width need
    not match the local `wc`'s -- GNU and BSD `wc` pad differently and downstream
    tools split on whitespace anyway. Column order, selection and the "total" row
    are still enforced via parse_output.
    """
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
