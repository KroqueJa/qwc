# QWC
Have you used `wc` before? I have - it's great! But it's not as fast as it could be. Hence, some people wrote `uutils` containing `uu-wc`. Which is even greater! But it's not as fast as it could be.

`wc` holds itself to a very rigorous standard of correctness, and faithfully counts lines, words, bytes etc in whatever file-like input it is given. `qwc` on the other hand omits some of these rigorous guarantees in order to specialize on the use case of performing on files.

This allows it to be substantially faster than its two big brothers for this particular use case, common in data engineering and data science.

`qwc` is benchmarked to be around 25-50x faster than GNU `wc` on a variety of file sizes, and 3-4x faster than `uu-wc`. Wherever possible, it is compliant with the output of GNU `wc` - the exception being for byte streams that do not constitute valid characters in the output locale. For this particular case, `wc`, `uu-wc` and `qwc` all differ in their interpretations. The input flags are all identical to `wc` - hence, one can learn to use `qwc` with eg `man wc`.

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
    - [ ] NEON
