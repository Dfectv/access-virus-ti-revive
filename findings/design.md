# Design: user-mode Virus TI driver for Windows 11 (draft)

_Goal (user, 2026-10-10): a user-level driver for the Virus TI, packaged in a new MSI that works on Windows 11._
_Status: M1 reached 2026-10-10 (E043): plugin works via injected emulation on WinUSB (Win10 VM). Architecture accepted 2026-10-10. Decisions: (1) MIDI/plugin first, audio later; (2) wrapper VST3 with in-memory IAT patching is OK; (3) Windows version still open._

## Constraints
- No custom kernel driver: the original VirusUSB.sys is blocked by Win11 Code Integrity (C8). Only Microsoft-signed inbox kernel drivers (WinUSB, usbaudio, usbccgp) may be used.
- The Access software (Virus Control.dll inside "Virus TI.vst3", Virus Control Center) must keep working unmodified on disk. It talks to the driver via SetupDiGetClassDevsA(GUID {8002DBFF-C173-4BFF-80BE-6815D7E13071}) + CreateFileA + DeviceIoControl (E017, E035).
- Never redistribute Access binaries: the MSI ships only our code; the user's own Access install is required.

## What the device needs (from spec.md)
- Handshake 4E 73 52 01 on EP 0x05 after SET_CONFIGURATION (§3).
- MIDI/Remote: raw USB-MIDI 4-byte packets on interrupt EPs 0x05/0x85 (MI_03) and 0x06/0x86 (MI_04) (§4, §5). The plugin builds the packets itself and the driver forwards them verbatim (E035), so a user-mode forwarder is enough.
- Control requests: vendor IN/OUT (flash read §2.1) via the generic STD_VENDOR_REQUEST IOCTL.
- Audio: isochronous EP 0x01 OUT / 0x82 IN, 48 kHz, 12-byte frames (E020); routing via global param "USB IO Configuration" (E014, C4).

## Proposed architecture
```
 DAW ── Virus TI.vst3 (Access, unmodified) ── Virus Control.dll
            ▲ loaded by
 vtiwrap.vst3 (ours) ── IAT hooks: SetupDi*/CreateFileA/DeviceIoControl/CloseHandle
            │  emulates the 31 IOCTLs (spec §11.1)
            ▼ named pipe + shared-memory rings
 vtibroker.exe (ours, per-user service / tray)
   - owns WinUSB handles (MI_03, MI_04), sends handshake, keepalive passthrough
   - multiplexes: plugin raw pipe (0x220084/88), Control Center, MIDI ports
   - exposes Windows MIDI ports for DAWs (Windows MIDI Services virtual device)
            ▼ WinUSB (inbox, MS-signed)
 Virus TI  MI_03 + MI_04 → WinUSB      MI_00..02 → usbaudio (inbox) or WinUSB (see DECIDE 1)
```
1. **Binding**: INF that binds WinUSB to USB\VID_133E&PID_0815&MI_03 and &MI_04 (composite via inbox usbccgp). Catalog signed with a self-signed cert the MSI installs into LocalMachine Root + TrustedPublisher (Zadig/libwdi approach; known to work on Win11 for WinUSB). Must be verified on the user's Win11 (Secure Boot, HVCI).
2. **vtibroker.exe**: single owner of the WinUSB handles (WinUSB allows one open per interface). Implements the raw-pipe ring semantics of E035 for clients.
3. **vtiwrap.vst3 / launcher for Control Center**: loads the original module, patches its import table so the device calls go to our emulation; everything else is forwarded unchanged.
4. **MIDI ports for DAWs**: Windows MIDI Services (in-box on current Win11) virtual endpoints, so any DAW sees "Virus TI MIDI" / "Virus TI Synth" without a kernel driver.
5. **MSI**: WiX 4; installs broker (autostart), wrapper plugin, INF/catalog (pnputil), cert; uninstall restores.

## DECIDE
1. **Audio path**: (a) Windows inbox usbaudio for MI_00..02 (least work; DAW uses WASAPI or FlexASIO; plugin-integrated audio streaming (APC IOCTLs 0x2200E4/E8) not supported), or (b) our own user-mode ASIO + WinUSB isochronous (full plugin integration, much more work, needs the iso frame format decoded). Test first: does Win11 inbox usbaudio drive MI_00..02 (claim C9)?
2. **Plugin hooking** via wrapper VST3 that patches Virus Control.dll in memory (no files changed on disk). OK?
3. **Minimum Windows version** (Windows MIDI Services availability).

## Risks / open
- Device wedges if IN endpoints are not drained or transfers are aborted (E042): the broker must read EP 0x85/0x86 continuously, time out every write, and port-cycle the device (IOCTL_USB_HUB_CYCLE_PORT / devnode restart, admin) if all pipes fail.
- 0x220008 (vendor request pass-through): translate recipient interface -> device, else WinUSB rewrites wIndex (E040).
- 0x220408/0x220410 are standard hub IOCTLs on the root hub (E036): pass through, no emulation.
- The hook must be installed synchronously when Virus Control.dll loads (it opens the device during load, E036): the wrapper loads it itself and patches before calling its entry points.
- Generic PtIOCTL (0x220010) and config blocks (0x22000C, 0x220064/68, 0x2200D0/D4) contents: needed to emulate what the plugin reads at start-up; capture real values from the VM (log DeviceIoControl in/out buffers) before emulating.
- Interrupt IN throughput with WinUSB on bare metal (E032 was VM-limited).

## Milestones
- M1 (done, E043): IOCTL emulation in-process (injected) on WinUSB; plugin connects, syncs, preset switching.
- M2 (done, E044): wrapper VST3 that loads the original plugin and installs the emulation itself (no injector).
- M3: vtibroker (per-user background process): owns WinUSB, drains IN endpoints permanently, port-cycle recovery, serves plugin instances + Control Center over named pipes; Windows MIDI ports.
- M4: MSI (WiX): INF + cert/catalog generation custom action, broker autostart, wrapper VST3; uninstall restores.
- M5: verify on Windows 11 (CI policy, HVCI, Windows MIDI Services); audio decision.
