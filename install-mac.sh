#!/usr/bin/env bash
# install-mac.sh  --  install clap-nr.clap on macOS (Apple Silicon / arm64)
#
# Usage:
#   ./install-mac.sh              # install to ~/Library/Audio/Plug-Ins/CLAP/  (per-user)
#   ./install-mac.sh --system     # install to /Library/Audio/Plug-Ins/CLAP/   (requires sudo)
#   ./install-mac.sh --uninstall  # remove from ~/Library/Audio/Plug-Ins/CLAP/
#   ./install-mac.sh --system --uninstall
#
# Standard CLAP search paths on macOS (per CLAP spec):
#   ~/Library/Audio/Plug-Ins/CLAP/    per-user   (no sudo)
#   /Library/Audio/Plug-Ins/CLAP/     system      (sudo)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_NAME="clap-nr.clap"
PLUGIN_SRC="$SCRIPT_DIR/build-mac/$PLUGIN_NAME"

USER_DEST="$HOME/Library/Audio/Plug-Ins/CLAP"
SYSTEM_DEST="/Library/Audio/Plug-Ins/CLAP"

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
            echo "  (no flags)   Install to ~/Library/Audio/Plug-Ins/CLAP/  (per-user, no sudo)"
            echo "  --system     Install to /Library/Audio/Plug-Ins/CLAP/   (system-wide, sudo)"
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
    if [[ -e "$TARGET" ]]; then
        rm -rf "$TARGET"
        echo "Removed: $TARGET"
    else
        echo "Not installed at $TARGET -- nothing to remove."
    fi
    exit 0
fi

# -- Check the plugin has been built -------------------------------------------
if [[ ! -f "$PLUGIN_SRC" ]]; then
    echo "ERROR: $PLUGIN_SRC not found."
    echo "Build first with:  ./build-mac.sh"
    exit 1
fi

# -- Install as a macOS bundle -------------------------------------------------
# CLAP plugins on macOS must be bundles: clap-nr.clap/Contents/MacOS/clap-nr
BUNDLE="$DEST/$PLUGIN_NAME"
BINARY_DIR="$BUNDLE/Contents/MacOS"

mkdir -p "$BINARY_DIR"
cp "$PLUGIN_SRC" "$BINARY_DIR/clap-nr"

# Copy RNNoise weights files alongside the binary so NR3 small/large models work.
# apply_nr3_model() uses dladdr() to find the binary path, then looks for the
# .bin files in the same directory (Contents/MacOS/).
WEIGHTS_SRC="$SCRIPT_DIR/libs/rnnoise"
for f in rnnoise_weights_small.bin rnnoise_weights_large.bin; do
    if [[ -f "$WEIGHTS_SRC/$f" ]]; then
        cp "$WEIGHTS_SRC/$f" "$BINARY_DIR/$f"
    else
        echo "WARNING: weights file not found: $WEIGHTS_SRC/$f"
    fi
done

# Write a minimal Info.plist so the OS recognises it as a bundle
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

echo "Installed: $BUNDLE"
echo ""
echo "You can now load the plugin in your CLAP host."
echo "If your host was already open, restart it to pick up the new version."
