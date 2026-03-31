#!/usr/bin/env bash
# build-mac.sh  --  configure and build clap-nr on macOS (Apple Silicon / arm64)
#
# Dependencies (install once via Homebrew):
#   brew install cmake pkg-config fftw rnnoise libspecbleach
#
# Usage:
#   ./build-mac.sh            # Release build
#   ./build-mac.sh --debug    # Debug build
#   ./build-mac.sh --install  # Release build + install to ~/Library/Audio/Plug-Ins/CLAP/

set -euo pipefail

# -- Parse arguments -----------------------------------------------------------
BUILD_TYPE="Release"
INSTALL=false

for arg in "$@"; do
    case "$arg" in
        --debug)   BUILD_TYPE="Debug" ;;
        --install) INSTALL=true ;;
        --help|-h)
            echo "Usage: $0 [--debug] [--install]"
            exit 0 ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-mac"

# Use universal static libs if present (built by build-universal-deps.sh),
# otherwise fall back to arm64-only Homebrew build.
UNIVERSAL_LIB_DIR="$SCRIPT_DIR/libs/mac-universal"
if [[ -f "$UNIVERSAL_LIB_DIR/libfftw3.a" && \
      -f "$UNIVERSAL_LIB_DIR/librnnoise.a" && \
      -f "$UNIVERSAL_LIB_DIR/libspecbleach.a" ]]; then
    CMAKE_ARCH="arm64;x86_64"
    echo "Universal libs found -- building arm64 + x86_64 universal binary."
else
    CMAKE_ARCH="arm64"
    echo "Universal libs not found -- building arm64 only."
    echo "(Run ./build-universal-deps.sh once to enable universal builds.)"
fi

# -- Check required tools ------------------------------------------------------
for tool in cmake pkg-config clang clang++; do
    if ! command -v "$tool" &>/dev/null; then
        echo "ERROR: '$tool' not found."
        if [[ "$tool" == "cmake" ]]; then
            echo "  Install with: brew install cmake"
        elif [[ "$tool" == "pkg-config" ]]; then
            echo "  Install with: brew install pkg-config"
        else
            echo "  Install Xcode Command Line Tools: xcode-select --install"
        fi
        exit 1
    fi
done

# -- Check required libraries -------------------------------------------------
echo "Checking dependencies..."
missing=()
if [[ "$CMAKE_ARCH" == "arm64" ]]; then
    # Homebrew dylib build — need pkg-config to find them
    pkg-config --exists fftw3         || missing+=("fftw         (brew install fftw)")
    pkg-config --exists rnnoise       || missing+=("rnnoise      (build from source: https://gitlab.xiph.org/xiph/rnnoise)")
    pkg-config --exists libspecbleach || missing+=("libspecbleach  (build from source v0.2.0: https://github.com/lucianodato/libspecbleach)")
fi

if [[ ${#missing[@]} -gt 0 ]]; then
    echo "ERROR: The following required libraries were not found:"
    for m in "${missing[@]}"; do
        echo "  - $m"
    done
    echo ""
    echo "Quick install:"
    echo "  brew install fftw"
    echo "  # Build rnnoise from source: https://gitlab.xiph.org/xiph/rnnoise"
    echo "  # Build libspecbleach v0.2.0 from source: https://github.com/lucianodato/libspecbleach"
    exit 1
fi
echo "All dependencies found."

# -- Configure -----------------------------------------------------------------
echo ""
echo "Configuring (${BUILD_TYPE}, ${CMAKE_ARCH})..."
cmake -S "$SCRIPT_DIR" \
      -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
      -DCMAKE_OSX_ARCHITECTURES="$CMAKE_ARCH" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET="12.0"

# -- Build ---------------------------------------------------------------------
JOBS=$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)
echo ""
echo "Building with $JOBS parallel jobs..."
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$JOBS"

# -- Verify the binary contains the expected architecture(s) ------------------
PLUGIN="$BUILD_DIR/clap-nr.clap"
if [[ ! -f "$PLUGIN" ]]; then
    echo "ERROR: Build succeeded but clap-nr.clap not found in $BUILD_DIR"
    exit 1
fi

echo ""
ARCH_INFO=$(lipo -info "$PLUGIN" 2>&1)
echo "Architectures: $ARCH_INFO"
if [[ "$CMAKE_ARCH" == "arm64;x86_64" ]] && ! echo "$ARCH_INFO" | grep -q "x86_64"; then
    echo "WARNING: Expected a universal binary but x86_64 slice is missing."
fi

echo ""
echo "Build complete: $PLUGIN"

# -- Install -------------------------------------------------------------------
if [[ "$INSTALL" == true ]]; then
    CLAP_DIR="$HOME/Library/Audio/Plug-Ins/CLAP"
    BUNDLE="$CLAP_DIR/clap-nr.clap"
    BINARY_DIR="$BUNDLE/Contents/MacOS"
    mkdir -p "$BINARY_DIR"
    cp "$PLUGIN" "$BINARY_DIR/clap-nr"
    cat > "$BUNDLE/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key>  <string>clap-nr</string>
  <key>CFBundleIdentifier</key>  <string>com.dorsetdevstudio.clap-nr</string>
  <key>CFBundleName</key>        <string>clap-nr</string>
  <key>CFBundlePackageType</key> <string>BNDL</string>
  <key>CFBundleVersion</key>     <string>1.0.0</string>
  <key>CFBundleSignature</key>   <string>????</string>
</dict>
</plist>
PLIST
    echo "Installed to: $BUNDLE"
fi
