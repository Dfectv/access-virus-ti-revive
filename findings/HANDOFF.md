# HANDOFF

_Last updated: 2026-10-10, Phase 1 mostly done; Phase 0 step 3 (usbmon check) still blocked_

## State
- Workspace ~/virus-ti-re; git tracks findings/ and scripts/ only. work/ = untracked working copies (Access binaries: never commit/redistribute).
- MSI: originals/Virus TI Software Suite 5.1.7.00 64-bit.msi (sha256 f9a6f5c9…, chmod a-w). Extracted to work/msi-extract, tables in work/msi-tables, CA DLLs in work/msi-binary.
- VM installed state pulled to originals/installed/vti/ (drivers, reg exports, enum-drivers, file list).
- Per-PE details: work/pe/*.txt (scripts/pe_inventory.py; run with PYTHONPATH=~/tools/pylib python3 -P).
- Remotedevice XML: work/remotedevice/rd{0,1,2}_*.xml (extracted from Virus Control.dll).
- Ghidra 12.1.4 extracted to ~/tools/ghidra_12.1.4_PUBLIC (needs JDK 21 + REA env vars).
- vm.sh pull/push fixed (scp needs -o User="..." because of the space in the user name).

## Key results so far (see evidence.md)
- VirusUSB.sys replaces usbccgp and creates the MI_03 child (vtimidi) and MEDIA\VTIAUDIOADAPTER (vtiaudio). (E005-E007)
- VirusUSB(_X64).dll = ASIO driver, not a device API. (E009)
- Virus Control.dll / Control Center: no WinMM MIDI imports; SetupDi + CreateFile + DeviceIoControl; embeds the Ploytec ASIO client. Points to answer (c) raw IOCTL. (E010, E011) NOT yet proven: need the interface GUID + IOCTL codes (Ghidra) and a runtime check (capture/ProcMon).
- MSI UpdateFirmware CA runs on install: never run/repair the MSI with the synth attached. (E003)

## Blockers
- usbmon not loaded (config files exist, but `sudo modprobe usbmon` was not run / /dev/usbmon* missing).
- No JDK 21 (`sudo apt install openjdk-21-jdk`), so REA/Ghidra can't start.
- Snapshot `lab-ready` does not exist (there is `claude-starts`); confirm which name to use.

## Open questions
- Key: (c) looks likely; confirm which device interface GUID/IOCTLs Virus Control.dll uses, and whether it uses MI_04 for Remote/editor traffic and MI_03 only for the Windows MIDI ports.
- User's "inbox driver handles playback": true only without VirusUSB.sys? (claims C9)

## Next step
1. After JDK: add GHIDRA_INSTALL_DIR + REA_GHIDRA_STARTUP_TIMEOUT_MS to the rea MCP env, restart Claude Code.
2. Phase 2.1: VirusUSB_X64.dll (smallest user-mode, same IOCTL client code): find the SetupDiGetClassDevsA GUID and DeviceIoControl codes; then the same in Virus Control.dll by xref (not whole-DLL decompile).
3. Phase 0.3 usbmon knob test once /dev/usbmon1 exists.
