#!/usr/bin/env python3
"""Empirically pin GNU wc's word-separator and printability behaviour.

Runs the system `wc -w` on single-codepoint probes under C and C.UTF-8, with
and without POSIXLY_CORRECT, and prints the truth table that words_scalar.cpp's
isSepCp() must reproduce. Re-run when glibc or coreutils versions change; the
conformance suite is the continuous oracle, this script is the explainer.
"""
import os
import subprocess

CPS = [0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x20, 0x85, 0xA0, 0x1680,
       *range(0x2000, 0x200B), 0x200B, 0x2028, 0x2029, 0x202F,
       0x205F, 0x2060, 0x3000, 0xFEFF]

def words(payload: bytes, locale: str, posix: bool) -> int:
    env = dict(os.environ, LC_ALL=locale)
    env.pop("POSIXLY_CORRECT", None)
    if posix:
        env["POSIXLY_CORRECT"] = "1"
    r = subprocess.run(["wc", "-w"], input=payload, capture_output=True, env=env)
    return int(r.stdout.split()[0])

def main() -> None:
    print(f"{'cp':>8} {'C':>3} {'UTF8':>5} {'UTF8+POSIX':>11}   (2 = separator)")
    for cp in CPS:
        probe = ("a" + chr(cp) + "b").encode("utf-8")
        print(f"U+{cp:04X} {words(probe, 'C', False):>3} "
              f"{words(probe, 'C.UTF-8', False):>5} "
              f"{words(probe, 'C.UTF-8', True):>11}")
    print("\nprintability (0 = barren run is not a word):")
    for payload, desc in [(b" \x01 ", "ctrl-only"), (b" \xff ", "highbyte-only"),
                          (" ​ ".encode(), "U+200B-only")]:
        print(f"  {desc:14} C={words(payload, 'C', False)} "
              f"UTF8={words(payload, 'C.UTF-8', False)}")

if __name__ == "__main__":
    main()
