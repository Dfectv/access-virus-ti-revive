#!/bin/bash
# Removes TI Revive (not the Access plug-ins: use Access' own uninstaller in /Applications/Access Music for those).
set -u
echo "Removing TI Revive (your administrator password is needed) ..."
launchctl bootout "gui/$(id -u)/io.github.derpsn.tirevive.vtibroker" 2>/dev/null   # agent of a 0.5.0 test build
sudo rm -f /Library/LaunchAgents/io.github.derpsn.tirevive.vtibroker.plist
sudo pkill -x vtibroker 2>/dev/null
sudo pkill -x vtihelper 2>/dev/null
sudo rm -rf "/Library/Audio/Plug-Ins/VST3/Virus TI (TI Revive).vst3" "/Library/Application Support/TI Revive"
# our replacement of Access' driver-package library (the Access plug-ins stop connecting without TI Revive)
sudo rm -f "/Library/Application Support/Access Music/Virus TI/Common/libVirusUSB.dylib"
sudo pkgutil --forget io.github.derpsn.tirevive >/dev/null 2>&1
echo "TI Revive removed."
