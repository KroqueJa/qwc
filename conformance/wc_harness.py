"""
Core engine for the qwc-vs-wc conformance suite.

qwc aims to be a drop-in replacement for GNU `wc` -- the `wc` on Linux, in CI and
in virtually every container -- so "correct" is defined as "produces the same
counts as GNU `wc`" on real text. The carve-out is deliberately narrow: on binary
/ invalid-UTF-8 input, where word splitting and `-L` stop being byte-defined and
`wc`'s answer depends on the C library's locale tables, qwc is permitted its own
fast, locale-independent interpretation and the suite does not demand agreement
(it still requires qwc not to crash -- see _check_qwc_sane).

This module knows nothing about *which* inputs to run; it just runs one `wc` and
one `qwc` invocation, parses both, and decides whether they are required to agree.
corpus.py supplies the inputs; run.py and test_conformance.py drive it.

----------------------------------------------------------------------------
The parity policy (see conformance/README.md)
----------------------------------------------------------------------------
qwc is held to GNU `wc` on the inputs people actually count -- ASCII and valid
UTF-8 text -- and excused on binary trash, where GNU's libc-driven classification
and qwc's locale-independent rule legitimately differ:

* lines (`-l`) and bytes (`-c`): pure byte counts, required on every input.
* chars (`-m`): bytes under the C locale (required everywhere); UTF-8 code
  points under a UTF-8 locale (required on valid UTF-8, allowed to differ on
  invalid input, where `wc` may also error).
* words (`-w`) and bare `wc`: required on printable-ASCII *text* only (printable
  bytes plus ASCII whitespace). On ASCII control bytes (NUL, 0x01-0x1F, 0x7F)
  and on non-ASCII bytes, word splitting is libc-defined and qwc may differ.
* longest line (`-L`): GNU `wc` expands tabs and uses display width while qwc
  counts bytes/characters, and GNU counts an unterminated final line qwc ignores.
  So a `-L` column is required only on printable-ASCII, newline-terminated input
  (no tabs, control or high bytes, ending in '\n'). Matching GNU's display-width
  `-L` is a tracked follow-up, not required here.

Counts are compared as parsed integers, so qwc's fixed-width padding vs GNU's
dynamic width is irrelevant; column order, selection and the `total` row are
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
    # display width (expanding tabs, applying wcwidth), whereas qwc counts raw
    # bytes/characters, so a -L column is only comparable on printable ASCII
    # input (see required_to_match).
    has_maxline: bool = False


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
         has_maxline=True),
    Mode("l+w+c",   ("-l", "-w", "-c"),       ("-l", "-w", "-c"),       "word", 3),
    Mode("l+w+c+L", ("-l", "-w", "-c", "-L"), ("-l", "-w", "-c", "-L"), "word", 4,
         has_maxline=True),
    # -c and -m together: GNU wc prints both columns -- chars first, then
    # bytes -- regardless of flag order. Both orders are exercised to pin the
    # fixed column order; the five-column form covers the full matrix.
    Mode("c+m",     ("-c", "-m"),             ("-c", "-m"),             "char", 2),
    Mode("m+c",     ("-m", "-c"),             ("-m", "-c"),             "char", 2),
    Mode("l+w+m+c+L",
         ("-l", "-w", "-m", "-c", "-L"),
         ("-l", "-w", "-m", "-c", "-L"),
         "word", 5, has_maxline=True),
)

MODE_BY_NAME = {m.name: m for m in MODES}


# ---------------------------------------------------------------------------
# Input classification
# ---------------------------------------------------------------------------
@dataclasses.dataclass(frozen=True)
class Meta:
    ascii: bool          # every byte < 0x80
    valid_utf8: bool     # decodes cleanly as UTF-8
    ascii_text: bool     # printable ASCII (0x20-0x7E) + ASCII whitespace (\t\n\v\f\r)
    ascii_print: bool    # only newlines + printable ASCII (0x20-0x7E); no tabs/ctrl
    nl_terminated: bool  # empty, or ends in '\n' (no unterminated final line)
    size: int


def classify(data: bytes) -> Meta:
    is_ascii = all(b < 0x80 for b in data)
    # Printable ASCII plus the ASCII whitespace bytes (\t\n\v\f\r): the input on
    # which word splitting is universal. GNU `wc` and qwc agree on every printable
    # byte and every ASCII whitespace byte; they diverge only on ASCII *control*
    # bytes (NUL, 0x01-0x08, 0x0E-0x1F, 0x7F), i.e. on binary trash that happens
    # to be < 0x80.
    is_ascii_text = all((0x09 <= b <= 0x0D) or (0x20 <= b <= 0x7E) for b in data)
    # Newlines (line separators) plus printable ASCII only -- the inputs on which
    # `-L` is universal. Tabs and other control bytes make GNU `wc` (display width
    # with tab expansion) diverge from qwc (raw byte count).
    is_ascii_print = all(b == 0x0A or 0x20 <= b <= 0x7E for b in data)
    # GNU `wc` counts an unterminated final line toward `-L`; qwc does not. They
    # agree only when every line ends in '\n'.
    nl_term = len(data) == 0 or data.endswith(b"\n")
    try:
        data.decode("utf-8")
        valid = True
    except UnicodeDecodeError:
        valid = False
    return Meta(
        ascii=is_ascii, valid_utf8=valid, ascii_text=is_ascii_text,
        ascii_print=is_ascii_print, nl_terminated=nl_term, size=len(data),
    )


def combine(metas: list[Meta]) -> Meta:
    """Merge per-file metadata for a multi-file invocation."""
    return Meta(
        ascii=all(m.ascii for m in metas),
        valid_utf8=all(m.valid_utf8 for m in metas),
        ascii_text=all(m.ascii_text for m in metas),
        ascii_print=all(m.ascii_print for m in metas),
        nl_terminated=all(m.nl_terminated for m in metas),
        size=sum(m.size for m in metas),
    )


def required_to_match(regime: str, mode: Mode, meta: Meta, wc_ok: bool) -> bool:
    """Should qwc be required to reproduce GNU `wc` exactly for this case?

    Parity is demanded on the inputs people actually count -- ASCII and valid
    UTF-8 text. Where the result depends on the C library's locale tables (word
    splitting, display width) or on binary / invalid input, qwc's own fast,
    locale-independent answer is allowed to differ; the suite then only requires
    that qwc not crash and emit well-formed output (see _check_qwc_sane).
    """
    if not wc_ok:
        # `wc` itself failed (e.g. -m on invalid UTF-8): our interpretation is
        # allowed, so do not demand agreement.
        return False
    # -L (longest line): GNU `wc` expands tabs and measures display width (so it
    # only agrees with qwc's byte/char count on printable ASCII), and it counts an
    # unterminated final line that qwc ignores (so the input must end in '\n').
    if mode.has_maxline and not (meta.ascii_print and meta.nl_terminated):
        return False
    # Word splitting: qwc applies a printability filter -- a run containing no
    # printable code point is "barren" and not counted (see runHasPrintable in
    # include/words_kernel.h and the asserts in tests/words_test.cpp). wc has
    # no analogous filter: it splits purely on (isspace / iswspace), so any
    # non-separator run is a word. The two therefore agree only on inputs that
    # never form barren runs -- printable-ASCII text. The earlier (4190263)
    # claim that "classification is purely bytewise, so parity holds on ANY
    # input" was incorrect in both supported regimes; reverted to the
    # pre-4190263 policy.
    # TODO (recorded in TODO.md): decide whether to keep qwc's barren-run
    # filter (status quo: deliberate divergence, documented) or drop it to
    # match wc bytewise. If dropped, this restriction can be tightened again.
    if mode.kind == "word" and not meta.ascii_text:
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
    Parse `wc`/`qwc` output into structured counts, tolerant of both qwc's
    fixed-width padding and GNU `wc`'s dynamic width (we compare numbers, not
    bytes: qwc's exact BSD-style column layout is pinned by the C++
    tests/cli_test.cpp instead).

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
    not match GNU `wc`'s -- qwc pads to fixed BSD-style columns, GNU pads
    dynamically, and downstream tools split on whitespace anyway. Column order,
    selection and the "total" row are still enforced via parse_output.
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
    total_differs = wc_parsed.total != qwc_parsed.total
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
