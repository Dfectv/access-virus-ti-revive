#!/bin/bash
# mac-build-bridge.sh - build TI Revive's macOS stage 2 parts on the Mac and install the bridge for the current user
# (findings/design-macos-bridge.md):
#   "Virus TI (TI Revive).vst3"  universal (arm64 + x86_64) VST3 bridge - x86_64 for DAWs running under Rosetta
#                                ("Open using Rosetta", E132) -, Contents/Resources/vtihelper = x86_64 helper (Rosetta)
#   libVirusUSB.dylib            x86_64, loaded by the original plug-in inside the helper (stage 1, E099-E103)
#   vtibroker                    native broker (make -C src/broker mac; LaunchAgent: scripts/mac-install-broker.sh)
# usage (on the Mac): mac-build-bridge.sh <repo dir with src/> [--install]
#   needs: Xcode Command Line Tools, VST3 pluginterfaces in <repo>/../vst3sdk/pluginterfaces (MIT,
#   git clone https://github.com/steinbergmedia/vst3_pluginterfaces vst3sdk/pluginterfaces)
set -euo pipefail
REPO="$(cd "${1:?repo dir}" && pwd)"
VST3SDK="${VST3SDK:-$REPO/../vst3sdk}"
OUT="${OUT:-$REPO/../build}"
# a stale MacOSX27.0.sdk in the Command Line Tools breaks ld (E098): pick the newest SDK that works
if [ -z "${SDKROOT:-}" ]; then
    for s in /Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk $(xcrun --show-sdk-path); do
        [ -d "$s" ] && { export SDKROOT="$s"; break; }
    done
fi
P="$VST3SDK/pluginterfaces"
mkdir -p "$OUT"
VERSION="${VERSION:-0.5.0}"
export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-14.4}" # os_sync_wait_on_address (tirbridge.h)
CXX="clang++ -std=c++17 -O2 -Wall -Wno-unused-parameter -I $VST3SDK -I $REPO/src/mac/common -DTIR_VERSION=\"$VERSION\""

echo "== helper (x86_64)"
$CXX -arch x86_64 -I "$REPO/src/mac/helper" -o "$OUT/vtihelper" "$REPO/src/mac/helper/vtihelper.cpp" \
    -fobjc-arc -x objective-c++ "$REPO/src/mac/helper/vtihelper_editor.mm" -x none \
    "$P/base/funknown.cpp" "$P/base/coreiids.cpp" -framework CoreFoundation -framework Cocoa

echo "== bridge core + VST3 + AU (universal: arm64 native DAWs, x86_64 DAWs under Rosetta)"
B_SRC="$REPO/src/mac/bridge"
COMMON=("$B_SRC/tirbridge_core.cpp" -fobjc-arc -x objective-c++ "$B_SRC/tirbridge_view.mm" -x none
        "$P/base/funknown.cpp" "$P/base/coreiids.cpp" -framework CoreFoundation -framework Cocoa)
$CXX -arch arm64 -arch x86_64 -bundle -fvisibility=hidden -o "$OUT/tirbridge" "$B_SRC/tirbridge_vst3.cpp" "${COMMON[@]}"
$CXX -arch arm64 -arch x86_64 -bundle -fvisibility=hidden -o "$OUT/tirbridge_au" "$B_SRC/tirbridge_au.cpp" \
    -fobjc-arc -x objective-c++ "$B_SRC/tirbridge_au_view.mm" -x none "${COMMON[@]}" \
    -framework AudioToolbox -framework AudioUnit -framework CoreAudio

echo "== libVirusUSB.dylib (x86_64)"
clang++ -arch x86_64 -dynamiclib -std=c++17 -O2 -Wall \
    -install_name "/Library/Application Support/Access Music/Virus TI/Common/libVirusUSB.dylib" \
    -o "$OUT/libVirusUSB.dylib" "$REPO/src/mac/libvirususb/VirusClient.cpp" -framework CoreAudio -framework CoreFoundation

echo "== bundles"
plist() { # <bundle> <executable> <identifier> <extra plist xml>
    cat > "$1/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key><string>English</string>
    <key>CFBundleExecutable</key><string>$2</string>
    <key>CFBundleIdentifier</key><string>$3</string>
    <key>CFBundleName</key><string>Virus TI (TI Revive)</string>
    <key>CFBundlePackageType</key><string>BNDL</string>
    <key>CFBundleShortVersionString</key><string>$VERSION</string>
    <key>CFBundleVersion</key><string>$VERSION</string>
    <key>CFBundleSignature</key><string>????</string>
$4
</dict>
</plist>
PLIST
    echo "BNDL????" > "$1/Contents/PkgInfo"
}
make_bundle() { # <bundle> <binary> <identifier> <extra plist xml>
    rm -rf "$1"
    mkdir -p "$1/Contents/MacOS" "$1/Contents/Resources"
    cp "$2" "$1/Contents/MacOS/Virus TI (TI Revive)"
    cp "$OUT/vtihelper" "$1/Contents/Resources/vtihelper" # each bundle starts its own copy (one helper per DAW)
    plist "$1" "Virus TI (TI Revive)" "$3" "$4"
    codesign --force --sign - "$1/Contents/Resources/vtihelper"
    codesign --force --sign - "$1"
    echo "built $1"
}
B="$OUT/Virus TI (TI Revive).vst3"
make_bundle "$B" "$OUT/tirbridge" io.github.derpsn.tirevive.bridge.vst3 ""
# Audio Unit: version as 0xMMMMmmbb
IFS=. read -r V1 V2 V3 <<< "$VERSION"
AUVER=$(( (${V1:-0} << 16) | (${V2:-0} << 8) | ${V3:-0} ))
C="$OUT/Virus TI (TI Revive).component"
make_bundle "$C" "$OUT/tirbridge_au" io.github.derpsn.tirevive.bridge.au "    <key>AudioComponents</key>
    <array>
        <dict>
            <key>type</key><string>aumu</string>
            <key>subtype</key><string>VTIr</string>
            <key>manufacturer</key><string>TIRv</string>
            <key>name</key><string>TI Revive: Virus TI (TI Revive)</string>
            <key>description</key><string>Access Virus TI Total Integration without the old drivers</string>
            <key>version</key><integer>$AUVER</integer>
            <key>factoryFunction</key><string>TIReviveVTIFactory</string>
            <key>sandboxSafe</key><false/>
            <key>tags</key><array><string>Synthesizer</string></array>
        </dict>
    </array>"

if [ "${2:-}" = "--install" ]; then
    D="$HOME/Library/Audio/Plug-Ins"
    mkdir -p "$D/VST3" "$D/Components"
    rm -rf "$D/VST3/Virus TI (TI Revive).vst3" "$D/Components/Virus TI (TI Revive).component"
    cp -R "$B" "$D/VST3/"
    cp -R "$C" "$D/Components/"
    L="/Library/Application Support/Access Music/Virus TI/Common/libVirusUSB.dylib"
    if [ -w "$L" ]; then cat "$OUT/libVirusUSB.dylib" > "$L"; else echo "note: $L not writable, library not updated"; fi
    killall -9 AudioComponentRegistrar 2>/dev/null || true # rescan Audio Units
    echo "installed to $D"
fi
