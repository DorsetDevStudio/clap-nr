#!/usr/bin/env bash
# uninstall-linux.sh  --  remove clap-nr.clap from Linux CLAP search paths
#
# Usage:
#   ./uninstall-linux.sh            # remove from ~/.clap/  (per-user, no sudo)
#   sudo ./uninstall-linux.sh --system  # remove from /usr/lib/clap/  (system-wide)

set -euo pipefail

PLUGIN_NAME="clap-nr.clap"
USER_DEST="$HOME/.clap"
SYSTEM_DEST="/usr/lib/clap"

# -- Parse arguments -----------------------------------------------------------
SYSTEM=false

for arg in "$@"; do
    case "$arg" in
        --system)  SYSTEM=true ;;
        --help|-h)
            echo "Usage: $0 [--system]"
            echo ""
            echo "  (no flags)  Remove from ~/.clap/        (per-user, no sudo)"
            echo "  --system    Remove from /usr/lib/clap/  (system-wide, sudo)"
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

if [[ -f "$TARGET" ]]; then
    rm -f "$TARGET"
    echo "Removed: $TARGET"
else
    echo "Not installed at $TARGET -- nothing to remove."
fi
