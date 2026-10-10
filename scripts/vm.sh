#!/usr/bin/env bash
set -euo pipefail
export LIBVIRT_DEFAULT_URI=qemu:///system
VM=win10; KEY=~/.ssh/vm_re; GUSER="${VTI_VM_USER:?set VTI_VM_USER to the Windows user of the VM}"
vmip() { virsh domifaddr "$VM" --source lease | awk '/192\.168\.122\./{print $4}' | cut -d/ -f1 | head -1; }
case "${1:-}" in
  start)  virsh start "$VM" ;;
  stop)   virsh shutdown "$VM" ;;
  snap)   virsh snapshot-create-as "$VM" "$2" ;;
  revert) virsh snapshot-revert "$VM" "$2" ;;   # destructive: ask first
  attach) virsh attach-device "$VM" ~/virus-ti-re/scripts/virus.xml --live ;;
  detach) virsh detach-device "$VM" ~/virus-ti-re/scripts/virus.xml --live ;;
  ssh)    shift; ssh -i "$KEY" -o StrictHostKeyChecking=accept-new "$GUSER@$(vmip)" "$@" ;;
  pull)   scp -i "$KEY" -r "$GUSER@$(vmip):$2" "$3" ;;
  push)   scp -i "$KEY" -r "$2" "$GUSER@$(vmip):$3" ;;
  *) echo "usage: vm.sh start|stop|snap N|revert N|attach|detach|ssh CMD|pull R L|push L R"; exit 1 ;;
esac
