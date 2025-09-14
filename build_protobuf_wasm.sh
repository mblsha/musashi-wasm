#!/bin/bash
# Purpose: Builds protobuf/abseil for Perfetto-enabled WASM (generates/libs for linking)

set -e

# Configuration
PROTOBUF_VERSION="v3.21.12"  # Latest stable version that works well with Emscripten
BUILD_DIR="third_party/protobuf-wasm"
INSTALL_DIR="$(pwd)/third_party/protobuf-wasm-install"

echo "Building protobuf ${PROTOBUF_VERSION} for WebAssembly..."

# Create build directory
mkdir -p ${BUILD_DIR}
cd ${BUILD_DIR}

# Clone protobuf if not already present
if [ ! -d "protobuf" ]; then
    echo "Cloning protobuf repository..."
    git clone https://github.com/protocolbuffers/protobuf.git
    cd protobuf
    git checkout ${PROTOBUF_VERSION}
else
    echo "Using existing protobuf repository..."
    cd protobuf
fi

# Build protobuf with CMake (preferred over autotools for Emscripten)
echo "Configuring protobuf with Emscripten..."
mkdir -p build_wasm
cd build_wasm

# Configure with Emscripten
emcmake cmake -G Ninja ../cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
    -Dprotobuf_BUILD_TESTS=OFF \
    -Dprotobuf_BUILD_PROTOC_BINARIES=OFF \
    -Dprotobuf_BUILD_SHARED_LIBS=OFF \
    -DBUILD_SHARED_LIBS=OFF \
    -Dprotobuf_DISABLE_RTTI=OFF \
    -Dprotobuf_BUILD_EXAMPLES=OFF \
    -DCMAKE_CXX_FLAGS="-O2 -flto -frtti -fexceptions" \
    -DCMAKE_C_FLAGS="-O2 -flto"

# Build
echo "Building protobuf..."
cmake --build . --parallel $(nproc)

# Install
echo "Installing protobuf to ${INSTALL_DIR}..."
cmake --install .

# Go back to root directory
cd ../../../..

# Also need to build abseil-cpp (protobuf dependency)
echo "Building abseil-cpp for WebAssembly..."
ABSEIL_DIR="third_party/abseil-wasm"
ABSEIL_INSTALL_DIR="$(pwd)/third_party/abseil-wasm-install"

mkdir -p ${ABSEIL_DIR}
cd ${ABSEIL_DIR}

if [ ! -d "abseil-cpp" ]; then
    echo "Cloning abseil-cpp repository..."
    git clone https://github.com/abseil/abseil-cpp.git
    cd abseil-cpp
    # Use a compatible version with protobuf v3.21.12
    git checkout 20230125.3
else
    echo "Using existing abseil-cpp repository..."
    cd abseil-cpp
fi

# Build abseil with CMake
echo "Configuring abseil-cpp with Emscripten..."
mkdir -p build_wasm
cd build_wasm

emcmake cmake -G Ninja .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=${ABSEIL_INSTALL_DIR} \
    -DABSL_BUILD_TESTING=OFF \
    -DABSL_ENABLE_INSTALL=ON \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_CXX_FLAGS="-O2 -flto -frtti -fexceptions" \
    -DCMAKE_C_FLAGS="-O2 -flto"

echo "Building abseil-cpp..."
cmake --build . --parallel $(nproc)

echo "Installing abseil-cpp to ${ABSEIL_INSTALL_DIR}..."
cmake --install .

cd ../../../..

# Build native protoc from the same version for compatibility
echo "Building native protoc for proto generation..."
cd ${BUILD_DIR}/protobuf
mkdir -p build_native
cd build_native

cmake -G Ninja ../cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -Dprotobuf_BUILD_TESTS=OFF \
    -Dprotobuf_BUILD_SHARED_LIBS=OFF

cmake --build . --target protoc --parallel $(nproc)

cd ../../../..

# Generate the perfetto.pb.cc and perfetto.pb.h from the proto file
echo "Generating perfetto protobuf files..."

# Create output directory if it doesn't exist
mkdir -p third_party/retrobus-perfetto/cpp/proto

# Use the protoc we just built for version compatibility
${BUILD_DIR}/protobuf/build_native/protoc --cpp_out=third_party/retrobus-perfetto/cpp/proto \
    -I third_party/retrobus-perfetto/proto \
    third_party/retrobus-perfetto/proto/perfetto.proto

echo "✅ Protobuf and abseil built successfully for WebAssembly!"
echo ""
echo "Libraries installed at:"
echo "  - Protobuf: ${INSTALL_DIR}"
echo "  - Abseil: ${ABSEIL_INSTALL_DIR}"
echo ""
echo "Generated protobuf files at:"
echo "  - third_party/retrobus-perfetto/cpp/proto/perfetto.pb.cc"
echo "  - third_party/retrobus-perfetto/cpp/proto/perfetto.pb.h"
echo ""
echo "Now you can enable Perfetto in your build by setting: ENABLE_PERFETTO=1"
