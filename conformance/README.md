# wcl conformance suite

wcl exists to be a faster, drop-in replacement for `wc`. A replacement is only
worth anything if you can swap it in without noticing — so the bar this suite
holds wcl to is simple and unforgiving: **for every input and every shared mode,
wcl must produce what `wc` produces.** These tests are the executable definition
of that promise.

## Conformance goals

Concretely, the suite is built to guarantee the following.

1. **Output-for-output parity with the system `wc`.** Not "morally equivalent",
   not "the same number somewhere in there" — the same answer, in every mode wcl
   shares with `wc`, across a broad corpus. `wc` is the oracle; we never hard-code
   our own expected counts, because the goal is to track `wc`'s behaviour rather
   than our belief about it.

2. **Drop-in fidelity, including formatting.** A replacement that prints the
   right numbers in the wrong layout still breaks scripts that parse columns.
   Where the local `wc` formats the way wcl does (BSD/macOS), the suite demands
   **byte-for-byte identical output** — padding, column widths, the `total` line,
   the lot. (See the formatting note under *What gets exercised* for how this
   degrades gracefully on GNU `wc`.)

3. **Comprehensiveness.** Replacing `wc` is the whole game, so coverage has to be
   broad and adversarial: hand-picked edge cases *and* a large body of seeded,
   reproducible fuzz input, exercised through every invocation form (single file,
   many files, stdin) and with wcl's parallel machinery deliberately stressed.

4. **A precisely-drawn line for "binary garbage".** `wc` itself is not
   byte-defined for word and character counting — it decodes text through the
   locale. On input that is not well-formed text, `wc`'s answer is a function of
   that machinery, not of any portable specification, and it sometimes errors
   outright. On exactly those inputs wcl is allowed its own interpretation. The
   suite's job is to draw that line *precisely* — to know, for each mode and
   input, whether agreement is required or merely allowed — and to demand
   agreement everywhere else.

5. **Robustness on anything.** Even where parity is not required, wcl must never
   crash, hang, or emit malformed output. Garbage in must not mean a crash out:
   wcl is expected to exit 0 and print a well-formed count line for *any* byte
   stream.

6. **Reproducibility and CI-readiness.** Every fuzz case is generated from a seed,
   so a failure is replayable from its label alone. The canonical runner is pure
   standard library (no third-party dependencies) so any CI with a Python
   interpreter and a C++ toolchain can run it, on Linux or macOS.

## What "the same as `wc`" means here

`wc`'s output is two separable things, and the suite treats them separately:

* **The counts** — the integers. This is the portable, semantic content of
  correctness, and it is *always* checked (by parsing both outputs into numbers).
* **The formatting** — the leading padding, the minimum-width-7 columns, the
  per-file lines, the `total`/max line. wcl matches BSD/macOS `wc`'s
  `" %7ju"`-style layout. This is checked byte-for-byte whenever the local `wc`
  uses the same layout.

## Modes covered

Every mode `wc` and wcl have in common:

| wcl | `wc` | meaning |
|-----|------|---------|
| *(none)* | `-l` | lines (newline count) |
| `-w` | `-w` | words (whitespace-separated) |
| `-c` | `-c` | bytes |
| `-m` | `-m` | characters (code points in a UTF-8 locale) |
| `-L` | `-L` | length of the longest line |
| `-a` | *(none)* | lines + words + bytes — i.e. bare `wc` |

wcl-only features (`--char`, `-r`/`--recursive`, `--sort-*`, `--reverse`,
`--top`, `--bytes-per-thread`) have no `wc` counterpart, so they are not a
conformance concern; they are covered by the C++ unit tests under `tests/`.

## Where wcl is allowed to differ, and why

The boundary between "must match" and "may differ" is not arbitrary — it falls
exactly where `wc` stops being byte-defined and starts depending on the locale's
text-decoding machinery:

* **Bytes (`-c`)**, **lines (`-l`)** and **longest line (`-L`)** are pure byte
  operations. `wc` computes them the same way in every locale, so wcl must match
  them on **every input, binary included**. No latitude.

* **Characters (`-m`)** counts code points. On **valid UTF-8** (ASCII is a
  subset) wcl and `wc` agree. On **invalid UTF-8** there is no right answer:
  `wc` may count partial sequences its own way or fail with *"Illegal byte
  sequence"*. There, wcl's interpretation (count bytes that are not UTF-8
  continuation bytes) is allowed to differ.

* **Words (`-w`)**, and therefore **bare `wc` (`-a`)** which includes the word
  column, are where `wc` and wcl genuinely part ways by design. In a UTF-8
  locale `wc` splits words using the wide-character `iswspace` predicate, so it
  treats Unicode whitespace as separators and — observably on macOS/BSD — even
  splits some non-whitespace scripts (e.g. it counts `你好` as *two* words). wcl
  deliberately uses C-locale, byte-wise whitespace. The two therefore agree only
  on **pure-ASCII** input; on any non-ASCII input wcl's interpretation is
  allowed. Under the C locale `wc` is byte-wise too, so they agree on everything.

This is why the suite's expectations are **locale-aware** rather than a flat
"always equal".

## The parity policy

Every input is run under **two locale regimes**, and the required-to-match rule
differs between them. (The rules below were derived empirically against
macOS/BSD `wc`; the divergence notes live in `wc_harness.py`.)

**Under `LC_ALL=C`** — `wc` is fully byte-defined, so wcl must match it on
**every mode and every input, binary included.** This is the strongest invariant
and the backbone of the suite; under it, *nothing* is skipped.

**Under a UTF-8 locale** (auto-detected; the regime is skipped if the host has no
multibyte locale installed):

| mode | required to match `wc` on… |
|------|----------------------------|
| lines, bytes, max-line-length | every input, including binary |
| chars (`-m`) | valid UTF-8 (incl. ASCII); free to differ on invalid UTF-8 |
| words (`-w`), bare `-a` | pure-ASCII input; free to differ on any non-ASCII |

Whenever a comparison is *not* required, the suite does not simply ignore it — it
still asserts that wcl exited cleanly and produced a well-formed count line, so
goal 5 (robustness) is enforced on exactly the inputs most likely to break it.

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
  (a distinct code path in wcl, with no filename in the output).

* **Parallel chunk stitching**: file-mode cases are re-run with
  `--bytes-per-thread 1`, which forces wcl to split the work into as many chunks
  as it has hardware threads. Chunk boundaries then land *inside* words, lines
  and multibyte sequences, so the cross-thread stitching logic is validated
  against `wc`'s ground truth. (`wc` runs unmodified — the counts are invariant
  to however wcl splits the work, which is precisely the property being tested.)

* **Output formatting**: when the local `wc` formats identically to wcl
  (BSD/macOS), comparisons are **byte-for-byte**. On GNU `wc`, whose column
  widths differ, the suite automatically falls back to comparing parsed numbers —
  the portable correctness signal — and says so in its banner. Exact-formatting
  fidelity on the BSD layout is then guaranteed by the macOS CI job and the C++
  `tests/cli_test.cpp`.

## Non-goals

* Re-implementing `wc`'s exact behaviour on malformed text — that is explicitly
  out of scope (see goal 4); wcl's own interpretation there is intentional.
* Matching **GNU** `wc`'s output formatting byte-for-byte. wcl targets the
  BSD/macOS layout; GNU parity is checked at the level of counts.
* Performance. This suite is about correctness only; benchmarks live elsewhere.

## Running

Build wcl first:

```sh
cmake -S . -B build && cmake --build build --target wcl
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
WCL_CONF_FUZZ=1000 pytest conformance/
```

The runner exits non-zero on any failure and prints, for each one, the offending
input together with the `wc` and wcl outputs side by side.

### Environment variables

| var | purpose |
|-----|---------|
| `WCL_BIN` | path to the wcl binary (default: `./wcl`, then `$PATH`) |
| `WCL_CONF_FUZZ` | number of fuzz inputs |
| `WCL_CONF_SEED` | fuzz RNG seed (a failing case is fully reproducible) |
| `WCL_CONF_UTF8_LOCALE` | force a specific UTF-8 locale instead of auto-detect |
| `WCL_CONF_NO_UTF8` | set to skip the UTF-8 regime entirely |

## Continuous integration

Ready-to-use configs live in [`ci/`](ci/):

* [`ci/github-actions.yml`](ci/github-actions.yml) — copy to
  `.github/workflows/conformance.yml`. Runs on both Linux (GNU `wc`, numeric
  comparison) and macOS (BSD `wc`, byte-for-byte comparison).
* [`ci/gitlab-ci.yml`](ci/gitlab-ci.yml) — copy to `.gitlab-ci.yml`, or
  `include:` it.

Both build wcl, ensure a UTF-8 locale is available, and run
`conformance/run.py`.

## How it fits together

| file | role |
|------|------|
| `wc_harness.py` | the engine: mode table, input classification, the parity policy, running `wc`/wcl, parsing and comparing |
| `corpus.py` | curated inputs and the seeded fuzz generators |
| `session.py` | environment discovery: locate wcl, pick a UTF-8 locale, detect format compatibility |
| `run.py` | dependency-free runner and CLI |
| `test_conformance.py`, `conftest.py` | pytest front-end over the same engine |
| `ci/` | ready-to-use GitHub Actions and GitLab CI configs |
