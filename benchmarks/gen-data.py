#!/usr/bin/env python3
"""Deterministic UTF-8 corpus generator for the qwc benchmark suite.

Produces realistic `wc`-shaped text -- space-separated words, newline-terminated
lines, a sprinkling of multibyte UTF-8 so that `-m` differs from `-c` and
character-mode `-L` is exercised, and the occasional long line so `-L` is not
constant. The same --seed and --size always yield identical bytes, so branch and
main are measured on exactly the same input.

Deliberately NOT random binary/control-byte data: that is not meaningful for
wc-style tools (the lesson from the legacy generator this replaces).
"""
import argparse
import random
import sys

# A small pool of multibyte tokens (Latin accents, a few CJK) mixed into the
# otherwise-ASCII word stream. Kept modest so the byte/char ratio stays realistic.
MULTIBYTE_WORDS = [
    "café", "naïve", "résumé", "Über", "jalapeño", "façade", "Zürich",
    "Москва", "日本語", "你好", "안녕하세요", "Ελλάδα", "emoji😀here",
]
ASCII_ALPHABET = "abcdefghijklmnopqrstuvwxyz"


def parse_size(text: str) -> int:
    """Parse a byte count: plain int, or with a KB/MB/GB or KiB/MiB/GiB suffix."""
    t = text.strip()
    units = {
        "kib": 1024, "mib": 1024**2, "gib": 1024**3,
        "kb": 1000, "mb": 1000**2, "gb": 1000**3,
        "k": 1024, "m": 1024**2, "g": 1024**3, "b": 1,
    }
    low = t.lower()
    for suffix, mult in sorted(units.items(), key=lambda kv: -len(kv[0])):
        if low.endswith(suffix):
            return int(float(low[: -len(suffix)]) * mult)
    return int(t)


def build_word_pool(rng: random.Random, n: int, multibyte_fraction: float) -> list:
    """Pre-generate a pool of words to sample from -- far faster than minting a
    fresh random word per token when writing hundreds of MiB."""
    pool = []
    for _ in range(n):
        if rng.random() < multibyte_fraction:
            pool.append(rng.choice(MULTIBYTE_WORDS))
        else:
            length = rng.randint(2, 12)
            pool.append("".join(rng.choices(ASCII_ALPHABET, k=length)))
    return pool


def generate(out_path: str, size: int, seed: int, multibyte_fraction: float) -> None:
    rng = random.Random(seed)
    pool = build_word_pool(rng, 50_000, multibyte_fraction)

    written = 0
    chunk = []          # list[str], flushed periodically
    chunk_bytes = 0
    FLUSH_AT = 8 * 1024 * 1024  # ~8 MiB of text per write

    with open(out_path, "wb") as f:
        while written < size:
            # ~1% of lines are long (so -L is non-trivial); the rest are normal.
            if rng.random() < 0.01:
                n_words = rng.randint(80, 200)
            else:
                n_words = rng.randint(3, 18)
            line = " ".join(rng.choices(pool, k=n_words)) + "\n"
            chunk.append(line)
            chunk_bytes += len(line)  # approx (chars); refined on encode
            if chunk_bytes >= FLUSH_AT:
                data = "".join(chunk).encode("utf-8")
                f.write(data)
                written += len(data)
                chunk.clear()
                chunk_bytes = 0
        if chunk:
            data = "".join(chunk).encode("utf-8")
            f.write(data)
            written += len(data)

    print(f"wrote {written} bytes ({written / 1024**2:.1f} MiB) to {out_path} "
          f"(seed={seed})", file=sys.stderr)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--size", default="512MiB",
                    help="target size, e.g. 512MiB, 1GB, 100000000 (default 512MiB)")
    ap.add_argument("--seed", type=int, default=20260610,
                    help="RNG seed (default fixed, for determinism)")
    ap.add_argument("--multibyte-fraction", type=float, default=0.08,
                    help="fraction of pool words that are multibyte (default 0.08)")
    ap.add_argument("--out", required=True, help="output file path")
    args = ap.parse_args()
    generate(args.out, parse_size(args.size), args.seed, args.multibyte_fraction)


if __name__ == "__main__":
    main()
