# qwc conformance suite

qwc exists to be a faster, drop-in replacement for **GNU `wc`** — the `wc` on
Linux, in CI, and in virtually every container, and the one nearly every script
and `awk` pipeline was written against. A replacement is only worth anything if
you can swap it in without noticing, so the bar this suite holds qwc to is
simple: **for every input and every shared mode, qwc must produce what GNU `wc`
produces — on real text.** These tests are the executable definition of that
promise.

## Conformance goals

Concretely, the suite is built to guarantee the following.

1. **Output-for-output parity with GNU `wc`.** Not "morally equivalent", not "the
   same number somewhere in there" — the same answer, in every mode qwc shares
   with `wc`, across a broad corpus. GNU `wc` is the oracle; we never hard-code
   our own expected counts, because the goal is to track `wc`'s behaviour rather
   than our belief about it.

2. **Drop-in fidelity of the columns.** A replacement that prints the right
   numbers in the wrong layout still breaks scripts that parse columns. The suite
   enforces the same column *selection*, *order*, per-file lines and `total` row
   as `wc`, comparing the counts as parsed integers. It deliberately does *not*
   require byte-identical padding: qwc emits fixed-width BSD-style columns while
   GNU `wc` pads dynamically, and the field-splitting tools people pipe `wc` into
   never see the difference. (qwc's exact column layout is pinned by the C++
   `tests/cli_test.cpp`, not here.)

3. **Comprehensiveness.** Replacing `wc` is the whole game, so coverage has to be
   broad and adversarial: hand-picked edge cases *and* a large body of seeded,
   reproducible fuzz input, exercised through every invocation form (single file,
   many files, stdin) and with qwc's parallel machinery deliberately stressed.

4. **A precisely-drawn line for where qwc is allowed to differ.** GNU `wc` is not
   byte-defined for word splitting, character counting or longest-line width — it
   decodes text and measures display width through the C library's locale tables.
   On two kinds of input qwc deliberately departs from that and is allowed to
   differ: **binary trash** (control bytes, invalid UTF-8, high bytes), where
   GNU's answer is locale-dependent and not meaningful, and **non-ASCII text**,
   where matching GNU requires Unicode-aware splitting and display-width `-L` —
   work that is a *tracked follow-up*, not yet done. On everything people
   actually count — ASCII and valid UTF-8 text — qwc must match GNU exactly. The
   suite's job is to draw that line *precisely*: to know, for each mode and input,
   whether parity is required, and to demand agreement on all of those.

5. **Robustness on anything.** Even where parity is not required, qwc must never
   crash, hang, or emit malformed output. Garbage in must not mean a crash out:
   qwc is expected to exit 0 and print a well-formed count line for *any* byte
   stream.

6. **Reproducibility and CI-readiness.** Every fuzz case is generated from a seed,
   so a failure is replayable from its label alone. The canonical runner is pure
   standard library (no third-party dependencies) so any CI with a Python
   interpreter, a C++ toolchain and a **GNU `wc`** can run it.

## What "the same as GNU `wc`" means here

`wc`'s output is two separable things, and the suite treats them separately:

* **The counts** — the integers. This is the portable, semantic content of
  correctness. Both outputs are parsed into numbers and compared, along with the
  column selection/order, the per-file rows and the `total` row.
* **The formatting** — the exact leading padding and field width. This is *not*
  asserted here: qwc pads to fixed-width BSD-style columns and GNU `wc` pads
  dynamically, and the difference is invisible to anything that splits on
  whitespace. qwc's exact byte layout is pinned by the C++ `tests/cli_test.cpp`
  instead.

## Modes covered

Every mode `wc` and qwc have in common:

| qwc | `wc` | meaning |
|-----|------|---------|
| *(none)* | *(none)* | lines + words + bytes (bare `wc`) |
| `-l` | `-l` | lines (newline count) |
| `-w` | `-w` | words (whitespace-separated) |
| `-c` | `-c` | bytes |
| `-m` | `-m` | characters (code points in a UTF-8 locale) |
| `-L` | `-L` | length of the longest line |

**Combinations** are tested too (`-lw`, `-lwc`, `-lwcL`, `-cm`, `-lwmcL`, …):
`wc` prints the selected columns in a fixed order — lines, words, chars, bytes,
longest line — regardless of the order the flags were given. The longest-line
column is measured in characters when `-m` is in effect, bytes otherwise. The
suite assigns a combination the most restrictive parity rule of its parts,
since every column must agree for the row to match.

**`-cm`/`-mc` are in the matrix.** GNU `wc` prints *both* a chars and a bytes
column for these — chars first, regardless of flag order — and qwc matches.
(BSD `wc` instead collapses them into one last-flag-wins column; qwc follows
GNU, its conformance target.) Both flag orders and the full five-column
`-lwmcL` form are exercised.

## Where qwc is allowed to differ, and why

The boundary between "must match" and "may differ" falls where GNU `wc`'s
counting stops being byte-defined and starts depending on the C library's locale
tables — plus, for now, the non-ASCII text that the deferred Unicode work has not
yet reached. We verified each boundary against real GNU `wc` 9.x:

* **Bytes (`-c`)** and **lines (`-l`)** are pure byte operations — newline counts
  and file sizes. GNU `wc` computes them identically in every locale, so qwc must
  match them on **every input, binary included**. No latitude.

* **Characters (`-m`)** counts code points. Under the **C locale** it collapses
  to a byte count (required everywhere). Under a **UTF-8 locale** it counts code
  points: on **valid UTF-8** (ASCII included) qwc must match GNU, but on
  **invalid UTF-8** there is no right answer — GNU `wc` may count partial
  sequences its own way or fail with *"Illegal byte sequence"* — so qwc's
  interpretation (count bytes that are not UTF-8 continuation bytes) is allowed
  to differ.

* **Words (`-w`)**, and therefore **bare qwc** (no flag) which includes the word
  column, are where qwc parts ways with GNU outside of plain text. GNU `wc`
  splits words with libc's whitespace classification; qwc uses fast,
  locale-independent ASCII-whitespace splitting. They agree on **printable-ASCII
  text** — printable bytes (`0x20`–`0x7E`) plus ASCII whitespace
  (`\t \n \v \f \r`) — so `-w` is required to match there. qwc is free to differ
  on two kinds of input: **ASCII control bytes** (NUL, `0x01`–`0x1F`, `0x7F`),
  which are binary trash where GNU's classification is not meaningful, and
  **non-ASCII bytes** — including *valid* UTF-8 whose separators are Unicode
  whitespace such as U+3000 (`wc -w` counts `你好　世界` differently in a UTF-8
  locale). Matching GNU on that valid-UTF-8 case requires Unicode-aware splitting
  and is a **tracked follow-up**, not yet implemented.

* **Longest line (`-L`)** is the subtlest. qwc reports the longest line as a
  **byte/character count**; GNU `wc` reports a **display width** — it expands
  tabs to 8-column stops and applies `wcwidth` (so a CJK char counts 2, a control
  byte 0), and it *also* counts an unterminated final line that qwc ignores.
  These collapse to the same number only on **printable ASCII** (`0x20`–`0x7E`)
  that is **newline-terminated**. So a `-L` column is required only on such input,
  and free to differ once tabs, control bytes, high bytes, or a missing trailing
  newline are involved. Matching GNU's display-width `-L` is a **tracked
  follow-up**.

This is why the suite's expectations are **content- and locale-aware** rather
than a flat "always equal".

## The parity policy

Every input is run under **two locale regimes** (`LC_ALL=C` and a UTF-8 locale),
and a single required-to-match rule is applied in both. The full rule lives in
`required_to_match()` in `wc_harness.py`; in words:

| mode | required to match GNU `wc` on… |
|------|----------------------------|
| lines (`-l`), bytes (`-c`) | every input, including binary |
| chars (`-m`) | always under `LC_ALL=C` (it's a byte count there); under a UTF-8 locale, valid UTF-8 only |
| words (`-w`), bare qwc | printable-ASCII **text** only (printable bytes + ASCII whitespace; *not* control/NUL bytes or non-ASCII) |
| longest line (`-L`) | printable-ASCII, newline-terminated input only |

Counts are always compared as parsed integers, and the cases where qwc is allowed
to differ (above) are simply not required. Whenever a comparison is *not*
required, the suite does not simply ignore it — it still asserts that qwc exited
cleanly and produced a well-formed count line with the right column count, so
goal 5 (robustness) is enforced on exactly the inputs most likely to break it.

> **The oracle must be GNU `wc`.** On Linux the system `wc` is GNU coreutils, so
> there is nothing to do. On macOS the system `wc` is **BSD `wc`**, which counts
> non-ASCII words and `-L` differently and will produce spurious failures — so to
> run the suite on a Mac, put GNU coreutils' `wc` on `PATH` (e.g.
> `brew install coreutils` and prepend its `gnubin` directory so `wc` resolves to
> the GNU build), or run the suite inside a Linux container. Verify with
> `wc --version` — it should report *"GNU coreutils"*.

## What gets exercised

* **Inputs** (`corpus.py`):
  * *Curated edge cases* — empty input, no-trailing-newline, blank lines,
    CRLF and lone-CR, tabs (no tab-stop expansion in `-L`), embedded NULs, a
    line longer than the read buffer, thousands of tiny lines, multi-MB blobs,
    valid UTF-8 (accents, CJK, emoji, combining marks, a BOM, NBSP and
    ideographic spaces), invalid/overlong UTF-8, and a file containing all 256
    byte values. The binary and control-byte cases are kept precisely so the
    robustness checks (goal 5) run on them, even though their counts are not
    required to match.
  * *Seeded fuzz* across five shapes: random binary, random ASCII, "texty"
    (words separated by whitespace runs), random valid UTF-8 built from random
    code points, and mostly-text with stray high bytes injected. The count and
    seed are configurable; a case's label encodes its seed and index so it
    replays exactly.

* **Invocation forms**: a single file, **multiple files** (checking every
  per-file line *and* the grand `total` / max-line aggregation), and **stdin**
  (a distinct code path in qwc, with no filename in the output).

* **Parallel chunk stitching**: file-mode cases are re-run with
  `--bytes-per-thread 1`, which forces qwc to split the work into as many chunks
  as it has hardware threads. Chunk boundaries then land *inside* words, lines
  and multibyte sequences, so the cross-thread stitching logic is validated
  against `wc`'s ground truth. (`wc` runs unmodified — the counts are invariant
  to however qwc splits the work, which is precisely the property being tested.)

* **Output structure**: counts are compared as parsed integers, but the suite
  still enforces the column *count*, *order*, the per-file rows and the `total`
  row (via `parse_output`). Exact field-width/padding is not asserted here — qwc
  pads to fixed BSD-style columns and GNU `wc` pads dynamically — and qwc's exact
  layout is pinned by the C++ `tests/cli_test.cpp`.

## Non-goals

* **Permanent.** Matching any `wc`'s output *formatting* byte-for-byte (the
  suite compares counts; qwc's column layout is pinned by the C++ tests);
  reproducing GNU `wc`'s behaviour on *invalid* UTF-8 / binary content or in
  exotic non-C/UTF-8 locales (qwc's own fast interpretation there is
  intentional); and performance (this suite is about correctness only —
  benchmarks live elsewhere).
* **Deferred (tracked follow-up, not a permanent exclusion).** Matching GNU
  `wc`'s locale-aware word splitting on *valid* UTF-8 (e.g. U+3000 as a word
  separator) and its display-width `-L` (tab expansion + `wcwidth`). qwc's
  locale-independent rules there are a *current* limitation, not a design
  endpoint; until that work lands, the suite does not require parity on it.

## Running

Build qwc first:

```sh
cmake -S . -B build && cmake --build build --target qwc
```

Make sure the oracle is GNU `wc` (see the note above — automatic on Linux; on
macOS put GNU coreutils' `wc` on `PATH`). Then use either entrypoint — both share
the same engine (`wc_harness.py`) and corpus (`corpus.py`):

```sh
# Standalone runner — pure stdlib, nothing to install. The canonical CI path.
python3 conformance/run.py                 # ~250 fuzz cases under each locale
python3 conformance/run.py --quick         # fast smoke run, no chunk-stress
python3 conformance/run.py --fuzz 5000     # deep run
python3 conformance/run.py --no-bpt-stress # skip the chunk-boundary variant

# pytest front-end — one selectable case per comparison (needs `pytest`).
pip install -r conformance/requirements.txt
pytest conformance/
pytest conformance/ -k "UTF8 and chars"    # slice the matrix
QWC_CONF_FUZZ=1000 pytest conformance/
```

The runner exits non-zero on any failure and prints, for each one, the offending
input together with the `wc` and qwc outputs side by side.

### Environment variables

| var | purpose |
|-----|---------|
| `QWC_BIN` | path to the qwc binary (default: `./qwc`, then `$PATH`) |
| `QWC_CONF_FUZZ` | number of fuzz inputs |
| `QWC_CONF_SEED` | fuzz RNG seed (a failing case is fully reproducible) |
| `QWC_CONF_UTF8_LOCALE` | force a specific UTF-8 locale instead of auto-detect |
| `QWC_CONF_NO_UTF8` | set to skip the UTF-8 regime entirely |

## Continuous integration

Ready-to-use configs live in [`ci/`](ci/):

* [`ci/github-actions.yml`](ci/github-actions.yml) — copy to
  `.github/workflows/conformance.yml`. Runs on Linux against GNU `wc`. (A macOS
  leg must install GNU coreutils and put its `wc` on `PATH` first, since the
  system `wc` there is BSD.)
* [`ci/gitlab-ci.yml`](ci/gitlab-ci.yml) — copy to `.gitlab-ci.yml`, or
  `include:` it.

Both build qwc, ensure a UTF-8 locale is available, and run
`conformance/run.py`.

## How it fits together

| file | role |
|------|------|
| `wc_harness.py` | the engine: mode table, input classification, the parity policy, running `wc`/qwc, parsing and comparing |
| `corpus.py` | curated inputs and the seeded fuzz generators |
| `session.py` | environment discovery: locate qwc, confirm `wc` is present, pick a UTF-8 locale |
| `run.py` | dependency-free runner and CLI |
| `test_conformance.py`, `conftest.py` | pytest front-end over the same engine |
| `ci/` | ready-to-use GitHub Actions and GitLab CI configs |
