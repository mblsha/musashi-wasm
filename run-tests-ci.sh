#!/usr/bin/env bash

set -euo pipefail

echo "Running musashi-wasm CI tests..."

# Run @m68k/memory tests (fast)
echo "Running @m68k/memory tests (30s timeout)..."
if command -v timeout >/dev/null 2>&1; then
  if ! timeout 30s npm run test:ci --workspace=@m68k/memory; then
    rc=$?
    if [[ $rc -eq 124 ]]; then
      echo "@m68k/memory tests timed out after 30s"
    else
      echo "@m68k/memory tests failed with exit code $rc"
    fi
    exit $rc
  fi
else
  npm run test:ci --workspace=@m68k/memory
fi

# Run @m68k/core tests with a hard 30s timeout (fail on timeout)
echo "Running @m68k/core tests (30s timeout)..."
if command -v timeout >/dev/null 2>&1; then
  timeout 30s npm run test:ci --workspace=@m68k/core
else
  npm run test:ci --workspace=@m68k/core
fi

echo "All tests completed"
