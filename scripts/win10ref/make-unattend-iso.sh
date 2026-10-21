#!/usr/bin/env bash
# make-unattend-iso.sh OUT.iso - build the answer-file ISO for the Windows 10 reference VM: autounattend.xml of this
# folder + the win11 VM's setup-ssh.ps1 (the same OpenSSH set-up) + the SSH public key of scripts/vm.sh (~/.ssh/vm_re.pub).
set -euo pipefail
OUT=${1:?usage: make-unattend-iso.sh OUT.iso}
HERE=$(cd "$(dirname "$0")" && pwd)
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
cp "$HERE/autounattend.xml" "$HERE/../win11/setup-ssh.ps1" "$STAGE/"
cp ~/.ssh/vm_re.pub "$STAGE/vm_re.pub"
xorriso -as mkisofs -quiet -J -r -V UNATTEND -o "$OUT" "$STAGE"
echo "built $OUT"
