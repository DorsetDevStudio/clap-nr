#!/usr/bin/env bash
# build-linux.sh  --  configure and build clap-nr on Linux (including WSL)
#
# Usage:
#   ./build-linux.sh            # Release build, no install
#   ./build-linux.sh --debug    # Debug build
#   ./build-linux.sh --install  # Release build + install to ~/.clap/

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
            echo "  --debug    Build with debug symbols instead of Release"
            echo "  --install  Copy clap-nr.clap to ~/.clap/ after building"
            exit 0 ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-linux"

# -- Check required tools ------------------------------------------------------
for tool in cmake pkg-config gcc g++; do
    if ! command -v "$tool" &>/dev/null; then
        echo "ERROR: '$tool' not found.  Install with:"
        echo "  sudo apt-get install cmake pkg-config build-essential"
        exit 1
    fi
done

# -- Check required libraries -------------------------------------------------
echo "Checking dependencies..."
missing=()
pkg-config --exists fftw3     || missing+=("libfftw3-dev")
pkg-config --exists rnnoise   || missing+=("librnnoise-dev  (or build from source)")
pkg-config --exists libspecbleach || missing+=("libspecbleach-dev  (or build from source)")
pkg-config --exists glfw3      || missing+=("libglfw3-dev")
pkg-config --exists gl         || missing+=("libgl-dev  (mesa-common-dev)")

if [[ ${#missing[@]} -gt 0 ]]; then
    echo "ERROR: The following required libraries were not found:"
    for m in "${missing[@]}"; do
        echo "  - $m"
    done
    echo ""
    echo "Typical Ubuntu/Debian install:"
    echo "  sudo apt-get install cmake pkg-config build-essential \\"
    echo "      libfftw3-dev libglfw3-dev libgl-dev mesa-common-dev"
    echo ""
    echo "For rnnoise and specbleach, see:"
    echo "  https://github.com/xiph/rnnoise"
    echo "  https://github.com/lucianodato/libspecbleach"
    exit 1
fi
echo "All dependencies found."

# -- Configure -----------------------------------------------------------------
echo ""
echo "Configuring (${BUILD_TYPE})..."
cmake -S "$SCRIPT_DIR" \
      -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

# -- Build ---------------------------------------------------------------------
JOBS=$(nproc 2>/dev/null || echo 4)
echo ""
echo "Building with $JOBS parallel jobs..."
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$JOBS"

# -- Result --------------------------------------------------------------------
PLUGIN="$BUILD_DIR/clap-nr.clap"
if [[ ! -f "$PLUGIN" ]]; then
    echo "ERROR: Build succeeded but clap-nr.clap not found in $BUILD_DIR"
    exit 1
fi
echo ""
echo "Build complete: $PLUGIN"

# -- Install -------------------------------------------------------------------
if [[ "$INSTALL" == true ]]; then
    CLAP_DIR="$HOME/.clap"
    mkdir -p "$CLAP_DIR"
    cp "$PLUGIN" "$CLAP_DIR/clap-nr.clap"
    
    # Copy RNNoise weights files alongside the plugin so NR3 small/large models work
    WEIGHTS_SRC="$SCRIPT_DIR/libs/rnnoise"
    for f in rnnoise_weights_small.bin rnnoise_weights_large.bin; do
        if [[ -f "$WEIGHTS_SRC/$f" ]]; then
            cp "$WEIGHTS_SRC/$f" "$CLAP_DIR/$f"
        else
            echo "WARNING: weights file not found: $WEIGHTS_SRC/$f"
        fi
    done
    
    echo "Installed to: $CLAP_DIR/clap-nr.clap"
fi
