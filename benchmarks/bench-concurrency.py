#!/usr/bin/env python3
"""Measure nested file/range threading against file-only threading.

Deterministic, physically written ASCII corpus; warm-cache intent (not a
guarantee if RAM is insufficient). No shell or competitor process is timed.
Optional --baseline compares another binary with both scheduling settings.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import resource
import statistics
import subprocess
import time


def measure(command, env):
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    start = time.perf_counter()
    result = subprocess.run(command, env=env, capture_output=True, check=True)
    wall = time.perf_counter() - start
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    return result.stdout, dict(
        wall_s=wall, cpu_s=after.ru_utime + after.ru_stime
        - before.ru_utime - before.ru_stime,
        voluntary_switches=after.ru_nvcsw - before.ru_nvcsw,
        involuntary_switches=after.ru_nivcsw - before.ru_nivcsw)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--qwc', required=True)
    ap.add_argument('--baseline')
    ap.add_argument('--data', default='benchmarks/test-data/concurrency')
    ap.add_argument('--file-mib', type=int, default=256)
    ap.add_argument('--runs', type=int, default=7)
    ap.add_argument('--warmup', type=int, default=2)
    ap.add_argument('--json-out', required=True)
    args = ap.parse_args()
    if args.file_mib < 128 or args.runs < 1 or args.warmup < 1:
        ap.error('require file-mib >= 128, runs >= 1, warmup >= 1')
    binaries = {'current': str(Path(args.qwc).resolve())}
    if args.baseline:
        binaries['baseline'] = str(Path(args.baseline).resolve())
    root = Path(args.data).resolve()
    root.mkdir(parents=True, exist_ok=True)
    # 32-byte line: four words, one newline; exact counts without another tool.
    line = b'alpha beta gamma delta         \n'
    assert len(line) == 32
    block = line * (1024 * 1024 // len(line))
    paths = []
    sizes = {}
    # Reuse uniform files in the uneven case; add one larger outlier.
    for i, mib in enumerate([args.file_mib] * 16 + [args.file_mib * 4]):
        path = root / f'ascii-v1-{i:02d}-{mib}MiB.txt'
        # Rewrite to guarantee corpus contents and avoid sparse-file shortcuts.
        with path.open('wb') as f:
            for _ in range(mib):
                f.write(block)
        paths.append(str(path))
        sizes[str(path)] = mib * 1024 * 1024
    cases = {'one': paths[:1], 'four': paths[:4], 'sixteen': paths[:16],
             'uneven': [paths[16]] + paths[:7]}
    env = os.environ.copy()
    env['LC_ALL'] = 'C'
    env.pop('POSIXLY_CORRECT', None)
    payload = dict(host=platform.platform(), logical_cpus=os.cpu_count(),
                   locale='C', file_mib=args.file_mib, runs=args.runs,
                   warmup=args.warmup, seed=42,
                   binaries={k: dict(path=v, sha256=hashlib.sha256(
                       Path(v).read_bytes()).hexdigest()) for k, v in binaries.items()},
                   cells=[])
    rng = random.Random(42)
    print('| Case | Mode | Binary / scheduling | Wall ms | CPU ms | Invol. switches |', flush=True)
    print('|---|---|---|---:|---:|---:|', flush=True)
    for case, targets in cases.items():
        for mode, flags in [('lines', ['-l']), ('words', ['-w']), ('default', [])]:
            variants = {}
            for name, binary in binaries.items():
                for scheduling in ['default', 'file-only']:
                    # Threshold exceeds every file, suppressing inner workers.
                    extra = ([] if scheduling == 'default' else
                             ['--bytes-per-thread', str(max(sizes.values()) + 1)])
                    variants[f'{name}/{scheduling}'] = [binary] + extra + flags + targets
            samples = {name: [] for name in variants}
            expected = [sizes[p] for p in targets]
            if len(targets) > 1:
                expected.append(sum(expected))
            for iteration in range(args.warmup + args.runs):
                order = list(variants)
                rng.shuffle(order)
                for name in order:
                    output, sample = measure(variants[name], env)
                    rows = output.decode().splitlines()
                    assert len(rows) == len(expected), (name, rows)
                    for row, size in zip(rows, expected):
                        counts = ([size // 32] if mode == 'lines' else
                                  [size // 8] if mode == 'words' else
                                  [size // 32, size // 8, size])
                        assert list(map(int, row.split()[:len(counts)])) == counts, row
                    if iteration >= args.warmup:
                        samples[name].append(sample)
            for name, values in samples.items():
                med = {k: statistics.median(v[k] for v in values) for k in values[0]}
                payload['cells'].append(dict(case=case, mode=mode, variant=name,
                    bytes=sum(sizes[p] for p in targets), command=variants[name],
                    samples=values, median=med))
                print(f'| {case} | {mode} | {name} | {med["wall_s"]*1000:.2f} | '
                      f'{med["cpu_s"]*1000:.2f} | {med["involuntary_switches"]:.0f} |', flush=True)
            Path(args.json_out).write_text(json.dumps(payload, indent=2) + '\n')


if __name__ == '__main__':
    main()
