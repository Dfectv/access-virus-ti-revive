# How the native Virus TI plug-in works

This is the technical overview for anyone who wants to understand the plug-in. Byte layouts and protocol details are in
[protocol.md](protocol.md) and [formats.md](formats.md); progress and measurements are in [status.md](status.md).

## 1. The big picture

```
 DAW (REAPER, Ableton, ...)
  │  VST3: notes, automation, audio buffers, project state
  ▼
 VirusTINative.vst3 ──────────────────────────────────────────────────────────────┐
  │ plugin/  VirusPlugin (VST3 component + controller)   VirusEditor (VSTGUI)       │
  │ src/model    RemoteDevice: parameters, MIDI/SysEx encoding, dumps  ◄── remotedevice.xml, editordevice.xml,
  │ src/editor   EditorLayout + EditorRenderer: the original UI         ◄── editor.xml + PNG skin   automation.xml
  │ src/access   Access files, firmware ROM banks, Personal Libraries   ◄── user's Access installation
  │ src/device   Broker client, audio ring, memory sync                                 │
  └────────────────────────────┬────────────────────────────────────────────────────┘
                               │ broker protocol 3 (Unix socket / named pipe) + shared-memory audio ring
                               ▼
 TI Revive broker (external/ti-revive) ── USB (libusb / WinUSB / IOKit) ──► Virus TI / TI2 / Snow
```

The plug-in never talks to USB directly. The **TI Revive broker** (the public project, a submodule here) owns the
Virus. It frames USB-MIDI, carries the 6-channel USB audio, and lets one plug-in session at a time use the device.

### No Access material in this repository

Access/Kemper have not open-sourced the Virus TI software. So the plug-in ships **none** of it: no graphics, no XML,
no firmware, no decompiled code. At runtime it loads everything from the user's own Access installation, so the
original Virus TI software must be installed (it can stay unused). These are the files it loads:

| loaded at runtime | from | used for |
|---|---|---|
| `remotedevice.xml`, `editordevice.xml` | Virus Control DLL / bundle | parameters, value lists, conditions, SysEx layouts |
| `editor.xml` + ~465 PNGs | same | the complete UI: positions, bitmaps, fonts, conditions |
| `automation.xml` | same | VST parameter IDs, so projects made with the original open unchanged |
| `firmware.bin` | Access `Common` folder | ROM banks A–Z (TI, TI2 or Snow image) |
| init patch / init multi / default page B | inside the DLL | new instances, Virus C patch upgrade |
| `*.mid` patch files | `Documents/Access Music/Virus TI/Patches` (with subfolders) | Personal Libraries |

## 2. Components

### Device model: `src/model/remotedevice.*`
- Parses `remotedevice.xml`. There are 587 parameters, each with range, default, value list, part scope and MIDI
  message (CC, poly pressure, parameter SysEx, bank dumps).
- **encode / decode:** parameter value ↔ MIDI bytes. Decoding goes the other way: incoming CC/SysEx from the Virus
  becomes parameter changes.
- **Single dumps:** 524 bytes, decoded to and updated from parameter values. Checksums sit at bytes 265 and 522.
- **Upgrader:** brings older dumps (format versions 1–12, Virus C 267-byte dumps) to the current layout, ported
  from the original.
- **`init_messages()`:** the original's start-up sequence (global init, parameter init), byte-identical to it.
- **Automation:** `automation.xml` maps (parameter, part) to a VST parameter ID.

### UI: `src/editor/*` and `plugin/source/viruseditor.*`
- `EditorLayout` reads `editor.xml` into a tree of containers and controls. `Conditions` evaluates show/hide
  conditions; a few of them the original computes in code (for example "Category Filter Selected").
- `EditorRenderer` draws the tree on an abstract `Canvas`:
  - filmstrip knobs, radio buttons and nine-part tiles;
  - bitmap LCD fonts;
  - envelope, filter and oscillator displays;
  - the patch browser and the MIDI activity meters.
- The same renderer runs in the plug-in (VSTGUI canvas) and in `tools/vtirender` (cairo, for pixel comparisons
  against screenshots of the original).
- Repaints are partial: a knob drag repaints only its controls and the LCD, and the meters repaint only the
  channels that changed.

### Plug-in: `plugin/source/virusplugin.*`
- **VST3 `SingleComponentEffect`:** stereo out, MIDI in, 224 automatable parameters with the original's IDs and an
  identical project-state format. The state is a "MIDI" container holding the multi, 16 part dumps, globals and CCs.
- **Real-time path:** notes and automation are stamped on a sample timeline. A MIDI clock and song-position markers
  are sent alongside, and a worker thread hands everything to the broker.
- **Audio:** read from the broker's shared-memory ring, drift-corrected, with latency compensation.
- **Banks:**
  - RAM A–D: synced from the Virus on first start, then cached.
  - ROM A–Z: from `firmware.bin`; the device model comes from the USB descriptor.
  - Personal Libraries: the patch files.
- **Browser:**
  - Search by name and/or category builds a "Search Results" bank like the original: all banks, one entry per
    sound, sorted, 128 per page.
  - Load and save into RAM or libraries, rename, drag and drop between parts.

### Device layer: `src/device/*`
- `Broker`: connects to the broker (Unix socket or Windows named pipe), handles HELLO/WRITE/CONTROL/AUDIO, and
  receives RX/STATE.
- `AudioIn`: the shared-memory ring of 16384 frames × 6 channels: analog in, USB 1, USB 2.
- `MemorySync`:
  - checks bank fingerprints against the cache, and requests single dumps only for changed programs;
  - one request at a time, at the Virus' pace of about 1 s per request.

## 3. Platforms

### Linux (working, tested in REAPER)
| topic | how |
|---|---|
| plug-in format | VST3 bundle `~/.vst3/VirusTINative.vst3` (`x86_64-linux`) |
| UI | VSTGUI on X11; uses the host's `IRunLoop` (from the plug frame or host context) and `X11::FrameConfig`; the host maps the XEmbed window |
| menus | VSTGUI's generic (in-window) menu with a compact light theme and no fade-in, because X11 has no native menus |
| broker | systemd user socket `/tmp/vtibroker-<uid>.sock` (starts on demand, exits after 30 s idle) |
| audio ring | `/dev/shm/tirevive-audio-<uid>` |
| cache | `~/.local/share/virus-ti-native/cache` (`RAM A`..`RAM D`) |
| Access files | the Access installation in the Wine prefix (installer run once; `firmware.bin` next to the DLL) |
| Personal Libraries | the Wine prefix's Documents folder; Wine links it to the XDG documents folder (e.g. `~/Dokumente`) |
| audio setup | PipeWire with JACK; the host runs at 44.1 kHz (`pw-metadata -n settings 0 clock.force-rate 44100`) |

### Windows (CI build, device-tested in a Win11 VM; DAW test pending)
| topic | how |
|---|---|
| plug-in format | VST3 bundle in `C:\Program Files\Common Files\VST3\VirusTINative.vst3` (`x86_64-win`), built with MSVC in CI (`.github/workflows/build.yml`, artifacts `VirusTINative-windows` + `tools-windows`) |
| UI | VSTGUI with Direct2D; native Win32 menus |
| broker | named pipe `\\.\pipe\vtibroker`; like TI Revive's own clients the plug-in starts `vtibroker.exe` on demand (path from `HKLM\SOFTWARE\TI Revive\InstallDir`, else `%ProgramW6432%\TI Revive`), detached from the DAW's job object; the broker exits by itself when idle |
| USB | TI Revive's WinUSB binding (MSI); no Access driver |
| audio ring | file mapping `Local\vtibroker_audio`, plus a named event per plug-in process |
| cache | `%LOCALAPPDATA%\virus-ti-native\cache` |
| Access files | the normal Access installation (`C:\Program Files\Access Music\Virus TI\Common\Virus Control.dll`, `firmware.bin`) |
| Personal Libraries | `%USERPROFILE%\Documents\Access Music\Virus TI\Patches` |

Verified in the VM (TI Revive 0.7.4, Access 5.1.7): Access files and libraries found, broker started on demand,
TI2 detected, audio streaming with notes, bank sync (cancellable when the plug-in closes).

### macOS (planned)
- Universal binary, so it runs on Apple Silicon without Rosetta.
- Broker through the TI Revive Mac helper.
- Native Cocoa menus.

## 4. Building and testing

- **Build:** see the [README](../README.md). It needs the VST3 SDK 3.8 (submodule) and CMake. The tools also need
  X11/Xtst (nativehost) and cairo (vtirender).
- **Headless tests:** `nativehost` loads the plug-in without a DAW. It can apply states and parameters, play notes,
  render WAV, and open the editor on a virtual X display, where it can click, drag, type and take screenshots.
  - `VTI_NATIVE_DRAWTIME=1` logs every repaint.
  - Running it in a private `/tmp` (`bwrap --tmpfs /tmp`) keeps it away from the Virus while a DAW is using it.
- **The original as a reference:** it runs under Wine with TI Revive's `vtihost`. Its USB traffic, audio and
  screens are recorded and compared; recordings stay in `work/` (git-ignored).
