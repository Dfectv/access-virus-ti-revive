# HANDOFF

_Last updated: 2026-10-10, Phase 0 (in progress)_

## State
- Workspace ~/virus-ti-re; git tracks findings/ and scripts/ only.
- MSI baseline: originals/Virus TI Software Suite 5.1.7.00 64-bit.msi (sha256 f9a6f5c9…, chmod a-w).
- VM win10 running, SSH OK, device passed through (host bus 1).

## Blockers
- usbmon kernel module not loaded / no /dev/usbmonN access (needs sudo).
- Ghidra not installed for REA (GHIDRA_INSTALL_DIR unset, no Java). Hopper provider is available.
- msitools / 7z not installed (needed for Phase 1).

## Open questions
- Key: how Virus Control reaches the device (MIDI port / VirusUSB DLL exports / raw IOCTL).

## Next step
- Resolve blockers, run usbmon visibility test (knob move), user takes `lab-ready` snapshot, then Phase 1.
