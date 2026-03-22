#!/usr/bin/env bash
# install-linux.sh  --  install clap-nr.clap on Linux (Ubuntu 24/25 and compatible)
#
# Usage:
#   ./install-linux.sh              # installs to ~/.clap/  (no sudo needed)
#   ./install-linux.sh --system     # installs to /usr/lib/clap/  (requires sudo)
#   ./install-linux.sh --uninstall  # removes ~/.clap/clap-nr.clap
#   ./install-linux.sh --system --uninstall  # removes /usr/lib/clap/clap-nr.clap
#
# On Linux the plugin is a single self-contained .clap file.
# All runtime libraries (fftw3, rnnoise, specbleach) are linked as system
# shared libraries installed by the package manager - no DLL copying needed.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-linux"
PLUGIN_NAME="clap-nr.clap"
PLUGIN_SRC="$BUILD_DIR/$PLUGIN_NAME"

# Standard CLAP search paths on Linux (per CLAP spec):
#   ~/.clap                    per-user   (no sudo)
#   /usr/lib/clap              system     (sudo)
#   /usr/local/lib/clap        system     (sudo)
USER_DEST="$HOME/.clap"
SYSTEM_DEST="/usr/lib/clap"

# -- Parse arguments -----------------------------------------------------------
SYSTEM=false
UNINSTALL=false

for arg in "$@"; do
    case "$arg" in
        --system)    SYSTEM=true ;;
        --uninstall) UNINSTALL=true ;;
        --help|-h)
            echo "Usage: $0 [--system] [--uninstall]"
            echo ""
            echo "  (no flags)   Install to ~/.clap/          (per-user, no sudo)"
            echo "  --system     Install to /usr/lib/clap/    (system-wide, requires sudo)"
            echo "  --uninstall  Remove the plugin instead of installing"
            exit 0 ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

# -- Choose destination --------------------------------------------------------
if [[ "$SYSTEM" == true ]]; then
    DEST="$SYSTEM_DEST"
    if [[ "$EUID" -ne 0 ]]; then
        echo "ERROR: System-wide install requires sudo."
        echo "Run:  sudo $0 --system"
        exit 1
    fi
else
    DEST="$USER_DEST"
fi

# -- Uninstall path ------------------------------------------------------------
if [[ "$UNINSTALL" == true ]]; then
    TARGET="$DEST/$PLUGIN_NAME"
    if [[ -f "$TARGET" ]]; then
        rm -f "$TARGET"
        echo "Removed: $TARGET"
    else
        echo "Not installed at $TARGET -- nothing to remove."
    fi
    exit 0
fi

# -- Check the plugin has been built -------------------------------------------
if [[ ! -f "$PLUGIN_SRC" ]]; then
    echo "ERROR: $PLUGIN_SRC not found."
    echo "Build first with:  ./build-linux.sh"
    exit 1
fi

# -- Install -------------------------------------------------------------------
mkdir -p "$DEST"
cp "$PLUGIN_SRC" "$DEST/$PLUGIN_NAME"
echo "Installed: $DEST/$PLUGIN_NAME"
echo ""
echo "You can now load the plugin in your CLAP host."
echo "If your host was already open, restart it to pick up the new version."
