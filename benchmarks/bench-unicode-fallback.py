#!/usr/bin/env python3
"""Matched-byte UTF-8 word-count probes, including dirty classifier rows."""
import argparse
import hashlib
import json
import locale
import os
from pathlib import Path
import platform
import random
import statistics
import subprocess
import time


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--qwc', required=True)
    ap.add_argument('--baseline')
    ap.add_argument('--mib', type=int, default=256)
    ap.add_argument('--runs', type=int, default=7)
    ap.add_argument('--locale', default='en_US.UTF-8')
    ap.add_argument('--json-out', required=True)
    args = ap.parse_args()
    if args.mib < 1 or args.runs < 1:
        ap.error('mib and runs must be positive')
    locale.setlocale(locale.LC_CTYPE, args.locale)
    if locale.nl_langinfo(locale.CODESET) != 'UTF-8':
        ap.error('locale must select UTF-8')
    binary = str(Path(args.qwc).resolve())
    binaries = {'candidate': binary}
    if args.baseline:
        binaries['baseline'] = str(Path(args.baseline).resolve())
    env = dict(os.environ, LC_ALL=args.locale)
    env.pop('POSIXLY_CORRECT', None)
    root = Path('benchmarks/test-data/unicode-fallback').resolve()
    root.mkdir(parents=True, exist_ok=True)
    path = root / 'probe.txt'
    tokens = {'ascii': 'xxxx', 'latin': 'éé', 'greek': 'αα',
              'cjk': '中x', 'punctuation': '、x', 'emoji': '😀'}
    base = b'alpha beta xxxx delta'
    base += b' ' * (63 - len(base)) + b'\n'
    size = args.mib * 1024 * 1024
    result = dict(host=platform.platform(), logical_cpus=os.cpu_count(),
                  binary=binary, sha256=hashlib.sha256(Path(binary).read_bytes()).hexdigest(),
                  binaries={k: dict(path=v, sha256=hashlib.sha256(Path(v).read_bytes()).hexdigest())
                            for k, v in binaries.items()},
                  locale=args.locale, bytes=size, runs=args.runs,
                  warmup=2, seed=42, cells=[])
    rng = random.Random(42)
    cases = [(name, every) for every in (256, 16, 1) for name in tokens]
    rng.shuffle(cases)
    print('| Token | One substitution per N lines | Serial ms | Default ms |', flush=True)
    print('|---|---:|---:|---:|', flush=True)
    for name, every in cases:
        token = tokens[name].encode()
        assert len(token) == 4
        changed = base[:11] + token + base[15:]
        period = changed + base * (every - 1)
        block = period * (1024 * 1024 // len(period))
        with path.open('wb') as f:
            for _ in range(args.mib):
                f.write(block)
        variants = {name + '/' + mode: [exe] + flags
                    for name, exe in binaries.items()
                    for mode, flags in [('serial', ['--bytes-per-thread', str(size + 1)]),
                                        ('default', [])]}
        samples = {key: [] for key in variants}
        for iteration in range(args.runs + 2):
            order = list(variants)
            rng.shuffle(order)
            for key in order:
                command = variants[key] + ['-w', str(path)]
                start = time.perf_counter()
                proc = subprocess.run(command, env=env, capture_output=True, check=True)
                elapsed = time.perf_counter() - start
                assert int(proc.stdout.split()[0]) == size // 16, proc.stdout
                if iteration >= 2:
                    samples[key].append(elapsed)
        medians = {k: statistics.median(v) for k, v in samples.items()}
        result['cells'].append(dict(token=name, text=tokens[name], every=every,
                                    samples=samples, median=medians))
        for label in binaries:
            print(f'| {name} ({label}) | {every} | '
                  f'{medians[label + "/serial"]*1000:.2f} | '
                  f'{medians[label + "/default"]*1000:.2f} |', flush=True)
        Path(args.json_out).write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
