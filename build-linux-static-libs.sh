#!/usr/bin/env bash
# build-linux-static-libs.sh  --  Build static libraries for standalone Linux distribution
#
# This creates a libs/linux-static directory with statically-linkable versions of:
#   - libfftw3.a / libfftw3f.a
#   - librnnoise.a  
#   - libspecbleach.a
#
# These allow creating a self-contained clap-nr.clap that works on any Linux
# distribution without requiring users to install dependencies.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STATIC_DIR="$SCRIPT_DIR/libs/linux-static"
BUILD_TEMP="$(mktemp -d)"

echo "======================================================================"
echo "Building static libraries for standalone Linux distribution"
echo "======================================================================"
echo "Target directory: $STATIC_DIR"
echo "Temporary build: $BUILD_TEMP"
echo ""

# Cleanup on exit
cleanup() {
    echo "Cleaning up temporary build directory..."
    rm -rf "$BUILD_TEMP"
}
trap cleanup EXIT

mkdir -p "$STATIC_DIR/lib"
mkdir -p "$STATIC_DIR/include"

cd "$BUILD_TEMP"

# ============================================================================
# Build static fftw3
# ============================================================================
echo ""
echo "======================================================================"
echo "Building FFTW3 (double precision)..."
echo "======================================================================"

wget http://www.fftw.org/fftw-3.3.10.tar.gz
tar xzf fftw-3.3.10.tar.gz
cd fftw-3.3.10

./configure --prefix="$STATIC_DIR" \
    --enable-static --disable-shared \
    --enable-threads --with-combined-threads \
    CFLAGS="-O3 -fPIC"

make -j$(nproc)
make install

# Single precision
cd "$BUILD_TEMP"
rm -rf fftw-3.3.10
tar xzf fftw-3.3.10.tar.gz
cd fftw-3.3.10

echo ""
echo "======================================================================"
echo "Building FFTW3 (single precision)..."
echo "======================================================================"

./configure --prefix="$STATIC_DIR" \
    --enable-static --disable-shared \
    --enable-threads --with-combined-threads \
    --enable-float \
    CFLAGS="-O3 -fPIC"

make -j$(nproc)
make install

cd "$BUILD_TEMP"

# ============================================================================
# Build static rnnoise
# ============================================================================
echo ""
echo "======================================================================"
echo "Building RNNoise..."
echo "======================================================================"

git clone https://github.com/xiph/rnnoise.git
cd rnnoise

./autogen.sh
./configure --prefix="$STATIC_DIR" \
    --enable-static --disable-shared \
    CFLAGS="-O3 -fPIC"

make -j$(nproc)
make install

cd "$BUILD_TEMP"

# ============================================================================
# Build static specbleach
# ============================================================================
echo ""
echo "======================================================================"
echo "Building libspecbleach..."
echo "======================================================================"

git clone https://github.com/lucianodato/libspecbleach.git
cd libspecbleach

meson setup build \
    --prefix="$STATIC_DIR" \
    --default-library=static \
    --buildtype=release \
    -Dc_args="-O3 -fPIC"

cd build
ninja
ninja install

# Copy the specbleach headers from libs/specbleach since the GitHub version
# has the older API that matches what we need
echo ""
echo "Copying specbleach headers from libs/specbleach..."
cp "$SCRIPT_DIR/libs/specbleach/specbleach_adenoiser.h" "$STATIC_DIR/include/"
cp "$SCRIPT_DIR/libs/specbleach/specbleach_denoiser.h" "$STATIC_DIR/include/"

echo ""
echo "======================================================================"
echo "Static libraries built successfully!"
echo "======================================================================"
echo ""
echo "Libraries installed to: $STATIC_DIR"
echo ""
ls -lh "$STATIC_DIR/lib"/*.a
echo ""
echo "You can now run: ./build-linux.sh"
echo "  It will automatically use these static libraries."
