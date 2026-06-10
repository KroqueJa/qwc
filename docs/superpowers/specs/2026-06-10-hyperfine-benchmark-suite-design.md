# Hyperfine benchmarking suite — design

**Date:** 2026-06-10
**Status:** Approved (design); pending implementation plan
**Branch context:** `avx`

## Goal

A GitHub Action that builds **the current branch** and **`main`** in Release and
uses [`hyperfine`](https://github.com/sharkdp/hyperfine) to compare counting
throughput across a matrix of `wc` flags, against three reference points:

1. **qwc @ main** — the "did this branch improve?" signal.
2. **uu-wc** (uutils coreutils) — the closest competitor.
3. **GNU `wc`** — the headline "how much faster than the standard tool" number.

It is a **comparison/reporting harness**, not a correctness check and not a hard
gate.

## Non-goals

- **No correctness assertion.** The conformance suite owns parity with `wc`.
  `-L` legitimately diverges from GNU `wc` on wide characters (qwc counts code
  points; `wc` counts `wcwidth` columns), so asserting equal *counts* here would
  be wrong. This suite measures speed only.
- **No regression gate.** CI runners are noisy; a hard fail would flake. Output
  is informational.
- **No automatic trigger.** Runs on manual dispatch only (cost/noise control).

## Decisions (with rationale)

| Decision | Choice | Why |
|---|---|---|
| Pass/fail | **Report-only** | Runner noise makes a hard gate flaky; the value is the comparison table. |
| Trigger | **`workflow_dispatch`** | On-demand; avoids re-running on every trivial push. |
| Input data | **Generated UTF-8 text** | Deterministic, no network dependency, exercises `-m`/`-w`/`-L` meaningfully. Legacy binary/control-byte data was not meaningful for `wc`-style tools. |
| On `main` | **Skip the main comparison** | Branch *is* main — nothing to compare; don't build main twice. |
| uutils install | **Pinned release binary (download)** | Fast and reproducible; `cargo install coreutils` recompiles all of uutils on every run. |
| Architecture | **Thin workflow + repo script** | Relative-speedup formatting is real work, clean in Python, painful in bash; the script is also runnable locally so perf work doesn't burn CI minutes. |

## Architecture (Approach B)

CI plumbing lives in the workflow; all benchmarking logic lives in committed,
locally-runnable Python scripts.

```
.github/workflows/benchmark.yml   # CI plumbing only (checkout, build, install deps, invoke)
benchmarks/gen-data.py            # deterministic UTF-8 corpus generator
benchmarks/bench.py               # orchestrator: flag matrix -> hyperfine -> markdown table
benchmarks/README.md              # findings doc (already exists; unchanged here)
```

### Component 1 — `benchmarks/gen-data.py`

Deterministic UTF-8 corpus generator.

- **Args:** `--size` (bytes, default 512 MiB), `--seed` (default fixed), `--out` (path).
- **Determinism:** same `--seed` + `--size` ⇒ identical bytes, so runs are
  comparable across invocations and across branch/main.
- **Content model:** newline-terminated lines of space-separated random words.
  - Word length drawn from a small distribution (~3–12 chars).
  - Line length varied, with an occasional long line so `-L` is non-trivial.
  - A configurable fraction of words carry multibyte UTF-8 (Latin accents, the
    occasional CJK codepoint) so `-m` differs from `-c` and character-mode `-L`
    is exercised.
  - No NUL / binary bytes.
- **Output:** a single file written to the path given (in CI: the runner
  workspace on ext4).

### Component 2 — `benchmarks/bench.py`

Orchestrator. Runnable both in CI and locally.

- **Args:**
  - `--qwc <path>` — branch binary (required).
  - `--qwc-main <path>` — main binary (optional; omitted when running on main).
  - `--uuwc <cmd>` — uutils invocation, e.g. `"coreutils wc"` (default; configurable).
  - `--gwc <cmd>` — GNU wc, default `wc`.
  - `--data <file>` — corpus path (required).
  - `--warmup <N>` (default 3), `--runs <N>` (default 10).
  - `--flags <list>` — override the default matrix.
- **Flag matrix (default):** `'' (default l/w/c)`, `-l`, `-w`, `-c`, `-m`, `-L`,
  `-L -m`. (`-c` kept for completeness; it mostly measures startup since all
  three tools `fstat` the size.)
- **Per flag:** run one `hyperfine --warmup W --runs R --export-json <tmp>` over
  the present binaries (branch, main if present, uu-wc, gwc), each invoked as
  `<cmd> <flags> <data>`; parse the JSON `results[].mean`.
- **Output:** a markdown table, rows = flags, columns =
  - mean time (ms) per binary,
  - **branch vs main** speedup (omitted when no `--qwc-main`),
  - **qwc vs uu-wc** speedup,
  - **qwc vs GNU wc** speedup.

  Printed to stdout; additionally appended to `$GITHUB_STEP_SUMMARY` when that
  env var is set.

Example output shape:

```
| flag  | qwc (ms) | main (ms) | uu-wc (ms) | gwc (ms) | vs main | vs uu-wc | vs gwc |
|-------|----------|-----------|------------|----------|---------|----------|--------|
| (def) |    58.2  |    59.0   |   210.4    |  1980.1  |  1.01x  |  3.62x   | 34.0x  |
| -w    |    61.5  |    97.3   |   ...      |  ...     |  1.58x  |  ...     | ...    |
| -L    |    70.1  |    71.0   |   ...      |  ...     |  1.01x  |  ...     | ...    |
```

### Component 3 — `.github/workflows/benchmark.yml`

CI plumbing only.

- **Trigger:** `workflow_dispatch` with inputs `data_size` (default `512MiB`),
  `runs` (default `10`), `warmup` (default `3`).
- **Runner:** `ubuntu-latest` (x86_64). Hosted x86 runners support AVX2 /
  `x86-64-v3`, which the qwc Release build targets.
- **Steps:**
  1. `actions/checkout` with full history (needs `main`).
  2. Install `hyperfine`; download a **pinned** uutils `coreutils` x86_64 release
     binary (invoked as `coreutils wc`); GNU `wc` is already present.
  3. Build branch: `cmake -DCMAKE_BUILD_TYPE=Release` (project default
     `-march=x86-64-v3`) → `./qwc`.
  4. **If `github.ref` ≠ `refs/heads/main`:** add a git worktree at
     `origin/main`, build it the same way → `./qwc-main`.
  5. `python benchmarks/gen-data.py --size <data_size> --out corpus.txt`.
  6. `python benchmarks/bench.py --qwc ./qwc [--qwc-main ./qwc-main]
     --uuwc 'coreutils wc' --gwc wc --data corpus.txt --runs <runs>
     --warmup <warmup>`.
  7. Upload the hyperfine JSON + rendered markdown as a workflow artifact.
- **Report-only:** the job never fails on a slowdown.

### Component 4 — Legacy cleanup

Delete `benchmarks/bench.py` (old), `benchmarks/gen-test-data.py`, and
`benchmarks/bench-spec.yml`. The new `bench.py` / `gen-data.py` replace them.
Keep `benchmarks/README.md`.

## Risks / caveats

- **Runner noise.** Shared CI hardware varies run-to-run; hyperfine warmup +
  multiple runs mitigate but do not eliminate it. This is why the suite is
  report-only — read the ratios as indicative, especially differences under
  ~10%.
- **Runner ISA.** The Release build targets `x86-64-v3` (AVX2). If a future
  hosted runner lacks it the binary SIGILLs; documented, and fixable by pinning a
  runner label or lowering `QWC_X86_ARCH`.
- **uutils version drift.** Pinning the release binary keeps the competitor
  fixed; bump deliberately.
- **`main` build cost.** Building twice roughly doubles job time; acceptable for
  a manual-dispatch job and skipped entirely when run on main.

## Reproducing locally

```sh
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel --target qwc
# (optionally build a main checkout into ./qwc-main)
python benchmarks/gen-data.py --size 512MiB --out /tmp/corpus.txt
python benchmarks/bench.py --qwc ./qwc --qwc-main ./qwc-main \
    --uuwc 'coreutils wc' --gwc wc --data /tmp/corpus.txt
```
