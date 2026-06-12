# qwc benchmarking notes

This file records benchmark findings and the reasoning behind them. The numbers
below were measured on **2026-06-10** on an Intel Core i7-8700 (6C/12T, 3.2 GHz;
L1d 32 KiB/core, L2 256 KiB/core, L3 12 MiB shared; AVX2, no AVX-512) under
**WSL2**. Both `qwc` and `qwc-scalar` were built `-O3 -march=x86-64-v3` so the
comparison is controlled for ISA (see `CMakeLists.txt` — `qwc-scalar` is given
the same `QWC_SIMD_FLAGS` as `qwc`).

> ⚠️ These are WSL2 numbers. WSL's syscall and page-cache behaviour, and
> especially `/mnt/c` filesystem I/O, differ substantially from a native Linux
> host. A native (Dockerized) x86 `hyperfine` suite in CI is the planned source
> of truth; treat the file-level numbers here as indicative, not authoritative.

## TL;DR

- **Per core, the hand-written AVX2 kernels beat the scalar build by 4–43×.**
  They are never "on par."
- A full-pipeline run can still *look* "on par" — but only because something
  outside the kernel (here, `/mnt/c` filesystem I/O at ~0.27 GB/s) is the
  bottleneck and starves both builds equally.
- **`movemask` + `popcount` is the wrong idiom on this microarchitecture.** The
  NEON-derived byte-lane accumulation is 10–31% faster. We keep byte-lane.

## Methodology: per-core kernel micro-benchmark

The full binary mixes threading, file I/O and page-cache effects with the kernel,
which makes it useless for isolating kernel cost. To measure the kernels alone we
call `count` / `chars` / `words` directly on an in-memory buffer, in a loop, on a
single pinned core (`taskset -c 2`), fixing total work at ~8 GiB and sweeping the
buffer size from L1-resident to far past L3. Small buffers stay in cache
(compute-bound); large buffers stream from DRAM (bandwidth-bound). The kernels
live in their own translation units, so the compiler cannot inline or elide them.
Each cell is the best of 3 runs, in GB/s.

## Finding 1 — AVX2 vs scalar, per core (byte-lane kernels)

| working set    | count: scalar / avx2 | chars: scalar / avx2 | words: scalar / avx2 |
|----------------|----------------------|----------------------|----------------------|
| 16 KiB  (L1d)  |  4.36 / 66.80 (15×)  |  4.38 / 71.13 (16×)  |  0.55 / 23.57 (43×)  |
| 128 KiB (L2)   |  4.35 / 64.10 (15×)  |  4.34 / 53.54 (12×)  |  0.56 / 20.95 (37×)  |
| 4 MiB   (L3)   |  4.06 / 39.63 (10×)  |  4.18 / 38.23 ( 9×)  |  0.54 / 18.05 (33×)  |
| 256 MiB (DRAM) |  4.00 / 16.52 ( 4×)  |  4.00 / 15.89 ( 4×)  |  0.55 / 11.18 (20×)  |

Two regimes:

- **`count` / `chars` are clean reductions.** GCC *does* autovectorize them
  (`-fopt-info-vec`: *"loop vectorized using 32 byte vectors"*), so this is not
  an ISA-support gap. But it vectorizes them *naively*: it sign-extends every
  comparison result byte→word→dword→qword before summing (36 `vpmovsx*` /
  `vextracti128` ops per 32-byte iteration) because that is the only form it can
  prove cannot overflow. That widening chain caps the scalar build at ~4 GB/s.
  The hand-written kernel's whole advantage is one idiom the autovectorizer will
  not invent: accumulate the `0xFF` match masks in *narrow byte lanes*
  (`_mm256_sub_epi8`) and defer the widening reduction (`_mm256_sad_epu8`) to
  once every 255 iterations.
- **`words` does not autovectorize at all** (*"missed: couldn't vectorize loop …
  complicated access pattern"*): the loop-carried `inWord` state and the
  data-dependent branch defeat it, leaving genuinely scalar 0.55 GB/s. The
  hand-written kernel computes the whitespace→word transition mask branchlessly,
  hence the 20–43× gap.

The AVX2 speedup shrinks with working-set size (15×→4× for the reductions)
because large buffers become DRAM-bandwidth-bound: at 256 MiB a single AVX2 core
already pulls ~16 GB/s, a large fraction of this box's ~40 GB/s dual-channel
DDR4. `words` stays compute-bound even at DRAM (still 20×), because the scalar
side is so slow it never approaches the bus limit.

## Finding 2 — `movemask` + `popcount` is slower than byte-lane

The AVX2 kernels are byte-for-byte ports of the NEON kernels, which accumulate in
byte lanes because **NEON has no `movemask`**. The obvious "more x86-native"
rewrite — collapse each compare to a 32-bit mask with `_mm256_movemask_epi8` and
`popcount` it straight into a 64-bit counter — was implemented and measured. It is
**slower across the board** (AVX2-only, byte-lane → movemask):

| working set    | count        | chars        | words        |
|----------------|--------------|--------------|--------------|
| 16 KiB  (L1d)  | 66.80→60.06 (−10%) | 71.13→48.95 (−31%) | 23.57→19.05 (−19%) |
| 128 KiB (L2)   | 64.10→49.47 (−23%) | 53.54→37.52 (−30%) | 20.95→17.33 (−17%) |
| 4 MiB   (L3)   | 39.63→34.55 (−13%) | 38.23→29.37 (−23%) | 18.05→14.96 (−17%) |
| 256 MiB (DRAM) | 16.52→16.42 ( −1%) | 15.89→15.29 ( −4%) | 11.18→10.26 ( −8%) |

**Why:** `vpcmpeqb` + `vpsubb` stays entirely in the vector ALU (2 ops/vector
across three issue ports) and reduces only once per 255 iterations. The movemask
form adds, *per vector*, a `vpmovmskb` (vector→GPR, single port) feeding a
`popcnt` (single port) — a cross-domain latency chain on two contended ports. The
gap closes only at DRAM, where bandwidth hides the extra work. The thing NEON
*can't* do turns out to be the thing we *shouldn't* do here: the byte-lane idiom
is the better AVX2 kernel too. (For `words`, movemask does yield much cleaner code
— the cross-lane carry collapses from `permute2x128`+`alignr` to a one-bit scalar
shift — but it is still ~17% slower, so we keep byte-lane.)

**Decision: kernels stay byte-lane.** This section is the recorded negative result
so the experiment is not repeated.

## Finding 3 — full-binary runs are I/O-bound on `/mnt/c` (the "on par" trap)

Measuring the whole binary on the 703 MB `train.csv` gave the misleading
impression that AVX2 was "on par" with scalar for `-w` and `-L`. It is not: the
WSL `/mnt/c` 9p/drvfs bridge delivers only ~185 MB/s, so every build is throttled
to the filesystem and ties bit-for-bit. Copy the same file to local ext4 (`/tmp`,
page-cache-warm) and the kernel advantage reappears exactly as Finding 1 predicts:

| location                | raw read | qwc `-w` / scalar | qwc `-L` / scalar | qwc `-l` / scalar |
|-------------------------|----------|-------------------|-------------------|-------------------|
| `/mnt/c` (WSL 9p)       | ~185 MB/s | 0.27 / 0.27 (1.0×) | 0.27 / 0.27 (1.0×) | 0.27 / 0.27 (1.0×) |
| `/tmp` (ext4, cached)   | 4.2 GB/s | 10.82 / 2.08 (5.2×) | 9.65 / 3.85 (2.5×) | 12.55 / 9.47 (1.3×) |

**Takeaways:**

- Never benchmark qwc against a file on `/mnt/c`. Use local ext4/tmpfs and warm
  the page cache first, or the result measures the 9p bridge, not qwc.
- `-l` shows only 1.3× even on fast storage because line counting is a clean
  reduction that becomes DRAM-bandwidth-bound; `-w` shows 5× because word counting
  stays compute-bound and the scalar build can't vectorize it.
- This also explains the cross-platform puzzle (NEON beats scalar on Apple
  Silicon, AVX2 "ties" under WSL): it was storage, not ISA or compiler. The Mac
  reads from fast native APFS and is not I/O-bound; WSL `/mnt/c` is.

## Finding 4 — the unicode/printability words kernel on AVX2

Measured **2026-06-11**, after the `-w` rewrite (unicode separators + the
≥1-printable-char rule, see `docs/superpowers/specs/2026-06-10-unicode-whitespace-words-design.md`).
`qwc` (AVX2 words kernel) vs `qwc-scalar` (unified scalar kernel), 256 MiB
`gen-data.py` corpora on `/tmp` (ext4, page-cache-warm), `hyperfine -m 8`,
threaded full-binary wall clock:

| corpus / locale                  | qwc      | qwc-scalar | speedup |
|----------------------------------|----------|------------|---------|
| ASCII, `LC_ALL=C`                | 6.1 GB/s | 0.89 GB/s  | 6.9×    |
| ASCII, `LC_ALL=C.UTF-8`          | 6.2 GB/s | 0.98 GB/s  | 6.3×    |
| mixed UTF-8, `LC_ALL=C.UTF-8`    | 2.4 GB/s | 0.94 GB/s  | 2.6×    |

**How the kernel gets there:** per 32-byte block it builds separator/printable
bitmasks in vectors, then a bit-parallel fast path (popcount of nonsep→sep
transitions) consumes blocks with no barren bytes — i.e. all real text — in a
handful of scalar ops. Blocks containing anything the vector model can't
classify bit-identically to the scalar kernel (3/4-byte UTF-8, invalid bytes,
leads with unassigned holes like 0xCE, C1 controls) fall back per-block: first
to a run-granular mask walk, ultimately to the scalar classifier. That is why
mixed text with CJK shows 2.6× rather than ~7×: its multibyte-bearing blocks
take the scalar path by design (correctness first — the AVX2 and scalar
kernels agree on *all* input, not just valid UTF-8).

**Context vs the old kernel:** the pre-unicode `-w` numbers in Finding 3
(10.82 GB/s) measured a much simpler problem — maximal non-whitespace runs,
no printability rule, no UTF-8. The new semantics cost ~40% of the old
headline throughput but make `-w` byte-for-byte wc-faithful in both locales.

## Finding 5 — many small files: the harness, not the kernel, was the bottleneck

Measured **2026-06-12** on the standard many-files corpus (`gen-data.py --many
--size 512MiB`, 2,719 files, log-uniform 4KiB–1MiB) on `/tmp` (ext4,
page-cache-warm), `hyperfine --shell=none -w 2 -r 10`. Before the fix, `qwc -l`
was **1.7× slower than GNU wc** (236 ms vs 137 ms) despite the AVX2 kernel —
and `qwc-scalar` was slower still (252 ms), proving the kernel wasn't the
problem. `/usr/bin/time -v` attributed it: ~5,200 voluntary context switches
(vs wc's 1) and ~9,000 minor faults (vs 221).

**Root cause — per-file fixed costs in `processFile`, paid 2,719 times:**

1. a `std::thread` spawn + join *per file*, even when `numThreads == 1`
   (i.e. for every file under `--bytes-per-thread` — on this corpus, all of
   them): two context switches plus stack/TLS setup per file;
2. a fresh **value-initialized 1 MiB `std::vector` buffer per file** — a 1 MiB
   memset (plus allocator churn) to scan a few-KiB file;
3. two `posix_fadvise` syscalls per file, useless for files one read consumes.

**Fix:** a one-chunk file is scanned inline on the calling `mapFiles` worker
into a per-thread reused buffer (`threadBuffer()`), and the readahead advice is
only issued for files larger than one scan buffer. The multi-chunk (large-file)
path is unchanged.

| `-l`, 2,719 small files | before | after | speedup |
|-------------------------|--------|-------|---------|
| qwc (AVX2)              | 236 ms | 43 ms | 5.6×    |
| vs GNU wc (135 ms)      | 0.57×  | **3.1×** | —    |
| vs qwc-scalar (52 ms)   | —      | 1.2×  | —       |

The win is workload-agnostic: bare `qwc` on the same corpus went 293→136 ms
and `-w` 289→130 ms. The single-large-file leg is unchanged (within noise on
`-l` and `-w`).

**Negative result — a dedicated lone-`-l` fast path buys nothing.** With the
per-file thread and allocation gone, a specialized lines-only path (no seam
context, no `ScanState`, no merge — just `pread` + `count()`) benchmarked at
exactly **1.00× ± 0.06** against the generic scan on this corpus: the generic
machinery costs a few predictable branches per 1 MiB buffer. Recorded here so
the specialization is not re-attempted; the remaining wall time is read()
copy-out (system time), which no dispatch shortcut touches.

**Also dropped:** the alphabetizing `std::sort` in `collectFiles` for sortless
`-r` runs (output order without a sort flag is now explicitly unspecified, and
a bare `--reverse` is a documented no-op). On 2,719 names the sort itself was
sub-millisecond, so this is a contract simplification more than a speed win.

## Reproducing

The per-core sweep uses a throwaway harness that `#include`s the kernel headers
and links one kernel TU per build, e.g.:

```sh
g++ -O3 -march=x86-64-v3 -std=c++17 -Iinclude harness.cpp \
    src/count_scalar.cpp src/chars_scalar.cpp src/words_scalar.cpp -o bench_scalar
g++ -O3 -march=x86-64-v3 -std=c++17 -Iinclude harness.cpp \
    src/count_avx2.cpp   src/chars_avx2.cpp   src/words_avx2.cpp   -o bench_avx2
# pin a core; sweep buffer size (L1→DRAM) with fixed total work; best of N
taskset -c 2 ./bench_avx2 <buffer-bytes> <count|chars|words>
```

The full-binary numbers are just `qwc <flag> <file>` vs `qwc-scalar <flag> <file>`
wall-clock over the file size, on local ext4 with the page cache warmed.
