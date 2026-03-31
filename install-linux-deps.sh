#!/usr/bin/env bash
# install-linux-deps.sh  --  Build and install rnnoise and specbleach from source

set -euo pipefail

TEMP_DIR=$(mktemp -d)
echo "Using temporary directory: $TEMP_DIR"

# Cleanup on exit
cleanup() {
    echo "Cleaning up temporary files..."
    rm -rf "$TEMP_DIR"
}
trap cleanup EXIT

cd "$TEMP_DIR"

# ============================================================================
# Build and install rnnoise
# ============================================================================
echo ""
echo "======================================================================"
echo "Building rnnoise..."
echo "======================================================================"

# Clone rnnoise
git clone https://github.com/xiph/rnnoise.git
cd rnnoise

# Build and install
./autogen.sh
./configure --prefix=/usr/local
make -j$(nproc)
make install
ldconfig

# Verify installation
if pkg-config --exists rnnoise; then
    echo "✓ rnnoise installed successfully"
    pkg-config --modversion rnnoise
else
    echo "✗ rnnoise installation failed"
    exit 1
fi

cd "$TEMP_DIR"

# ============================================================================
# Build and install specbleach
# ============================================================================
echo ""
echo "======================================================================"
echo "Building specbleach..."
echo "======================================================================"

# Clone specbleach
git clone https://github.com/lucianodato/libspecbleach.git
cd libspecbleach

# Build using meson
echo "Running meson setup..."
meson setup build --prefix=/usr/local --buildtype=release || {
    echo "Meson setup failed. Trying with default options..."
    meson setup build --prefix=/usr/local || exit 1
}

cd build
echo "Building with ninja..."
ninja || { echo "Build failed"; exit 1; }

echo "Installing..."
ninja install || { echo "Installation failed"; exit 1; }
ldconfig

# Check if library files exist
echo "Checking installed files..."
ls -la /usr/local/lib/*specbleach* 2>/dev/null || echo "No library files found in /usr/local/lib"
ls -la /usr/local/include/*specbleach* 2>/dev/null || echo "No header files found in /usr/local/include"
ls -la /usr/local/lib/pkgconfig/*specbleach* 2>/dev/null || echo "No pkg-config file found"

# Try to find where pkg-config was installed
PKG_CONFIG_FILE=$(find /usr/local -name "specbleach.pc" 2>/dev/null | head -n1)
if [[ -n "$PKG_CONFIG_FILE" ]]; then
    echo "Found pkg-config file: $PKG_CONFIG_FILE"
    # Make sure PKG_CONFIG_PATH includes this directory
    PKG_DIR=$(dirname "$PKG_CONFIG_FILE")
    export PKG_CONFIG_PATH="$PKG_DIR:${PKG_CONFIG_PATH:-}"
    echo "Added to PKG_CONFIG_PATH: $PKG_DIR"
fi

# Verify installation
if pkg-config --exists specbleach; then
    echo "✓ specbleach installed successfully"
    pkg-config --modversion specbleach
else
    echo "⚠ Warning: specbleach built but pkg-config can't find it"
    echo "Checking if library exists anyway..."
    if [[ -f /usr/local/lib/libspecbleach.so ]] || [[ -f /usr/local/lib/x86_64-linux-gnu/libspecbleach.so ]]; then
        echo "Library exists. You may need to set PKG_CONFIG_PATH manually."
    else
        echo "✗ specbleach installation failed - library not found"
        exit 1
    fi
fi

echo ""
echo "======================================================================"
echo "All dependencies installed successfully!"
echo "======================================================================"
echo ""

# Make sure PKG_CONFIG_PATH includes all necessary directories
echo "Setting up PKG_CONFIG_PATH..."
export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:/usr/local/lib/x86_64-linux-gnu/pkgconfig:${PKG_CONFIG_PATH:-}"
echo "export PKG_CONFIG_PATH=\"$PKG_CONFIG_PATH\"" >> ~/.bashrc

echo ""
echo "IMPORTANT: Run the following command to update your environment:"
echo "  export PKG_CONFIG_PATH=\"$PKG_CONFIG_PATH\""
echo ""
echo "Or simply restart your terminal, then run: ./build-linux.sh"
