# Changelog

Notable, user-visible changes to `qwc`. Format follows
[Keep a Changelog](https://keepachangelog.com); the changelog is hand-curated,
not generated from commit messages.

## [Unreleased]

### Performance

- `qwc` starts ~1.6× faster (2.6 → 1.6 ms on the measurement box, vs GNU wc's
  1.4 ms), which dominates short invocations like `qwc -c <file>` that never
  scan the file at all. Two changes: libstdc++/libgcc are now linked
  statically, removing the dynamic-loader relocation and page-fault cost that
  made up most of the gap to `wc`, and all output goes through stdio instead
  of iostreams. Output is byte-identical to before.

### Changed

- The binary is larger: ~350 KB, up from ~96 KB, the price of the statically
  linked C++ runtime (it would have been 1.8 MB; dropping `<filesystem>` and
  `<string>` from the implementation clawed back the rest). It no longer
  depends on `libstdc++.so`/`libgcc_s.so` at runtime.
- The `--recursive` directory walk is hand-rolled on `opendir`/`readdir`
  instead of `std::filesystem`. Behavior is unchanged (unreadable
  subdirectories are skipped, directory symlinks are not followed, a symlink
  to a regular file is counted) and verified against `find -type f`.

### Added

- `CONTRIBUTING.md`: build, test, benchmark, and PR guidance.
- A clang-format CI gate (`scripts/check-format.sh`, also usable locally with
  `--fix`); the whole tree was reformatted once to make it pass.

## [0.1.0] - 2026-06-12

Initial tagged release: a drop-in `wc` (`-l`, `-w`, `-c`, `-m`, `-L`, stdin,
multi-file totals) with GNU-conformant counting verified by a fuzzing
conformance suite, SIMD kernels (AVX2 on x86-64, NEON on arm64) with a scalar
fallback that is the correctness oracle, threaded scanning of large files and
file lists, and qwc extensions: `--char`, `--recursive`, `--sort-by-*`,
`--top`, `--reverse`.
