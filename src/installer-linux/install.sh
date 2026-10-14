#!/bin/sh
# TI Revive for Linux - installer (run as your normal user, from the unpacked TI-Revive-<version>-linux folder).
#
#   ./install.sh [path to "Virus TI Software Suite ... 64-bit.msi"]
#
# What it does (findings/design-linux.md):
#   1. udev rule so you (not only root) may use the Virus over USB          (asks for your password once, sudo)
#   2. TI Revive broker in ~/.local/lib/tirevive + systemd user socket (starts on demand, exits when idle)
#   3. your own Access Virus TI software into your Wine prefix, without its old drivers / firmware updater
#   4. the plug-in "Virus TI (TI Revive)" into the Wine prefix, and - if yabridge is installed - bridged for your DAW
#   5. the native plug-in "TI Revive Virus Control" (Linux VST3, no Wine/yabridge needed) into ~/.vst3
# Wine and yabridge are NOT installed by this script; it tells you how if they are missing.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
PREFIX="${WINEPREFIX:-$HOME/.wine}"
LIB="$HOME/.local/lib/tirevive"
UNITS="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
CONF="${XDG_CONFIG_HOME:-$HOME/.config}/tirevive"
VST3_DIR="$PREFIX/drive_c/Program Files/Common Files/VST3"
PORT=47615

say() { printf '\n== %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -ne 0 ] || die "run install.sh as your normal user, not as root (it asks for sudo when needed)"

# --- 0. Wine -------------------------------------------------------------------------------------------------------
if ! command -v wine >/dev/null 2>&1; then
    cat <<'EOT'

Wine is not installed. TI Revive runs the Access Virus TI plug-in (Windows) through Wine and yabridge.
Install Wine with your package manager, for example:
    Ubuntu / Debian:  sudo apt install wine
    Fedora:           sudo dnf install wine
    Arch:             sudo pacman -S wine
then run ./install.sh again.
EOT
    exit 1
fi

# --- 1. USB access (udev) ------------------------------------------------------------------------------------------
RULE=/etc/udev/rules.d/70-tirevive-virus-ti.rules
virus_accessible() { # a plugged-in Virus whose device node we may already open (rule present or other setup)
    for d in /sys/bus/usb/devices/*; do
        [ "$(cat "$d/idVendor" 2>/dev/null)" = 133e ] && [ "$(cat "$d/idProduct" 2>/dev/null)" = 0815 ] || continue
        node=$(printf '/dev/bus/usb/%03d/%03d' "$(cat "$d/busnum")" "$(cat "$d/devnum")")
        [ -r "$node" ] && [ -w "$node" ] && return 0
    done
    return 1
}
if ! cmp -s "$HERE/udev/70-tirevive-virus-ti.rules" "$RULE" 2>/dev/null && ! virus_accessible; then
    say "USB access for the Virus TI (udev rule, needs sudo)"
    sudo install -m 0644 "$HERE/udev/70-tirevive-virus-ti.rules" "$RULE"
    sudo udevadm control --reload
    sudo udevadm trigger --subsystem-match=usb --attr-match=idVendor=133e || true
fi

# --- 2. broker + systemd user socket --------------------------------------------------------------------------------
say "TI Revive broker"
mkdir -p "$LIB" "$UNITS" "$CONF"
install -m 0755 "$HERE/bin/vtibroker" "$LIB/vtibroker"
install -m 0644 "$HERE/systemd/tirevive-broker.socket" "$HERE/systemd/tirevive-broker.service" "$UNITS/"
if ! grep -q '^token=' "$CONF/broker.token" 2>/dev/null; then # secret for the plug-in's connection from Wine
    umask 077
    printf 'port=%s\ntoken=%s\n' "$PORT" "$(od -An -N32 -tx1 /dev/urandom | tr -d ' \n')" > "$CONF/broker.token"
    umask 022
fi
chmod 600 "$CONF/broker.token"
if command -v systemctl >/dev/null 2>&1 && systemctl --user show-environment >/dev/null 2>&1; then
    if systemctl --user daemon-reload && systemctl --user enable tirevive-broker.socket >/dev/null &&
        systemctl --user restart tirevive-broker.socket; then
        echo "broker socket enabled (systemd user unit tirevive-broker.socket)"
    else
        echo "WARNING: could not enable the systemd user socket; start $LIB/vtibroker yourself before using the plug-in"
    fi
else
    echo "no systemd user session: start $LIB/vtibroker yourself before using the plug-in"
fi

# --- 3. Wine prefix + Access software ------------------------------------------------------------------------------
say "Wine prefix $PREFIX"
export WINEPREFIX="$PREFIX" WINEDEBUG=-all
[ -d "$PREFIX/drive_c" ] || wine wineboot -i >/dev/null 2>&1 || die "could not create the Wine prefix $PREFIX"
if [ -f "$VST3_DIR/Virus TI.vst3" ]; then
    echo "Access Virus TI software is already installed in this prefix"
else
    MSI="${1:-}"
    if [ -z "$MSI" ]; then
        for d in "$HERE" "$HOME/Downloads" "$HOME/Desktop" "$HOME"; do
            for f in "$d"/*Virus*TI*64-bit*.msi; do [ -f "$f" ] && MSI="$f" && break 2; done
        done
    fi
    [ -n "$MSI" ] && [ -f "$MSI" ] || die "Access installer not found. Download \"Virus TI Software Suite 5.1.7.00 64-bit.msi\"
       (your own copy) and run: ./install.sh \"/path/to/Virus TI Software Suite 5.1.7.00 64-bit.msi\""
    say "installing the Access Virus TI software (without its old drivers) from $MSI"
    TMP=$(mktemp -d)
    trap 'rm -rf "$TMP"' EXIT
    cp "$MSI" "$TMP/access.msi"
    chmod u+w "$TMP/access.msi"
    wine "$HERE/wine/vtimsipatch.exe" "$(wine winepath -w "$TMP/access.msi" 2>/dev/null)"
    wine msiexec /i "$(wine winepath -w "$TMP/access.msi" 2>/dev/null)" /qb || die "the Access installer failed"
    [ -f "$VST3_DIR/Virus TI.vst3" ] || die "the Access plug-in is missing after the install"
fi

# the native plug-in reads the Access files from there (a DAW started from the desktop does not see WINEPREFIX)
mkdir -p "$CONF"
printf '%s\n' "$PREFIX/drive_c/Program Files/Access Music/Virus TI/Common" > "$CONF/access-dir"

# --- 4. the TI Revive plug-in + yabridge ----------------------------------------------------------------------------
say "plug-in Virus TI (TI Revive)"
install -m 0644 "$HERE/wine/VirusTI_TIRevive.vst3" "$VST3_DIR/Virus TI (TI Revive).vst3"
yabridge_version() { # e.g. 5.1.1 (release) or 5.1.1-57-gb580a9f7 (newer build) - read from the VST3 library
    for d in "$(dirname "$(command -v yabridgectl)")" "$HOME/.local/share/yabridge" /usr/lib /usr/local/lib /usr/lib64; do
        f="$d/libyabridge-vst3.so"
        [ -f "$f" ] && tr -c '[:print:]' '\n' < "$f" | grep -E '^5\.[0-9]+\.[0-9]+(-[0-9]+-g[0-9a-f]+)?$' | head -1 && return
    done
}
wine_new() { # Wine 9.22 or newer changed window embedding (yabridge issue #409)
    v=$(wine --version 2>/dev/null | sed -n 's/^wine-\([0-9]*\)\.\([0-9]*\).*/\1 \2/p')
    set -- $v
    [ -n "${1:-}" ] && { [ "$1" -gt 9 ] || { [ "$1" -eq 9 ] && [ "${2:-0}" -ge 22 ]; }; }
}
# yabridge's releases unpack to ~/.local/share/yabridge, which is often not in PATH
if ! command -v yabridgectl >/dev/null 2>&1 && [ -x "$HOME/.local/share/yabridge/yabridgectl" ]; then
    PATH="$HOME/.local/share/yabridge:$PATH"
fi
if command -v yabridgectl >/dev/null 2>&1; then
    YV=$(yabridge_version || true)
    if [ "$YV" = 5.1.1 ] && wine_new; then
        cat <<EOT

WARNING: yabridge $YV with $(wine --version 2>/dev/null): the plug-in window will NOT react to mouse clicks
(yabridge issue #409, fixed after 5.1.1). Install a newer yabridge build from
    https://github.com/robbert-vdh/yabridge/actions  (Automated builds, master branch)
into ~/.local/share/yabridge and run ./install.sh again. Continuing anyway.
EOT
    fi
    yabridgectl add "$VST3_DIR" >/dev/null 2>&1 || true
    # only the TI Revive plug-in is usable; the original "Virus TI" alone finds no Virus
    yabridgectl blacklist add "$VST3_DIR/Virus TI.vst3" >/dev/null 2>&1 || true
    yabridgectl sync
    echo "bridged with yabridge: rescan plug-ins in your DAW and load \"Virus TI (TI Revive)\" (VST3)"
else
    cat <<EOT
yabridge is not installed. It makes Windows plug-ins usable in Linux DAWs (REAPER, Bitwig, Ardour, ...):
    https://github.com/robbert-vdh/yabridge  (see "Installation"), then:
    yabridgectl add "$VST3_DIR" && yabridgectl sync
EOT
fi
# --- 5. the native plug-in ------------------------------------------------------------------------------------------
if [ -d "$HERE/native/TI Revive Virus Control.vst3" ]; then
    say "native plug-in TI Revive Virus Control (VST3 for Linux DAWs, no Wine/yabridge)"
    mkdir -p "$HOME/.vst3"
    rm -rf "$HOME/.vst3/TI Revive Virus Control.vst3" "$HOME/.vst3/VirusTINative.vst3" # VirusTINative = test builds
    cp -R "$HERE/native/TI Revive Virus Control.vst3" "$HOME/.vst3/"
    MISSING=$(ldd "$HOME/.vst3/TI Revive Virus Control.vst3/Contents/x86_64-linux/TI Revive Virus Control.so" 2>/dev/null |
        awk '/not found/ {print $1}' | tr '\n' ' ')
    if [ -n "$MISSING" ]; then
        cat <<EOT
WARNING: libraries the native plug-in needs are missing: $MISSING
Install them with your package manager (Ubuntu/Debian: libxcb-cursor0 libxcb-util1 libxkbcommon-x11-0 libcairo2
libpango-1.0-0 libpangocairo-1.0-0 libfontconfig1), then rescan plug-ins.
EOT
    fi
    echo "installed to ~/.vst3: rescan plug-ins in your DAW and load \"TI Revive Virus Control\" (VST3)"
fi

say "done. In your DAW use the VST3 \"TI Revive Virus Control\" (native) or \"Virus TI (TI Revive)\" (through yabridge); the original \"Virus TI\" plug-in does not work on its own."
