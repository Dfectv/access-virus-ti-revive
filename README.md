# TI Revive

**Total Integration for the Access Virus TI on Windows 11 — without the old kernel driver.**

TI Revive brings the Access Virus TI (Desktop, Polar, Keyboard, Snow) back to current Windows:
the original Virus TI plug-in with its "Total Integration" (parameter sync, preset browsing,
the Virus audio streamed into your DAW through the plug-in) and Virus Control Center work again,
on Microsoft's own, signed USB drivers. No kernel code of its own is installed.

> TI Revive is an independent project. It is **not affiliated with Access Music Electronics GmbH or
> Kemper GmbH**. "Access", "Virus" and "Virus TI" are trademarks of their owners and are used only to
> describe compatibility. TI Revive contains no Access software; you need your own copy of the Access
> "Virus TI Software Suite" installer.

---

## Why

The Virus TI talks to its plug-in through a custom USB kernel driver (`VirusUSB.sys`, version 2.9.66
from 2014, built on a Ploytec OEM driver). Windows 11 refuses to load it: Code Integrity blocks the
old cross-signed driver (Device Manager shows **Code 39**, event log CodeIntegrity 3076/3077). Without
that driver the plug-in reports "no Virus TI connected" and the synthesizer is reduced to a plain
MIDI module — if at all.

Access discontinued the Virus TI and no updated driver exists. The goal of this project was to
understand what the old driver actually does, and to replace it with something that Windows 11
accepts — so the synth, the plug-in and the user's existing Access software keep working.

## How it works

```
 DAW ── Virus TI (TI Revive).vst3 ── loads the user's original "Virus TI.vst3" / Virus Control.dll
          │  in-memory import hooks: SetupDi*/CreateFile/DeviceIoControl/CM_* are answered by
          │  an emulation of the old driver's interface (31 IOCTLs, shared audio memory)
          │
          ├── MIDI/remote: named pipe \\.\pipe\vtibroker
          │      vtibroker.exe (per-user background process, started on demand, exits when idle): owns the MIDI interfaces,
          │      keeps them drained, re-connects after unplug, serves plug-in + Control Center
          │        ▼  WinUSB (Microsoft, inbox)  ──  Virus TI MI_03 + MI_04
          │
          └── audio: isochronous streams, directly from the plug-in process
                   ▼  WinUSB (Microsoft, inbox)  ──  Virus TI audio function MI_00 (interfaces 0..2)
```

**Driver binding.** An INF-only driver package assigns Windows' built-in **WinUSB** driver to the
Virus TI's two vendor-specific (MIDI/remote) interfaces and to its audio function. Because the package contains no
binaries, it only needs a signed catalog: the installer creates a certificate on your computer, signs
the catalog, trusts the certificate's public part and **deletes its private key** right away, so it
cannot sign anything else. Uninstall removes package and certificate again.

**Plug-in and Control Center.** The Access software stays unmodified on disk. TI Revive's VST3 wrapper
loads the original plug-in and patches its import table in memory; Virus Control Center is started
through a small launcher that does the same. Every call the Access code makes to the old driver is
answered by a user-mode emulation (`src/wrapper/vtiemu.c`):

- the raw USB-MIDI channel (the plug-in writes 4-byte USB-MIDI packets; received data goes into a
  ring buffer shared with the plug-in and an event is signalled — exactly like the old driver did),
- vendor control requests (flash/version reads; translated so WinUSB passes them unchanged),
- start-up queries (version strings, configuration blocks) replayed from the original driver,
- the device topology that Control Center inspects (`CM_Get_Parent` / `CM_Get_Device_IDA`).

**Audio through the plug-in.** The old driver mapped a shared ring buffer ("AudioSM") into the
plug-in's process and filled it with the Virus' six USB audio channels. TI Revive allocates that buffer
itself; the broker streams the Virus' isochronous audio endpoints directly on WinUSB, the way the old driver did,
and shares the samples with the plug-in (and the ASIO driver) through shared memory:
44.1 kHz, every received packet's frame count is mirrored on the stream towards the synth, so the synth's
clock is the master and the plug-in's sample counter is exactly the number of frames the synth produced
(its MIDI timestamps then never drift against the synth). Reads are scheduled on USB frame numbers, so
even a stalled PC keeps the counter exact (the gap is filled with silence). The samples go into the
buffer in the driver's format and the plug-in's audio thread is clocked with the same buffer-switch event.
If the audio function is not on WinUSB, TI Revive falls back to the Windows "Virus TI" recording device
(WASAPI, exclusive 44.1 kHz).

**ASIO sound card.** Like the Access ASIO driver, TI Revive offers the Virus as an ASIO device,
**"Virus TI (TI Revive)"**: a small COM wrapper (`TIRevive_ASIO.dll`) loads your original Access ASIO driver
(`VirusUSB_X64.dll`) and answers its driver calls with the same emulation as the plug-in. Its 2 inputs are the
Virus analog inputs, its 2 outputs play through the Virus outputs, 44.1 kHz, 256 samples - exactly the
original's channels and buffer. Plug-in and ASIO driver run at the same time and follow the same synth clock.

**The broker.** WinUSB allows one owner per interface, and the Virus stops accepting data when nobody
reads its input endpoints. `vtibroker.exe` therefore owns the device while it is in use, sends the
initialisation handshake, keeps both IN endpoints drained, re-opens the device after unplug or
errors, and multiplexes it for the plug-in and Control Center. It is not started at logon: the plug-in or
Control Center starts it when they need the Virus, and it exits 30 seconds after the last of them has gone.

The full protocol description is in [`findings/spec.md`](findings/spec.md); every statement there is
backed by an entry in [`findings/evidence.md`](findings/evidence.md) (captures, traces, decompiled
driver code).

## Installation

Requirements: Windows 10/11 x64, an Access Virus TI on USB, your own **Access "Virus TI Software Suite"
64-bit installer** (`.msi`, e.g. 5.1.7), a VST3 host.

1. Do **not** install the Access software on its own first (it would try to install the blocked driver
   and run its firmware updater). If it is already installed, that is fine — TI Revive removes the
   old driver packages.
2. Put `TI Revive.msi` and the Access `.msi` in the same folder (or in *Downloads*).
3. Run `TI Revive.msi`. The MSI is not code-signed (a certificate costs money), so Windows SmartScreen
   may show **"Windows protected your PC"**: click **More info → Run anyway**. On the last page keep **"Install the Access Virus TI software now"** checked:
   TI Revive installs your Access software **without its old drivers and without the firmware
   updater** (a local copy of the MSI with those steps disabled).
4. Connect the Virus TI. In your DAW, rescan plug-ins and use **"Virus TI (TI Revive)"** (not the
   original "Virus TI"). Start Control Center from **Start → TI Revive → Virus Control Center (TI Revive)**.

Uninstall via *Settings → Apps → TI Revive* (removes driver package, certificate, broker, plug-in and
shortcuts). The Access software is uninstalled separately.

## Good to know

- **Tested with** Ableton Live 11 on Windows 10 and Windows 11 (Secure Boot, TPM 2.0). REAPER loads and
  connects the plug-in; make sure the track is record-armed with a MIDI input and monitoring on.

- **One plug-in instance per synth.** Virus Control is designed that way (it controls all 16 parts);
  a second instance reports "no Virus TI connected". This is the original behaviour.
- **Audio buffer size** must be 64, 128, 256 or 512 samples, otherwise the plug-in reports
  "communication problems" (original behaviour).
- **Hearing the Virus**: with a part output set to *USB*, the audio comes through the plug-in track.
- **Virus as sound card**: choose the ASIO device **"Virus TI (TI Revive)"** (not the Access "Virus USB ASIO
  driver" entry) to play your DAW through the Virus outputs and record its analog inputs; buffer 256 samples.
  Keep the Virus "USB Audio mode" (Patch Utility -> Config) at **2 outs / no input**: that is the sound card
  mode ("3 outs / 1 input" disables it).
- **No Windows (WDM) sound card**: the Virus audio function runs on WinUSB and is streamed by TI Revive itself
  (as with the old Access driver), so "Line (Virus TI)" / "Speakers (Virus TI)" no longer appear in Windows;
  use the ASIO device in your DAW. You hear the Virus through the plug-in track. When you update from TI Revive 0.2.x,
  the audio part of the Virus switches to WinUSB automatically; if the Virus audio does not work afterwards,
  unplug and replug the Virus once.
- **"A sync error occurred. Press Stop and Play to clear."** can appear once right after the first Play;
  Stop and Play clears it.
- **Bank sync** takes about one second per preset — the synth itself sends dumps at that speed.
- **Windows MIDI ports** ("Virus TI MIDI"/"Virus TI Synth") of the old driver are not provided.
- Do not run firmware/OS updates of the Virus through TI Revive; that path is not verified.

## Status

| Milestone | |
|---|---|
| Reverse engineering of driver, protocol and plug-in interface | done (`findings/`) |
| User-mode emulation, plug-in on WinUSB (MIDI, sync, presets) | done |
| Broker: multi-client, unplug recovery, Control Center | done |
| Plug-in-integrated USB audio | done |
| Windows 11 (Secure Boot + TPM, VM) | done |
| MSI installer (GitHub Actions build) | tested on a clean Windows 11 VM: install, plug-in + audio, Control Center, uninstall |
| Real Win11 hardware with memory integrity (HVCI) on: long session without sync errors or drift (0.3.0) | done |
| ASIO sound card "Virus TI (TI Revive)" (2 in / 2 out, wraps the Access ASIO driver), audio to the Virus | done: clean on a real PC (0.3.91 test build) |
| Latency measurements | open |

## Building

Everything except the MSI is cross-compiled on Linux with mingw-w64:

```sh
make -C src/broker      # vtibroker.exe + test tools (vtiprobe, vtisend, vtiflash; Linux libusb probe)
make -C src/wrapper     # VirusTI_WinUSB.vst3 (wrapper), vtiemu-pipe.dll, vtilaunch.exe
make -C src/vtihook     # research tools: IOCTL tracer, injector
```

The MSI is built with WiX 5 on Windows (`src/installer/build.ps1`). GitHub Actions
(`.github/workflows/build.yml`) does both steps, but only when a version tag `vX.Y.Z` is pushed
(`git tag -a v0.2.3 -m ... && git push origin v0.2.3`): it then publishes a **Release** with `TI-Revive-X.Y.Z.msi`.
Branch pushes and pull requests do not build.
`scripts/win11/build-msi.sh` does the same round trip through a local Windows 11 build VM.

## Repository layout

| Path | Content |
|---|---|
| `findings/` | the research: `spec.md` (protocol), `evidence.md` (every observation with its source), `claims.md` (community claims checked), `design.md`, `HANDOFF.md` (working log) |
| `src/broker/` | device access (WinUSB / libusb / named-pipe backends), broker, hardware test tools |
| `src/wrapper/` | driver-interface emulation, VST3 wrapper, audio bridge, Control Center launcher |
| `src/inf/` | the WinUSB driver package |
| `src/installer/` | MSI (WiX), driver and Access-installer helper scripts |
| `src/vtihook/` | research tools: IOCTL tracer with AudioSM monitor, process memory readers |
| `scripts/` | capture/decode tools (usbmon + tshark), VM tooling, Windows 11 test VM setup |

## How this was built

The work started from community notes about the Virus TI's USB protocol and a Windows 10 VM with USB
passthrough. Each claim was verified against evidence: USB captures of the original driver (usbmon),
the installer and driver binaries (static analysis with Ghidra), runtime traces of the plug-in's driver
calls (an import-table hook), and finally tests of each replacement piece against the real synthesizer —
first on Linux with libusb, then on Windows 10 and Windows 11 VMs. `findings/evidence.md` lists 60+
numbered observations, including the wrong turns and their corrections.

The reverse engineering and the code were done by derpsn together with Claude (Anthropic) as an
AI pair programmer.

## License

[MIT](LICENSE) © 2026 derpsn. Provided as is, without warranty — this installs a driver
package and a certificate on your computer; use at your own risk.
