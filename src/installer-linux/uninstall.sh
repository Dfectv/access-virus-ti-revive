#!/bin/sh
# TI Revive for Linux - uninstaller. Removes the broker, its systemd user units, the token and the plug-in from the
# Wine prefix. The Access software in the Wine prefix and Wine / yabridge stay (remove them yourself if you like);
# the udev rule is removed with sudo.
set -u
PREFIX="${WINEPREFIX:-$HOME/.wine}"
UNITS="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
if command -v systemctl >/dev/null 2>&1; then
    systemctl --user disable --now tirevive-broker.socket 2>/dev/null
    systemctl --user stop tirevive-broker.service 2>/dev/null
fi
rm -f "$UNITS/tirevive-broker.socket" "$UNITS/tirevive-broker.service"
command -v systemctl >/dev/null 2>&1 && systemctl --user daemon-reload 2>/dev/null
rm -rf "$HOME/.local/lib/tirevive"
rm -f "${XDG_CONFIG_HOME:-$HOME/.config}/tirevive/broker.token"
rm -f "$PREFIX/drive_c/Program Files/Common Files/VST3/Virus TI (TI Revive).vst3"
rm -rf "$HOME/.vst3/TI Revive Virus Control.vst3" "$HOME/.vst3/VirusTINative.vst3"
rm -f "${XDG_CONFIG_HOME:-$HOME/.config}/tirevive/access-dir"
command -v yabridgectl >/dev/null 2>&1 && yabridgectl sync --prune >/dev/null 2>&1
if [ -f /etc/udev/rules.d/70-tirevive-virus-ti.rules ]; then
    sudo rm -f /etc/udev/rules.d/70-tirevive-virus-ti.rules && sudo udevadm control --reload
fi
echo "TI Revive removed. The Access software is still in the Wine prefix $PREFIX."
