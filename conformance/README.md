# qwc conformance suite

qwc exists to be a faster, drop-in replacement for `wc`. A replacement is only
worth anything if you can swap it in without noticing — so the bar this suite
holds qwc to is simple and unforgiving: **for every input and every shared mode,
qwc must produce what `wc` produces.** These tests are the executable definition
of that promise.

## Conformance goals

Concretely, the suite is built to guarantee the following.

1. **Output-for-output parity with the system `wc`.** Not "morally equivalent",
   not "the same number somewhere in there" — the same answer, in every mode qwc
   shares with `wc`, across a broad corpus. `wc` is the oracle; we never hard-code
   our own expected counts, because the goal is to track `wc`'s behaviour rather
   than our belief about it.

2. **Drop-in fidelity of the columns.** A replacement that prints the right
   numbers in the wrong layout still breaks scripts that parse columns. The suite
   enforces the same column *selection*, *order*, per-file lines and `total` row
   as `wc`, comparing the counts as parsed integers. It deliberately does *not*
   require byte-identical padding: GNU and BSD `wc` choose different field widths,
   and the field-splitting tools people pipe `wc` into never see the difference.
   (qwc emits BSD-style fixed-width columns; that exact layout is pinned by the
   C++ `tests/cli_test.cpp`, not here.)

3. **Comprehensiveness.** Replacing `wc` is the whole game, so coverage has to be
   broad and adversarial: hand-picked edge cases *and* a large body of seeded,
   reproducible fuzz input, exercised through every invocation form (single file,
   many files, stdin) and with qwc's parallel machinery deliberately stressed.

4. **A precisely-drawn line for "where `wc`s disagree".** `wc` is not
   byte-defined for word splitting, character counting or longest-line width — it
   decodes text and measures display width through the C library's locale tables.
   The `wc` implementations themselves diverge there: BSD `wc`, GNU `wc` on glibc,
   and GNU `wc` built against macOS libc all give different answers on non-ASCII
   words and on `-L`. qwc cannot match all of them, so on exactly those inputs it
   uses its own fast, locale-independent rules and is allowed to differ. The
   suite's job is to draw that line *precisely* — to know, for each mode and
   input, whether the count is *universal* (the same for every `wc` everywhere)
   and therefore required — and to demand agreement on all of those.

5. **Robustness on anything.** Even where parity is not required, qwc must never
   crash, hang, or emit malformed output. Garbage in must not mean a crash out:
   qwc is expected to exit 0 and print a well-formed count line for *any* byte
   stream.

6. **Reproducibility and CI-readiness.** Every fuzz case is generated from a seed,
   so a failure is replayable from its label alone. The canonical runner is pure
   standard library (no third-party dependencies) so any CI with a Python
   interpreter and a C++ toolchain can run it, on Linux or macOS.

## What "the same as `wc`" means here

`wc`'s output is two separable things, and the suite treats them separately:

* **The counts** — the integers. This is the portable, semantic content of
  correctness. Both outputs are parsed into numbers and compared, along with the
  column selection/order, the per-file rows and the `total` row.
* **The formatting** — the exact leading padding and field width. This is *not*
  asserted here, because GNU and BSD `wc` pad differently and the difference is
  invisible to anything that splits on whitespace. qwc emits BSD/macOS `wc`'s
  `" %7ju"`-style layout; that exact byte layout is pinned by the C++
  `tests/cli_test.cpp` instead.

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

**Combinations** are tested too (`-lw`, `-lwc`, `-lwcL`, …): `wc`
prints the selected columns in a fixed order — lines, words, char/byte, longest
line — regardless of the order the flags were given. The longest-line column is
measured in the active char/byte unit (characters when `-m` is in effect, bytes
otherwise). The suite assigns a combination the most restrictive parity rule of
its parts, since every column must agree for the row to match.

**`-cm`/`-mc` are where BSD and GNU `wc` genuinely disagree, and we zeroed in on
the BSD/macOS behavior.** BSD `wc` treats `-c` and `-m` as a single shared
char/byte column on a last-flag-wins basis — `-cm` counts characters, `-mc`
counts bytes — emitting one column. GNU `wc` instead prints *both* a byte and a
character column. qwc follows the BSD interpretation (one column, last wins), so
these two modes diverge from GNU `wc` in column *count*, not merely formatting,
and no single binary can match both systems. They are therefore excluded from
the cross-`wc` differential modes; qwc's chosen single-column output is pinned
directly by the `CliCombined` C++ tests under `tests/` instead.

qwc-only features (`--char`, `-r`/`--recursive`, `--sort-*`, `--reverse`,
`--top`, `--bytes-per-thread`) have no `wc` counterpart, so they are not a
conformance concern; they are covered by the C++ unit tests under `tests/`.

### A `wc` bug we deliberately do not replicate

macOS/BSD `wc` mis-totals the character column when `-m` and `-L` are given
together: the multi-file `total` line prints `0` for characters (the per-file
values, and `wc -m` on its own, are correct). qwc reports the correct total, so
for that one combination the suite compares the per-file rows but not the buggy
`total`.

## Where qwc is allowed to differ, and why

The boundary between "must match" and "may differ" is not arbitrary — it falls
exactly where the various `wc`s stop agreeing among themselves, which is where
counting stops being byte-defined and starts depending on the C library's locale
tables. We verified each boundary against real BSD `wc` *and* real GNU `wc` 9.11:

* **Bytes (`-c`)** and **lines (`-l`)** are pure byte operations — newline counts
  and file sizes. Every `wc` computes them identically in every locale, so qwc
  must match them on **every input, binary included**. No latitude.

* **Characters (`-m`)** counts code points. Under the **C locale** it collapses
  to a byte count (universal — required everywhere). Under a **UTF-8 locale** it
  counts code points: on **valid UTF-8** (ASCII included) every `wc` agrees, but
  on **invalid UTF-8** there is no right answer — `wc` may count partial
  sequences its own way or fail with *"Illegal byte sequence"* — so qwc's
  interpretation (count bytes that are not UTF-8 continuation bytes) is allowed
  to differ.

* **Words (`-w`)**, and therefore **bare qwc** (no flag) which includes the word
  column, are where the `wc`s genuinely part ways. `wc` splits words with libc's
  whitespace classification, and the implementations disagree: in a UTF-8 locale
  BSD `wc` even counts `你好` as *two* words, while glibc, macOS libc and qwc each
  classify non-ASCII bytes differently again (we measured three different word
  counts for the same non-ASCII input). qwc deliberately uses fast, locale-
  independent ASCII-whitespace splitting. So `-w` is required to match only on
  **pure-ASCII** input — where every `wc` agrees — and free to differ on anything
  non-ASCII, in either locale.

* **Longest line (`-L`)** is the subtlest. qwc and BSD `wc` report the longest
  line as a **byte count**; GNU `wc` reports a **display width** — it expands
  tabs to 8-column stops and applies `wcwidth` (so a CJK char counts 2, a control
  byte 0), and it *also* counts an unterminated final line that qwc and BSD `wc`
  ignore. These three behaviours collapse to the same number only on **printable
  ASCII** (`0x20`–`0x7E`) that is **newline-terminated**. So a `-L` column is
  required only on such input, and free to differ once tabs, control bytes, high
  bytes, or a missing trailing newline are involved.

This is why the suite's expectations are **content- and locale-aware** rather
than a flat "always equal".

## The parity policy

Every input is run under **two locale regimes** (`LC_ALL=C` and a UTF-8 locale),
and a **single** required-to-match rule is applied in both — calibrated so it
holds against any `wc` on any platform. The full rule lives in
`required_to_match()` in `wc_harness.py`; in words:

| mode | required to match `wc` on… |
|------|----------------------------|
| lines (`-l`), bytes (`-c`) | every input, including binary |
| chars (`-m`) | always under `LC_ALL=C` (it's a byte count there); under a UTF-8 locale, valid UTF-8 only |
| words (`-w`), bare qwc | pure-ASCII input only |
| longest line (`-L`) | printable-ASCII, newline-terminated input only |

This is **one rule set, not two**: it passes against BSD `wc` on macOS and GNU
`wc` on Linux alike, because it only ever requires agreement on counts that are
universal across `wc` implementations. There is no "exact formatting" mode and no
auto-detection of the local `wc` flavour — counts are always compared as parsed
integers, and the cases where `wc`s disagree (above) are simply not required.

> **Why not just run GNU `wc` locally?** Because it wouldn't help. GNU `wc`
> delegates word/`-L`/character classification to the platform's libc, so GNU
> `wc` on macOS produces *different* locale-dependent counts than GNU `wc` on
> Linux (we measured it). No binary runnable on a Mac can reproduce Linux `wc`'s
> locale-dependent output — which is exactly why the suite requires parity only
> on the universal cases instead of chasing a "matching" oracle.

Whenever a comparison is *not* required, the suite does not simply ignore it — it
still asserts that qwc exited cleanly and produced a well-formed count line with
the right column count, so goal 5 (robustness) is enforced on exactly the inputs
most likely to break it.

## What gets exercised

* **Inputs** (`corpus.py`):
  * *Curated edge cases* — empty input, no-trailing-newline, blank lines,
    CRLF and lone-CR, tabs (no tab-stop expansion in `-L`), embedded NULs, a
    line longer than the read buffer, thousands of tiny lines, multi-MB blobs,
    valid UTF-8 (accents, CJK, emoji, combining marks, a BOM, NBSP and
    ideographic spaces), invalid/overlong UTF-8, and a file containing all 256
    byte values.
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
  row (via `parse_output`). Exact field-width/padding is not asserted here — GNU
  and BSD `wc` differ on it — and is instead pinned for qwc's BSD-style layout by
  the C++ `tests/cli_test.cpp`.

## Non-goals

* Re-implementing any `wc`'s locale-dependent behaviour on non-ASCII text, tabs,
  or display width — that is explicitly out of scope (see goal 4); qwc's own
  fast, locale-independent interpretation there is intentional.
* Matching any `wc`'s output *formatting* byte-for-byte. The suite compares
  counts; qwc's BSD-style column layout is pinned by the C++ tests.
* Performance. This suite is about correctness only; benchmarks live elsewhere.

## Running

Build qwc first:

```sh
cmake -S . -B build && cmake --build build --target qwc
```

Then use either entrypoint — both share the same engine (`wc_harness.py`) and
corpus (`corpus.py`):

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
  `.github/workflows/conformance.yml`. Runs on both Linux (GNU `wc`) and macOS
  (BSD `wc`); the same universal rule set is expected to pass against either.
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
