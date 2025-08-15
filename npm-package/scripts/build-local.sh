#!/bin/bash

# Build script for local testing of the npm package

set -e

echo "🔧 Building musashi-wasm npm package..."

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if we're in the npm-package directory
if [ ! -f "package.json" ]; then
    echo -e "${RED}Error: Must run from npm-package directory${NC}"
    exit 1
fi

# Go to parent directory for building
cd ..

# Check for emscripten
if ! command -v emcc &> /dev/null; then
    echo -e "${YELLOW}Warning: emcc not found. Checking for emsdk...${NC}"
    if [ -n "$EMSDK" ]; then
        source "$EMSDK/emsdk_env.sh"
    else
        echo -e "${RED}Error: Emscripten not found. Please install emsdk.${NC}"
        exit 1
    fi
fi

# Build native components
echo "📦 Building native components..."
make clean > /dev/null 2>&1 || true
make -j8

# Build standard WASM
echo "🏗️  Building standard WASM..."
if [ -f "build_wasm_simple.sh" ]; then
    ./build_wasm_simple.sh
else
    echo -e "${YELLOW}Using emmake to build WASM...${NC}"
    emmake make -j8
    emcc m68kcpu.o m68kops.o m68kdasm.o softfloat/softfloat.o myfunc.o \
        -o musashi-node.out.js \
        -s MODULARIZE=1 \
        -s EXPORT_NAME="'MusashiModule'" \
        -s ENVIRONMENT=node \
        -s ALLOW_MEMORY_GROWTH=1 \
        -s WASM_BIGINT=1 \
        -s EXPORTED_FUNCTIONS='["_m68k_init","_m68k_execute","_m68k_pulse_reset","_m68k_cycles_run","_m68k_get_reg","_m68k_set_reg","_add_region","_clear_regions","_set_read_mem_func","_set_write_mem_func","_set_pc_hook_func","_enable_printf_logging","_malloc","_free"]' \
        -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","getValue","setValue","addFunction","removeFunction","allocateUTF8"]' \
        -O2
fi

# Copy standard build files
echo "📋 Copying standard build files..."
mkdir -p npm-package/dist
cp musashi-node.out.wasm npm-package/dist/musashi.wasm
cp musashi-node.out.wasm.map npm-package/dist/musashi.wasm.map 2>/dev/null || true
cp musashi-node.out.js npm-package/dist/musashi-loader.js

# Check if we should build Perfetto version
echo "🔍 Checking for Perfetto support..."
if [ -f "build_perfetto_wasm_simple.sh" ] && [ -d "third_party/retrobus-perfetto" ]; then
    echo "🏗️  Building Perfetto-enabled WASM..."
    
    # Generate protobuf files if needed
    if [ ! -f "third_party/retrobus-perfetto/proto/perfetto.pb.cc" ]; then
        echo "  Generating protobuf files..."
        mkdir -p third_party/retrobus-perfetto/proto
        
        if [ -f "third_party/protobuf-3.21.12/build.host/protoc" ]; then
            third_party/protobuf-3.21.12/build.host/protoc \
                --cpp_out=third_party/retrobus-perfetto/proto \
                -I third_party/retrobus-perfetto/proto \
                third_party/retrobus-perfetto/proto/perfetto.proto
        else
            echo -e "${YELLOW}  Warning: protoc not found, skipping Perfetto build${NC}"
        fi
    fi
    
    # Build with Perfetto
    if [ -f "third_party/retrobus-perfetto/proto/perfetto.pb.cc" ]; then
        ./build_perfetto_wasm_simple.sh
        cp musashi-node.out.wasm npm-package/dist/musashi-perfetto.wasm
        cp musashi-node.out.wasm.map npm-package/dist/musashi-perfetto.wasm.map 2>/dev/null || true
        cp musashi-node.out.js npm-package/dist/musashi-perfetto-loader.js
        echo -e "${GREEN}✓ Perfetto build complete${NC}"
    fi
else
    echo -e "${YELLOW}  Perfetto support not available${NC}"
fi

# Go back to npm-package directory
cd npm-package

# Install dependencies
echo "📦 Installing dependencies..."
npm install

# Generate wrapper modules
echo "🔧 Generating wrapper modules..."
node scripts/generate-wrapper.js

# Run tests
echo "🧪 Running tests..."
if npm test; then
    echo -e "${GREEN}✓ All tests passed${NC}"
else
    echo -e "${YELLOW}⚠ Some tests failed${NC}"
fi

# Create package tarball
echo "📦 Creating package tarball..."
npm pack

echo -e "${GREEN}✅ Build complete!${NC}"
echo ""
echo "Package contents:"
tar -tzf *.tgz | head -20
echo "..."
echo ""
echo "To test locally:"
echo "  npm install ./musashi-wasm-*.tgz"
echo ""
echo "To publish:"
echo "  npm publish"