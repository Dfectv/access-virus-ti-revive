#!/bin/sh
# Cross-build the device/model tools for Windows with mingw (compile check of the Windows paths: broker pipe
# client, audio ring mapping). The plug-in itself needs the VST3 SDK's Windows toolchain (MSVC, later in CI).
#   scripts/win-cross-tools.sh [outdir]   (needs a configured Linux build for pugixml: cmake -B build)
set -e
cd "$(dirname "$0")/.."
OUT=${1:-build-win}
mkdir -p "$OUT/obj"
P=$(find build/_deps -name pugixml.cpp | head -1)
[ -n "$P" ] || { echo "configure the Linux build first (FetchContent pugixml)"; exit 1; }
CXX="x86_64-w64-mingw32-g++ -std=c++17 -O2 -Isrc -Iexternal/ti-revive/src/broker -I$(dirname "$P")"
for f in src/access/accessfiles.cpp src/model/remotedevice.cpp src/device/broker.cpp src/device/memsync.cpp \
         src/device/audioin.cpp src/editor/editormodel.cpp src/editor/editorrender.cpp "$P"; do
    $CXX -c "$f" -o "$OUT/obj/$(basename "$f").o"
done
for t in vtimodel virusctl virussync; do
    $CXX "tools/$t.cpp" "$OUT"/obj/*.o -o "$OUT/$t.exe" -static
done
ls -la "$OUT"/*.exe
