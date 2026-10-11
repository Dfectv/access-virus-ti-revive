#!/usr/bin/env bash
# swap-disk.sh <source-volume> - shut win11 down and replace its disk win11.qcow2 by a clone of <source-volume>
# (win11-base.qcow2 = fresh OS, win11-build.qcow2 = WiX build machine), then start it and wait for SSH.
set -euo pipefail
cd "$(dirname "$0")/../.."
export LIBVIRT_DEFAULT_URI=qemu:///system VM=win11
SRC=${1:?usage: swap-disk.sh <source-volume>}
virsh detach-device win11 scripts/virus.xml --live >/dev/null 2>&1 || true
if [ "$(virsh domstate win11)" != "ausgeschaltet" ] && [ "$(virsh domstate win11)" != "shut off" ]; then
  scripts/vm.sh ps 'Stop-Computer -Force' >/dev/null 2>&1 || true
  for i in $(seq 1 60); do case "$(virsh domstate win11)" in ausgeschaltet|"shut off") break ;; esac; sleep 5; done
fi
virsh vol-delete --pool default win11.qcow2 >/dev/null
virsh vol-clone --pool default "$SRC" win11.qcow2 >/dev/null
virsh start win11 >/dev/null
for i in $(seq 1 60); do scripts/vm.sh ps hostname 2>/dev/null | grep -q VTI && { echo "win11 up on $SRC"; exit 0; }; sleep 10; done
echo "win11 did not come up" >&2; exit 1
