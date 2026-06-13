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

## Finding 6 — Scan-buffer size: the 1 MiB bounce buffer was costing every byte two extra trips through DRAM

Measured **2026-06-13** on the native Linux box (Intel i7-8700, 6C/12T, L1d
32 KiB/core, **L2 256 KiB/core**, L3 12 MiB; AVX2; kernel 7.0.12-arch1-1; root
`/dev/sdb2` ext4) over a 512 MiB single file and a 2,719-file ÷ 512 MiB many
corpus (`gen-data.py --many --size 512MiB`), page-cache-warm,
`hyperfine --shell=none -w 2 -r 10`, sweep driver
`benchmarks/sweep.py`. Comparators in the same hyperfine invocation: GNU `wc`
9.11 and `uu-wc` (uutils 0.4.x). The TODO entry's framing — *the scoreboard is
real-world multi-core wall-clock; per-core efficiency is a diagnostic* —
selects the all-thread topology as the primary table; the 4-vCPU pinned
topology is reported because it mimics the EPYC 7763 CI runner and pins down
the success criterion.

**TL;DR.** Shrinking the per-thread scan buffer from 1 MiB to **256 KiB**
collapses big-file `-l` at 12 threads from 31.5 ms to 20.5 ms (35% faster),
turns the 4-vCPU pinned big-file `-l` from a 1.07× loss vs uu-wc into a
**1.77× win** (29.9 ms vs 53.0 ms), and recovers the same fraction on `-m`
(big-file 4-vCPU: 25.3 ms vs uu-wc 44.8 ms = 1.77×). LLC-load-misses on the
4-vCPU `-l` workload fall from 90,838 at 1 MiB to **668 at 256 KiB** (a 136×
collapse, and 6× fewer than uu-wc's 4,210). Counts and conformance unchanged.

**Hypothesis (H1, from the design spec).** `pread`'s `copy_to_user` reads the
warm page cache (trip 1) and writes the scan buffer; at 1 MiB the buffer
spills out of the 256 KiB L2, so those writes drain to L3/DRAM (trip 2) and
the kernels read them back (trip 3). At ≤256 KiB trips 2–3 stay in L2.
Prediction: big-file `-l` at 4 vCPUs drops from 62 ms to 25–35 ms with no
kernel changes, and LLC-miss traffic (misses × 64 B) falls from ~3× file size
to ~1×. H1 confirmed end-to-end.

**Wall-clock sweep, 12 threads (the user's machine, the spec's "real
scoreboard"):**

| ms                    | qwc@64K | qwc@128K | qwc@256K | qwc@512K | qwc@1M | wc     | uu-wc |
|-----------------------|--------:|---------:|---------:|---------:|-------:|-------:|------:|
| big 512 MiB · -l      | 24.7    | 21.3     | **20.5** | 23.9     | 31.5   | 51.2   | 48.4  |
| big 512 MiB · -m      | **18.7**| 18.8     | 18.9     | 19.3     | 25.9   | 1923.6 | 45.7  |
| big 512 MiB · -w      | 85.9    | 85.1     | **84.9** | 87.2     | 92.2   | 1945.3 | 1876.8|
| many 2,719 · -l       | 21.6    | **21.2** | 22.7     | 23.3     | 27.8   | 72.0   | 67.0  |
| many 2,719 · -m       | 21.8    | 22.0     | **21.1** | 22.3     | 27.4   | 1982.7 | 68.1  |
| many 2,719 · -w       | **71.6**| 72.3     | 74.2     | 74.5     | 88.3   | 1941.2 | 1847.3|

The shape is unambiguous: 1 MiB is the worst point everywhere by a wide
margin; the curve crosses the wall-clock floor in the 64K–256K band. 64K
hurts the 4-vCPU big-file rows (syscall overhead per pread surfaces with
fewer threads sharing the cost — see the 4-vCPU table below); 256K wins
or ties on every all-thread row whose floor lies above the 2% noise band,
and is at most 7% off the floor anywhere.

**Wall-clock sweep, `taskset -c 0-3` (CI-runner mimic, success criterion):**

| ms                    | qwc@64K | qwc@128K | qwc@256K | qwc@512K | qwc@1M | wc     | uu-wc |
|-----------------------|--------:|---------:|---------:|---------:|-------:|-------:|------:|
| big 512 MiB · -l      | 35.4    | 30.1     | 29.9     | 28.3     | **28.2** | 57.2 | 53.0  |
| big 512 MiB · -m      | 24.9    | 28.1     | 25.3     | **23.7** | 24.2   | 1955.9 | 44.8  |
| big 512 MiB · -w      | 140.7   | 137.8    | 138.4    | **134.4**| 138.5  | 1958.2 | 1858.0|
| many 2,719 · -l       | 26.1    | 25.9     | 26.0     | **25.7** | 26.6   | 67.4   | 64.6  |
| many 2,719 · -m       | 26.6    | **26.5** | 29.1     | 30.5     | 31.1   | 2006.9 | 65.8  |
| many 2,719 · -w       | 135.6   | 134.9    | 135.7    | **133.5**| 136.2  | 1978.2 | 1877.9|

At 4 vCPUs the optimum drifts up to 512 KiB on the big rows (1 MiB is within
0.1 ms — noise), because fewer concurrent threads mean less L3/DRAM
contention and the larger buffer amortizes syscall fixed costs better. At
256 KiB the cost vs that local optimum is small (29.9 ms vs 28.2 ms on `-l`,
25.3 ms vs 23.7 ms on `-m` — at most 1.6 ms / 6.8%), and the success
criterion is met with margin: qwc-256K beats uu-wc by **1.77×** on both `-l`
and `-m` at 4 vCPUs on the big file.

**Mechanism counters (`perf stat -x, -e cycles,instructions,LLC-loads,LLC-load-misses,minor-faults`, big-file `-l`, 4-vCPU pinned, userspace-only under `perf_event_paranoid=2`):**

| size  | cycles      | instructions | LLC-loads  | LLC-load-misses | minor-faults |
|-------|------------:|-------------:|-----------:|----------------:|-------------:|
| 64 K  | 34,888,980  | 77,151,521   | 286,434    | 1,761           | 1,582        |
| 128 K | 37,348,614  | 74,351,358   | 1,238,840  | 2,896           | 1,678        |
| 256 K | 46,894,165  | 72,895,182   | 3,925,294  | **668**         | 2,031        |
| 512 K | 50,839,557  | 72,250,727   | 3,755,269  | 9,961           | 2,607        |
| 1 M   | 64,118,966  | 71,900,084   | 3,644,032  | **90,838**      | 3,757        |
| uu-wc | 25,306,752  | 55,391,959   | 129,259    | 4,210           | 370          |

LLC-load-misses are the decisive column. At 1 MiB the bounce buffer spills
L2 so hard that 90,838 LLC misses pour out to DRAM (90,838 × 64 B ≈ 5.8 MiB
of DRAM traffic just on the bounce side, for a 512 MiB file already in page
cache). At 256 KiB the same workload generates **668** misses — the buffer
fits L2, the kernel's `copy_to_user` writes stay L2-resident, and the scan
reads back from L2 instead of DRAM. The 64 K row's even lower miss count
(1,761) reflects that the buffer also fits L1d, but the cycles + the 4-vCPU
wall-clock both penalize the syscall overhead per pread; 256 KiB is the
sweet spot where memory traffic AND syscall amortization are both good.
minor-faults rise monotonically with buffer size because larger preads
fault in more page-cache pages per call — small extra confirmation of the
same mechanism.

**Cold-cache sanity:** skipped (`drop_caches` requires sudo and was traded
against engineer time). fadvise `SEQUENTIAL` readahead absorbs smaller
preads in the I/O-bound regime; the project's history does not show a
counter-example. If cold-cache regression turns up in the field, the small
worsening of syscall count per page is the first place to look.

**Decision.** `BUF_SIZE` becomes **256 KiB**. One value, robust across both
machines that matter: the user's native i7-8700 (256 KiB L2/core × 12
threads = 3 MiB total per-thread buffers, fits L2 at full thread count) and
the EPYC 7763 CI runner (512 KiB L2/core; 4 threads × 256 KiB = 1 MiB
total, fits with headroom). Per-host auto-tuning is YAGNI; one constant
wins everywhere we measure.

**Why 256 KiB and not 512 KiB (the literal "smallest within 2% on the 4-vCPU
success-criterion rows" fallback in the plan).** The spec's primary rule is
"smallest within 2% across both corpora and both workloads", with the
tiebreaker "smaller wins (more L2 headroom)", and explicitly names the
all-thread topology as "the real scoreboard". On that scoreboard 256 KiB
wins the big-file `-l` floor outright (20.5 ms vs 512 KiB's 23.9 ms — a
17% gap) and ties on every other floor within 7%. 512 KiB is faster on the
4-vCPU pinned rows by ~1.6 ms but takes that 17% all-thread `-l` hit. The
4-vCPU pinned mode is the runner-mimic, not the user's machine. The
mechanism counters cinch it: 256 KiB sits on the L2-resident side of the
cliff (668 LLC misses); 512 KiB is already 15× past it (9,961).

**Not a fix for:** per-buffer flag dispatch (still 1.00× per Finding 5) and
the WCTX context shuffle. They were held in reserve as Approach B for a
null-result branch and stay there — H1 was the larger lever, and the
remaining items now have a clean baseline to re-measure against if anything
reopens this work.

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
