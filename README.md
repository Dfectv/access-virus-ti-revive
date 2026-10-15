# TI Revive

*Like meeting an old best friend again.*

**Total Integration for the Access Virus TI on Windows 11, macOS and Linux — without the old drivers.**

> ☕ **Free, and it will stay free.** If TI Revive gives your Virus a second life and you'd like to say thanks, you
> can [buy me a coffee](https://buymeacoffee.com/callmederpsn) - entirely optional, every feature works the same
> either way. A test report or a kind word helps just as much.

TI Revive brings the Access Virus TI (Desktop, Polar, Keyboard) back to current computers. Since 0.8.0 it comes
with **"TI Revive Virus Control"**: a plug-in written from scratch that looks and works like the original Virus
Control - parameter sync, preset browser, patch libraries, automation, the Virus audio in your DAW - and runs natively
on Windows, macOS (Apple Silicon and Intel) and Linux, without the old Access drivers, Rosetta, Wine or yabridge. It
reads the skin, the parameter descriptions and the ROM banks from your own Access installer at runtime; TI Revive
contains no Access material. No kernel code of its own is installed.

> **Testers wanted.** TI Revive is tested on a TI2 Desktop with Ableton Live, REAPER and GarageBand. Help us
> cover more: **Virus TI Snow** (not supported by 0.8.0 yet - your test results are what makes it possible),
> **Polar and Keyboard** models, other DAWs (**Logic Pro**, Cubase, Bitwig, FL Studio, Studio One), **Intel Macs**,
> other **Linux** distributions and **Windows 10**. No coding needed: follow the
> [testing guide](.claude/skills/tirevive-test/SKILL.md) and post a
> [test report](https://github.com/derpsn/access-virus-ti-revive/issues/new?template=test_report.yml) - partial
> results help too.
>
> **With an AI coding agent** (Claude Code, Codex, Cursor, ...): this repository brings its own instructions
> ([AGENTS.md](AGENTS.md), skills for testing, diagnosing logs and preparing pull requests). Clone it, start your
> agent in the folder and paste:
>
> ```text
> Read AGENTS.md and .claude/skills/tirevive-test/SKILL.md. Help me test TI Revive with my Virus TI on this
> computer: check my setup against the README, help me install it, go through the test stages safely (no firmware
> updates, no writing stored patches, a disposable DAW project), collect the logs and draft a test report for
> me to review before posting it as a GitHub issue.
> ```

| | Windows 11 | macOS | Linux |
|---|---|---|---|
| **Download** ([Releases](https://github.com/derpsn/access-virus-ti-revive/releases)) | `TI-Revive-<version>-setup.exe` (or the same as `.msi`) | `TI-Revive-<version>-mac.zip` | `TI-Revive-<version>-linux.tar.gz` |
| **System** | Windows 11 x64 (also Windows 10) | macOS 14.4 or newer (the plug-in itself: Apple Silicon and Intel) | x86_64, Ubuntu 22.04 / Debian 12 or newer, systemd |
| **You need** | your own Access "Virus TI Software Suite" 64-bit `.msi` (only unpacked, not installed) | your own Access "Virus TI Software Suite" `.pkg` (only unpacked, not installed) | your own Access 64-bit `.msi` (only unpacked: msitools or Wine) |
| **In your DAW** | VST3 **"TI Revive Virus Control"**; ASIO device **"TI Revive Virus Audio"** (the Virus as sound card) | **"TI Revive Virus Control"** as VST3 and Audio Unit (Logic, GarageBand) | VST3 **"TI Revive Virus Control"** |
| **Tested with** | Ableton Live 11 (Windows 11 PC and VM) | Ableton Live 12 on a MacBook Air M3 (VST3), GarageBand (AU) | REAPER 7.82 on Ubuntu 26.04 |

- **Timing:** notes land sample-accurately on the grid (measured < 0.1 ms jitter on Linux and macOS); the plug-in
  reports its latency so the DAW compensates it. Any host sample rate works (the Virus runs at 44.1 kHz, the plug-in
  converts).
- **First start:** the plug-in reads RAM A-D from the Virus (a few minutes without a cache); you can play meanwhile -
  the sync pauses while notes arrive.
- **Coming from 0.7.x:** the classic plug-in "Virus TI (TI Revive)" (the original Access plug-in in a wrapper) is
  replaced. Projects saved with it open with "TI Revive Virus Control" (same plug-in ID). The Access software that
  earlier versions installed is offered for removal; the files the plug-in needs stay.
- **Virus TI Snow:** not supported by 0.8.0 yet. **Snow owners please stay with
  [TI Revive 0.7.4](https://github.com/derpsn/access-virus-ti-revive/releases/tag/v0.7.4)** for now (its classic
  plug-in "Virus TI Snow (TI Revive)" works on Windows). If TI Revive does not find your Snow, run
  [`tirevive-usbinfo.ps1`](scripts/tirevive-usbinfo.ps1) with the Snow connected and attach the
  `TI-Revive-usbinfo.txt` it saves on your Desktop to a [GitHub issue](https://github.com/derpsn/access-virus-ti-revive/issues).
- **Virus Control Center**: Access' app does not work without its old driver. TI Revive brings its own, step by
  step: **TI Revive Control Center** (device info, backup of the RAM banks, library export; also as the command
  line tool `tirevive-cc`; see "Backups" below). Firmware/OS updates and flash functions are not supported, on purpose.

> TI Revive is an independent project. It is **not affiliated with Access Music Electronics GmbH or
> Kemper GmbH**. "Access", "Virus" and "Virus TI" are trademarks of their owners and are used only to
> describe compatibility. TI Revive contains no Access software; you need your own copy of the Access
> "Virus TI Software Suite" installer.

---

## Installation

### Windows 11

Requirements: Windows 11 x64 (TI Revive also runs on Windows 10, but there the original Access drivers still
work and are the simpler choice), an Access Virus TI on USB, your own **Access "Virus TI Software Suite"
64-bit installer** (`.msi`, e.g. 5.1.7), a VST3 host.

1. You do **not** need to install the Access software: TI Revive only unpacks the files its plug-in needs. If the
   Access software is already installed (TI Revive 0.7 installed it), that is fine - TI Revive offers to remove
   it and keeps those files.
2. Put `TI-Revive-<version>-setup.exe` and the Access `.msi` in the same folder (or in *Downloads*).
   The `.msi` of the same release is the identical installer without the `setup.exe` wrapper; use either one
   (if double-clicking an `.msi` does nothing on your PC, take the `setup.exe`).
3. Run it. The installer is not code-signed (a certificate costs money), so Windows SmartScreen
   may show **"Windows protected your PC"**: click **More info → Run anyway**. On the last page keep
   **"Set up the Access Virus TI files now"** checked: TI Revive unpacks your Access `.msi` (nothing of it is
   installed) and copies `Virus Control.dll`, `firmware.bin` and the manuals to `C:\Program Files\Access Music\Virus
   TI` and the factory patch libraries to *Documents\Access Music\Virus TI\Patches*. Later: *Start → TI Revive →
   Set up Access Virus TI files*.
4. Connect the Virus TI. In your DAW, rescan plug-ins and use **"TI Revive Virus Control"** (vendor *TI Revive*).
   For the Virus as a sound card choose the ASIO device **"TI Revive Virus Audio"**.

Uninstall via *Settings → Apps → TI Revive* (removes driver package, certificate, broker, plug-in, ASIO driver and
shortcuts). Your patch libraries and the Access files stay.

### macOS

Requirements: macOS 14.4 or newer (Apple Silicon or Intel), an Access Virus TI on USB, your own **Access "Virus TI
Software Suite" installer** (`.pkg`, e.g. 5.1.7) and a VST3 or Audio Unit host. No Rosetta needed: the plug-in "TI
Revive Virus Control" runs natively as VST3 and as Audio Unit.

Unpack `TI-Revive-<version>-mac.zip`. Both parts are needed, in any order:

1. **`TI Revive.pkg`** installs TI Revive: the plug-in **"TI Revive Virus Control"** (VST3 and Audio Unit) and the USB
   service. It is not signed: right-click → *Open*, or *System Settings → Privacy & Security → Open Anyway*.
2. **`Set up Access Virus TI files.command`** unpacks **your own Access `.pkg`** (put it next to the script) and copies
   only what the plug-in reads (skin and parameter descriptions, ROM banks), the manuals and the factory patch
   libraries - the Access software itself is not installed. It parks old drivers that would block the Virus and
   offers to remove the original Access plug-ins of earlier TI Revive versions (Intel-only, no longer used).

Then connect the Virus, start your DAW and **allow it to use the microphone** when macOS asks (*System Settings →
Privacy & Security → Microphone*): macOS gives the Virus' audio only to apps with that permission. Rescan
plug-ins and use **"TI Revive Virus Control"** (VST3, or the Audio Unit in Logic / GarageBand). Projects saved with
"Virus TI (TI Revive)" of earlier versions (VST3 and Audio Unit) open with it. Your DAW may also list the original **"Virus TI"** plug-in as unusable (Intel-only):
that is expected.

Uninstall with `Uninstall TI Revive.command` (also in `/Library/Application Support/TI Revive`); it removes only
TI Revive (the Access plug-ins are removed with Access' own uninstaller).

### Linux

Requirements: an x86_64 Linux with systemd (Ubuntu 22.04 / Debian 12 or newer; tested: Ubuntu 26.04), a VST3 host
(REAPER, Bitwig, Ardour, …), your own Access 64-bit `.msi` and, to unpack it, **msitools** (`sudo apt install
msitools`) or Wine. Wine and yabridge are no longer needed for the plug-in.

1. Unpack `TI-Revive-<version>-linux.tar.gz` and run `./install.sh "/path/to/Virus TI Software Suite 5.1.7.00 64-bit.msi"`.
   It allows your user to use the Virus over USB (udev rule, asks for your password once), installs the TI Revive
   broker as a systemd user socket, unpacks the Access files the plug-in needs (nothing of Access is installed)
   and copies **"TI Revive Virus Control"** to `~/.vst3`. The classic plug-in of 0.7 (Wine + yabridge) is removed;
   its projects open with the new one.
2. For stable real-time audio, let your user use real-time priority (Ubuntu: `sudo usermod -aG pipewire $USER`,
   then log out and in).
3. In your DAW, rescan plug-ins and use **"TI Revive Virus Control"**.

Any sample rate works; JACK through PipeWire (`pw-jack reaper`) works well, avoid PulseAudio as the DAW's audio
system. While TI Revive uses the Virus, it is not available as a Linux sound card; it is handed back about 30 seconds
after the plug-in is closed. Uninstall with `./uninstall.sh`. Details: `README-linux.txt` in the package.

## Good to know

**One driver at a time.** TI Revive needs the Virus' USB interfaces for itself. Other Virus drivers on the same
computer — the old Access drivers from a full Access install, or third-party ones such as the USB driver of Aura
Plugins' Virus Editor — take the same interfaces, and TI Revive then shows "Virus TI not connected" (broker log:
`claim interface 3: LIBUSB_ERROR_ACCESS`). Disable the other driver (macOS: System Settings → General → Login Items
& Extensions → Driver Extensions, or its uninstaller) and restart.

**Connected but silent?** The Virus decides per part where its sound goes (analog outputs or USB 1-3). If another
editor or driver changed that, the plug-in shows the right setting but the Virus still sends to an analog output:
in the plug-in, switch the part's output to "Out 1" and back to "USB 1" (this sends the setting to the Virus again).
On macOS also check that your DAW may use the microphone (System Settings → Privacy & Security → Microphone).

**Start playback slightly before the first note.** The plug-in sends notes to the Virus ahead of time to make up
for the USB round trip; right at Play, notes in the first quarter second can come late or be skipped. This was the
same with Access' own driver.

**All systems**

- **One plug-in instance per synth.** Virus Control is designed that way (it controls all 16 parts);
  a second instance reports "no Virus TI connected". This is the original behaviour.
- **Hearing the Virus**: with a part output set to *USB*, the audio comes through the plug-in track.
- **Bank sync** takes about one second per preset — the synth itself sends dumps at that speed. It runs once (then
  only changed presets are fetched), and you can play while it runs.
- **Logs for bug reports:** create an empty file `diag.on` in the plug-in's folder (Windows
  `%LOCALAPPDATA%\virus-ti-native`, macOS `~/Library/Application Support/virus-ti-native`, Linux
  `~/.local/share/virus-ti-native`), reproduce the problem and attach `diag.log` from there (see CONTRIBUTING.md).
- Do not run firmware/OS updates of the Virus through TI Revive; that path is not verified.

**Backups: TI Revive Control Center (read-only)**

Start **TI Revive Control Center** (Windows: Start menu, folder "TI Revive"; macOS: Applications; Linux: the
application menu, or `tirevive-control-center`). Close the plug-in (or your DAW) first: the Virus serves one program
at a time. The window shows the connected Virus and its RAM banks, backs them up as patch libraries and converts
libraries between `.mid` and `.syx`. **Nerd mode** (button or *View > Nerd mode*) shows the plug-in's and the
broker's logs live, switches the plug-in log on and off and saves all logs for a bug report in one file. The same
from a terminal:

```
tirevive-cc info      # the connected Virus and its RAM banks
tirevive-cc backup    # RAM A..D as patch libraries (.mid) in Documents/Access Music/Virus TI/Backups/<date time>
tirevive-cc list FILE.mid             # the patch names in a library
tirevive-cc export FILE.mid OUT.syx   # convert a library (.mid <-> .syx)
tirevive-cc logs      # plug-in + broker logs in one file for a bug report (home folder and user name removed)
tirevive-cc debug on  # the plug-in's log (from the next start of the plug-in)
```

Where it is: Windows `C:\Program Files\TI Revive\tirevive-cc.exe`, macOS
`"/Library/Application Support/TI Revive/tirevive-cc"`, Linux `~/.local/bin/tirevive-cc`. A backup takes a few
seconds when the plug-in's bank sync is current, otherwise about 2 minutes per bank. To open a backup in the plug-in,
copy its `.mid` files into the `Patches` folder. `tirevive-cc` only reads from the Virus; restoring a backup into the
Virus comes later. Firmware/OS updates, "Burn To Flash" and factory reset are not supported, on purpose: they write
the Virus' flash and can brick it. Details: [native/docs/controlcenter.md](native/docs/controlcenter.md).

**Windows**

- REAPER loads and connects the plug-in; make sure the track is record-armed with a MIDI input and monitoring on.
- **Virus as sound card**: choose the ASIO device **"TI Revive Virus Audio"** to play your DAW through the Virus
  outputs and record its inputs (analog in, USB 1, USB 2); buffer 256 samples. No Access driver is needed.
- **No Windows (WDM) sound card**: the Virus audio function runs on WinUSB and is streamed by TI Revive itself
  (as with the old Access driver), so "Line (Virus TI)" / "Speakers (Virus TI)" no longer appear in Windows;
  use the ASIO device in your DAW. When you update from TI Revive 0.2.x, the audio part of the Virus switches to
  WinUSB automatically; if the Virus audio does not work afterwards, unplug and replug the Virus once.
- **"A sync error occurred. Press Stop and Play to clear."** can appear once right after the first Play;
  Stop and Play clears it.
- **Windows MIDI ports** ("Virus TI MIDI"/"Virus TI Synth") of the old driver are not provided.

**macOS**

- **Microphone permission** for your DAW is required (see above); without it the plug-in stays silent.
- **One program at a time** uses the Virus (as with the Access driver): a second DAW or GarageBand next to it shows
  "no Virus TI connected" until the first one is closed. If the Virus ever stays silent although the plug-in is
  connected, switch the Virus off and on once.
- **VST3 and Audio Unit**: VST3 hosts (Ableton Live, Cubase, Bitwig, REAPER, ...) and Logic Pro / GarageBand (*AU
  Instruments → TI Revive → TI Revive Virus Control*; in GarageBand under *Smart Controls → Plug-ins*) use the same
  native plug-in. Tested: Ableton Live 12 (VST3), GarageBand (AU); other hosts should work — reports welcome. Each
  DAW needs its own microphone permission.
- Access' **Virus Control Center** (32-bit) does not run on current macOS; for backups use `tirevive-cc` (above).
- The Virus' audio comes from macOS' own USB audio driver (the "Virus TI" audio device, 44.1 kHz); unplugging and
  replugging the Virus is detected by the plug-in.
- Logs for bug reports: the plug-in's `diag.log` (see above) and `/tmp/tirevive-vtibroker.log`.

## Performance and improvements (0.8.0)

What the native plug-in "TI Revive Virus Control" does better than the classic plug-in of 0.7.x, measured on real
hardware (TI2 Desktop) - details and raw numbers in [`findings/evidence.md`](findings/evidence.md) and
[`native/docs/status.md`](native/docs/status.md).

**Timing** (16th notes at 120 BPM recorded from the Virus' USB audio, onset analysis)

| | Notes on the grid | Jitter (std. dev.) | Max. deviation | Drift |
|---|---|---|---|---|
| Classic plug-in 0.7.x (Linux, REAPER) | first note late, second missing | 0.06 ms | 1.31 ms | - |
| **0.8.0, Linux** (REAPER render) | 256 / 256, first at exactly 2000.00 ms | **0.01 ms** | 0.05 ms | 0 ppm |
| **0.8.0, macOS** (Ableton 12, M3) | 256 / 256 | **0.02 ms** | 0.28 ms | 0 ppm |
| **0.8.0, Windows 11 PC** (Ableton 11) | 304 / 304 | ≤ 0.66 ms (upper bound of the method) | - | 0 ppm |

- The plug-in finds the Virus' own clock (a sync marker the Virus puts on USB 2), stamps every event against it and
  **reports its latency to the DAW**, so delay compensation lines recordings up with the grid (E139-E141, E143).
- **Any sample rate:** the Virus runs at 44.1 kHz; at other host rates a 32-tap windowed-sinc resampler converts
  with -90 dB error (1 kHz and 10 kHz test tones at 32/48/88.2/96 kHz), no clicks, same timing.

**CPU and real-time safety**

| | Before | After |
|---|---|---|
| Idle CPU (plug-in loaded, Virus connected) | 5 % | **1.4 %** |
| Stress test (notes on 16 channels + 32 automated parameters every block) | 19 % | **2.9 %** |
| Automation worker thread | 12.4 % | **~3 %** |
| `process()` per audio block, 3 Max for Live LFOs on Virus parameters, ~40 min | - | max 122 µs (~1 % of the block), 0 dropouts |

- The audio thread never allocates or locks; events go through fixed buffers, and note-offs always get a slot, so
  no note hangs.
- **Automation rate limit** (250 messages/s, coalesced per parameter): unthrottled parameter floods hung the Virus'
  audio until a power cycle within 4 minutes; with the limit a 4-minute full-load test (16 channels of notes + 32
  automation lanes) passes.
- Checked with ThreadSanitizer and AddressSanitizer (one race in the broker connection found and fixed); 28 unit
  tests run in CI on all three platforms.

**Editor**

| | Before | After |
|---|---|---|
| Full repaint on Linux | ~70 ms | **1-7 ms** (bitmaps kept as X pixmaps, static layer cached) |
| Knob drag step | ~5 ms | **< 1 ms** (only the changed controls are redrawn) |
| Full repaints during 4 s of automation | 56 | **4** |
| First visit of a page | 27-52 ms | **10-13 ms** (8 pages cached, bitmaps preloaded) |

**Reliability fixes found with the Virus**

- New projects reliably reset the Virus: it drops the message that follows a multi dump, so the plug-in now pauses
  250 ms after one (part 1 kept its old patch in 3 of 11 start-ups before, 0 of 8 after; E145).
- Notes play while the bank sync runs: the sync pauses while you play (dump requests delay the Virus' notes by
  ~130 ms) and continues in the gaps; notes are only held back for the first ~4 s, when the Virus would play them
  up to 1.6 s late (E144).
- Big knobs on Windows (images over Direct2D's 16384-pixel limit are drawn in tiles), unplug/replug recovery,
  project state byte-identical to the original's.

## Why

**Windows.** The Virus TI talks to its plug-in through a custom USB kernel driver (`VirusUSB.sys`, version 2.9.66
from 2014, built on a Ploytec OEM driver). Windows 11 refuses to load it: Code Integrity blocks the
old cross-signed driver (Device Manager shows **Code 39**, event log CodeIntegrity 3076/3077). Without
that driver the plug-in reports "no Virus TI connected" and the synthesizer is reduced to a plain
MIDI module — if at all.

**macOS.** The Access kernel extension is Intel-only and can never load on an Apple Silicon Mac, and the Access
plug-ins themselves are Intel-only, so they would only run in a DAW started under Rosetta — and even there they
find no driver.

Access discontinued the Virus TI and no updated driver exists. The goal of this project was to
understand what the old drivers actually do, and to replace them with something that current systems
accept — so the synth, the plug-in and the user's existing Access software keep working. Up to 0.7.x TI Revive did
that around the original plug-in (driver emulation, Rosetta, Wine). Since 0.8.0 it has its own plug-in, rewritten
from the documented behaviour, that runs natively everywhere and only reads the skin, the parameter descriptions and
the ROM banks from the user's Access installer.

## How it works

Since 0.8.0 the plug-in is **"TI Revive Virus Control"**, a native rewrite: how it works per platform is in
[`native/docs/architecture.md`](native/docs/architecture.md). The sections below describe the classic plug-in of
TI Revive 0.7.x (the original Access plug-in in a wrapper), which 0.8.0 replaces; the broker, the USB layer and the
driver package described there are still the ones the native plug-in uses.

### Windows (WinUSB binding and broker; up to 0.7.x also the driver emulation for the original plug-in)

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

### macOS (0.7.x: the bridge hosting the original under Rosetta; since 0.8.0 only the broker is still used)

```
 DAW (native, arm64) ── Virus TI (TI Revive).vst3 (native bridge)
          │  audio + MIDI per block through shared memory, control/state/parameters over a socket,
          │  the editor shown as a live picture with mouse/keyboard forwarded (menus appear natively in the DAW)
          ▼
 vtihelper (x86_64, under Rosetta 2) ── hosts the user's original "Virus TI.vst3" / Virus Control.bundle
          │  Virus Control links Access' driver library libVirusUSB.dylib - TI Revive installs its own in its place:
          ├── audio: an IOProc on macOS' own "Virus TI" USB audio device (Core Audio, 6 in / 2 out, 44.1 kHz)
          └── USB control requests, device presence, MIDI ──▶ vtibroker (native, started on demand)
                                                                 ▼  libusb  ──  Virus TI vendor interfaces 3 + 4
```

**No Access drivers.** On the Mac, Virus Control does not talk to the kernel extension itself but to Access'
user-space driver library `libVirusUSB.dylib` (20 methods of a C++ class `VirusClient`) and, for MIDI, to Access'
Core MIDI driver through shared memory and a message port. TI Revive replaces both: its own `libVirusUSB.dylib`
answers the plug-in's calls, and the broker stands in for the MIDI driver (same shared-memory layout and ring
buffers). The Virus' audio function is class compliant, so macOS' built-in driver provides it; TI Revive feeds it
to the plug-in in the format Access' driver used.

**Native DAW, Intel plug-in.** The Access plug-ins are Intel-only, so a native DAW cannot load them. TI Revive's
native bridge plug-in starts a helper under Rosetta 2 that hosts the original, passes audio, MIDI, parameters,
automation and the saved state through, and mirrors the original editor into the DAW window — including its popup
menus and text fields. The helper is started by the DAW, so the DAW's microphone permission covers it.

The full protocol description is in [`findings/spec.md`](findings/spec.md); every statement there is
backed by an entry in [`findings/evidence.md`](findings/evidence.md) (captures, traces, decompiled
driver code). The macOS design is in [`findings/design-macos.md`](findings/design-macos.md) and
[`findings/design-macos-bridge.md`](findings/design-macos-bridge.md).

### Linux (0.7.x: the classic plug-in under Wine + yabridge)

```
 Linux DAW (native) ── yabridge ── Wine: "Virus TI (TI Revive).vst3" (the Windows wrapper) ── your original Virus TI.vst3
                                          │  the same driver emulation as on Windows; device access over a
                                          │  token-protected local socket, audio ring in /dev/shm
                                          ▼
                              vtibroker (native, libusb, started by systemd on demand): the whole Virus TI -
                              MIDI on interfaces 3/4 and the USB audio, streamed isochronously like on Windows
```

Linux' own USB audio driver cannot share the Virus (taking the vendor interfaces away shuts its sound card down),
so the broker owns the whole device while TI Revive is in use and hands it back to Linux afterwards. Design and
measurements: [`findings/design-linux.md`](findings/design-linux.md), evidence E113–E121.

## Status

| Milestone | |
|---|---|
| Reverse engineering of driver, protocol and plug-in interface | done (`findings/`) |
| **0.8.0: native plug-in "TI Revive Virus Control" (all platforms)** | |
| Plug-in (editor, browser, libraries, bank sync, automation, project state), opens projects of the classic plug-in | done: tested in REAPER (Linux), Ableton Live 12 (macOS M3), Ableton Live 11 (Windows 11 PC) |
| Timing: sample-accurate, latency reported to the DAW, any sample rate | done: 0.01-0.02 ms jitter, 0 ppm drift (E139-E143) |
| ASIO sound card "TI Revive Virus Audio" (Windows, 6 in / 2 out, no Access driver) | done: clean on a real PC |
| Installers: Access software only unpacked (Windows/Linux), clean-up of earlier versions | done |
| Virus TI Snow | open (Snow owners: 0.7.4) |
| Audio Unit (AUv2) of the native plug-in, with the classic Audio Unit's codes (its Logic / GarageBand projects open) | done: passes `auval`, tested in GarageBand |
| Control Center app + command line `tirevive-cc` (device info, backup, library export; read-only) | done: CLI tested on macOS (M3), app on Linux, both with a TI2 |
| Control Center: restore into RAM | open (flash functions and OS update: never) |
| Windows MIDI ports | open |
| **Windows** | |
| WinUSB driver package, broker with USB audio, unplug recovery, Windows 11 with memory integrity (HVCI) | done |
| MSI + setup.exe (GitHub Actions build) | done |
| 0.2-0.7.x: user-mode driver emulation for the original plug-in, ASIO wrapper, Control Center | done, replaced by 0.8.0 |
| **macOS** | |
| USB broker (no Access kernel extension); the MIDI driver stand-in of the classic path is opt-in since 0.8.2 (`VTI_BROKER_MIDISHIM=1`) | done |
| Installer (.pkg + Access file setup, GitHub Actions build) | done |
| 0.5-0.7.x: bridge (VST3 + AU) with a Rosetta helper hosting the original, own libVirusUSB | done, replaced by 0.8.x |
| **Linux** | |
| Broker with USB audio on libusb, device handed back to Linux | done |
| Installer (`install.sh`, systemd user socket, udev rule, GitHub Actions build) | done |
| 0.7.x: the Windows wrapper under Wine/yabridge | done, replaced by 0.8.0 |

## Building

**Plug-in "TI Revive Virus Control" (all platforms).** CMake 3.25+, with the VST3 SDK as a submodule:

```sh
git submodule update --init --recursive native/external/vst3sdk
cmake -S native -B build-native -DCMAKE_BUILD_TYPE=Release && cmake --build build-native && ctest --test-dir build-native
```

Windows needs MSVC (Visual Studio 2022; also builds the ASIO driver), macOS Xcode (universal:
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`), Linux the X11/cairo development packages listed in the release
workflow (job `native-linux`). Details: [`native/README.md`](native/README.md).

**Broker and Windows tools.** Cross-compiled on Linux with mingw-w64 (the Linux broker with gcc and libusb):

```sh
make -C src/broker      # vtibroker.exe + test tools (vtiprobe, vtisend, vtiflash), Linux broker / libusb probe
make -C src/wrapper     # the classic 0.7.x Windows plug-in (not shipped since 0.8.0)
make -C src/vtihook     # research tools: IOCTL tracer, injector
```

The MSI is built with WiX 5 on Windows (`src/installer/build.ps1 -NativeBin <folder>`); `scripts/win11/build-msi.sh`
does the same round trip through a local Windows 11 build VM.

**macOS package.** On a Mac with the Xcode Command Line Tools and the (MIT-licensed) VST3 `pluginterfaces` next to the
repository: `scripts/mac-build-release.sh <repo> <version>` builds `TI-Revive-<version>-mac.zip` (native plug-in,
broker with libusb built from source, .pkg, scripts).

**Releases.** GitHub Actions (`.github/workflows/build.yml`) builds everything, but only when a version tag `vX.Y.Z`
is pushed (`git tag -a v0.8.0 -m ... && git push origin v0.8.0`): it then publishes a **Release** with
`TI-Revive-X.Y.Z-setup.exe`, `TI-Revive-X.Y.Z.msi`, `TI-Revive-X.Y.Z-mac.zip` and `TI-Revive-X.Y.Z-linux.tar.gz`.
Branch pushes and pull requests do not build.

## Repository layout

| Path | Content |
|---|---|
| `native/` | the native plug-in "TI Revive Virus Control" (VST3, all platforms) and the ASIO driver "TI Revive Virus Audio": `native/README.md`, `native/docs/architecture.md` |
| `findings/` | the research: `spec.md` (protocol), `evidence.md` (every observation with its source), `claims.md` (community claims checked), `design*.md`, `HANDOFF.md` (working log) |
| `src/broker/` | device access (WinUSB / libusb / named-pipe backends), broker (Windows and macOS; the macOS MIDI driver stand-in for the classic plug-in is opt-in), hardware test tools |
| `src/wrapper/` | the classic Windows plug-in of 0.7.x (driver-interface emulation, VST3 wrapper, ASIO wrapper, Control Center launcher); not shipped since 0.8.0 |
| `src/inf/` | Windows: the WinUSB driver package |
| `src/installer/` | Windows: MSI (WiX), driver script, `tirevive-access.ps1` (unpacks the Access files) |
| `src/mac/` | macOS, the classic 0.5-0.7.x path (not shipped since 0.8.x, kept for reference): `libvirususb/` (replacement libVirusUSB.dylib), `bridge/` (VST3/AU bridge), `helper/` (Rosetta host), `common/` (bridge protocol), diagnostics tools |
| `src/installer-mac/` | macOS: package scripts, Access installer script, uninstaller, README-mac |
| `src/vtihook/` | research tools: IOCTL tracer with AudioSM monitor, process memory readers |
| `scripts/` | capture/decode tools (usbmon + tshark), Windows 11 VM tooling, Mac build and lab scripts |

## How this was built

The work started from community notes about the Virus TI's USB protocol and a Windows 10 VM with USB
passthrough. Each claim was verified against evidence: USB captures of the original driver (usbmon),
the installer and driver binaries (static analysis with Ghidra), runtime traces of the plug-in's driver
calls (an import-table hook), and finally tests of each replacement piece against the real synthesizer —
first on Linux with libusb, then on Windows 10 and Windows 11 VMs, and on a MacBook Air M3 driven over SSH for
the macOS version. For 0.8.0 the plug-in itself was rewritten (`native/`): its UI is drawn from the original's layout
and skin files at runtime, every protocol detail was measured on the synth (timing with recorded test patterns,
read-backs of the Virus' memory), and parts that were first ported from the analysis were rewritten from documented
behaviour with golden tests. `findings/evidence.md` lists 145 numbered observations, including the wrong turns and
their corrections.

The reverse engineering and the code were done by derpsn together with Claude (Anthropic) as an
AI pair programmer.

## Contributing

Bug reports, tests on your setup and pull requests are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md). If you work
with an AI coding assistant, it finds its instructions in [AGENTS.md](AGENTS.md) (Claude Code also reads
`CLAUDE.md` and the skills in `.claude/skills/`).

## License

[MIT](LICENSE) © 2026 derpsn. Provided as is, without warranty — on Windows this installs a driver
package and a certificate on your computer; use at your own risk.
