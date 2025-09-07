#!/bin/bash
#
# Test with Real WASM - Comprehensive Local Development Script
#
# This script automates the complete build and test process for local development:
# 1. Builds WASM modules using ./build.fish (with optional Perfetto support)
# 2. Copies WASM artifacts to packages/core/wasm/
# 3. Runs TypeScript tests in packages/core/
# 4. Runs integration tests in musashi-wasm-test/
# 5. Provides detailed progress reporting and error handling
#
# Usage:
#   ./test_with_real_wasm.sh                    # Standard build
#   ENABLE_PERFETTO=1 ./test_with_real_wasm.sh  # With Perfetto tracing
#
# Environment Variables:
#   ENABLE_PERFETTO=1     Enable Perfetto tracing support
#   SKIP_WASM_BUILD=1     Skip WASM build step (use existing artifacts)
#   SKIP_CORE_TESTS=1     Skip packages/core tests
#   SKIP_INTEGRATION=1    Skip musashi-wasm-test integration tests
#   VERBOSE=1             Enable verbose output
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGES_CORE_DIR="$SCRIPT_DIR/packages/core"
WASM_TEST_DIR="$SCRIPT_DIR/musashi-wasm-test"
WASM_ARTIFACTS_DIR="$PACKAGES_CORE_DIR/wasm"

# Environment flags
ENABLE_PERFETTO="${ENABLE_PERFETTO:-0}"
SKIP_WASM_BUILD="${SKIP_WASM_BUILD:-0}"
SKIP_CORE_TESTS="${SKIP_CORE_TESTS:-0}"
SKIP_INTEGRATION="${SKIP_INTEGRATION:-0}"
VERBOSE="${VERBOSE:-0}"

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_step() {
    echo
    echo -e "${PURPLE}===========================================${NC}"
    echo -e "${PURPLE}$1${NC}"
    echo -e "${PURPLE}===========================================${NC}"
}

# Verbose execution helper
run_verbose() {
    if [ "$VERBOSE" = "1" ]; then
        echo -e "${CYAN}Running:${NC} $*"
        "$@"
    else
        "$@" > /dev/null 2>&1
    fi
}

# Check if a command exists
check_command() {
    if ! command -v "$1" &> /dev/null; then
        log_error "Required command '$1' not found. Please install it and try again."
        return 1
    fi
}

# Check if a directory exists
check_directory() {
    if [ ! -d "$1" ]; then
        log_error "Required directory '$1' not found."
        return 1
    fi
}

# Check if a file exists
check_file() {
    if [ ! -f "$1" ]; then
        log_error "Required file '$1' not found."
        return 1
    fi
}

# Dependency checks
check_dependencies() {
    log_step "Checking Dependencies"
    
    local deps_ok=true
    
    # Check for required commands
    log_info "Checking required commands..."
    for cmd in fish node npm; do
        if check_command "$cmd"; then
            log_success "$cmd found"
        else
            deps_ok=false
        fi
    done
    
    # Check for Emscripten if not skipping WASM build
    if [ "$SKIP_WASM_BUILD" != "1" ]; then
        if [ -n "$EMSDK" ]; then
            log_success "EMSDK environment detected: $EMSDK"
            if [ -f "$EMSDK/upstream/emscripten/emcc" ]; then
                log_success "emcc found at: $EMSDK/upstream/emscripten/emcc"
            else
                log_error "emcc not found in EMSDK directory"
                deps_ok=false
            fi
        else
            log_warning "EMSDK not set. Checking if emcc is in PATH..."
            if check_command "emcc"; then
                log_success "emcc found in PATH"
            else
                log_error "Emscripten not found. Please install EMSDK and source emsdk_env.sh"
                deps_ok=false
            fi
        fi
    fi
    
    # Check for required directories
    log_info "Checking project structure..."
    for dir in "$PACKAGES_CORE_DIR" "$WASM_TEST_DIR"; do
        if check_directory "$dir"; then
            log_success "Directory found: $dir"
        else
            deps_ok=false
        fi
    done
    
    # Check for required files
    log_info "Checking required files..."
    for file in "$SCRIPT_DIR/build.fish" "$PACKAGES_CORE_DIR/package.json" "$WASM_TEST_DIR/package.json"; do
        if check_file "$file"; then
            log_success "File found: $(basename "$file")"
        else
            deps_ok=false
        fi
    done
    
    # Check for Perfetto dependencies if enabled
    if [ "$ENABLE_PERFETTO" = "1" ]; then
        log_info "Checking Perfetto dependencies..."
        
        # Check for protobuf build
        if [ -f "$SCRIPT_DIR/third_party/protobuf-3.21.12/build.host/protoc" ]; then
            log_success "Protobuf host build found"
        else
            log_warning "Protobuf host build not found. Will attempt to build it automatically."
        fi
        
        # Check for retrobus-perfetto proto files
        if [ -f "$SCRIPT_DIR/third_party/retrobus-perfetto/proto/perfetto.pb.h" ]; then
            log_success "Perfetto protobuf files found"
        else
            log_warning "Perfetto protobuf files not found. Will attempt to generate them automatically."
        fi
    fi
    
    if [ "$deps_ok" = false ]; then
        log_error "Dependency check failed. Please fix the issues above."
        exit 1
    fi
    
    log_success "All dependencies OK"
}

# Build Perfetto dependencies
build_perfetto_deps() {
    if [ "$ENABLE_PERFETTO" != "1" ]; then
        return 0
    fi
    
    log_step "Building Perfetto Dependencies"
    
    # Build protobuf if needed
    if [ ! -f "$SCRIPT_DIR/build_protobuf_wasm.sh" ]; then
        log_error "build_protobuf_wasm.sh not found. Cannot build Perfetto dependencies."
        exit 1
    fi
    
    log_info "Running build_protobuf_wasm.sh..."
    cd "$SCRIPT_DIR"
    
    if [ "$VERBOSE" = "1" ]; then
        ./build_protobuf_wasm.sh
    else
        ./build_protobuf_wasm.sh > /dev/null 2>&1
    fi
    
    log_success "Perfetto dependencies built"
}

# Build WASM modules
build_wasm() {
    if [ "$SKIP_WASM_BUILD" = "1" ]; then
        log_step "Skipping WASM Build (SKIP_WASM_BUILD=1)"
        return 0
    fi
    
    log_step "Building WASM Modules"
    
    cd "$SCRIPT_DIR"
    
    # Export Perfetto flag for build.fish
    if [ "$ENABLE_PERFETTO" = "1" ]; then
        export ENABLE_PERFETTO=1
        log_info "Building with Perfetto tracing enabled"
    else
        log_info "Building without Perfetto tracing"
    fi
    
    log_info "Running ./build.fish..."
    if [ "$VERBOSE" = "1" ]; then
        ./build.fish
    else
        ./build.fish > build_output.log 2>&1 || {
            log_error "WASM build failed. Check build_output.log for details:"
            tail -20 build_output.log
            exit 1
        }
    fi
    
    # Verify WASM artifacts were created
    local artifacts=("musashi.out.mjs" "musashi.out.wasm" "musashi-node.out.mjs" "musashi-node.out.wasm")
    for artifact in "${artifacts[@]}"; do
        if [ -f "$SCRIPT_DIR/$artifact" ]; then
            log_success "Created: $artifact"
        else
            log_error "Failed to create: $artifact"
            exit 1
        fi
    done
    
    log_success "WASM build completed successfully"
}

# Copy WASM artifacts to packages/core/wasm/
copy_wasm_artifacts() {
    log_step "Copying WASM Artifacts"
    
    # Create wasm directory if it doesn't exist
    mkdir -p "$WASM_ARTIFACTS_DIR"
    
    cd "$SCRIPT_DIR"
    
    # List of files to copy
    local wasm_files=(
        "musashi-node.out.mjs"
        "musashi-node.out.js"
        "musashi-node.out.wasm"
        "musashi-node.out.wasm.map"
    )
    
    # Copy wrapper files if they exist
    local wrapper_files=(
        "musashi-node-wrapper.mjs"
        "musashi-node-wrapper.js"
        "musashi-node-wrapper.d.ts"
    )
    
    log_info "Copying WASM artifacts to $WASM_ARTIFACTS_DIR..."
    
    for file in "${wasm_files[@]}"; do
        if [ -f "$file" ]; then
            cp "$file" "$WASM_ARTIFACTS_DIR/"
            log_success "Copied: $file"
        else
            log_error "Missing WASM artifact: $file"
            exit 1
        fi
    done
    
    # Copy wrapper files if available (optional)
    for file in "${wrapper_files[@]}"; do
        if [ -f "$file" ]; then
            cp "$file" "$WASM_ARTIFACTS_DIR/"
            log_success "Copied: $file"
        else
            log_info "Optional file not found: $file"
        fi
    done
    
    log_success "WASM artifacts copied successfully"
}

# Install npm dependencies for a package
install_npm_deps() {
    local dir="$1"
    local name="$2"
    
    if [ ! -f "$dir/package.json" ]; then
        log_error "package.json not found in $dir"
        return 1
    fi
    
    cd "$dir"
    
    log_info "Installing npm dependencies for $name..."
    if [ "$VERBOSE" = "1" ]; then
        npm install
    else
        npm install > /dev/null 2>&1
    fi
    
    log_success "Dependencies installed for $name"
}

# Run TypeScript tests in packages/core
run_core_tests() {
    if [ "$SKIP_CORE_TESTS" = "1" ]; then
        log_step "Skipping Core Tests (SKIP_CORE_TESTS=1)"
        return 0
    fi
    
    log_step "Running Core TypeScript Tests"
    
    cd "$PACKAGES_CORE_DIR"
    
    # Install dependencies
    install_npm_deps "$PACKAGES_CORE_DIR" "packages/core"
    
    # Build TypeScript
    log_info "Building TypeScript..."
    if [ "$VERBOSE" = "1" ]; then
        npm run build
    else
        npm run build > /dev/null 2>&1
    fi
    
    # Run tests
    log_info "Running tests..."
    if [ "$VERBOSE" = "1" ]; then
        npm test
    else
        npm test > core_test_output.log 2>&1 || {
            log_error "Core tests failed. Check core_test_output.log for details:"
            tail -20 core_test_output.log
            return 1
        }
    fi
    
    log_success "Core tests passed"
}

# Run integration tests
run_integration_tests() {
    if [ "$SKIP_INTEGRATION" = "1" ]; then
        log_step "Skipping Integration Tests (SKIP_INTEGRATION=1)"
        return 0
    fi
    
    log_step "Running Integration Tests"
    
    cd "$WASM_TEST_DIR"
    
    # Install dependencies
    install_npm_deps "$WASM_TEST_DIR" "musashi-wasm-test"
    
    # Copy latest WASM artifacts to test directory
    log_info "Copying latest WASM artifacts to test directory..."
    cp "$SCRIPT_DIR/musashi-node.out.mjs" .
    cp "$SCRIPT_DIR/musashi-node.out.js" .
    cp "$SCRIPT_DIR/musashi-node.out.wasm" .
    cp "$SCRIPT_DIR/musashi-node.out.wasm.map" .
    
    # Run integration tests
    log_info "Running integration tests..."
    if [ "$VERBOSE" = "1" ]; then
        npm test
    else
        npm test > integration_test_output.log 2>&1 || {
            log_error "Integration tests failed. Check integration_test_output.log for details:"
            tail -20 integration_test_output.log
            return 1
        }
    fi
    
    log_success "Integration tests passed"
}

# Display usage information
show_usage() {
    echo "Test with Real WASM - Local Development Script"
    echo
    echo "Usage:"
    echo "  $0                               # Standard build and test"
    echo "  ENABLE_PERFETTO=1 $0             # Build with Perfetto tracing"
    echo "  SKIP_WASM_BUILD=1 $0             # Skip WASM build (use existing)"
    echo "  VERBOSE=1 $0                     # Show detailed output"
    echo
    echo "Environment Variables:"
    echo "  ENABLE_PERFETTO=1     Enable Perfetto tracing support"
    echo "  SKIP_WASM_BUILD=1     Skip WASM build step"
    echo "  SKIP_CORE_TESTS=1     Skip packages/core tests"
    echo "  SKIP_INTEGRATION=1    Skip integration tests"
    echo "  VERBOSE=1             Show detailed output"
    echo
    echo "Examples:"
    echo "  # Full test with Perfetto"
    echo "  ENABLE_PERFETTO=1 ./test_with_real_wasm.sh"
    echo
    echo "  # Quick test with existing WASM"
    echo "  SKIP_WASM_BUILD=1 ./test_with_real_wasm.sh"
    echo
    echo "  # Only test integration"
    echo "  SKIP_WASM_BUILD=1 SKIP_CORE_TESTS=1 ./test_with_real_wasm.sh"
}

# Main execution
main() {
    # Handle help flags
    if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
        show_usage
        exit 0
    fi
    
    # Display configuration
    log_step "Test with Real WASM - Configuration"
    echo "Working directory: $SCRIPT_DIR"
    echo "Perfetto enabled: $ENABLE_PERFETTO"
    echo "Skip WASM build: $SKIP_WASM_BUILD"
    echo "Skip core tests: $SKIP_CORE_TESTS"
    echo "Skip integration: $SKIP_INTEGRATION"
    echo "Verbose output: $VERBOSE"
    
    # Start timestamp
    START_TIME=$(date +%s)
    
    # Execute steps
    check_dependencies
    build_perfetto_deps
    build_wasm
    copy_wasm_artifacts
    run_core_tests
    run_integration_tests
    
    # Calculate elapsed time
    END_TIME=$(date +%s)
    ELAPSED=$((END_TIME - START_TIME))
    
    # Success summary
    log_step "Test Complete - Summary"
    log_success "All tests completed successfully!"
    log_info "Total elapsed time: ${ELAPSED}s"
    
    if [ "$ENABLE_PERFETTO" = "1" ]; then
        log_info "Perfetto tracing was enabled"
    fi
    
    if [ "$SKIP_WASM_BUILD" = "1" ]; then
        log_info "WASM build was skipped"
    fi
    
    echo
    echo -e "${GREEN}✅ Ready for development!${NC}"
    echo "The WASM artifacts are now available in:"
    echo "  - $WASM_ARTIFACTS_DIR"
    echo "  - $WASM_TEST_DIR"
}

# Trap to clean up on exit
cleanup() {
    if [ -f "$SCRIPT_DIR/build_output.log" ] && [ "$VERBOSE" != "1" ]; then
        rm -f "$SCRIPT_DIR/build_output.log"
    fi
    if [ -f "$PACKAGES_CORE_DIR/core_test_output.log" ] && [ "$VERBOSE" != "1" ]; then
        rm -f "$PACKAGES_CORE_DIR/core_test_output.log"
    fi
    if [ -f "$WASM_TEST_DIR/integration_test_output.log" ] && [ "$VERBOSE" != "1" ]; then
        rm -f "$WASM_TEST_DIR/integration_test_output.log"
    fi
}

trap cleanup EXIT

# Run main function
main "$@"