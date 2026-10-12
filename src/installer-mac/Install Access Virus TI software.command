#!/bin/bash
# Installs the Access "Virus TI" plug-ins (VST, VST3, AU, Virus Control) from YOUR OWN Access installer
# ("Virus TI Software Suite ... .pkg"), WITHOUT its driver packages (kernel extension, HAL and MIDI drivers): those
# cannot run on Apple Silicon and TI Revive replaces them. The Access installer itself cannot leave its driver
# package out, so this script installs only its "Core components" and "Additional items" parts.
# Put the Access .pkg next to this script, or drop it onto the Terminal window when asked.
set -euo pipefail
cd "$(dirname "$0")"
PKG=$(ls -1 Virus\ TI\ Software\ Suite*.pkg 2>/dev/null | head -1 || true)
if [ -z "$PKG" ]; then
    echo "Drag your Access 'Virus TI Software Suite ... .pkg' into this window and press Return:"
    read -r PKG
    PKG="${PKG%\'}"; PKG="${PKG#\'}"; PKG="${PKG//\\ / }"
fi
[ -f "$PKG" ] || { echo "not found: $PKG"; exit 1; }
WORK=$(mktemp -d /tmp/tirevive-access.XXXXXX)
trap 'rm -rf "$WORK"' EXIT
echo "Reading $PKG ..."
pkgutil --expand "$PKG" "$WORK/x"
for part in Core_components Additional_items; do
    [ -d "$WORK/x/$part.pkg" ] || { echo "unexpected installer layout (no $part.pkg)"; exit 1; }
    pkgutil --flatten "$WORK/x/$part.pkg" "$WORK/$part.pkg"
done
echo "Installing the Access plug-ins without drivers (your administrator password is needed) ..."
sudo installer -pkg "$WORK/Core_components.pkg" -target /
sudo installer -pkg "$WORK/Additional_items.pkg" -target /
echo
echo "Done. In your DAW use the plug-in \"Virus TI (TI Revive)\" (VST3)."
echo "Allow your DAW to use the microphone (System Settings > Privacy & Security > Microphone): macOS gives"
echo "the Virus' audio only to apps with that permission."
