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
