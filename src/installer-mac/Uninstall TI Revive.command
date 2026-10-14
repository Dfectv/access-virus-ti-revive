#!/bin/bash
# Removes TI Revive (not the Access plug-ins: use Access' own uninstaller in /Applications/Access Music for those).
set -u
echo "Removing TI Revive (your administrator password is needed) ..."
launchctl bootout "gui/$(id -u)/io.github.derpsn.tirevive.vtibroker" 2>/dev/null   # agent of a 0.5.0 test build
sudo rm -f /Library/LaunchAgents/io.github.derpsn.tirevive.vtibroker.plist
sudo pkill -x vtibroker 2>/dev/null
sudo pkill -x vtihelper 2>/dev/null
# put the Access plug-ins back where Access installed them ("Install Access Virus TI software.command" moved them)
PRIV="/Library/Application Support/TI Revive/Access plug-ins"
for f in "VST3/Virus TI.vst3" "VST/Virus TI.vst" "VST/Virus TI Snow.vst" "Components/Virus TI.component" \
         "Components/VirusTI.component" "Components/Virus TI Snow.component"; do
    if [ -e "$PRIV/$f" ]; then
        sudo mkdir -p "/Library/Audio/Plug-Ins/$(dirname "$f")"
        sudo rm -rf "/Library/Audio/Plug-Ins/$f"
        sudo mv "$PRIV/$f" "/Library/Audio/Plug-Ins/$f"
    fi
done
sudo rm -rf "/Library/Audio/Plug-Ins/VST3/TI Revive Virus Control.vst3" "$HOME/Library/Audio/Plug-Ins/VST3/VirusTINative.vst3"
sudo rm -rf "/Library/Audio/Plug-Ins/VST3/Virus TI (TI Revive).vst3" \
    "/Library/Audio/Plug-Ins/Components/Virus TI (TI Revive).component" "/Library/Application Support/TI Revive"
# our replacement of Access' driver-package library (the Access plug-ins stop connecting without TI Revive)
sudo rm -f "/Library/Application Support/Access Music/Virus TI/Common/libVirusUSB.dylib"
sudo pkgutil --forget io.github.derpsn.tirevive >/dev/null 2>&1
echo "TI Revive removed."
