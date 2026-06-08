"""
Inputs for the conformance suite: a hand-picked set of edge cases plus seeded,
reproducible fuzz generators across several input shapes (ASCII text, valid
UTF-8, and binary garbage). Everything is raw `bytes`; the harness classifies
each blob (ascii / valid-utf8) to decide which parity rules apply.

Non-ASCII literals are written with explicit \\u / \\U escapes so the byte
content is unambiguous regardless of how this source file is stored.
"""

from __future__ import annotations

import random


# ---------------------------------------------------------------------------
# Curated / known inputs -- the cases a human would think to check.
# ---------------------------------------------------------------------------
def curated() -> list[tuple[str, bytes]]:
    cases: list[tuple[str, bytes]] = [
        ("empty", b""),
        ("single-space", b" "),
        ("single-newline", b"\n"),
        ("single-char-no-nl", b"a"),
        ("single-char-nl", b"a\n"),
        ("no-trailing-newline", b"abc\ndef"),
        ("trailing-newline", b"abc\ndef\n"),
        ("only-spaces", b"      "),
        ("only-whitespace-mixed", b"   \t\t\n\n  \n"),
        ("all-whitespace-kinds", b" \t\n\v\f\r"),
        ("words-one-line", b"the quick brown fox jumps over the lazy dog\n"),
        ("words-multi-line", b"the quick brown fox\njumps over\nthe lazy dog\n"),
        ("leading-trailing-spaces", b"   hello   world   \n"),
        ("many-blank-lines", b"\n\n\n\n\n"),
        ("crlf", b"line one\r\nline two\r\nline three\r\n"),
        ("cr-only", b"a\rb\rc\r"),
        ("tabs", b"a\tb\tc\td\n"),
        ("tab-no-expansion", b"a\tb\n"),  # wc -L counts bytes, not tab stops
        ("interior-blank-line", b"alpha beta\ngamma\n\nx y z\n"),
        ("spaces-then-newline", b"word    \nword2\n"),
        ("long-line", b"x" * 10000 + b"\n"),
        ("long-line-no-nl", b"y" * 9999),
        ("many-short-lines", b"x\n" * 2000),
        ("many-words-one-line", b"w " * 5000 + b"\n"),
        ("embedded-nul", b"a\x00b\x00c\n"),
        ("nul-only", b"\x00\x00\x00\x00"),
        ("nul-and-words", b"foo\x00bar baz\x00\n"),
        ("high-ascii-control", b"\x01\x02\x03 word \x7f\n"),
        # Valid UTF-8 (explicit escapes so the bytes are unambiguous).
        ("utf8-accent", "café\n".encode("utf-8")),               # café
        ("utf8-cjk", "你好世界\n".encode("utf-8")),    # 你好世界
        ("utf8-emoji",
         "\U0001f600\U0001f389\U0001f680\n".encode("utf-8")),
        ("utf8-mixed",
         "naïve café 你好 \U0001f600 façade\n".encode("utf-8")),
        ("utf8-combining", "é café\n".encode("utf-8")),    # combining acute
        ("utf8-bom", "﻿hello world\n".encode("utf-8")),
        ("utf8-nbsp", "foo bar baz\n".encode("utf-8")),           # non-breaking space
        ("utf8-ideographic-space", "a　b c\n".encode("utf-8")),    # U+3000
        ("utf8-line-sep-2028", "line1 line2 word\n".encode("utf-8")),
        ("utf8-long", ("héllo wörld " * 1000 + "\n").encode("utf-8")),
        ("utf8-no-trailing-nl", "café 你好 façade".encode("utf-8")),
        # Invalid UTF-8 (binary-ish) -- wcl's interpretation is allowed for -m/-w.
        ("invalid-utf8-lone-cont", b"abc\x80\x81def\n"),
        ("invalid-utf8-truncated", b"caf\xc3"),  # dangling lead byte
        ("invalid-utf8-overlong", b"\xc0\xaf word\n"),
        ("high-bytes", bytes(range(0x80, 0x100)) + b"\n"),
        ("all-byte-values", bytes(range(256))),
        # A few-MB input so the chunk-stitch paths run with real thread splits
        # even at the default bytes-per-thread.
        ("multi-mb-text", (b"lorem ipsum dolor sit amet\n" * 200000)),
    ]
    return cases


# ---------------------------------------------------------------------------
# Fuzz generators -- seeded for reproducibility (a failing case is replayable).
# ---------------------------------------------------------------------------
_ASCII_WS = " \t\n\v\f\r"
_ASCII_WORD = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,;!?-"

# Unicode whitespace code points are deliberately excluded from word content so
# that "valid UTF-8" fuzz inputs stay comparable to `wc -w` on ASCII whitespace.
_UNICODE_WS = {
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x20, 0x85, 0xA0, 0x1680, 0x2000, 0x2001,
    0x2002, 0x2003, 0x2004, 0x2005, 0x2006, 0x2007, 0x2008, 0x2009, 0x200A,
    0x2028, 0x2029, 0x202F, 0x205F, 0x3000,
}


def _rand_binary(rng: random.Random, n: int) -> bytes:
    return rng.randbytes(n)


def _rand_ascii(rng: random.Random, n: int) -> bytes:
    alpha = (_ASCII_WORD + _ASCII_WS * 3)  # weight toward separators
    return "".join(rng.choice(alpha) for _ in range(n)).encode("ascii")


def _rand_texty(rng: random.Random, n_words: int) -> bytes:
    out: list[str] = []
    # Maybe lead with whitespace so leading-separator handling is exercised.
    if rng.random() < 0.3:
        out.append("".join(rng.choice(_ASCII_WS) for _ in range(rng.randint(1, 4))))
    for _ in range(n_words):
        wlen = rng.randint(1, 12)
        out.append("".join(rng.choice(_ASCII_WORD) for _ in range(wlen)))
        out.append("".join(rng.choice(_ASCII_WS) for _ in range(rng.randint(1, 3))))
    if out and rng.random() < 0.5:  # sometimes drop the trailing whitespace
        out.pop()
    return "".join(out).encode("ascii")


def _rand_codepoint(rng: random.Random) -> str:
    while True:
        cp = rng.randint(0x21, 0x10FFFF)
        if 0xD800 <= cp <= 0xDFFF:        # surrogates are not valid scalars
            continue
        if cp in _UNICODE_WS:             # keep word content non-whitespace
            continue
        if 0xFDD0 <= cp <= 0xFDEF:        # noncharacters
            continue
        if (cp & 0xFFFE) == 0xFFFE:       # ...FFFE / ...FFFF noncharacters
            continue
        return chr(cp)


def _rand_utf8(rng: random.Random, n_words: int) -> bytes:
    out: list[str] = []
    for _ in range(n_words):
        wlen = rng.randint(1, 8)
        out.append("".join(_rand_codepoint(rng) for _ in range(wlen)))
        # Separate words with ASCII whitespace only (see _UNICODE_WS note).
        out.append("".join(rng.choice(" \t\n") for _ in range(rng.randint(1, 3))))
    if out and rng.random() < 0.5:
        out.pop()
    return "".join(out).encode("utf-8")


def _rand_mixed(rng: random.Random, n: int) -> bytes:
    """Mostly text, with random high bytes injected -- usually invalid UTF-8."""
    base = bytearray(_rand_texty(rng, max(1, n // 6)))
    for _ in range(max(1, n // 40)):
        if base:
            base[rng.randrange(len(base))] = rng.randint(0x80, 0xFF)
    return bytes(base)


def fuzz(seed: int, count: int) -> list[tuple[str, bytes]]:
    """Generate *count* reproducible fuzz inputs cycling through the shapes."""
    rng = random.Random(seed)
    out: list[tuple[str, bytes]] = []
    for i in range(count):
        # Occasionally make a big input so multithreaded chunking is exercised
        # even without the bytes-per-thread stress knob.
        big = rng.random() < 0.05
        strat = i % 5
        if strat == 0:
            n = rng.randint(0, 4096) if not big else rng.randint(1 << 20, 1 << 21)
            data = _rand_binary(rng, n)
            kind = "binary"
        elif strat == 1:
            n = rng.randint(0, 4096) if not big else rng.randint(1 << 20, 1 << 21)
            data = _rand_ascii(rng, n)
            kind = "ascii"
        elif strat == 2:
            w = rng.randint(0, 800) if not big else rng.randint(80000, 160000)
            data = _rand_texty(rng, w)
            kind = "texty"
        elif strat == 3:
            w = rng.randint(0, 400) if not big else rng.randint(40000, 80000)
            data = _rand_utf8(rng, w)
            kind = "utf8"
        else:
            n = rng.randint(0, 4096) if not big else rng.randint(1 << 20, 1 << 21)
            data = _rand_mixed(rng, n)
            kind = "mixed"
        out.append((f"fuzz-{seed}-{i:04d}-{kind}", data))
    return out
