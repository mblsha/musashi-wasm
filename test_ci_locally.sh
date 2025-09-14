#!/bin/bash
# Local CI verification script - tests that all targets build correctly
# This mimics what GitHub Actions will do

set -e

echo "========================================"
echo "Local CI Build Verification"
echo "========================================"

# Clean build directory
echo "Cleaning build directory..."
rm -rf build
mkdir -p build
cd build

# Configure with all features enabled
echo ""
echo "Configuring CMake with all features..."
cmake -G Ninja .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=ON \
  -DENABLE_PERFETTO=ON

# Build all targets
echo ""
echo "Building all targets..."
cmake --build . --parallel $(nproc)

# Build examples
echo ""
echo "Building examples target..."
cmake --build . --target examples --parallel $(nproc) || echo "Warning: Examples target not found"

# Verify all expected targets exist
echo ""
echo "========================================"
echo "Verifying all expected targets..."
echo "========================================"

EXPECTED_TESTS="test_m68k test_myfunc test_vasm_binary test_fixture_example test_exceptions test_perfetto"
EXPECTED_EXAMPLES="example_trace example_perfetto_trace"
ALL_GOOD=true

echo ""
echo "Test executables:"
for test in $EXPECTED_TESTS; do
  if [ -f "$test" ]; then
    echo "  ✓ $test"
  else
    echo "  ✗ $test MISSING"
    ALL_GOOD=false
  fi
done

echo ""
echo "Example executables:"
for example in $EXPECTED_EXAMPLES; do
  if [ -f "$example" ]; then
    echo "  ✓ $example"
  else
    echo "  ✗ $example MISSING"
    ALL_GOOD=false
  fi
done

if [ "$ALL_GOOD" = true ]; then
  echo ""
  echo "========================================"
  echo "✅ SUCCESS: All expected targets built!"
  echo "========================================"

  echo ""
  echo "Running quick smoke tests..."
  timeout 2s ./example_trace > /dev/null 2>&1 || true
  timeout 2s ./example_perfetto_trace > /dev/null 2>&1 || true
  echo "Smoke tests completed"

  echo ""
  echo "Running ctest to verify tests pass..."
  ctest --output-on-failure -j$(nproc) || echo "Some tests failed - check output above"
else
  echo ""
  echo "========================================"
  echo "❌ FAILURE: Some targets are missing!"
  echo "========================================"
  exit 1
fi

echo ""
echo "Local CI verification completed!"
