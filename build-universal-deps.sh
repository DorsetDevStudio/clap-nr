#!/usr/bin/env bash
# build-universal-deps.sh
#
# Builds x86_64 static libraries for fftw3, rnnoise, and libspecbleach,
# then lipo-combines them with the existing arm64 Homebrew static libs to
# produce fat (arm64 + x86_64) universal static libraries under:
#
#   libs/mac-universal/
#       libfftw3.a
#       libfftw3f.a        (single-precision, required by libspecbleach)
#       librnnoise.a
#       libspecbleach.a
#
# After running this script once, build-mac.sh will automatically use these
# libs and produce a universal binary.  You only need to re-run if you update
# one of the dependency versions.
#
# Requirements (all available via Homebrew):
#   brew install autoconf automake libtool meson ninja cmake pkg-config fftw rnnoise
#   libspecbleach must also be installed (built from source, see build-mac.sh)
#
# Usage:
#   ./build-universal-deps.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="$SCRIPT_DIR/libs/mac-universal"
BUILD_DIR="$SCRIPT_DIR/build-mac/universal-deps"
MACOS_MIN_ARM="12.0"
MACOS_MIN_X86="10.15"

ARM_SDK="$(xcrun --sdk macosx --show-sdk-path)"

mkdir -p "$OUT_DIR"
mkdir -p "$BUILD_DIR"

echo "======================================================================="
echo " Building universal (arm64 + x86_64) static dependency libraries"
echo " Output: $OUT_DIR"
echo "======================================================================="

# -----------------------------------------------------------------------
# Helper: lipo arm64 .a + x86_64 .a → fat .a
# -----------------------------------------------------------------------
make_fat() {
    local name="$1"
    local arm_a="$2"
    local x86_a="$3"
    local out="$OUT_DIR/$name"
    echo "  lipo → $name"
    lipo -create "$arm_a" "$x86_a" -output "$out"
    lipo -info "$out"
}

# -----------------------------------------------------------------------
# 1. FFTW 3.3.10  (double-precision libfftw3 + single-precision libfftw3f)
#    Homebrew already has arm64 static libs; we cross-compile x86_64.
# -----------------------------------------------------------------------
FFTW_VERSION="3.3.10"
FFTW_TAR="$BUILD_DIR/fftw-${FFTW_VERSION}.tar.gz"
FFTW_SRC="$BUILD_DIR/fftw-${FFTW_VERSION}"
FFTW_BUILD_X86="$BUILD_DIR/fftw-x86"

echo ""
echo "--- fftw ${FFTW_VERSION} ---"

if [[ ! -f "$FFTW_TAR" ]]; then
    echo "Downloading fftw ${FFTW_VERSION}..."
    curl -L "https://www.fftw.org/fftw-${FFTW_VERSION}.tar.gz" -o "$FFTW_TAR"
fi

if [[ ! -d "$FFTW_SRC" ]]; then
    echo "Extracting..."
    tar -xzf "$FFTW_TAR" -C "$BUILD_DIR"
fi

# Build arm64 double-precision
echo "Building fftw arm64 (double)..."
rm -rf "$FFTW_BUILD_X86-arm64-double"
mkdir -p "$FFTW_BUILD_X86-arm64-double"
pushd "$FFTW_BUILD_X86-arm64-double" > /dev/null
"$FFTW_SRC/configure" \
    --host=aarch64-apple-darwin \
    --disable-shared --enable-static \
    CC="$(xcrun -f clang) -arch arm64 -target arm64-apple-macos${MACOS_MIN_ARM} -isysroot ${ARM_SDK}" \
    CFLAGS="-arch arm64 -target arm64-apple-macos${MACOS_MIN_ARM} -isysroot ${ARM_SDK} -O3" \
    --prefix="$FFTW_BUILD_X86-arm64-double/install" \
    > configure.log 2>&1
make -j"$(sysctl -n hw.logicalcpu)" >> build.log 2>&1
popd > /dev/null

# Build arm64 single-precision (--enable-float)
echo "Building fftw arm64 (single/float)..."
rm -rf "$FFTW_BUILD_X86-arm64-single"
mkdir -p "$FFTW_BUILD_X86-arm64-single"
pushd "$FFTW_BUILD_X86-arm64-single" > /dev/null
"$FFTW_SRC/configure" \
    --host=aarch64-apple-darwin \
    --disable-shared --enable-static --enable-float \
    CC="$(xcrun -f clang) -arch arm64 -target arm64-apple-macos${MACOS_MIN_ARM} -isysroot ${ARM_SDK}" \
    CFLAGS="-arch arm64 -target arm64-apple-macos${MACOS_MIN_ARM} -isysroot ${ARM_SDK} -O3" \
    --prefix="$FFTW_BUILD_X86-arm64-single/install" \
    > configure.log 2>&1
make -j"$(sysctl -n hw.logicalcpu)" >> build.log 2>&1
popd > /dev/null

# Build x86_64 double-precision
echo "Building fftw x86_64 (double)..."
rm -rf "$FFTW_BUILD_X86-double"
mkdir -p "$FFTW_BUILD_X86-double"
pushd "$FFTW_BUILD_X86-double" > /dev/null
"$FFTW_SRC/configure" \
    --host=x86_64-apple-darwin \
    --disable-shared --enable-static \
    CC="$(xcrun -f clang) -arch x86_64 -target x86_64-apple-macos${MACOS_MIN_X86} -isysroot ${ARM_SDK}" \
    CFLAGS="-arch x86_64 -target x86_64-apple-macos${MACOS_MIN_X86} -isysroot ${ARM_SDK} -O3" \
    --prefix="$FFTW_BUILD_X86-double/install" \
    > configure.log 2>&1
make -j"$(sysctl -n hw.logicalcpu)" >> build.log 2>&1
popd > /dev/null

# Build x86_64 single-precision (--enable-float)
echo "Building fftw x86_64 (single/float)..."
rm -rf "$FFTW_BUILD_X86-single"
mkdir -p "$FFTW_BUILD_X86-single"
pushd "$FFTW_BUILD_X86-single" > /dev/null
"$FFTW_SRC/configure" \
    --host=x86_64-apple-darwin \
    --disable-shared --enable-static --enable-float \
    CC="$(xcrun -f clang) -arch x86_64 -target x86_64-apple-macos${MACOS_MIN_X86} -isysroot ${ARM_SDK}" \
    CFLAGS="-arch x86_64 -target x86_64-apple-macos${MACOS_MIN_X86} -isysroot ${ARM_SDK} -O3" \
    --prefix="$FFTW_BUILD_X86-single/install" \
    > configure.log 2>&1
make -j"$(sysctl -n hw.logicalcpu)" >> build.log 2>&1
popd > /dev/null

ARM_FFTW_LIB="$FFTW_BUILD_X86-arm64-double/.libs"
make_fat "libfftw3.a"  "$ARM_FFTW_LIB/libfftw3.a"  "$FFTW_BUILD_X86-double/.libs/libfftw3.a"
make_fat "libfftw3f.a" "$FFTW_BUILD_X86-arm64-single/.libs/libfftw3f.a" "$FFTW_BUILD_X86-single/.libs/libfftw3f.a"

# -----------------------------------------------------------------------
# 2. rnnoise  (autoconf/automake project, no version tag — build both arches)
# -----------------------------------------------------------------------
RNNOISE_BUILD_ARM="$BUILD_DIR/rnnoise-arm"
RNNOISE_BUILD_X86="$BUILD_DIR/rnnoise-x86"

echo ""
echo "--- rnnoise ---"

RNNOISE_SRC="$BUILD_DIR/rnnoise-src"
if [[ ! -d "$RNNOISE_SRC/.git" ]]; then
    echo "Cloning rnnoise..."
    git clone --depth=1 https://gitlab.xiph.org/xiph/rnnoise.git "$RNNOISE_SRC"
fi

if [[ ! -f "$RNNOISE_SRC/configure" ]]; then
    echo "Bootstrapping rnnoise autotools..."
    pushd "$RNNOISE_SRC" > /dev/null
    ./autogen.sh > autogen.log 2>&1
    popd > /dev/null
fi

echo "Building rnnoise arm64..."
rm -rf "$RNNOISE_BUILD_ARM"
mkdir -p "$RNNOISE_BUILD_ARM"
pushd "$RNNOISE_BUILD_ARM" > /dev/null
"$RNNOISE_SRC/configure" \
    --host=aarch64-apple-darwin \
    --disable-shared --enable-static \
    CC="$(xcrun -f clang) -arch arm64 -target arm64-apple-macos${MACOS_MIN_ARM} -isysroot ${ARM_SDK}" \
    CFLAGS="-arch arm64 -target arm64-apple-macos${MACOS_MIN_ARM} -isysroot ${ARM_SDK} -O3" \
    --prefix="$RNNOISE_BUILD_ARM/install" \
    > configure.log 2>&1
make -j"$(sysctl -n hw.logicalcpu)" >> build.log 2>&1
popd > /dev/null

echo "Building rnnoise x86_64..."
rm -rf "$RNNOISE_BUILD_X86"
mkdir -p "$RNNOISE_BUILD_X86"
pushd "$RNNOISE_BUILD_X86" > /dev/null
"$RNNOISE_SRC/configure" \
    --host=x86_64-apple-darwin \
    --disable-shared --enable-static \
    CC="$(xcrun -f clang) -arch x86_64 -target x86_64-apple-macos${MACOS_MIN_X86} -isysroot ${ARM_SDK}" \
    CFLAGS="-arch x86_64 -target x86_64-apple-macos${MACOS_MIN_X86} -isysroot ${ARM_SDK} -O3" \
    --prefix="$RNNOISE_BUILD_X86/install" \
    > configure.log 2>&1
make -j"$(sysctl -n hw.logicalcpu)" >> build.log 2>&1
popd > /dev/null

make_fat "librnnoise.a" \
    "$RNNOISE_BUILD_ARM/.libs/librnnoise.a" \
    "$RNNOISE_BUILD_X86/.libs/librnnoise.a"

# -----------------------------------------------------------------------
# 3. libspecbleach 0.2.0  (meson + ninja)
#    Depends on fftw3f — we point it at our freshly built x86_64 fftw3f.
# -----------------------------------------------------------------------
SPECBLEACH_VERSION="0.2.0"
SPECBLEACH_TAR="$BUILD_DIR/libspecbleach-${SPECBLEACH_VERSION}.tar.gz"
SPECBLEACH_SRC="$BUILD_DIR/libspecbleach-${SPECBLEACH_VERSION}"
SPECBLEACH_BUILD_X86="$BUILD_DIR/specbleach-x86"
SPECBLEACH_INSTALL_X86="$BUILD_DIR/specbleach-x86-install"

echo ""
echo "--- libspecbleach ${SPECBLEACH_VERSION} ---"

if [[ ! -f "$SPECBLEACH_TAR" ]]; then
    echo "Downloading libspecbleach ${SPECBLEACH_VERSION}..."
    curl -L "https://github.com/lucianodato/libspecbleach/archive/refs/tags/v${SPECBLEACH_VERSION}.tar.gz" \
         -o "$SPECBLEACH_TAR"
fi

if [[ ! -d "$SPECBLEACH_SRC" ]]; then
    echo "Extracting..."
    tar -xzf "$SPECBLEACH_TAR" -C "$BUILD_DIR"
fi

# Provide a temporary pkg-config pointing at our x86_64 fftw3f static lib.
# Must be defined BEFORE the cross-file heredoc which embeds the path.
FAKE_PC_DIR="$BUILD_DIR/fake-pc-x86"
mkdir -p "$FAKE_PC_DIR"
cat > "$FAKE_PC_DIR/fftw3f.pc" <<PC
prefix=$BUILD_DIR/fftw-x86-single
libdir=$BUILD_DIR/fftw-x86-single/.libs
includedir=$(brew --prefix fftw)/include

Name: fftw3f
Description: fftw3 single precision (x86_64 static)
Version: ${FFTW_VERSION}
Libs: -L\${libdir} -lfftw3f
Cflags: -I\${includedir}
PC

# Write a cross-file for meson targeting x86_64
MESON_CROSS="$BUILD_DIR/meson-x86_64.ini"
cat > "$MESON_CROSS" <<CROSSFILE
[binaries]
c           = '$(xcrun -f clang)'
ar          = '$(xcrun -f ar)'
strip       = '$(xcrun -f strip)'
pkg-config  = 'pkg-config'

[built-in options]
c_args      = ['-arch', 'x86_64', '-target', 'x86_64-apple-macos${MACOS_MIN_X86}', '-isysroot', '${ARM_SDK}', '-O3']
c_link_args = ['-arch', 'x86_64', '-target', 'x86_64-apple-macos${MACOS_MIN_X86}', '-isysroot', '${ARM_SDK}']

[properties]
pkg_config_libdir = '${FAKE_PC_DIR}'

[host_machine]
system     = 'darwin'
cpu_family = 'x86_64'
cpu        = 'x86_64'
endian     = 'little'
CROSSFILE

echo "Building libspecbleach x86_64..."
rm -rf "$SPECBLEACH_BUILD_X86" "$SPECBLEACH_INSTALL_X86"

meson setup "$SPECBLEACH_BUILD_X86" "$SPECBLEACH_SRC" \
    --cross-file "$MESON_CROSS" \
    --default-library=static \
    --prefix="$SPECBLEACH_INSTALL_X86" \
    --buildtype=release \
    > "$BUILD_DIR/specbleach-meson.log" 2>&1

ninja -C "$SPECBLEACH_BUILD_X86" >> "$BUILD_DIR/specbleach-build.log" 2>&1

# Find the built static lib (meson puts it in the build dir)
SPECBLEACH_X86_A=$(find "$SPECBLEACH_BUILD_X86" -name "libspecbleach.a" | head -1)
if [[ -z "$SPECBLEACH_X86_A" ]]; then
    echo "ERROR: libspecbleach x86_64 .a not found after build"
    echo "Check: $BUILD_DIR/specbleach-build.log"
    exit 1
fi

# arm64: build with explicit deployment target via a native file
SPECBLEACH_BUILD_ARM="$BUILD_DIR/specbleach-arm"
SPECBLEACH_INSTALL_ARM="$BUILD_DIR/specbleach-arm-install"
ARM_FFTW3F_PC="$BUILD_DIR/fake-pc-arm"
mkdir -p "$ARM_FFTW3F_PC"
cat > "$ARM_FFTW3F_PC/fftw3f.pc" <<PC
prefix=$BUILD_DIR/fftw-x86-arm64-single
libdir=$BUILD_DIR/fftw-x86-arm64-single/.libs
includedir=$(brew --prefix fftw)/include

Name: fftw3f
Description: fftw3 single precision (arm64 static)
Version: ${FFTW_VERSION}
Libs: -L\${libdir} -lfftw3f
Cflags: -I\${includedir}
PC

# Write a native file forcing the arm64 deployment target
MESON_NATIVE_ARM="$BUILD_DIR/meson-native-arm64.ini"
cat > "$MESON_NATIVE_ARM" <<NATIVEFILE
[binaries]
c          = '$(xcrun -f clang)'
pkg-config = 'pkg-config'

[built-in options]
c_args      = ['-arch', 'arm64', '-target', 'arm64-apple-macos${MACOS_MIN_ARM}', '-isysroot', '${ARM_SDK}', '-O3']
c_link_args = ['-arch', 'arm64', '-target', 'arm64-apple-macos${MACOS_MIN_ARM}', '-isysroot', '${ARM_SDK}']
NATIVEFILE

echo "Building libspecbleach arm64 (static)..."
rm -rf "$SPECBLEACH_BUILD_ARM" "$SPECBLEACH_INSTALL_ARM"

PKG_CONFIG_PATH="$ARM_FFTW3F_PC" \
meson setup "$SPECBLEACH_BUILD_ARM" "$SPECBLEACH_SRC" \
    --native-file "$MESON_NATIVE_ARM" \
    --default-library=static \
    --prefix="$SPECBLEACH_INSTALL_ARM" \
    --buildtype=release \
    > "$BUILD_DIR/specbleach-arm-meson.log" 2>&1

PKG_CONFIG_PATH="$ARM_FFTW3F_PC" \
ninja -C "$SPECBLEACH_BUILD_ARM" >> "$BUILD_DIR/specbleach-arm-build.log" 2>&1

SPECBLEACH_ARM_A=$(find "$SPECBLEACH_BUILD_ARM" -name "libspecbleach.a" | head -1)
if [[ -z "$SPECBLEACH_ARM_A" ]]; then
    echo "ERROR: libspecbleach arm64 .a not found after build"
    echo "Check: $BUILD_DIR/specbleach-arm-build.log"
    exit 1
fi

make_fat "libspecbleach.a" "$SPECBLEACH_ARM_A" "$SPECBLEACH_X86_A"

# -----------------------------------------------------------------------
# Copy headers into the universal dir so CMakeLists can find them in one place
# -----------------------------------------------------------------------
echo ""
echo "Copying headers..."
HEADERS_DIR="$OUT_DIR/include"
mkdir -p "$HEADERS_DIR"
cp "$(brew --prefix fftw)/include/"fftw3*.h "$HEADERS_DIR/"
cp "$(brew --prefix)/include/rnnoise.h"     "$HEADERS_DIR/"
cp "$(brew --prefix)/include/"specbleach*.h "$HEADERS_DIR/" 2>/dev/null || \
    find "$(brew --prefix)/include" -name "specbleach*" -exec cp {} "$HEADERS_DIR/" \;

# -----------------------------------------------------------------------
# Done
# -----------------------------------------------------------------------
echo ""
echo "======================================================================="
echo " Universal static libs ready:"
for f in "$OUT_DIR"/*.a; do
    printf "   %-30s  " "$(basename "$f")"
    lipo -info "$f" 2>&1 | sed 's/.*are://'
done
echo ""
echo " Headers: $HEADERS_DIR"
echo ""
echo " Next: run ./build-mac.sh  -- it will automatically build a universal binary."
echo "======================================================================="
