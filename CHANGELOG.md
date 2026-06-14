# Changelog

Notable, user-visible changes to `qwc`. Format follows
[Keep a Changelog](https://keepachangelog.com); the changelog is hand-curated,
not generated from commit messages.

## [Unreleased]

### Performance

- Big-file `-l` and `-m` are markedly faster on multi-core hosts. The
  per-thread scan buffer was retuned from 1 MiB to 256 KiB after the sweep
  documented in `benchmarks/README.md` Finding 6: at the old size the
  buffer spilled out of L2 on every host we measured, so `pread`'s
  `copy_to_user` paid two extra DRAM trips per byte; at 256 KiB it stays
  L2-resident. On a native i7-8700 with a warm 512 MiB file, 4-vCPU pinned
  `-l` drops from 0.85× vs uu-wc to **1.77×** (29.9 ms vs 53.0 ms), and
  `-m` matches (25.3 ms vs 44.8 ms). At all 12 threads the big-file `-l`
  win is **2.36×** (20.5 ms vs 48.4 ms). LLC-load-misses on the headline
  workload fall from ~91k at 1 MiB to 668 at 256 KiB. Counts are
  unchanged.
- `qwc` starts ~1.6× faster (2.6 → 1.6 ms on the measurement box, vs GNU wc's
  1.4 ms), which dominates short invocations like `qwc -c <file>` that never
  scan the file at all. Two changes: libstdc++/libgcc are now linked
  statically, removing the dynamic-loader relocation and page-fault cost that
  made up most of the gap to `wc`, and all output goes through stdio instead
  of iostreams. Output is byte-identical to before.
- Bare `qwc -c <file>` is now faster than GNU `wc -c` on the measurement box
  — the first time qwc has measured ahead on the bytes-only invocation. Four
  small cuts closed the residual ~0.2 ms left over after the static-link
  work: `std::thread::hardware_concurrency()` no longer runs as a static
  initializer (the sysconf reading `/sys/devices/system/cpu/online` is now
  paid lazily, and the lone-file `-c` and `-l` paths skip it entirely);
  `setlocale(LC_CTYPE, "")`, `nl_langinfo(CODESET)`, and
  `getenv("POSIXLY_CORRECT")` are gated on `-w`/`-m`, so bare `-c`/`-l`
  never fault in libc's cold locale pages; `collectFiles` short-circuits
  when `--recursive` is off; and the binary is built with
  `-ffunction-sections -fdata-sections -Wl,--gc-sections` (GNU ld / lld
  only), shrinking the ~276 minor page faults the kernel paid to map the
  354 KB static image on startup. Counts and output are unchanged.

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
