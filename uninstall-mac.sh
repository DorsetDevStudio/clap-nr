#!/usr/bin/env bash
# uninstall-mac.sh  --  remove clap-nr.clap from macOS CLAP search paths
#
# Usage:
#   ./uninstall-mac.sh            # remove from ~/Library/Audio/Plug-Ins/CLAP/  (per-user, no sudo)
#   sudo ./uninstall-mac.sh --system  # remove from /Library/Audio/Plug-Ins/CLAP/  (system-wide)

set -euo pipefail

PLUGIN_NAME="clap-nr.clap"
USER_DEST="$HOME/Library/Audio/Plug-Ins/CLAP"
SYSTEM_DEST="/Library/Audio/Plug-Ins/CLAP"

# -- Parse arguments -----------------------------------------------------------
SYSTEM=false

for arg in "$@"; do
    case "$arg" in
        --system)  SYSTEM=true ;;
        --help|-h)
            echo "Usage: $0 [--system]"
            echo ""
            echo "  (no flags)  Remove from ~/Library/Audio/Plug-Ins/CLAP/  (per-user, no sudo)"
            echo "  --system    Remove from /Library/Audio/Plug-Ins/CLAP/   (system-wide, sudo)"
            exit 0 ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

# -- Choose destination --------------------------------------------------------
if [[ "$SYSTEM" == true ]]; then
    DEST="$SYSTEM_DEST"
    if [[ "$EUID" -ne 0 ]]; then
        echo "ERROR: System-wide uninstall requires sudo."
        echo "Run:  sudo $0 --system"
        exit 1
    fi
else
    DEST="$USER_DEST"
fi

# -- Remove --------------------------------------------------------------------
TARGET="$DEST/$PLUGIN_NAME"

if [[ -e "$TARGET" ]]; then
    rm -rf "$TARGET"
    echo "Removed: $TARGET"
else
    echo "Not installed at $TARGET -- nothing to remove."
fi
