#!/bin/bash
# Installs the Access "Virus TI" plug-ins (VST, VST3, AU, Virus Control) from YOUR OWN Access installer
# ("Virus TI Software Suite ... .pkg"), WITHOUT its driver packages (kernel extension, HAL and MIDI drivers): those
# cannot run on Apple Silicon and TI Revive replaces them. The Access installer itself cannot leave its driver
# package out, so this script installs only its "Core components" and "Additional items" parts.
# Put the Access .pkg next to this script, or drop it onto the Terminal window when asked.
set -euo pipefail
cd "$(dirname "$0")"
# The Access plug-ins are Intel-only; TI Revive runs them in a helper under Rosetta 2. The helper is started in the
# background by your DAW, where macOS does not offer to install Rosetta - so check it here.
if ! /usr/bin/arch -x86_64 /usr/bin/true 2>/dev/null; then
    echo "Rosetta 2 is not installed. TI Revive needs it to run the (Intel-only) Access plug-in."
    printf "Install Rosetta 2 now (Apple's software licence applies)? [Y/n] "
    read -r ans
    case "$ans" in
    [nN]*) echo "Without Rosetta 2 the plug-in \"Virus TI (TI Revive)\" cannot load. Install it later with:"
           echo "    softwareupdate --install-rosetta --agree-to-license" ;;
    *) sudo /usr/sbin/softwareupdate --install-rosetta --agree-to-license ;;
    esac
fi
PKG=$(ls -1 Virus\ TI\ Software\ Suite*.pkg 2>/dev/null | head -1 || true)
if [ -z "$PKG" ]; then
    echo "Drag your Access 'Virus TI Software Suite ... .pkg' into this window and press Return:"
    read -r PKG
    PKG="${PKG%\'}"; PKG="${PKG#\'}"; PKG="${PKG//\\ / }"
fi
[ -f "$PKG" ] || { echo "not found: $PKG"; exit 1; }
WORK=$(mktemp -d /tmp/tirevive-access.XXXXXX)
trap 'rm -rf "$WORK"' EXIT
# A full Access installation (with its driver package) leaves drivers behind that macOS still runs through Rosetta on
# Apple Silicon; they hold the Virus' USB interfaces, so TI Revive cannot open it (broker: "claim interface 3:
# LIBUSB_ERROR_ACCESS", plug-in: "not connected", E134). TI Revive replaces all of them.
OLD_DRIVERS=()
for f in "/Library/Audio/MIDI Drivers/Virus TI MIDI Driver.plugin" "/Library/Audio/Plug-Ins/HAL/de.access-music.virus_ti.plugin" \
         "/Library/Audio/Plug-Ins/HAL/de.access-music.virus_ti.driver" "/Library/Extensions/AccessVirusTI.kext"; do
    [ -e "$f" ] && OLD_DRIVERS+=("$f")
done
if [ ${#OLD_DRIVERS[@]} -gt 0 ]; then
    echo "The old Access drivers are installed on this Mac:"
    printf '    %s\n' "${OLD_DRIVERS[@]}"
    echo "They cannot work on Apple Silicon, but they block the Virus for TI Revive (\"Virus TI not connected\")."
    printf "Disable them now (moved to /Library/Application Support/Access Music/Virus TI/Drivers disabled by TI Revive)? [Y/n] "
    read -r ans
    case "$ans" in
    [nN]*) echo "Left in place - TI Revive will probably not be able to use the Virus." ;;
    *) BAK="/Library/Application Support/Access Music/Virus TI/Drivers disabled by TI Revive"
       for f in "${OLD_DRIVERS[@]}"; do
           sudo mkdir -p "$BAK$(dirname "$f")"
           sudo rm -rf "$BAK$f"
           sudo mv "$f" "$BAK$f"
       done
       echo "Done. Please RESTART your Mac afterwards so macOS stops the old drivers." ;;
    esac
fi
echo "Reading $PKG ..."
pkgutil --expand "$PKG" "$WORK/x"
for part in Core_components Additional_items; do
    [ -d "$WORK/x/$part.pkg" ] || { echo "unexpected installer layout (no $part.pkg)"; exit 1; }
    pkgutil --flatten "$WORK/x/$part.pkg" "$WORK/$part.pkg"
done
echo "Installing the Access plug-ins without drivers (your administrator password is needed) ..."
sudo installer -pkg "$WORK/Core_components.pkg" -target /
sudo installer -pkg "$WORK/Additional_items.pkg" -target /
# The Access plug-ins are Intel-only: a native DAW would list (or blocklist) them as unusable "Virus TI". TI Revive
# loads them itself, so move them out of the plug-in folders into TI Revive's folder ("Uninstall TI Revive.command"
# moves them back).
PRIV="/Library/Application Support/TI Revive/Access plug-ins"
for f in "VST3/Virus TI.vst3" "VST/Virus TI.vst" "VST/Virus TI Snow.vst" "Components/Virus TI.component" \
         "Components/VirusTI.component" "Components/Virus TI Snow.component"; do
    if [ -e "/Library/Audio/Plug-Ins/$f" ]; then
        sudo mkdir -p "$PRIV/$(dirname "$f")"
        sudo rm -rf "$PRIV/$f"
        sudo mv "/Library/Audio/Plug-Ins/$f" "$PRIV/$f"
    fi
done
echo
echo "Done. In your DAW use the plug-in \"Virus TI (TI Revive)\" (VST3 or Audio Unit). The original Access plug-ins"
echo "(Intel-only) were moved to $PRIV, so your DAW no longer lists them."
echo "Allow your DAW to use the microphone (System Settings > Privacy & Security > Microphone): macOS gives"
echo "the Virus' audio only to apps with that permission."
