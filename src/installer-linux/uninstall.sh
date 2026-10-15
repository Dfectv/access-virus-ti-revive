#!/bin/sh
# TI Revive for Linux - uninstaller. Removes the broker, its systemd user units, the token, the plug-in, the unpacked
# Access files and the classic plug-in of TI Revive 0.7 from the Wine prefix; the udev rule is removed with sudo.
set -u
PREFIX="${WINEPREFIX:-$HOME/.wine}"
UNITS="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
if command -v systemctl >/dev/null 2>&1; then
    systemctl --user disable --now tirevive-broker.socket 2>/dev/null
    systemctl --user stop tirevive-broker.service 2>/dev/null
fi
rm -f "$UNITS/tirevive-broker.socket" "$UNITS/tirevive-broker.service"
rm -f "$HOME/.local/bin/tirevive-cc"
command -v systemctl >/dev/null 2>&1 && systemctl --user daemon-reload 2>/dev/null
rm -rf "$HOME/.local/lib/tirevive"
rm -f "${XDG_CONFIG_HOME:-$HOME/.config}/tirevive/broker.token"
rm -f "$PREFIX/drive_c/Program Files/Common Files/VST3/Virus TI (TI Revive).vst3"
rm -rf "$HOME/.vst3/TI Revive Virus Control.vst3" "$HOME/.vst3/VirusTINative.vst3"
rm -f "${XDG_CONFIG_HOME:-$HOME/.config}/tirevive/access-dir"
rm -rf "$HOME/.local/share/tirevive/access" # the unpacked Access files (your patch libraries in Documents stay)
command -v yabridgectl >/dev/null 2>&1 && yabridgectl sync --prune >/dev/null 2>&1
if [ -f /etc/udev/rules.d/70-tirevive-virus-ti.rules ]; then
    sudo rm -f /etc/udev/rules.d/70-tirevive-virus-ti.rules && sudo udevadm control --reload
fi
echo "TI Revive removed. Your patch libraries in ~/Documents/Access Music stay."
