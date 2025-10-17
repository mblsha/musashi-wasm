#!/usr/bin/env bash

set -euo pipefail

echo "Running musashi-wasm CI tests..."

# Ensure `timeout` is available (mandatory)
if ! command -v timeout >/dev/null 2>&1; then
  echo "Error: 'timeout' command is required but not found on PATH." >&2
  echo "Install coreutils (e.g., 'sudo apt-get install coreutils') and retry." >&2
  exit 2
fi

# Generic helper to run a workspace's CI tests with a timeout
run_workspace_ci() {
  local workspace="$1"   # e.g., @m68k/core
  local seconds="$2"     # e.g., 30
  echo "Running ${workspace} tests (${seconds}s timeout)..."
  if ! timeout "${seconds}s" npm run test:ci --workspace="${workspace}"; then
    local rc=$?
    if [[ $rc -eq 124 ]]; then
      echo "${workspace} tests timed out after ${seconds}s" >&2
    else
      echo "${workspace} tests failed with exit code ${rc}" >&2
    fi
    exit $rc
  fi
}

# Define test matrix as workspace:timeout pairs
tests=(
  "@m68k/memory:30"
  "@m68k/core:30"
)

for spec in "${tests[@]}"; do
  IFS=":" read -r ws secs <<<"${spec}"
  run_workspace_ci "${ws}" "${secs}"
done

echo "Building npm-package bundle for integration smoke test (with Perfetto)..."
if ! timeout 60 ENABLE_PERFETTO=1 npm --prefix npm-package run build; then
  rc=$?
  if [[ $rc -eq 124 ]]; then
    echo "npm-package build timed out after 60s" >&2
  else
    echo "npm-package build failed with exit code ${rc}" >&2
  fi
  exit $rc
fi

echo "Running npm-package integration smoke test (30s timeout)..."
if ! timeout 30 node npm-package/test/integration.mjs; then
  rc=$?
  if [[ $rc -eq 124 ]]; then
    echo "npm-package integration test timed out after 30s" >&2
  else
    echo "npm-package integration test failed with exit code ${rc}" >&2
  fi
  exit $rc
fi

echo "Running npm-package browser smoke test (60s timeout)..."
if ! timeout 60 npm --prefix npm-package run test:browser; then
  rc=$?
  if [[ $rc -eq 124 ]]; then
    echo "npm-package browser smoke test timed out after 60s" >&2
  else
    echo "npm-package browser smoke test failed with exit code ${rc}" >&2
  fi
  exit $rc
fi

echo "Validating packaged npm tarball exposes Perfetto tracing..."
if ! timeout 120 node npm-package/test/package-consumer.mjs; then
  rc=$?
  if [[ $rc -eq 124 ]]; then
    echo "npm-package consumer verification timed out after 120s" >&2
  else
    echo "npm-package consumer verification failed with exit code ${rc}" >&2
  fi
  exit $rc
fi

echo "All tests completed"
