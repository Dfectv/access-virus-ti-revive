# HANDOFF

_Last updated: 2026-10-11 ~06:30. M1-M5 done (Win11 VM: plugin + audio in REAPER, Control Center OK, E059). Next: M6 final MSI "TI Revive" (design.md M6 decisions)._


## M5 Windows 11 VM (done 2026-10-11, E058/E059)
- VM "win11": scripts/win11/create-vm.sh (q35, UEFI Secure Boot with MS keys, TPM 2.0 swtpm, 4 vCPU, 8 GB, SATA 80 GB win11.qcow2, e1000e). Unattended install from pool volumes win11-install.iso (Windows11_Client_x64_de-de_26300_9457.iso from the Ventoy stick) + win11-unattend.iso (make-unattend-iso.sh: autounattend.xml = Windows 11 Pro via public generic key (not activated), local account vti/vti, autologon, setup-ssh.ps1 -> OpenSSH + ~/.ssh/vm_re.pub). Control: `VM=win11 scripts/vm.sh ps ...`.
- Deployed via scripts/win11/deploy-m5.sh (Access MSI without drivers/firmware, WinUSB INF, broker + vticc tasks, wrapper, Reaper 7.82). Disk backup of the fresh OS: pool volume win11-base.qcow2 (internal snapshots impossible: raw UEFI nvram).
- Currently the Virus is attached to **win11** (detached from win10).
- The Virus can only be passed to one VM at a time: virsh detach-device win10 scripts/virus.xml, attach to win11 (and back).

## M6 TI Revive MSI (2026-10-11)
- Sources: src/installer (TIRevive.wxs, tirevive-driver.ps1, tirevive-access.ps1, notice.rtf, build.ps1). Build: scripts/win11/build-msi.sh (needs the win11 VM running with the BUILD disk: .NET 8 in C:\build\dotnet, WiX 5.0.2 in C:\build\wix + UI/Util extensions) -> work/release/TI Revive.msi.
- Disks (pool default): win11-base.qcow2 = fresh OS + SSH; win11-build.qcow2 = build machine (WiX/.NET, old manual deploy); win11.qcow2 = currently the clean MSI test machine (base + TI Revive.msi + Reaper). Swap = VM off, vol-delete win11.qcow2, vol-clone <src> win11.qcow2.
- E060: clean install test passed (driver, cert, broker, plugin files, Access via helper); E061: plugin audible in REAPER; Control Center via the Access shortcut runs without the bridge ("no device") -> 0.1.1 retargets those shortcuts. Virus attached to win11 now.
- Do NOT use virt-manager's USB redirection for the Virus while it is passed through (E061): it re-enumerates the device and leaves the hostdev stale -> virsh detach/attach.
- Open for M6: uninstall test, rebuild 0.1.1 with the signing-order log fix, user-facing README.

## RESUME HERE (2026-10-11)
State at pause:
- VM `win10` was reverted to snapshot **pre-winusb** (Access driver, 17:20 state) for the M4 audio analysis. Our working WinUSB setup (broker task, wrapper VST3, vtilaunch, vticc task) is saved in snapshot **m3-done** (18:34). Return: `scripts/vm.sh revert m3-done`, then `scripts/vm.sh detach && scripts/vm.sh attach` (after a revert the host snd-usb-audio grabs the Virus; check `1-9:1.*` drivers = usbfs).
- Ableton (pid 7180, original "Virus TI" plugin, Ableton audio = Windows default driver, looped MIDI clip on the plugin track) has vtihook.dll + vtihook-b.dll injected. Logs: `%TEMP%\vtihook-7180.log` (pull with vm.sh pull). These are gone if Ableton/VM restarted: then start Ableton, inject `C:\vti\vtihook-b.dll` with vtiinject before loading the plugin.
- 2026-10-11: E054 = input audio source found: shared mapping (ptr at plugin obj+0x1181F8, i.e. 0x220030 buffer - 0x40; header 0x1FFDD8 + AudioSM, 6 ch, int32 with 16-bit audio in the upper half). E055: it is mapped by the driver via IOCTL 0x220010 (layout spec §11.4). M4 implemented (src/wrapper/vtiaudio.c: 0x220010 own input region, WASAPI capture of the "Virus TI" endpoint -> int16<<16 ch0/1, clock: output ring consumed + buffer-switch event per packet; fallback silence clock). VM is back on m3-done (snapshot XML fixed: the stored hostdev bus/device address made the revert fail after a host reboot; removed via snapshot-dumpxml + snapshot-create --redefine). E056: plugin USB1 = device channels 2/3 -> capture now takes all endpoint channels. Installed and tested: M4 works (E057). VM snapshot **m4-done** (2026-10-11 05:24) = working state incl. audio; its stored hostdev bus/device address was removed so it reverts after host reboots too (do the same for new snapshots: snapshot-dumpxml, drop `<address bus= device=>` under the 133e:0815 source, snapshot-create --redefine).
- Open question (E053): the plugin outputs the Virus audio, but its AudioSM ring always reads empty from outside. Next step: with vtihook-b injected, make the plugin restart its stream (Preferences -> Audio: change buffer size and back, or reload the plugin), keep the clip looping, then `grep "BS #\|interposed" vtihook-7180.log`. If blocks_with_signal > 0: the ring IS the source (consumer clears it) -> M4 = emulate AudioSM (fill the ring from WASAPI capture of the inbox "Virus TI" device, advance +0x81028, SetEvent every +0x810B8 frames). If still 0: look for another source (decompile the Virus Control.dll reader of +0x81028/+0x810BC; CreateFileMappingA wrapper at 0x180031f70).
- Known AudioSM facts: E051 (layout), E052 (live: nCh 2, 44100 at +0x81030, 256 at +0x810B8, frame counter +0x81028, read pos +0x810BC, threaded event registration), spec §11.3.
- Plan after M4: M5 Win11 verification, M6 final MSI (user order). Windows MIDI ports deferred.
- Tools: src/vtihook (vtihook tracer with AudioSM monitor + buffer-switch interposer, vtiinject, vtipeek: `vtipeek <pid> <buf> -N` scans, `0` race mode), scripts/usbdecode.py (`--events`), capture.sh.

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
- Broker clients get per-client send queues (256 KB, overflow drops the client) and sender threads; vti_is_dead() triggers close/reopen. Port cycle (admin) still TODO for the MSI (elevated helper).
- vtiemu reconnects transparently (dev_get) after broker restart / unplug.
- Control Center (VM): scheduled task "vticc" runs C:\vti\start-cc.cmd (= scripts/vm-start-cc.cmd: vtilaunch + vtiemu-pipe.dll); start with `schtasks /Run /TN vticc`.
- Control Center: `C:\vti\vtilaunch.exe "C:\Program Files\Access Music\Virus TI\Common\Virus Control Center.exe"` (suspended start + APC LoadLibraryW of vtiemu-pipe.dll). Do NOT run an OS/firmware update through the emulation before vendor OUT requests are verified.

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
