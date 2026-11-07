# Changelog

User-visible releases. Engineering evidence and tradeoffs live in the companion
[journal](../qwc-companion/docs/journal/README.md).

## [v0.5.0] - 2026-06-25

- Parallelize invalid CSV row enumeration over flagged chunks; preserve ordered,
  capped reports. Valid-file and `--fast` paths are unchanged.

## [v0.4.0] - 2026-06-25

- Add `--validate-csv` rectangularity checking, configurable dialects, stdin and
  file lists, with `--all`, `--first`, `--list` and `--fast` reporting.
- Vectorize quote-aware CSV validation; fix escaped delimiters/newlines at
  chunk boundaries in backslash mode.
- Enable NEON nibble masks for word counting: common UTF-8 improves, with a
  documented dense two-byte regression and an OFF switch for comparison.

## [0.3.0] - 2026-06-21

- Vectorize clean three-byte UTF-8 word classification on AVX2 and NEON,
  improving CJK and mixed-script throughput. Four-byte fallback remains scalar.

## [0.2.1] - 2026-06-20

- Enable LTO by default; drop PGO after word-counting regressions.
- Reduce Linux release binary size to about 93 KB while retaining static C++
  runtime linkage. Debug and Apple runtime behavior are unchanged.

## [0.2.0] - 2026-06-16

- Retune the scan buffer to 256 KiB and reduce startup overhead through static
  runtime linkage, stdio output, deferred setup and section garbage collection.
- Replace filesystem traversal with opendir/readdir while preserving behavior.
- Add contribution guidance and the formatting gate.

## [0.1.0] - 2026-06-12

Initial release: standard wc count modes, stdin, multi-file totals, AVX2/NEON
and scalar kernels, parallel scans, plus `--char`, `--recursive`, sorting,
`--top` and `--reverse`. GNU conformance is subject to the documented
[parity policy](conformance/README.md#required-parity).
