# Conformance

The shared engine compares parsed counts, column order, per-file rows and
totals against **GNU coreutils wc**. Exact padding is covered by CLI unit tests.
Oracle discovery checks `gnuwc`, `gwc`, then `wc`, verifying `--version`;
`QWC_CONF_WC=/path/to/wc` overrides discovery and is also verified. A system
`wc` may be BSD or uutils, so its name alone is insufficient.

## Run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
QWC_BIN=./build/qwc uv run python3 conformance/run.py --quick
QWC_BIN=./build/qwc uv run python3 conformance/run.py --fuzz 2000
uv run python3 conformance/test_oracle.py
```

The standalone runner uses only the standard library. Optional pytest tests
use `requirements.txt` and the same engine. `--no-bpt-stress` skips the
forced-chunk variant. Configuration includes `QWC_BIN`, `QWC_CONF_WC`,
`QWC_CONF_FUZZ`, `QWC_CONF_SEED`, `QWC_CONF_UTF8_LOCALE`, and
`QWC_CONF_NO_UTF8`. See runner help for defaults.

## Required parity

The executable policy is `required_to_match()` in [wc_harness.py](wc_harness.py).
If the oracle succeeds:

| Counts | Inputs required to match |
| --- | --- |
| Lines / bytes | All byte streams |
| Characters | All input in C; valid UTF-8 in a UTF-8 locale |
| Words | Printable ASCII text, including ASCII whitespace |
| Longest line | Printable ASCII with a trailing newline |

Combinations inherit their constituent restrictions. Outside the required
set, qwc must still exit cleanly and produce structurally valid output.
Unicode word classification is implemented; the broader word restriction
remains because qwc filters barren runs. `-L` counts bytes or code points
(with `-m`), not display columns, and ignores unterminated final lines.
`-cm` and `-mc` both print characters then bytes, following GNU column selection.
See the companion [Unicode journal](../../qwc-companion/docs/journal/2026-06-22-unicode-words.md)
for the decisions and unresolved policy.

The corpus covers curated edge cases and seeded binary/ASCII/UTF-8 fuzz,
single/multiple files, stdin, and chunk-boundary stress with
`--bytes-per-thread 1`. Add reproductions to [corpus.py](corpus.py) when fixing
bugs. [session.py](session.py) discovers binaries/locales; [run.py](run.py) is
the CLI; [test_conformance.py](test_conformance.py) is the pytest frontend.
CSV has a separate differential runner at [csv/diff_csv.py](csv/diff_csv.py).
Reusable CI templates live in [ci/](ci/).
