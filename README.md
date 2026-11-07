# QWC

Have you used `wc` before? I have - it's great! But it's not as fast as it could be. Hence, some people wrote [uutils](https://github.com/uutils/coreutils) containing `uu-wc`. Which is even greater! But it's not as fast as it could be. So I wrote `qwc`. `qwc` is as fast as it can be.

Why is it faster than the above mentioned byte counters? Many reasons, here are some:
1. It doesn't hold itself to as rigorous a standard of correctness with regards to exotic locales. `qwc` specializes in the UTF-8 and C locales, where 99.99% of modern use cases live.
2. It employs a novel SIMD-based, threaded architecture that is both faster and more cache-efficient than the traditional scalar approach. It is probably not faster on a single-core machine.

**Update 2026-06-24**: `qwc` now validates rectangularity of csv files! For more info, do:
```bash
qwc --validate-csv --help
```
It's about 5-12x faster than `zsv check`.

# Installation

## Homebrew
```shell
brew install kroqueja/qwc/qwc
qwc --version
```

## Binaries
Grab the latest release binary (the link always points at the newest tag):

```sh
# Linux x86-64 (needs an AVX2-capable CPU -- anything from ~2015 onward)
curl -fsSL https://github.com/KroqueJa/qwc/releases/latest/download/qwc-linux-x86_64.tar.gz | tar xz

# macOS (Apple Silicon)
curl -fsSL https://github.com/KroqueJa/qwc/releases/latest/download/qwc-macos-arm64.tar.gz | tar xz
```

This drops a `qwc` binary in the current directory; move it somewhere on your
`PATH` (e.g. `mv qwc ~/.local/bin/`). Check what you got with `qwc --version`.

On any other platform (or a pre-AVX2 CPU), build from source: clone the repo
and run `cmake -S . -B build && cmake --build build` -- the build picks the
right SIMD (or scalar) implementation for your machine automatically.

# Benchmarks

See the [engineering journal](../qwc-companion/docs/journal/README.md) for measured results and tradeoffs, and [benchmarking](docs/benchmarking.md) to reproduce comparisons.

# How AI is used in this project
I feel that it's important to state that the core of this project is designed, benchmarked, and implemented by myself. The first version of the project, then called `wcl`, is from a time when AI was not a thing to the extent that it is today in 2026. This is not to say that AI does not play a significant part in the development of the project. AI has enabled rapid development on time-consuming tasks. AI assistance has helped build the conformance suite, tests, SIMD ports, and analysis tooling. The developer designs and reviews the core architecture and performance work. Current behavior and limitations are documented in the [conformance guide](conformance/README.md); historical findings live in the journal.

# The Creed of Speed (Zoom Zoom)
A note on why performance matters to me.

Partly, it's because optimization is fun. There is something deeply satisfying about making a machine do the same work with fewer instructions, fewer cycles, and less wasted effort.

But I also think the industry - dare I say humanity? - has become annoyingly comfortable with waste. Efficient programs consume less energy, require less hardware, and emit less CO2 to accomplish the same task as inefficient ones. *Computer programs are only faster if they are more efficient.*

Most software does not become slow because the underlying problems are challenging. It becomes slow because we tolerate inefficiency. Sometimes, performance is spent providing security, monitoring, or features. That's fine. "Performance is the currency of computing," according to Bjarne Stroustrup. But very often, software is slow because software teams are taught that 50 half-baked features are better than three good ones. We learn as programmers that premature optimization is evil, and all optimization that does not stem from a user complaint or opex report is premature. I humbly posit that this is dumb and bad.

Hence; this project is equally a challenge, a hobby, and a small act of protest. If we could spend less time waiting for our pocket computers to turn on, we could spend more time with our children, in the sun, or hand-tuning SIMD.
