#!/usr/bin/env bash
set -euo pipefail
export LIBVIRT_DEFAULT_URI=qemu:///system
[ -f ~/.config/tirevive/vm.env ] && . ~/.config/tirevive/vm.env   # local, untracked: VTI_VM_USER=...
VM=${VM:-win10}; KEY=~/.ssh/vm_re   # VM=win11 scripts/vm.sh ... drives the Windows 11 test VM
case "$VM" in win11) GUSER=vti ;; *) GUSER="${VTI_VM_USER:?set VTI_VM_USER to the Windows user of the VM}" ;; esac
vmip() { virsh domifaddr "$VM" --source lease | awk '/192\.168\.122\./{print $4}' | cut -d/ -f1 | head -1; }
case "${1:-}" in
  start)  virsh start "$VM" ;;
  stop)   virsh shutdown "$VM" ;;
  snap)   virsh snapshot-create-as "$VM" "$2" ;;
  revert) virsh snapshot-revert "$VM" "$2" ;;   # destructive: ask first
  attach) virsh attach-device "$VM" ~/virus-ti-re/scripts/virus.xml --live ;;
  detach) virsh detach-device "$VM" ~/virus-ti-re/scripts/virus.xml --live ;;
  ssh)    shift; ssh -i "$KEY" -o StrictHostKeyChecking=accept-new "$GUSER@$(vmip)" "$@" ;;
  ps)     shift; ENC=$(printf '%s' "\$ProgressPreference='SilentlyContinue'; $*" | iconv -t UTF-16LE | base64 -w0)   # PowerShell without quoting pain
          ssh -i "$KEY" -o StrictHostKeyChecking=accept-new -o LogLevel=ERROR "$GUSER@$(vmip)" "powershell -NoProfile -NonInteractive -EncodedCommand $ENC" ;;
  pull)   scp -i "$KEY" -o "User=\"$GUSER\"" -r "$(vmip):$2" "$3" ;;   # -o User: scp rejects spaces in user@host
  push)   scp -i "$KEY" -o "User=\"$GUSER\"" -r "$2" "$(vmip):$3" ;;
  *) echo "usage: vm.sh start|stop|snap N|revert N|attach|detach|ssh CMD|ps PSCODE|pull R L|push L R"; exit 1 ;;
esac
