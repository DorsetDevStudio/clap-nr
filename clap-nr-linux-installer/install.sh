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
