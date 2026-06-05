#!/bin/bash

echo "=== Big files ==="
hyperfine --warmup 1 \
  "./wcl --bytes-per-thread 4194304 benchmarks/test-data/big*" \
  "./wcl --bytes-per-thread 8388608 benchmarks/test-data/big*" \
  "./wcl --bytes-per-thread 16777216 benchmarks/test-data/big*" \
  "./wcl --bytes-per-thread 33554432 benchmarks/test-data/big*" \
  "./wcl --bytes-per-thread 67108864 benchmarks/test-data/big*" \
  "./wcl --bytes-per-thread 134217728 benchmarks/test-data/big*" \
  "./wcl --bytes-per-thread 268435456 benchmarks/test-data/big*" \
  "./wcl --bytes-per-thread 536870912 benchmarks/test-data/big*" \
  "./wcl --bytes-per-thread 1073741824 benchmarks/test-data/big*"

echo ""
echo "=== Medium files ==="
hyperfine --warmup 1 \
  "./wcl --bytes-per-thread 4194304 benchmarks/test-data/medium*" \
  "./wcl --bytes-per-thread 8388608 benchmarks/test-data/medium*" \
  "./wcl --bytes-per-thread 16777216 benchmarks/test-data/medium*" \
  "./wcl --bytes-per-thread 33554432 benchmarks/test-data/medium*" \
  "./wcl --bytes-per-thread 67108864 benchmarks/test-data/medium*" \
  "./wcl --bytes-per-thread 134217728 benchmarks/test-data/medium*" \
  "./wcl --bytes-per-thread 268435456 benchmarks/test-data/medium*" \
  "./wcl --bytes-per-thread 536870912 benchmarks/test-data/medium*" \
  "./wcl --bytes-per-thread 1073741824 benchmarks/test-data/medium*"

echo ""
echo "=== Small files ==="
hyperfine --warmup 1 \
  "./wcl --bytes-per-thread 4194304 benchmarks/test-data/small*" \
  "./wcl --bytes-per-thread 8388608 benchmarks/test-data/small*" \
  "./wcl --bytes-per-thread 16777216 benchmarks/test-data/small*" \
  "./wcl --bytes-per-thread 33554432 benchmarks/test-data/small*" \
  "./wcl --bytes-per-thread 67108864 benchmarks/test-data/small*" \
  "./wcl --bytes-per-thread 134217728 benchmarks/test-data/small*" \
  "./wcl --bytes-per-thread 268435456 benchmarks/test-data/small*" \
  "./wcl --bytes-per-thread 536870912 benchmarks/test-data/small*" \
  "./wcl --bytes-per-thread 1073741824 benchmarks/test-data/small*"
