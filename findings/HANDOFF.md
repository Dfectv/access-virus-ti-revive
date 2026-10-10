# HANDOFF

_Last updated: 2026-10-10 ~21:45. M3 broker works with vtiprobe-pipe (E047); wrapper via broker under test. M2 reached (E044): "Virus TI (WinUSB)" wrapper VST3 works in Ableton without injection. Inbox usbaudio works for Virus audio in/out (E045, C9 confirmed); sound on Virus phones with part output "Out 1" (E046). Next: M3 broker._ VM now runs WinUSB instead of the Access driver (E038); broker core works on Linux/libusb and Windows/WinUSB._ Final goal: user-mode Win11 driver + MSI (see findings/design.md). IOCTL semantics decoded (E034/E035, spec §11.1)._

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

- Wire protocol (spec.md §2-7): handshake 4E 73 52 01 on EP 0x05 at device start; USB-MIDI packets on interrupt EPs; plugin syncs RAM banks via SysEx 30/10 and pushes cached Single Dumps on every preset switch (E033).

## Broker core (src/broker)
- vticore.c/h: libusb open/claim MI_03+MI_04, handshake, interrupt read threads, USB-MIDI SysEx pack/unpack. vtiprobe = hardware test (make -C src/broker; run work/build/linux/vtiprobe with the Virus detached from the VM). Passed on the host (E037).
- Host test prerequisites: scripts/99-virus-ti.rules installed (done); after `vm.sh detach` run `sudo udevadm trigger --action=change --sysname-match=1-9` in a real terminal (libvirt resets the node to root:root) and close the virt-manager console (its SPICE USB redirection grabs the device).

## Broker M3 (src/broker)
- vtibroker.exe: owns WinUSB (vticore_winusb), drains IN permanently, handshake per device open, reopen loop, named pipe \\.\pipe\vtibroker (vtiproto.h framing), fans out RX to all clients; log %TEMP%\vtibroker.log, -v = per message.
- vticore_pipe.c: same vticore.h API over the pipe; the wrapper VST3 now links this (needs the broker running). vtiprobe-pipe.exe = probe through the broker.
- In the VM the broker runs from scheduled task "vtibroker" (ONLOGON, interactive, also started with `schtasks /Run /TN vtibroker`); exe at C:\vti\vtibroker.exe. Stop with Stop-Process before replacing the exe.
- Known risk: broadcast does blocking WriteFile to client pipes from the device reader thread; a stuck client could stall draining (TODO: per-client queue).

## Emulation M1 (src/wrapper)
- vtiemu.dll: injected like the tracer (vtiinject.exe "<Ableton exe>" C:\vti\vtiemu.dll BEFORE loading the plugin). Redirects SetupDi(Access GUID) -> WinUSB MI_04 GUID, virtual handles for CreateFile on that path, emulates IOCTLs (pipe + RX ring, vendor requests recipient->device, replay table), logs to %TEMP%\vtiemu-<pid>.log.
- replay_table.h is generated: python3 -I scripts/gen_replay.py captures/trace/vtihook-1896.log src/wrapper/replay_table.h
- A loaded vtiemu*.dll is locked in the VM while the DAW runs: push rebuilds under a new name (vtiemu-f.dll is current).
- If all pipes fail with error 31 / writes time out (121): USB reset via `vm.sh detach && vm.sh attach` (guest stays clean with usbccgp).
- M2 wrapper: src/wrapper/vtiwrap.c -> work/build/VirusTI_WinUSB.vst3 (emulation linked in, -DVTIEMU_NO_DLLMAIN). Installs to C:\Program Files\Common Files\VST3\. Shows as "Virus TI (WinUSB)" (class IDs byte 15 XOR 0x5A). Do not also inject vtiemu into the same DAW.
- Next: test the wrapper in Ableton (rescan plug-ins); then M3 broker.

## VM driver state (IMPORTANT)
- Since E038 the VM has NO Access driver: usbccgp + WinUSB on MI_03/MI_04 (src/inf/vti_winusb.inf), usbaudio on MI_00. The original plugin cannot work in this state until our emulation exists.
- Back to the Access driver: `vm.sh revert pre-winusb` (live snapshot 2026-10-10 17:20, Access driver + Ableton setup). Other snapshots: clean, suite-ready, lab-ready, claude-starts.
- Windows build: make -C src/broker -> work/build/vtiprobe.exe, vtisend.exe (WinUSB backend vticore_winusb.c). vtisend sends SysEx hex strings and prints all RX; vtiflash repeats the flash read (copied to VM C:\vti\).
- Synth global 0x3A is back at 1 (dumps on cable 1) after the E039 test.

## Working procedure (VM)
- Never physically replug the Virus during a session: host snd-usb-audio grabs it first (host also uses USB audio: mainboard + Sennheiser mic, so don't unload it).
- To reset the link: close the DAW, `vm.sh detach && vm.sh attach`, then in the guest remove the stale USB\VID_133E&PID_0815 devnode (the one that stays "present" from before), `pnputil /scan-devices`, remove the one left in Error. Exactly one Virus TI USB must be present. AbletonAudioCpl can block pnputil; retry.
- DAW: Ableton Live 11, ASIO "Virus TI", buffer 64/128/256/512 (Waveform at an odd buffer size gave "not synced").
- usbdecode.py CAPTURE ADDR decodes non-iso traffic; capture.sh resolves the current device address itself.

## Blockers
- (cleared) usbmon loaded; JDK 21 + Ghidra via REA work (open_binary needs provider_id="ghidra"); passthrough now live (1-9:1.* bound to usbfs, E015 resolved).
- (resolved) snapshots exist: clean, suite-ready, lab-ready, claude-starts, pre-winusb.

## Open questions
- Key: (c) looks likely; confirm which device interface GUID/IOCTLs Virus Control.dll uses, and whether it uses MI_04 for Remote/editor traffic and MI_03 only for the Windows MIDI ports.
- User's "inbox driver handles playback": true only without VirusUSB.sys? (claims C9)

## Key results (Phase 2.2)
- VirusUSB.sys dispatcher decompiled: work/decomp/VirusUSB.sys_ioctl_dispatch_f102c010.c (untracked). Plugin MIDI/Remote = raw pipe IOCTLs 0x220084/88/8C/90 forwarding USB-MIDI packets verbatim (E035). Flash read = 0x220008 vendor request pass-through.
- Architecture proposal + open user decisions: findings/design.md (DECIDE 1-3).

## Next step
0. IOCTL tracer built: src/vtihook (make -C src/vtihook -> work/build/vtihook.dll + vtiinject.exe, copied to VM C:\vti\). Use: start Ableton, then `vm.sh ps 'C:\vti\vtiinject.exe "Ableton Live 11 Suite.exe" C:\vti\vtihook.dll'` BEFORE loading the plugin; log = %TEMP%\vtihook-<pid>.log (pull with vm.sh pull). Ask the user before injecting into a running DAW (crash risk).
   First trace done (E036, captures/trace/vtihook-1896.log): start-up values recorded, 0x2204xx = hub IOCTLs. Tracer now also dumps UserBuffer inputs before the call (rebuild + push before next trace).
   User decided: MIDI/plugin first, audio later; wrapper VST3 OK (design.md). Then: runtime-log Virus Control.dll DeviceIoControl in/out buffers in the VM (API hook or ProcMon-like tracer) to capture real values of 0x22000C/0x220010/0x220014/0x220064-68/0x2200D0-D4 for emulation; test claim C9 (inbox usbaudio on MI_00..02) on Win11.
1. Open: meaning of 74/39/14/6E, the post-dump CC 113 / Poly Pressure A0 messages, and F5 xx yy. Use usbdecode.py --events for any counting/filtering (several events share one transfer).
2. Test C7 "needs": does the synth behave differently without F5/F8 (needs a host-side sender; Linux amidi to hw:Virus with snd-usb-audio is one option).
3. (done) Phase 2.2 IOCTL semantics, see E034/E035. Remaining: confirm raw pipe = EP 0x06/0x86 at runtime; 0x220408/410 meaning.
4. Tip: objdump -d -M intel + scanning for `mov edx,imm` before `call [IAT]` is much faster than per-function decompile for listing call-site constants.
