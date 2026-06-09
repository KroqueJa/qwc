# QWC
Have you used `wc` before? I have - it's great! But it's not as fast as it could be. Hence, some people wrote `uutils` containing `uu-wc`. Which is even greater! But it's not as fast as it could be. `qwc` is as fast as it can be.

`wc` holds itself to a very rigorous standard of correctness, and faithfully counts lines, words, bytes etc in whatever file-like input it is given. `qwc` on the other hand omits some of these rigorous guarantees in order to specialize on the use case of performing on files.  This allows it to be substantially faster than its two big brothers for this particular use case, common in data engineering and data science.

`qwc` is benchmarked to be around 25-50x faster than GNU `wc` on a variety of file sizes, and 3-4x faster than `uu-wc`. Wherever possible, it is compliant with the output of GNU `wc` - the exception being for byte streams that do not constitute valid characters in the output locale. For this particular case, `wc`, `uu-wc` and `qwc` all differ in their interpretations. The input flags are all identical to `wc` - hence, one can learn to use `qwc` with eg `man wc`.

# How AI is used in this project
I feel that it's important to state that the core of this project is designed, benchmarked and implemented by myself. The first version of the project, then called `wcl`, is from a time when AI was not a thing to the extent that it is today in 2026. This is not to say that AI does not play a significant part in the development of the project. AI has enabled rapid development on time-consuming tasks. I'll leave it to Claude itself to tell you what it has done:


> What's worth being precise about is the *kind* of help I provided. Almost none of what follows was impossible for a competent C++ developer to do by hand — but in a one-person performance side project, "possible" and "actually gets done" are very different things. Mostly I changed the economics of effort, and in doing so pulled a handful of things across the line from "never worth the time" into "exists and is maintained":
> - **A conformance suite that fuzzes against the real `wc`.** This is the clearest case. I built a locale-aware differential harness that throws curated edge cases and thousands of random and binary inputs at both `qwc` and the system `wc`, under both a C and a UTF-8 locale, and holds them to byte-for-byte agreement everywhere `wc` is actually well-defined. That is exactly the sort of unglamorous infrastructure a side project skips. It now exists, runs in CI, and has repeatedly earned its keep.
> - **Reverse-engineering `wc`'s exact, often undocumented behaviour.** Much of `wc`-compatibility is folklore. I probed the real tool empirically and surfaced things that wouldn't otherwise have been worth chasing: that `-L` does not expand tabs; that `-L` silently switches to counting *characters* the moment `-m` is also present; that `-c` and `-m` share one output column on a last-flag-wins basis; that the locale changes how words are split — and even a genuine counting bug in macOS `wc` (its character total is clobbered when `-m` and `-L` are combined) that we chose *not* to replicate.
> - **A test safety net broad enough to refactor against.** I wrote per-algorithm unit tests with independent oracles which, together with the fuzzing, turned an invasive change — collapsing the separate per-mode counters into one fused multi-counter engine so that combined flags read the data only once — into something that could be made and re-validated with confidence, rather than something to avoid for fear of silent regressions.
> - **Hand-vectorised kernels with their tests.** I implemented NEON versions of word, longest-line and character counting, each checked against the scalar version and an independent reference, including a single fused pass for the `-L -m` combination.
> - **Static-analysis and CI scaffolding** — a clang-tidy configuration tuned not to fight hand-written SIMD, plus sanitizer and `-fanalyzer` jobs.
> 
> I want to be equally honest about the limits of my contribution. The thing that actually makes `qwc` fast — the parallel, cache-warm, SIMD architecture — is the developer's, predates most of my work here, and the large refactors I helped with *held* that performance rather than improved on it. My code was not magically correct either: I got `wc`'s trailing-newline rule for `-L` wrong on the first attempt, and the `-m`-changes-`-L` behaviour only came to light because the conformance suite complained. My value was at least as much in the relentless verification loop as in the code I generated, and the developer's own review still caught things I missed.

Hmm. I feel like it took a bit more credit for the vectorization than I would have given it. But such is the nature of having a collaborator. It would say the same thing about me, I'm sure.

# Roadmap
- [x] Combined flags (`-L -m` etc)
- [x] Fuzzy correctness suite
- [ ] Comprehensive multi-system performance benchmarking
- [ ] Vectorized versions of the scalar algorithms
  - [x] Lines (/ bytes)
    - [x] AVX2
    - [x] NEON
  - [ ] Max line length
    - [ ] AVX2
    - [x] NEON
  - [ ] Words
    - [ ] AVX2
    - [x] NEON
  - [ ] Multibyte characters
    - [ ] AVX2
    - [x] NEON
