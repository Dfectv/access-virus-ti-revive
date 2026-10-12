# Plan: TI Revive for macOS on Apple Silicon (M processors)

Status: plan (2026-10-12). Same approach as on Windows: keep the user's original Access software, replace the
blocked driver with a user-space emulation plus a broker, no kernel code.

## Starting point (static recon of the Access installer, E091)

Installer: `originals/Virus TI Software Suite 5.1.7.00.pkg` (xar, built 2017; unpacked to `work/mac/`, not in git).

| Component | Path (installed) | Architectures | On an Apple Silicon Mac |
|---|---|---|---|
| Kernel driver `AccessVirusTI.kext` (matches IOUSBDevice) | /Library/Extensions | x86_64 only | never loads (Apple Silicon loads arm64e kexts only) - same situation as Windows 11, stricter |
| Core Audio HAL plug-in `de.access-music.virus_ti.plugin` | /Library/Audio/Plug-Ins/HAL | i386 + x86_64 | Intel only; talks to the kext |
| Core MIDI driver `Virus TI MIDI Driver.plugin` | /Library/Audio/MIDI Drivers | i386 + x86_64 | Intel only; talks to the kext |
| Plug-ins Virus TI (VST, VST3, AU), Virus TI Snow | /Library/Audio/Plug-Ins | i386 + x86_64 | run only in a DAW started under Rosetta 2 |
| `Virus Control.bundle` (shared by the plug-ins) | /Library/Application Support/Access Music/Virus TI/Common | i386 + x86_64 | under Rosetta 2 |
| `Virus Control Center.app` | /Applications/Access Music/Virus TI | i386 only | does not start on macOS 10.15+ (no 32-bit) |

Consequences:
- The original driver can never work on an M Mac; TI Revive's approach is needed there.
- Our plug-in wrapper must be x86_64 (it loads the Intel-only original) and the DAW must run under Rosetta.
- Control Center (OS updates, settings) is gone on modern macOS; out of scope at first.

## Lab

- **This Linux PC (x86)**: no macOS VM for Apple Silicon is possible (arm64 macOS needs Apple hardware; an Intel
  macOS VM would be against Apple's licence and does not reproduce the M Mac situation: kext, Rosetta). Used for
  everything static: installer recon, REA/Ghidra analysis of the kext and plug-ins, code.
- **GitHub Actions `macos-14`** (Apple Silicon): builds (x86_64 + arm64), packaging.
- **A real Apple Silicon Mac** with the Virus attached: all runtime tests (USB, plug-in, audio, MIDI), remote
  controlled over SSH from this PC (Remote Login + key), Homebrew + Xcode Command Line Tools; optionally a macOS
  VM on that Mac (UTM / tart) for clean installer tests - USB pass-through of the Virus into such a VM is
  probably not possible (Apple Virtualization framework), to verify.

## Phases

1. **Recon (static, now)**: inventory of the installer (done, E091); Info.plists, install scripts, kext
   personalities; strings and imports of Virus Control.bundle (IOKit calls instead of DeviceIoControl).
2. **Reverse engineering (REA, now)**: the kext's IOUserClient (external methods = selectors and structures,
   shared memory via clientMemoryForType / IOConnectMapMemory, notifications for the buffer switch), and how
   Virus Control, the HAL plug-in and the MIDI driver use it. Map to the Windows IOCTLs (same Ploytec framework,
   E075) - expected large overlap.
3. **On the Mac (needs the lab)**: with the Access software installed and the Virus attached: confirm the kext is
   rejected, whether Apple's USB audio class driver offers the Virus as a Core Audio device, and what the plug-in
   shows ("not connected").
4. **Broker for macOS**: vtibroker on libusb (backend `vticore.c` exists, tested on Linux, E037), Unix domain
   socket instead of the named pipe, LaunchAgent with on-demand start and idle exit (as 0.3.1).
5. **Plug-in wrapper (VST3 + AU, x86_64)**: loads the original plug-in and rebinds its IOKit imports
   (IOServiceGetMatchingServices, IOServiceOpen, IOConnectCall*Method, IOConnectMapMemory, ...) to an emulation
   that answers like the kext's user client, backed by the broker (port of vtiemu.c).
6. **Audio**: (A) Core Audio class driver, if it serves the Virus: read it in the broker/emulation, check the
   clock (device clock = no drift, unlike WASAPI, E074); (B) else stream it over libusb (claiming the audio
   interface from Apple's driver is restricted on macOS).
7. **MIDI ports**: macOS has virtual Core MIDI sources/destinations in user space (MIDISourceCreate /
   MIDIDestinationCreate) - "Virus TI Synth" / "Virus TI MIDI" from the broker without any driver (the
   counterpart of the Windows MIDI Services work).
8. **Packaging**: .pkg (unsigned first: Gatekeeper "Open Anyway"; signing/notarisation needs an Apple Developer
   account), uninstall script.
9. **Tests**: DAW under Rosetta (sync, presets, audio, timing like E078).

## Needed from the user

- An Apple Silicon Mac for the lab (model, macOS version) with SSH access.
- Which DAW on the Mac.
