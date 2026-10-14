#!/usr/bin/env bash
# package-windows.sh RUN_ID VERSION OUTDIR - test package from a CI run's Windows artifacts + packaging/windows
set -euo pipefail
RUN=${1:?run id}; VER=${2:?version}; OUT=${3:?output dir}
R=$(cd "$(dirname "$0")/.." && pwd); N=virus-ti-native-windows-$VER; T=$(mktemp -d)
mkdir -p "$T/$N"
gh run download "$RUN" -R derpsn/virus-ti-native -n VirusTINative-windows -D "$T/$N/VirusTINative.vst3"
gh run download "$RUN" -R derpsn/virus-ti-native -n asio-windows -D "$T/$N"
rm -f "$T/$N/VirusTINative.vst3/Contents/x86_64-win/"*.map
cp "$R"/packaging/windows/* "$T/$N/"
mkdir -p "$OUT"; (cd "$T" && zip -qr "$OUT/$N.zip" "$N")
rm -rf "$T"; echo "$OUT/$N.zip"
