#!/usr/bin/env bash
# build-msi.sh - cross-build our binaries on Linux, build "TI Revive.msi" with WiX in the win11 VM, fetch it to
# work/release/. The VM needs C:\build\dotnet + C:\build\wix (see findings/HANDOFF.md M6).
set -euo pipefail
cd "$(dirname "$0")/../.."
export VM=win11
make -C src/broker >/dev/null
make -C src/wrapper >/dev/null
R='C:/build/tirevive'
scripts/vm.sh ps 'Remove-Item -Recurse -Force C:\build\tirevive -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force C:\build\tirevive\bin, C:\build\tirevive\src\inf, C:\build\tirevive\src\installer | Out-Null' >/dev/null
for f in work/build/vtibroker.exe work/build/vtilaunch.exe work/build/vtiemu-pipe.dll work/build/VirusTI_WinUSB.vst3; do
  scripts/vm.sh push "$f" "$R/bin/"
done
scripts/vm.sh push src/inf/vti_winusb.inf "$R/src/inf/"
scripts/vm.sh push src/installer "$R/src/" 
scripts/vm.sh ps 'powershell -ExecutionPolicy Bypass -File C:\build\tirevive\src\installer\build.ps1 2>&1 | Out-String -Width 200'
mkdir -p work/release
scripts/vm.sh pull "$R/out/TI Revive.msi" work/release/
ls -la work/release/
