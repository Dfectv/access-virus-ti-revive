#!/bin/sh
# TI Revive for Linux - installer (run as your normal user, from the unpacked TI-Revive-<version>-linux folder).
#
#   ./install.sh [path to "Virus TI Software Suite ... 64-bit.msi"]
#
# What it does (findings/design-linux.md):
#   1. udev rule so you (not only root) may use the Virus over USB          (asks for your password once, sudo)
#   2. TI Revive broker in ~/.local/lib/tirevive + systemd user socket (starts on demand, exits when idle)
#   3. the files of your own Access "Virus TI Software Suite" .msi that the plug-in reads (skin + device description,
#      ROM banks, manuals, factory patch libraries) - unpacked with msiextract (msitools) or Wine, NOT installed
#   4. the plug-in "TI Revive Virus Control" (Linux VST3, no Wine/yabridge) into ~/.vst3; the classic plug-in of
#      TI Revive 0.7 ("Virus TI (TI Revive)" through yabridge) is removed - the new one opens its projects
# Wine is only needed to unpack the .msi when msiextract is missing.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
PREFIX="${WINEPREFIX:-$HOME/.wine}"
LIB="$HOME/.local/lib/tirevive"
UNITS="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
CONF="${XDG_CONFIG_HOME:-$HOME/.config}/tirevive"
VST3_DIR="$PREFIX/drive_c/Program Files/Common Files/VST3"
ACCESS="$HOME/.local/share/tirevive/access/Access Music/Virus TI" # Common/ (Virus Control.dll, firmware.bin), Documentation/
PORT=47615

say() { printf '\n== %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -ne 0 ] || die "run install.sh as your normal user, not as root (it asks for sudo when needed)"

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

# --- 3. the Access files ---------------------------------------------------------------------------------------------
say "Access Virus TI files"
OLD_COMMON="$PREFIX/drive_c/Program Files/Access Music/Virus TI/Common" # TI Revive 0.7 installed Access into Wine
if [ -f "$ACCESS/Common/Virus Control.dll" ] && [ -f "$ACCESS/Common/firmware.bin" ]; then
    echo "already set up in $ACCESS"
elif [ -f "$OLD_COMMON/Virus Control.dll" ] && [ -f "$OLD_COMMON/firmware.bin" ]; then
    mkdir -p "$ACCESS/Common"
    cp "$OLD_COMMON/Virus Control.dll" "$OLD_COMMON/firmware.bin" "$ACCESS/Common/"
    echo "taken from the Access installation in the Wine prefix $PREFIX"
else
    MSI="${1:-}"
    if [ -z "$MSI" ]; then
        for d in "$HERE" "$HOME/Downloads" "$HOME/Desktop" "$HOME"; do
            for f in "$d"/*Virus*TI*64-bit*.msi; do [ -f "$f" ] && MSI="$f" && break 2; done
        done
    fi
    [ -n "$MSI" ] && [ -f "$MSI" ] || die "Access installer not found. Download \"Virus TI Software Suite 5.1.7.00 64-bit.msi\"
       (your own copy) and run: ./install.sh \"/path/to/Virus TI Software Suite 5.1.7.00 64-bit.msi\""
    TMP=$(mktemp -d)
    trap 'rm -rf "$TMP"' EXIT
    if command -v msiextract >/dev/null 2>&1; then
        echo "unpacking $MSI (msiextract)"
        (cd "$TMP" && msiextract "$MSI" >/dev/null) || die "msiextract could not unpack $MSI"
    elif command -v wine >/dev/null 2>&1; then
        echo "unpacking $MSI (Wine, administrative extraction - nothing is installed)"
        cp "$MSI" "$TMP/access.msi"
        WINEPREFIX="$TMP/wineprefix" WINEDEBUG=-all wine msiexec /a "$(WINEPREFIX="$TMP/wineprefix" wine winepath -w "$TMP/access.msi" 2>/dev/null)" \
            /qn TARGETDIR="$(WINEPREFIX="$TMP/wineprefix" wine winepath -w "$TMP/x" 2>/dev/null)" >/dev/null 2>&1 ||
            die "Wine could not unpack $MSI"
        WINEPREFIX="$TMP/wineprefix" wineserver -k 2>/dev/null || true # its files must go with $TMP
    else
        die "to unpack the Access .msi install msitools (Ubuntu/Debian: sudo apt install msitools) or Wine, then run ./install.sh again"
    fi
    DLL=$(find "$TMP" -path '*Access Music/Virus TI/Common/Virus Control.dll' | head -1)
    [ -n "$DLL" ] || die "no Virus Control.dll in $MSI - is it the 64-bit \"Virus TI Software Suite\" .msi?"
    SRC=$(dirname "$(dirname "$DLL")") # .../Access Music/Virus TI
    mkdir -p "$ACCESS/Common"
    cp "$SRC/Common/Virus Control.dll" "$SRC/Common/firmware.bin" "$ACCESS/Common/"
    [ -d "$SRC/Documentation" ] && cp -R "$SRC/Documentation" "$ACCESS/"
    LIB_SRC=$(find "$TMP" -type d -path '*Access Music/Virus TI/Patches' | grep -v '/Common/' | head -1)
    if [ -n "$LIB_SRC" ]; then # factory patch libraries; existing files (your own libraries) stay untouched
        DOCS="$HOME/Documents/Access Music/Virus TI/Patches"
        (cd "$LIB_SRC" && find . -type f) | while read -r f; do
            [ -e "$DOCS/$f" ] || { mkdir -p "$(dirname "$DOCS/$f")"; cp "$LIB_SRC/$f" "$DOCS/$f"; }
        done
    fi
    echo "set up in $ACCESS (the Access software itself is not installed)"
fi
# the plug-in reads the Access files from there
mkdir -p "$CONF"
printf '%s\n' "$ACCESS/Common" > "$CONF/access-dir"

# --- 4. the plug-in --------------------------------------------------------------------------------------------------
say "plug-in TI Revive Virus Control"
mkdir -p "$HOME/.vst3"
# the classic plug-in of TI Revive 0.7 (through yabridge) and the test builds: the new plug-in opens their projects
rm -f "$VST3_DIR/Virus TI (TI Revive).vst3"
rm -rf "$HOME/.vst3/yabridge/Virus TI (TI Revive).vst3" "$HOME/.vst3/VirusTINative.vst3" "$HOME/.vst3/TI Revive Virus Control.vst3"
cp -R "$HERE/native/TI Revive Virus Control.vst3" "$HOME/.vst3/"
MISSING=$(ldd "$HOME/.vst3/TI Revive Virus Control.vst3/Contents/x86_64-linux/TI Revive Virus Control.so" 2>/dev/null |
    awk '/not found/ {print $1}' | tr '\n' ' ')
if [ -n "$MISSING" ]; then
    cat <<EOT
WARNING: libraries the plug-in needs are missing: $MISSING
Install them with your package manager (Ubuntu/Debian: libxcb-cursor0 libxcb-util1 libxkbcommon-x11-0 libcairo2
libpango-1.0-0 libpangocairo-1.0-0 libfontconfig1), then rescan plug-ins.
EOT
fi

say "done. Rescan plug-ins in your DAW and load the VST3 \"TI Revive Virus Control\"."
