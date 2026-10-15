#!/bin/bash
# mac-build-release.sh - build the macOS release of TI Revive (stage 2: native bridge + Rosetta helper) on a Mac:
#   TI-Revive-<version>-mac.zip = TI Revive.pkg + "Install Access Virus TI software.command"
#                                 + "Uninstall TI Revive.command" + README-mac.txt
# usage: mac-build-release.sh <repo dir> <version> [out dir]
# needs: Xcode Command Line Tools (libusb is built from source), VST3 pluginterfaces in <repo>/../vst3sdk or $VST3SDK.
set -euo pipefail
REPO="$(cd "${1:?repo dir}" && pwd)"
VERSION="${2:?version}"
OUT="$(mkdir -p "${3:-$REPO/../release}" && cd "${3:-$REPO/../release}" && pwd)"
export VERSION MACOSX_DEPLOYMENT_TARGET=14.4
# a stale MacOSX27.0.sdk in some Command Line Tools installs breaks ld (E098): use a working SDK
if [ -z "${SDKROOT:-}" ]; then
    for s in /Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk $(xcrun --show-sdk-path); do
        [ -d "$s" ] && { export SDKROOT="$s"; break; }
    done
fi
BUILD="$OUT/build"
rm -rf "$BUILD" && mkdir -p "$BUILD"

echo "== bridge, helper, libVirusUSB"
OUT="$BUILD" bash "$REPO/scripts/mac-build-bridge.sh" "$REPO"
echo "== libusb (static, built for macOS $MACOSX_DEPLOYMENT_TARGET; Homebrew's targets the build machine's macOS)"
LIBUSB_VER=1.0.30
LIBUSB="$OUT/libusb-$LIBUSB_VER-macos$MACOSX_DEPLOYMENT_TARGET"
if [ ! -f "$LIBUSB/lib/libusb-1.0.a" ]; then
    T=$(mktemp -d)
    curl -fsSL "https://github.com/libusb/libusb/releases/download/v$LIBUSB_VER/libusb-$LIBUSB_VER.tar.bz2" -o "$T/libusb.tar.bz2"
    tar -xjf "$T/libusb.tar.bz2" -C "$T"
    (cd "$T/libusb-$LIBUSB_VER" && ./configure -q --disable-shared --enable-static --prefix="$LIBUSB" \
        CFLAGS="-arch arm64 -O2 -mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET" >/dev/null && make -s -j4 install >/dev/null)
    rm -rf "$T"
fi
echo "== broker (static libusb)"
make -C "$REPO/src/broker" mac-static OUT="$BUILD" LIBUSB_PREFIX="$LIBUSB"
otool -L "$BUILD/mac/vtibroker" | grep -q libusb && { echo "broker still links a libusb dylib"; exit 1; }
codesign --force --sign - "$BUILD/mac/vtibroker"

NATIVE=""
if [ -f "$REPO/native/CMakeLists.txt" ]; then # the native plug-in "TI Revive Virus Control" (native/, VST3, universal)
    echo "== native plug-in (arm64 + x86_64)"
    cmake -S "$REPO/native" -B "$BUILD/native" -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 ${SDKROOT:+-DCMAKE_OSX_SYSROOT="$SDKROOT"} >/dev/null
    cmake --build "$BUILD/native" --config Release --target VirusTINative -- -quiet
    NATIVE="$BUILD/native/VST3/Release/TI Revive Virus Control.vst3"
    codesign --force --deep --sign - "$NATIVE"
fi

echo "== payload"
ROOT="$BUILD/root"
SUP="$ROOT/Library/Application Support"
mkdir -p "$ROOT/Library/Audio/Plug-Ins/VST3" "$ROOT/Library/Audio/Plug-Ins/Components" "$SUP/TI Revive" \
    "$SUP/Access Music/Virus TI/Common"
# 0.8.0: no bridge VST3 - "TI Revive Virus Control" (native) has its class id and opens its projects; the bridge's
# Audio Unit stays (Logic / GarageBand)
cp -R "$BUILD/Virus TI (TI Revive).component" "$ROOT/Library/Audio/Plug-Ins/Components/"
[ -n "$NATIVE" ] && cp -R "$NATIVE" "$ROOT/Library/Audio/Plug-Ins/VST3/"
cp "$BUILD/mac/vtibroker" "$SUP/TI Revive/vtibroker"
cp "$REPO/src/installer-mac/Uninstall TI Revive.command" "$SUP/TI Revive/"
cp "$BUILD/libVirusUSB.dylib" "$SUP/Access Music/Virus TI/Common/libVirusUSB.dylib"
codesign --force --sign - "$SUP/Access Music/Virus TI/Common/libVirusUSB.dylib"
chmod -R go-w "$ROOT"

echo "== pkg"
pkgbuild --analyze --root "$ROOT" "$BUILD/components.plist" >/dev/null
# never relocate the bundle onto another copy with the same identifier (e.g. a development install in ~/Library)
n=$(/usr/libexec/PlistBuddy -c "Print" "$BUILD/components.plist" | grep -c "RootRelativeBundlePath" || true)
for ((i = 0; i < n; i++)); do
    /usr/libexec/PlistBuddy -c "Delete :$i:BundleIsRelocatable" "$BUILD/components.plist" 2>/dev/null || true
    /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$BUILD/components.plist"
done
STAGE="$BUILD/TI-Revive-$VERSION-mac"
mkdir -p "$STAGE"
pkgbuild --root "$ROOT" --component-plist "$BUILD/components.plist" --identifier io.github.derpsn.tirevive \
    --version "$VERSION" --scripts "$REPO/src/installer-mac/scripts" --install-location / "$STAGE/TI Revive.pkg"
cp "$REPO/src/installer-mac/Install Access Virus TI software.command" "$REPO/src/installer-mac/Uninstall TI Revive.command" \
    "$REPO/src/installer-mac/README-mac.txt" "$STAGE/"
chmod +x "$STAGE"/*.command
(cd "$BUILD" && ditto -c -k --keepParent "TI-Revive-$VERSION-mac" "$OUT/TI-Revive-$VERSION-mac.zip")
echo "built $OUT/TI-Revive-$VERSION-mac.zip"
