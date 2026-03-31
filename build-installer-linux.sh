#!/usr/bin/env bash
# build-installer-linux.sh  --  Create self-contained Linux installer package
#
# This creates a distributable clap-nr-linux-installer.tar.gz that users can
# extract and run without needing to install any dependencies via package managers.
#
# The installer includes:
#   - Pre-built clap-nr.clap (statically linked with fftw3, rnnoise, specbleach)
#   - RNNoise weight files
#   - Simple install.sh script
#
# Only system dependencies required on target machine:
#   - glibc, libGL, libglfw3 (universally available on Linux with GUI)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-linux"
PLUGIN_NAME="clap-nr.clap"

# Extract version from version.h
VERSION_FILE="$SCRIPT_DIR/src/version.h"
if [[ -f "$VERSION_FILE" ]]; then
    VERSION=$(grep -oP '#define\s+CLAP_NR_VERSION\s+"\K[^"]+' "$VERSION_FILE" || echo "unknown")
else
    VERSION="unknown"
fi

INSTALLER_NAME="clap-nr-$VERSION-linux-installer"
INSTALLER_DIR="$SCRIPT_DIR/$INSTALLER_NAME"

echo "======================================================================"
echo "Building Linux Installer Package v$VERSION"
echo "======================================================================"
echo ""

# -- Check that static libraries exist -----------------------------------------
STATIC_DIR="$SCRIPT_DIR/libs/linux-static"
if [[ ! -d "$STATIC_DIR" ]] || [[ ! -f "$STATIC_DIR/lib/libfftw3.a" ]]; then
    echo "ERROR: Static libraries not found."
    echo "You must build them first with:"
    echo "  ./build-linux-static-libs.sh"
    echo ""
    echo "This will download and build fftw3, rnnoise, and specbleach as static"
    echo "libraries, allowing the plugin to run on any Linux distribution without"
    echo "requiring users to install these dependencies."
    exit 1
fi

# -- Build the plugin with static linking --------------------------------------
echo "Building clap-nr with static dependencies..."
echo ""

./build-linux.sh

if [[ ! -f "$BUILD_DIR/$PLUGIN_NAME" ]]; then
    echo "ERROR: Build failed - $BUILD_DIR/$PLUGIN_NAME not found"
    exit 1
fi

echo ""
echo "======================================================================"
echo "Creating installer package..."
echo "======================================================================"

# -- Create installer directory ------------------------------------------------
rm -rf "$INSTALLER_DIR"
mkdir -p "$INSTALLER_DIR"

# Copy plugin
cp "$BUILD_DIR/$PLUGIN_NAME" "$INSTALLER_DIR/"

# Copy RNNoise weights
cp "$SCRIPT_DIR/libs/rnnoise/rnnoise_weights_small.bin" "$INSTALLER_DIR/"
cp "$SCRIPT_DIR/libs/rnnoise/rnnoise_weights_large.bin" "$INSTALLER_DIR/"

# Copy documentation
cp "$SCRIPT_DIR/README.md" "$INSTALLER_DIR/"
cp "$SCRIPT_DIR/LICENSE" "$INSTALLER_DIR/"
cp "$SCRIPT_DIR/THIRD-PARTY-NOTICES.md" "$INSTALLER_DIR/"

# -- Create simple install script ----------------------------------------------
cat > "$INSTALLER_DIR/install.sh" << 'INSTALL_SCRIPT_EOF'
#!/usr/bin/env bash
# install.sh - Install clap-nr plugin on Linux
#
# Usage:
#   ./install.sh          # Install to ~/.clap/ (recommended)
#   ./install.sh --system # Install to /usr/lib/clap/ (requires sudo)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_NAME="clap-nr.clap"

# Standard CLAP search paths
USER_DEST="$HOME/.clap"
SYSTEM_DEST="/usr/lib/clap"

# Parse arguments
SYSTEM=false
for arg in "$@"; do
    case "$arg" in
        --system) SYSTEM=true ;;
        --help|-h)
            echo "Usage: $0 [--system]"
            echo ""
            echo "  (no flags)  Install to ~/.clap/        (per-user, recommended)"
            echo "  --system    Install to /usr/lib/clap/  (system-wide, requires sudo)"
            exit 0 ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

# Choose destination
if [[ "$SYSTEM" == true ]]; then
    DEST="$SYSTEM_DEST"
    if [[ "$EUID" -ne 0 ]]; then
        echo "ERROR: System-wide install requires sudo."
        echo "Run: sudo $0 --system"
        exit 1
    fi
else
    DEST="$USER_DEST"
fi

# Install
mkdir -p "$DEST"
cp "$SCRIPT_DIR/$PLUGIN_NAME" "$DEST/"
cp "$SCRIPT_DIR/rnnoise_weights_small.bin" "$DEST/"
cp "$SCRIPT_DIR/rnnoise_weights_large.bin" "$DEST/"

echo ""
echo "======================================================================"
echo "Installation complete!"
echo "======================================================================"
echo ""
echo "Plugin installed to: $DEST/$PLUGIN_NAME"
echo ""
echo "The clap-nr plugin provides 5 noise reduction algorithms:"
echo "  NR1: Adaptive LMS (best for constant tones)"
echo "  NR2: Spectral MMSE (best for broadband noise)"
echo "  NR3: RNNoise neural network (best for voice + background noise)"
echo "  NR4: Spectral bleach (adaptive spectral subtraction)"
echo "  NR0: Spectral tone notcher (removes specific frequencies)"
echo ""
echo "Load the plugin in your CLAP-compatible audio host."
echo "If the host is already running, restart it to detect the new plugin."
echo ""
INSTALL_SCRIPT_EOF

chmod +x "$INSTALLER_DIR/install.sh"

# -- Create uninstall script ---------------------------------------------------
cat > "$INSTALLER_DIR/uninstall.sh" << 'UNINSTALL_SCRIPT_EOF'
#!/usr/bin/env bash
# uninstall.sh - Remove clap-nr plugin from Linux

set -euo pipefail

PLUGIN_NAME="clap-nr.clap"
USER_DEST="$HOME/.clap"
SYSTEM_DEST="/usr/lib/clap"

# Parse arguments
SYSTEM=false
for arg in "$@"; do
    case "$arg" in
        --system) SYSTEM=true ;;
        --help|-h)
            echo "Usage: $0 [--system]"
            echo ""
            echo "  (no flags)  Remove from ~/.clap/"
            echo "  --system    Remove from /usr/lib/clap/ (requires sudo)"
            exit 0 ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

# Choose destination
if [[ "$SYSTEM" == true ]]; then
    DEST="$SYSTEM_DEST"
    if [[ "$EUID" -ne 0 ]]; then
        echo "ERROR: System-wide uninstall requires sudo."
        echo "Run: sudo $0 --system"
        exit 1
    fi
else
    DEST="$USER_DEST"
fi

# Uninstall
REMOVED=false
for file in "$PLUGIN_NAME" rnnoise_weights_small.bin rnnoise_weights_large.bin; do
    if [[ -f "$DEST/$file" ]]; then
        rm -f "$DEST/$file"
        echo "Removed: $DEST/$file"
        REMOVED=true
    fi
done

if [[ "$REMOVED" == false ]]; then
    echo "clap-nr is not installed at $DEST"
fi
UNINSTALL_SCRIPT_EOF

chmod +x "$INSTALLER_DIR/uninstall.sh"

# -- Create README for installer -----------------------------------------------
cat > "$INSTALLER_DIR/README-INSTALLER.txt" << 'README_EOF'
clap-nr - Noise Reduction Plugin for CLAP
==========================================

This is a standalone Linux installer package that requires no additional
dependencies beyond what's typically available on any Linux system with a GUI.

INSTALLATION
------------

1. Extract this archive to any location
2. Open a terminal in this directory
3. Run the installer:

   ./install.sh

   This installs to ~/.clap/ (user-level, recommended)

   For system-wide installation (all users):

   sudo ./install.sh --system

UNINSTALLATION
--------------

Run the uninstall script from this directory:

   ./uninstall.sh

   Or for system-wide removal:

   sudo ./uninstall.sh --system

SYSTEM REQUIREMENTS
-------------------

This plugin should work on any modern Linux distribution with:
- glibc 2.31 or newer (Ubuntu 20.04+, Debian 11+, Fedora 30+, etc.)
- OpenGL/Mesa (for GUI)
- GLFW3 (for GUI)

These are standard libraries available on virtually all Linux desktop systems.

The plugin uses statically-linked builds of:
- FFTW3 (Fast Fourier Transform library)
- RNNoise (Xiph.org's neural network denoiser)
- libspecbleach (Spectral noise reduction)

So you don't need to install these separately.

USAGE
-----

After installation, load "clap-nr" in your CLAP-compatible DAW or audio host.

The plugin provides 5 noise reduction algorithms - see README.md for details.

SUPPORT
-------

For issues, visit: https://github.com/yourusername/clap-nr
Website: https://clapnr.com

README_EOF

# -- Create tarball ------------------------------------------------------------
echo ""
echo "Creating tarball..."

# Create dist directory if it doesn't exist
DIST_DIR="$SCRIPT_DIR/dist"
mkdir -p "$DIST_DIR"

cd "$SCRIPT_DIR"
tar czf "$DIST_DIR/${INSTALLER_NAME}.tar.gz" "$INSTALLER_NAME"

# Clean up temporary installer directory
rm -rf "$INSTALLER_DIR"

# -- Show result ---------------------------------------------------------------
TARBALL="$DIST_DIR/${INSTALLER_NAME}.tar.gz"
SIZE=$(du -h "$TARBALL" | cut -f1)

echo ""
echo "======================================================================"
echo "Linux installer package created successfully!"
echo "======================================================================"
echo ""
echo "Package: $TARBALL"
echo "Size: $SIZE"
echo ""
echo "To test the installer:"
echo "  1. Extract: tar xzf $TARBALL"
echo "  2. cd $INSTALLER_NAME"
echo "  3. ./install.sh"
echo ""
echo "This package can be distributed to Linux users who can install"
echo "clap-nr without needing to install fftw3, rnnoise, or specbleach."
echo ""
