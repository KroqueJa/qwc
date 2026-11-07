# Benchmarking

Decisions and historical results live in the companion
[journal](../../qwc-companion/docs/journal/README.md). Benchmark drivers and raw
November measurements remain here; generated corpora and routine logs are ignored.

## Run a comparison

From the repository root (Python tooling uses `uv`; timing needs hyperfine):

```sh
uv run python3 benchmarks/gen-data.py --bench-corpora --out-dir benchmarks/test-data
scripts/bench/sweep.sh --no-prep
scripts/bench/sweep.sh --no-prep --with-competitors
```

The sweep builds the working tree, refreshes the latest-release baseline,
compares both locales, and writes tables plus JSON sidecars under `logs/`.
`WARMUP` and `RUNS` override the default 3/20 repetitions. Per-corpus wrappers
such as `scripts/bench/cjk-short.sh` support focused work. On Linux, omitting
`--no-prep` applies privileged machine settings and restores them on exit;
macOS skips that preparation. Check the script help before using it.

Focused drivers expose their options through `--help`:

```sh
uv run python3 benchmarks/bench.py --help
uv run python3 benchmarks/bench-csv.py --help
uv run python3 benchmarks/bench-concurrency.py --help
uv run python3 benchmarks/bench-unicode-fallback.py --help
```

Use explicit binary paths for A/B builds. CMake outputs into each build
directory; `sync-current-build.sh` copies its candidate to `./qwc`. Do not
trust an existing root binary to match the current checkout. Keep both
candidate and baseline before rebuilding, and record revisions and hashes.
The concurrency default physically writes 5 GiB; corpus generation is outside
timing. JSON probes retain samples and verify expected counts.

## Interpret results

Record CPU/ISA, OS/compiler, source revisions, file sizes/shapes, locale,
thread configuration, warmups/repetitions, and page-cache policy. Compare
matched runs on one host; different CPUs or hosted runners are not a
controlled comparison. Use local storage, not WSL `/mnt/c`, for kernel-focused
work. Warm-cache throughput is not cold-storage throughput.

Measure wall time first, then explain it with CPU time and counters. Include
ASCII, mixed scripts, dense two-/three-byte text, and fallback-heavy input.
Keep no-scan startup cells separate: microsecond jitter produces large ratios.
A whole-binary SIMD comparison mixes kernels, threading, syscalls and storage;
a pinned in-memory kernel benchmark answers a different question. The original
June microbenchmark used a throwaway harness that is not checked in.

## Profile a hypothesis

```sh
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=RelWithDebInfo -DQWC_SUFFIX=perf
cmake --build build-perf --target qwc
QWC_BIN=./build-perf/qwc-perf scripts/bench/perf-annotate.sh -w benchmarks/test-data/cjk-short.txt
QWC_BIN=./build-perf/qwc-perf scripts/bench/toplev.sh -L benchmarks/test-data/long
scripts/bench/ab.sh -- -w benchmarks/test-data/mixed
```

The annotate wrapper uses `perf record -e cycles:pp`; `toplev.sh` requires
pmu-tools' `toplev.py` on PATH and uses `--global` for migrating workers.
`QWC_PERF_LOCALE` selects the profiling locale. The wrappers describe required
Linux perf permissions. Release-shaped builds matter: Debug code has different
hotspots. LTO may inline helper symbols into the outer kernel; annotate the
actual sampled symbol. Stripped unwind tables prevent DWARF callgraphs; LBR is
an alternative on supported hardware.

TMA leaf counters can be unreliable when multiplexed on short runs. Use
restricted counter groups/non-multiplexed measurements for cache-level claims.
Pair normalized slot percentages with absolute cycles/instructions and wall
time. When an unchanged path regresses, inspect disassembly, alignment, and
inlining before attributing the regression to the new algorithm.

Publish one concise journal entry per experiment with evidence, limitations,
validation and the decision. Keep raw data when it supports a durable claim;
do not maintain a second mutable “latest” narrative.
