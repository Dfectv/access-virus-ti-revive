# Plan: TI Revive for macOS on Apple Silicon (M processors)

Status: plan (2026-10-12). Same approach as on Windows: keep the user's original Access plug-ins, replace the
blocked driver with user-space code plus a broker, no kernel code. **User guideline: avoid the old Access
drivers at all cost** (no kext, no HAL plug-in, no Access MIDI driver are installed); only the plug-ins and
Virus Control are used, as on Windows.

Test machine: the user's **MacBook Air M3 with Ableton Live** (set up by the user; SSH from this PC).

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

## Driver interface of the Mac plug-in (static RE, E093)

On the Mac, Virus Control does not talk to the kernel driver itself. Everything driver-side sits in user-space
components with small, clear interfaces (all x86_64 + i386, symbols kept):

| Direction | Interface | Access component (driver package) |
|---|---|---|
| Device, audio, USB control requests | C++ class `VirusClient` in `/Library/Application Support/Access Music/Virus TI/Common/libVirusUSB.dylib` (absolute install name, hard-linked by Virus Control): 20 methods used (`vc_init`, `vc_open(void *deviceID)`, `vc_start(rate)`, `vc_stop`, `vc_close`, `vc_term`, `vc_getNumDevices`, `vc_getDeviceInfo`, `vc_getDeviceID`, `vc_request(dir, level, recipient, request, value, index, data, length)`, `vc_getBuffersize`, `vc_get/setSamplerate`, `vc_getLatencies`, `vc_setIntParam`, `vc_isPluginMaster`, version/status queries) | libVirusUSB.dylib (Ploytec PGOSXDevice / OSXDeviceManager, talks to the kext) |
| Audio to/from the plug-in | Core Audio style IOProc registered in `vc_open`; calls the client's virtual method at vtable +0x18 with (device, in channels, in bytes, in data, out channels, out bytes, out data, sample time) | same |
| MIDI plug-in -> Virus | CFMessagePort named **"VirusTIWritePort"** (Virus Control: `CFMessagePortCreateRemote` + `CFMessagePortSendRequest`) | "Virus TI MIDI Driver.plugin" (Core MIDI driver, creates the port locally) |
| MIDI Virus -> plug-in | probably POSIX shared memory "VirusTISharedMem" + named semaphores (shm_open/mmap/sem_* in both; the MIDI driver's VirusTIUSBMIDIDevice handles the "hidden port" = the plug-in channel MI_04); Virus Control itself also has "VirusTICommMem" (Access::Threading::IPC::SharedMemory, likely instance coordination). To confirm at runtime on the Mac (which shm segments the DAW opens) | "Virus TI MIDI Driver.plugin" |

`VirusClient` virtual methods called by the library (implemented in Virus Control): +0x00 device attached
(PGOSXDevice* = opaque device ID), +0x08 device removed, +0x10 notification (device, event: 1 other client
attached, 2 detached, 3 state change, a, b), +0x18 audio IOProc. Private data at VirusClient+8 (owned by the
library: device, device manager, USB/HAL versions). `VC_DeviceInfo`: u16 +0, u8 +2, u16 +4/+6/+8, u8 +0xa..+0xd,
u32 +0x10, char[256] +0x14, char[256] +0x114 (from the USB descriptors). `vc_request` = USB control request
1:1, `vc_start(framesize)` sets the buffer size and starts the IOProc.

## Approach for macOS (preferred: no wrapper needed)

Because the interfaces are user-space, TI Revive can provide them itself instead of wrapping the plug-in:

- **Our own `libVirusUSB.dylib`** (x86_64, same exported `VirusClient` methods, installed at the path above -
  that file belongs to Access' *driver package*, which is not installed): device list and control requests via
  the broker; audio from Apple's USB audio class driver (the Virus' audio function is class compliant, so
  Core Audio should offer it as a device without any Access driver - to confirm on the Mac) through a Core Audio
  IOProc on that device, forwarded to the client's callback; device clock = synth clock (no drift as with
  WASAPI, E074).
- **Our own "VirusTIWritePort"** (CFMessagePort server in the broker) for MIDI from the plug-in, and the receive
  path once analysed.
- **Broker** (`vtibroker` for macOS on libusb, Unix domain socket, LaunchAgent on demand): owns the vendor
  interfaces MI_03/MI_04 (handshake, drain, MIDI), like on Windows.
- The original plug-ins (VST/VST3/AU) stay untouched. **Fallback**: a VST3/AU wrapper that rebinds Virus
  Control's imports to our implementation (the Windows approach), if replacing the library is not enough.
- The DAW must run under Rosetta 2 (Ableton: Finder > Get Info > "Open using Rosetta"), because the Access
  plug-ins are Intel-only.

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

- MacBook Air M3: macOS version, Ableton installed (+ Rosetta), the Access software installed **without** its
  driver package (or with it - it cannot load anyway; to decide when installing), SSH access enabled for this PC.
