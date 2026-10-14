#!/bin/bash
# linux-build-release.sh - build the Linux release of TI Revive (findings/design-linux.md):
#   TI-Revive-<version>-linux.tar.gz = install.sh, uninstall.sh, README-linux.txt,
#     bin/vtibroker (native, libusb linked statically), wine/VirusTI_TIRevive.vst3 + wine/vtimsipatch.exe (mingw),
#     systemd/ (user socket + service), udev/70-tirevive-virus-ti.rules
# usage: linux-build-release.sh <repo dir> <version> [out dir]
# needs: gcc, make, mingw-w64 (x86_64), curl, bzip2
set -euo pipefail
REPO="$(cd "${1:?repo dir}" && pwd)"
VERSION="${2:?version}"
OUT="$(mkdir -p "${3:-$REPO/work/release}" && cd "${3:-$REPO/work/release}" && pwd)"
BUILD="$OUT/build-linux"
rm -rf "$BUILD" && mkdir -p "$BUILD"

echo "== libusb (static, without udev: the broker finds the Virus through sysfs)"
LIBUSB_VER=1.0.30
LIBUSB="$OUT/libusb-$LIBUSB_VER-linux-static"
if [ ! -f "$LIBUSB/lib/libusb-1.0.a" ]; then
    T=$(mktemp -d)
    curl -fsSL "https://github.com/libusb/libusb/releases/download/v$LIBUSB_VER/libusb-$LIBUSB_VER.tar.bz2" -o "$T/libusb.tar.bz2"
    tar -xjf "$T/libusb.tar.bz2" -C "$T"
    (cd "$T/libusb-$LIBUSB_VER" && ./configure -q --disable-shared --enable-static --disable-udev --prefix="$LIBUSB" \
        CFLAGS="-O2 -fPIC" >/dev/null && make -s -j4 install >/dev/null)
    rm -rf "$T"
fi
echo "== broker"
B="$REPO/src/broker"
gcc -O2 -Wall -Wextra -I"$LIBUSB/include/libusb-1.0" -o "$BUILD/vtibroker" "$B/vtibroker_posix.c" "$B/vticore.c" \
    "$B/vtiusbaudio_libusb.c" "$B/vticore_midi.c" "$LIBUSB/lib/libusb-1.0.a" -lpthread
! ldd "$BUILD/vtibroker" | grep -q libusb || { echo "broker still links libusb dynamically"; exit 1; }
echo "== Windows side (mingw): wrapper for Wine, MSI patch tool"
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -DVTIEMU_NO_DLLMAIN -DVTIEMU_VIRTUAL_DEVICE -shared -o "$BUILD/VirusTI_TIRevive.vst3" \
    -I"$REPO/src/wrapper" "$REPO/src/wrapper/vtiwrap.c" "$REPO/src/wrapper/vtiemu.c" "$REPO/src/wrapper/vtiaudio.c" \
    "$REPO/src/wrapper/vtiusbaudio.c" "$B/vticore_sock.c" "$B/vticore_midi.c" -lwinusb -lsetupapi -lshlwapi \
    -lshell32 -lole32 -luuid -lws2_32 -lwinmm -static -lpthread
x86_64-w64-mingw32-gcc -O2 -Wall -o "$BUILD/vtimsipatch.exe" "$REPO/src/installer-linux/vtimsipatch.c" -lmsi -static

NATIVE=""
if [ -f "$REPO/native/CMakeLists.txt" ]; then # the native plug-in "TI Revive Virus Control" (native/, Linux VST3)
    echo "== native plug-in"
    cmake -S "$REPO/native" -B "$BUILD/native" -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$BUILD/native" --target VirusTINative -j4 >/dev/null
    NATIVE="$BUILD/native/VST3/Release/TI Revive Virus Control.vst3"
    strip "$NATIVE/Contents/x86_64-linux/TI Revive Virus Control.so"
fi

echo "== package"
NAME="TI-Revive-$VERSION-linux"
P="$BUILD/$NAME"
I="$REPO/src/installer-linux"
mkdir -p "$P/bin" "$P/wine" "$P/systemd" "$P/udev"
install -m 0755 "$BUILD/vtibroker" "$P/bin/"
install -m 0644 "$BUILD/VirusTI_TIRevive.vst3" "$BUILD/vtimsipatch.exe" "$P/wine/"
install -m 0644 "$I/tirevive-broker.socket" "$I/tirevive-broker.service" "$P/systemd/"
install -m 0644 "$I/70-tirevive-virus-ti.rules" "$P/udev/"
install -m 0755 "$I/install.sh" "$I/uninstall.sh" "$P/"
[ -n "$NATIVE" ] && mkdir -p "$P/native" && cp -R "$NATIVE" "$P/native/"
sed "s/@VERSION@/$VERSION/g" "$I/README-linux.txt" > "$P/README-linux.txt"
install -m 0644 "$REPO/LICENSE" "$P/"
tar -C "$BUILD" -czf "$OUT/$NAME.tar.gz" "$NAME"
echo "$OUT/$NAME.tar.gz"
