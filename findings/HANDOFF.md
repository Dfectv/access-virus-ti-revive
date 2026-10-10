# HANDOFF

_Last updated: 2026-10-10, Phase 2.1 started (GUID + IOCTL list done); blockers cleared_

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
- Virus Control.dll / Control Center: no WinMM MIDI imports; SetupDi + CreateFile + DeviceIoControl; embeds the Ploytec ASIO client. Points to answer (c) raw IOCTL. (E010, E011)
- Static (c) evidence: interface GUID {8002DBFF-C173-4BFF-80BE-6815D7E13071}, shared by VirusUSB.sys and all clients; 22 METHOD_BUFFERED IOCTLs, identical set in VirusUSB_X64.dll / Virus Control.dll / Control Center (E017-E019, spec.md §11). Still missing: what each IOCTL does, and a runtime check.
- MSI UpdateFirmware CA runs on install: never run/repair the MSI with the synth attached. (E003)

## Blockers
- (cleared) usbmon loaded; JDK 21 + Ghidra via REA work (open_binary needs provider_id="ghidra"); passthrough now live (1-9:1.* bound to usbfs, E015 resolved).
- Snapshot `lab-ready` does not exist (there is `claude-starts`); confirm which name to use.

## Open questions
- Key: (c) looks likely; confirm which device interface GUID/IOCTLs Virus Control.dll uses, and whether it uses MI_04 for Remote/editor traffic and MI_03 only for the Windows MIDI ports.
- User's "inbox driver handles playback": true only without VirusUSB.sys? (claims C9)

## Next step
1. Phase 0.3 knob test again, now that passthrough is live (user turns Cutoff; capture.sh on usbmon1, device 17).
2. Phase 2.2: decode IOCTL semantics. Decompile the VirusUSB_X64.dll wrappers around each call site (spec.md §11), then the VirusUSB.sys IRP_MJ_DEVICE_CONTROL dispatch (switch on code-0x220000), to find which IOCTLs submit bulk URBs on EP 0x05/0x06 (MIDI/SysEx path).
3. Tip: objdump -d -M intel + scanning for `mov edx,imm` before `call [IAT]` is much faster than per-function decompile for listing call-site constants.
