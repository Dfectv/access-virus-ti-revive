#!/usr/bin/env bash
# create-vm.sh - define and start the Windows 11 test VM "win11" (milestone M5) and run the unattended install.
# Prerequisites (pool "default"): win11.qcow2 (80G), win11-install.iso (Windows 11 de-DE), win11-unattend.iso
# (make-unattend-iso.sh). UEFI Secure Boot with Microsoft keys + TPM 2.0 (swtpm), like a real Win11 PC, so
# driver signing / Code Integrity behave as on the user's machine. SATA disk + e1000e: no extra drivers needed.
set -euo pipefail
export LIBVIRT_DEFAULT_URI=qemu:///system
VM=win11
virt-install \
  --name "$VM" --memory 8192 --vcpus 4 --cpu host-passthrough \
  --machine q35 --features smm.state=on \
  --boot loader=/usr/share/OVMF/OVMF_CODE_4M.secboot.fd,loader.readonly=yes,loader.type=pflash,loader.secure=yes,nvram.template=/usr/share/OVMF/OVMF_VARS_4M.ms.fd \
  --tpm backend.type=emulator,backend.version=2.0,model=tpm-crb \
  --disk vol=default/win11.qcow2,bus=sata \
  --disk vol=default/win11-install.iso,device=cdrom,bus=sata \
  --disk vol=default/win11-unattend.iso,device=cdrom,bus=sata \
  --network network=default,model=e1000e \
  --graphics spice --video qxl --sound ich9 \
  --osinfo win11 --noautoconsole --wait 0
# The Windows ISO asks "Press any key to boot from CD or DVD" for a few seconds: answer it.
for i in $(seq 1 15); do
  virsh send-key "$VM" KEY_ENTER >/dev/null 2>&1 || true
  sleep 1
done
echo "$VM started; installation runs unattended (watch with virt-manager / virt-viewer)"
