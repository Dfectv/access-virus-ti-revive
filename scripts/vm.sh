#!/usr/bin/env bash
set -euo pipefail
export LIBVIRT_DEFAULT_URI=qemu:///system
[ -f ~/.config/tirevive/vm.env ] && . ~/.config/tirevive/vm.env   # local, untracked: VTI_VM_USER=...
VM=${VM:-win10}; KEY=~/.ssh/vm_re   # VM=win11 scripts/vm.sh ... drives the Windows 11 test VM
case "$VM" in win11|win10ref) GUSER=vti ;; *) GUSER="${VTI_VM_USER:?set VTI_VM_USER to the Windows user of the VM}" ;; esac
vmip() { virsh domifaddr "$VM" --source lease | awk '/192\.168\.122\./{print $4}' | cut -d/ -f1 | head -1; }
case "${1:-}" in
  start)  virsh start "$VM" ;;
  stop)   virsh shutdown "$VM" ;;
  snap)   virsh snapshot-create-as "$VM" "$2" ;;
  revert) virsh snapshot-revert "$VM" "$2" ;;   # destructive: ask first
  # The Linux broker must be gone before libvirt takes the Virus: on 2026-10-16 it reopened the device (handshake)
  # while libvirt attached it, three resets in 24 s, and the VM got error 31 on every transfer until a power cycle
  # (findings/evidence.md E179). Stopping it gives the Virus back once (its reset); wait before libvirt resets it again.
  attach) systemctl --user stop tirevive-broker.socket tirevive-broker.service 2>/dev/null; sleep 3
          virsh attach-device "$VM" ~/virus-ti-re/scripts/virus.xml --live ;;
  detach) virsh detach-device "$VM" ~/virus-ti-re/scripts/virus.xml --live
          systemctl --user start tirevive-broker.socket 2>/dev/null ;;
  ssh)    shift; ssh -i "$KEY" -o StrictHostKeyChecking=accept-new "$GUSER@$(vmip)" "$@" ;;
  ps)     shift; ENC=$(printf '%s' "\$ProgressPreference='SilentlyContinue'; $*" | iconv -t UTF-16LE | base64 -w0)   # PowerShell without quoting pain
          ssh -i "$KEY" -o StrictHostKeyChecking=accept-new -o LogLevel=ERROR "$GUSER@$(vmip)" "powershell -NoProfile -NonInteractive -EncodedCommand $ENC" ;;
  pull)   scp -i "$KEY" -o "User=\"$GUSER\"" -r "$(vmip):$2" "$3" ;;   # -o User: scp rejects spaces in user@host
  push)   scp -i "$KEY" -o "User=\"$GUSER\"" -r "$2" "$(vmip):$3" ;;
  *) echo "usage: vm.sh start|stop|snap N|revert N|attach|detach|ssh CMD|ps PSCODE|pull R L|push L R"; exit 1 ;;
esac
