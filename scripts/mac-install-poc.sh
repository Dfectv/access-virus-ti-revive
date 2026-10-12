#!/bin/bash
# mac-install-poc.sh - macOS stage 1 (POC) setup, run on the Mac with sudo:
#   sudo ~/tirevive/mac-install-poc.sh
# Installs the Access Virus TI plug-ins WITHOUT the Access driver package (no kext, no HAL plug-in, no MIDI
# driver, no libVirusUSB.dylib from Access) and TI Revive's libVirusUSB.dylib replacement in its place.
set -euo pipefail
cd "$(dirname "$0")"
[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
installer -pkg "Virus TI Core components only.pkg" -target /
installer -pkg "Virus TI Additional items.pkg" -target /
LIB="/Library/Application Support/Access Music/Virus TI/Common/libVirusUSB.dylib"
install -m 0755 build/libVirusUSB.dylib "$LIB"
xattr -c "$LIB" 2>/dev/null || true
# record what was installed (for a clean removal later)
pkgutil --files de.kemper.virusti.app.installer.corecomponents > installed-files.txt
pkgutil --files de.kemper.virusti.app.installer.payload >> installed-files.txt
echo "$LIB" >> installed-files.txt
echo "done: Access plug-ins (no drivers) + TI Revive libVirusUSB stub installed"
