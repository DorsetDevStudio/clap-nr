#!/usr/bin/env bash
# build-installer-mac.sh  --  build, sign, notarise and package clap-nr for macOS
#
# Produces:  dist/clap-nr-<version>.pkg
#
# Usage:
#   ./build-installer-mac.sh            # full build + sign + notarise
#   ./build-installer-mac.sh --nosign   # build only, skip signing/notarisation
#

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# -----------------------------------------------------------------------
# 1. Load credentials from .env.mac (if not already in environment)
# -----------------------------------------------------------------------
ENV_FILE="$SCRIPT_DIR/.env.mac"
if [[ -f "$ENV_FILE" ]]; then
    # shellcheck source=.env.mac
    source "$ENV_FILE"
fi

# -----------------------------------------------------------------------
# 2. Parse arguments
# -----------------------------------------------------------------------
DO_SIGN=true
for arg in "$@"; do
    case "$arg" in
        --nosign) DO_SIGN=false ;;
        --help|-h)
            echo "Usage: $0 [--nosign]"
            exit 0 ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

# -----------------------------------------------------------------------
# 3. Read version from src/version.h  (single source of truth)
# -----------------------------------------------------------------------
VER_MAJOR=$(awk '/#define CLAP_NR_VERSION_MAJOR/{gsub(/\r/,""); print $3}' "$SCRIPT_DIR/src/version.h")
VER_MINOR=$(awk '/#define CLAP_NR_VERSION_MINOR/{gsub(/\r/,""); print $3}' "$SCRIPT_DIR/src/version.h")
VER_PATCH=$(awk '/#define CLAP_NR_VERSION_PATCH/{gsub(/\r/,""); print $3}' "$SCRIPT_DIR/src/version.h")
VERSION="${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}"

echo "-----------------------------------------------------------------------"
echo " clap-nr ${VERSION} macOS installer build"
echo "-----------------------------------------------------------------------"
echo " Signing:    $( $DO_SIGN && echo yes || echo skipped --nosign )"
[[ "$DO_SIGN" == true ]] && echo " Identity:   ${APPLE_SIGNING_IDENTITY:-NOT SET}"
[[ "$DO_SIGN" == true ]] && echo " Team ID:    ${APPLE_TEAM_ID:-NOT SET}"
[[ "$DO_SIGN" == true ]] && echo " Profile:    ${NOTARY_PROFILE:-NOT SET}"
echo "-----------------------------------------------------------------------"

# -----------------------------------------------------------------------
# 4. Validate credentials when signing
# -----------------------------------------------------------------------
if [[ "$DO_SIGN" == true ]]; then
    missing=()
    [[ -z "${APPLE_SIGNING_IDENTITY:-}" ]] && missing+=("APPLE_SIGNING_IDENTITY")
    [[ -z "${APPLE_TEAM_ID:-}"           ]] && missing+=("APPLE_TEAM_ID")
    [[ -z "${NOTARY_PROFILE:-}"          ]] && missing+=("NOTARY_PROFILE")
    if [[ ${#missing[@]} -gt 0 ]]; then
        echo ""
        echo "ERROR: The following variables are not set in .env.mac:"
        for v in "${missing[@]}"; do echo "  $v"; done
        echo "Populate .env.mac and re-run, or use --nosign for a test build."
        exit 1
    fi
fi

# -----------------------------------------------------------------------
# 5. Build
# -----------------------------------------------------------------------
echo ""
echo "Building clap-nr..."
bash "$SCRIPT_DIR/build-mac.sh"

# -----------------------------------------------------------------------
# 6. Assemble the .clap bundle in a staging area
# -----------------------------------------------------------------------
STAGE_DIR="$SCRIPT_DIR/build-mac/stage"
BUNDLE_DIR="$STAGE_DIR/clap-nr.clap"
BINARY_DIR="$BUNDLE_DIR/Contents/MacOS"

echo ""
echo "Assembling bundle..."
rm -rf "$STAGE_DIR"
mkdir -p "$BINARY_DIR"

# Binary
cp "$SCRIPT_DIR/build-mac/clap-nr.clap" "$BINARY_DIR/clap-nr"

# RNNoise weights (required for NR3 small/large models)
for f in rnnoise_weights_small.bin rnnoise_weights_large.bin; do
    src="$SCRIPT_DIR/libs/rnnoise/$f"
    if [[ -f "$src" ]]; then
        cp "$src" "$BINARY_DIR/$f"
    else
        echo "WARNING: $f not found at $src -- NR3 custom models will not work"
    fi
done

# Info.plist
cat > "$BUNDLE_DIR/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key>    <string>clap-nr</string>
  <key>CFBundleIdentifier</key>    <string>com.dorsetdevstudio.clap-nr</string>
  <key>CFBundleName</key>          <string>clap-nr</string>
  <key>CFBundleVersion</key>       <string>${VERSION}</string>
  <key>CFBundleShortVersionString</key> <string>${VERSION}</string>
  <key>CFBundlePackageType</key>   <string>BNDL</string>
  <key>CFBundleSignature</key>     <string>????</string>
  <key>MinimumOSVersion</key>      <string>12.0</string>
</dict>
</plist>
PLIST

# -----------------------------------------------------------------------
# 6b. Bundle third-party dylibs into Contents/Frameworks/
#     The plugin links against fftw3, rnnoise, and libspecbleach from
#     Homebrew.  End-user machines don't have Homebrew, so we embed the
#     dylibs in the bundle and rewrite all load paths to be self-contained.
#     (Analogous to the Windows installer copying its DLLs alongside the
#     plugin -- see the Windows CMakeLists install() block.)
# -----------------------------------------------------------------------
echo ""
echo "Bundling third-party dylibs..."
FRAMEWORKS_DIR="$BUNDLE_DIR/Contents/Frameworks"
mkdir -p "$FRAMEWORKS_DIR"

PLUGIN_BINARY="$BINARY_DIR/clap-nr"

# Let the plugin binary find its bundled dylibs via @rpath
install_name_tool -add_rpath "@loader_path/../Frameworks" "$PLUGIN_BINARY"

# List the non-system dylib paths referenced by a binary.
# Skips /usr/lib/*, /System/*, and any @-prefixed entries (already relative).
_nonsy_deps() {
    otool -L "$1" | awk 'NR>1{print $1}' | while IFS= read -r dep; do
        case "$dep" in @*|/usr/lib/*|/System/*) continue ;; esac
        [[ -f "$dep" ]] && printf '%s\n' "$dep"
    done
}

# Copy a dylib into Contents/Frameworks/ (once) and rewrite the calling
# binary's LC_LOAD_DYLIB entry to use @rpath so it is location-independent.
_embed_dylib() {
    local dep="$1" patcher="$2"
    local name; name=$(basename "$dep")
    local dest="$FRAMEWORKS_DIR/$name"
    if [[ ! -f "$dest" ]]; then
        echo "  + $name"
        cp "$dep" "$dest"
        chmod u+w "$dest"
        # Self-identify as @rpath-relative so peers can reference it the same way
        install_name_tool -id "@rpath/$name" "$dest"
    fi
    install_name_tool -change "$dep" "@rpath/$name" "$patcher" 2>/dev/null || true
}

# Pass 1: direct deps of the plugin binary (fftw3, rnnoise, libspecbleach)
while IFS= read -r dep; do
    _embed_dylib "$dep" "$PLUGIN_BINARY"
done < <(_nonsy_deps "$PLUGIN_BINARY")

# Pass 2: transitive deps of the bundled dylibs
# (e.g. libspecbleach.dylib may reference libfftw3.3.dylib)
# Give each bundled dylib an @rpath pointing to its own directory so the
# dynamic linker resolves peer dylibs from Frameworks/ at runtime.
for bundled in "$FRAMEWORKS_DIR"/*.dylib; do
    install_name_tool -add_rpath "@loader_path" "$bundled" 2>/dev/null || true
    while IFS= read -r dep; do
        _embed_dylib "$dep" "$bundled"
    done < <(_nonsy_deps "$bundled")
done

# Sanity check: no non-system references should remain unresolved
echo ""
echo "Verifying bundle is self-contained..."
_bad=0
for _bin in "$PLUGIN_BINARY" "$FRAMEWORKS_DIR"/*.dylib; do
    while IFS= read -r dep; do
        echo "  ERROR: unresolved dep in $(basename "$_bin"): $dep"
        _bad=$((_bad + 1))
    done < <(_nonsy_deps "$_bin")
done
if [[ $_bad -gt 0 ]]; then
    echo "ERROR: $_bad external dylib reference(s) remain -- stopping."
    exit 1
fi
echo "Bundle is self-contained."

# -----------------------------------------------------------------------
# 7. Code-sign the bundle
# -----------------------------------------------------------------------
if [[ "$DO_SIGN" == true ]]; then
    echo ""
    echo "Code-signing bundle..."
    codesign \
        --sign         "$APPLE_SIGNING_IDENTITY" \
        --timestamp \
        --options      runtime \
        --force \
        --deep \
        --identifier   "com.dorsetdevstudio.clap-nr" \
        "$BUNDLE_DIR"
    echo "Verifying signature..."
    codesign --verify --deep --strict --verbose=2 "$BUNDLE_DIR"
    echo "Signed OK."
fi

# -----------------------------------------------------------------------
# 8. Notarise the signed bundle
#    Apple requires the bundle to be notarised before packaging.
#    We zip the signed bundle, submit the zip, wait for approval,
#    then staple the ticket directly to the bundle.
#    The .pkg is built afterwards so it contains the stapled bundle.
# -----------------------------------------------------------------------
if [[ "$DO_SIGN" == true ]]; then
    ZIP_PATH="$SCRIPT_DIR/build-mac/clap-nr-${VERSION}-notarize.zip"
    echo ""
    echo "Zipping bundle for notarisation..."
    ditto -c -k --keepParent "$BUNDLE_DIR" "$ZIP_PATH"

    echo "Submitting to Apple notary service (this takes ~1-2 minutes)..."
    xcrun notarytool submit "$ZIP_PATH" \
        --keychain-profile "$NOTARY_PROFILE" \
        --wait

    echo ""
    echo "Stapling notarisation ticket to bundle..."
    xcrun stapler staple "$BUNDLE_DIR"
    echo "Notarised and stapled OK."
fi

# -----------------------------------------------------------------------
# 9. Build the .pkg from the (now stapled) bundle
#    Two-step: pkgbuild (component pkg) → productbuild (distribution pkg)
#    productbuild locks the install location so the user gets no choice
#    of destination — it always goes to /Library/Audio/Plug-Ins/CLAP/.
# -----------------------------------------------------------------------
DIST_DIR="$SCRIPT_DIR/dist"
mkdir -p "$DIST_DIR"

PKG_ID="com.dorsetdevstudio.clap-nr.pkg"
COMPONENT_PKG="$SCRIPT_DIR/build-mac/clap-nr-${VERSION}-component.pkg"
DISTRIBUTION_PKG="$SCRIPT_DIR/build-mac/clap-nr-${VERSION}-distribution.pkg"
PKG_FINAL="$DIST_DIR/clap-nr-${VERSION}.pkg"

echo ""
echo "Building component .pkg..."
pkgbuild \
    --root             "$STAGE_DIR" \
    --identifier       "$PKG_ID" \
    --version          "$VERSION" \
    --install-location "/Library/Audio/Plug-Ins/CLAP" \
    "$COMPONENT_PKG"

# Write a distribution XML that hides the destination chooser and
# forces a domain of LocalSystem (volume root, no user home option).
DIST_XML="$SCRIPT_DIR/build-mac/distribution.xml"
cat > "$DIST_XML" <<DISTXML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>clap-nr ${VERSION}</title>

    <!-- Lock destination: system volume only, no custom location picker -->
    <domains enable_localSystem="true" enable_currentUserHome="false"
             enable_anywhere="false" />
    <options customize="never" require-scripts="false"
             hostArchitectures="arm64" />

    <pkg-ref id="${PKG_ID}" />
    <choices-outline>
        <line choice="default" />
    </choices-outline>
    <choice id="default" title="clap-nr">
        <pkg-ref id="${PKG_ID}" />
    </choice>
    <pkg-ref id="${PKG_ID}" version="${VERSION}"
             onConclusion="none">${COMPONENT_PKG##*/}</pkg-ref>
</installer-gui-script>
DISTXML

echo "Building distribution .pkg..."
productbuild \
    --distribution "$DIST_XML" \
    --package-path "$SCRIPT_DIR/build-mac" \
    "$DISTRIBUTION_PKG"

# -----------------------------------------------------------------------
# 10. Sign the distribution .pkg  (requires "Developer ID Installer" cert)
# -----------------------------------------------------------------------
INSTALLER_IDENTITY="Developer ID Installer: stuart green (${APPLE_TEAM_ID})"
if [[ "$DO_SIGN" == true ]] && security find-identity -v -p basic | grep -qF "$INSTALLER_IDENTITY"; then
    echo ""
    echo "Signing .pkg with Developer ID Installer..."
    productsign \
        --sign      "$INSTALLER_IDENTITY" \
        --timestamp \
        "$DISTRIBUTION_PKG" \
        "$PKG_FINAL"
    echo "Signed .pkg: $PKG_FINAL"
else
    cp "$DISTRIBUTION_PKG" "$PKG_FINAL"
    if [[ "$DO_SIGN" == true ]]; then
        echo ""
        echo "NOTE: 'Developer ID Installer' certificate not found -- .pkg outer wrapper is unsigned."
        echo "      The bundle inside IS notarised and stapled; the .pkg will install fine."
        echo "      To sign the .pkg: developer.apple.com → Certificates → Developer ID Installer"
    else
        echo "Unsigned .pkg: $PKG_FINAL"
    fi
fi

# -----------------------------------------------------------------------
# Done
# -----------------------------------------------------------------------
echo ""
echo "-----------------------------------------------------------------------"
echo " Done."
echo " Output: $PKG_FINAL"
echo "-----------------------------------------------------------------------"
