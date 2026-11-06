#!/usr/bin/env bash
# Repository-root invocation; writes 5 GiB of corpus at the default size.
set -euo pipefail
"$(dirname "$0")/sync-current-build.sh"
exec python3 benchmarks/bench-concurrency.py --qwc ./qwc "$@"
