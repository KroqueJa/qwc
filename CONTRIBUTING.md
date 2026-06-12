# Contributing to qwc

Thanks for your interest in `qwc` — a fast, drop-in replacement for GNU `wc`. This document covers how to build, test, and contribute changes.

## Project goals

`qwc` aims to be:

- **Conformant** — count-for-count agreement with GNU `wc` (the oracle is GNU coreutils `wc`, not BSD) across ASCII, valid UTF-8, and the awkward inputs: multibyte/Unicode whitespace, `/proc` virtual files that lie about their size, empty and non-seekable inputs. Two deliberate divergences exist: `-L` counts code points rather than `wcwidth` display columns, and column padding uses a fixed minimum width rather than wc's computed width — the conformance suite compares parsed counts, not raw bytes.
- **Fast** — SIMD-accelerated counting (AVX2 on x86-64, NEON on arm64) with a scalar implementation that is both the portable fallback and the correctness oracle, plus threaded scanning of large files and file lists.
- **Portable** — clean builds on Linux and macOS. (MinGW/Windows is not supported: the I/O path needs `pread`.)

Correctness comes first. A faster path that diverges from GNU `wc` is a bug, not an optimization.

## Building

```sh
git clone --recurse-submodules <repo>   # tests need the vendored googletest

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The binary lands at the **repo root** (`./qwc`), not inside the build tree — benchmark and conformance tooling look for it there.

The SIMD implementation is selected at **configure time**, per architecture, not at runtime: x86-64 builds the AVX2 kernels at `-march=x86-64-v3` (override with `-DQWC_X86_ARCH=`, e.g. `native`), arm64 builds NEON, and anything else — or `-DQWC_SCALAR=ON` — builds the scalar kernels. A v3 binary will SIGILL on pre-Haswell CPUs; that's the documented floor. The `qwc-scalar` target (`cmake --build build --target qwc-scalar`) builds the scalar baseline used for benchmarking SIMD changes.

Sanitizer builds (recommended while developing; the two are mutually exclusive):

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DQWC_ASAN=ON  # ASan + UBSan
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DQWC_TSAN=ON  # ThreadSanitizer
```

## Testing

There are two test layers, and both should pass before you open a PR.

### Unit tests (`tests/`, googletest)

Kernel-level tests of every counter (scalar and SIMD), the chunk-seam stitching, and end-to-end CLI tests that drive the built binary.

```sh
ctest --test-dir build --output-on-failure
```

### Conformance suite (`conformance/`)

Verifies that `qwc` agrees with GNU `wc` across a curated + fuzzed corpus, under both the C and a UTF-8 locale, in single-file, multi-file and stdin forms, with the thread-chunking knob cranked to force chunk boundaries inside words, lines and multibyte sequences. This is the most important guard in the project — any change touching counting logic, output formatting, or input handling must pass it.

```sh
python3 conformance/run.py              # default run (~250 fuzz cases)
python3 conformance/run.py --quick      # fast smoke run
python3 conformance/run.py --fuzz 2000  # deep run
QWC_BIN=./qwc python3 conformance/run.py  # explicit binary (default: repo root)
```

Pure standard-library Python; no dependencies. The oracle is the `wc` first on `PATH`, which must be GNU: on macOS install coreutils (`brew install coreutils`) and put its gnubin first on `PATH` — the CI template in `conformance/ci/` shows how.

When you fix a conformance bug, add a case that reproduces it so it can't regress. Curated cases live in `conformance/corpus.py`; follow the structure of the existing ones.

## Benchmarks (`benchmarks/`)

`benchmarks/gen-data.py` generates a deterministic synthetic corpus; `benchmarks/bench.py` runs a [hyperfine](https://github.com/sharkdp/hyperfine) matrix of wc flags comparing `qwc` against uutils `wc` and GNU `wc` (see `python3 benchmarks/bench.py --help`). The GitHub `benchmark` workflow (manual dispatch) runs the same comparison of your branch against `main` on a hosted runner.

Generated corpora are large and **must not be committed** — `benchmarks/test-data` is gitignored. If you add a new generator profile, commit the generator change, not its output.

Run benchmarks before and after any change to a SIMD or hot path, and include the before/after numbers in your PR description, noting the machine and ISA. Numbers are not comparable across hosts — even identically-named CI runners vary by CPU model (bench.py prints a host line for exactly this reason); compare only runs with matching host lines. `benchmarks/README.md` documents the methodology and accumulated findings.

## Commits

The project does not use Conventional Commits or commit-message-driven release tooling. Releases are tags (`vX.Y.Z`) cut by the maintainer; `--version` reports the release tag (or the commit for dev builds).

Unless very difficult, should end the phrase "This commit will...". Always start with a capital letter.

## Pull requests

Before opening a PR:

1. Formatting is clean: `scripts/check-format.sh` (use `--fix` to repair).
2. Unit tests pass: `ctest --test-dir build --output-on-failure`.
3. The conformance suite passes: `python3 conformance/run.py`.
4. Any counting/formatting/input change has a new conformance case or unit test covering it.
5. SIMD or hot-path changes include before/after benchmark numbers (machine + ISA noted).

Keep PRs focused — one logical change per PR makes review and bisection far easier.

CI runs the `analysis` workflow on every push/PR: the clang-format gate, clang-tidy on Linux **and** macOS (each SIMD TU only compiles on its own arch), the unit tests and conformance suite under ASan/UBSan and TSan, and a GCC `-fanalyzer` pass over the syscall-heavy paths.

## Code style

- The repo's `.clang-format` is authoritative; CI enforces it. Run `scripts/check-format.sh --fix` before committing.
- Use the `typedef.h` aliases (`u32`, `usize`, `isize`, …) rather than raw `unsigned`/`size_t` spellings.
- Counting paths are performance-sensitive: keep the scalar reference implementation clear, and comment the SIMD tricks (movemask/popcount transition counting, per-block punts) — they aren't self-evident, and the comments are load-bearing for the next port.
- Every SIMD kernel must produce results **byte-identical to its scalar sibling on all input** — not just valid text. The scalar kernel is the correctness oracle; when the vector model can't classify a block identically, punt to scalar for that block rather than approximating.
- clang-tidy is part of the gate (`-DQWC_CLANG_TIDY=ON` locally); findings are errors in CI.

## Questions

Open an issue for anything unclear, or to discuss a larger change before investing in it.
