# virus-ti-native (private)

A native rewrite of the Access Virus TI plug-in ("Virus Control") as VST3, stage 3 of TI Revive. It looks and
behaves like the original: same UI, same automation IDs, same project state. It doesn't need the Intel-only Access
plug-in, Wine or Rosetta. The Virus is reached through the TI Revive broker (USB MIDI and audio).

| platform | status |
|---|---|
| Linux (x86_64, X11) | **working**; tested in REAPER: sound, automation, browser, LOAD/SAVE, libraries, sync |
| Windows | **working**; tested in Ableton 11 (Win11 VM): sound, timing, UI, browser, menus |
| macOS | **working**; universal (Apple Silicon + Intel), tested in Ableton 12 on an M3: sound, UI, sync |

**How it works:** [docs/architecture.md](docs/architecture.md). More detail: protocol in
[docs/protocol.md](docs/protocol.md), file and render formats in [docs/formats.md](docs/formats.md), progress and
measurements in [docs/status.md](docs/status.md), the plan in [docs/plan.md](docs/plan.md).

**Rule:** no Access material in git (graphics, XML, firmware, decompiled code). The plug-in loads the skin, the
device description and the ROM from the user's own Access installation at runtime. Recordings stay in `work/`.

## Requirements (Linux)

- **TI Revive installed:** the broker owns the Virus via the systemd user socket `/tmp/vtibroker-<uid>.sock`.
- **The original Virus TI software installed once:** in Wine, it provides `Virus Control.dll` and `firmware.bin`.
  The plug-in only reads these files.
- **A VST3 host.** REAPER is tested, with PipeWire/JACK running at 44.1 kHz.

## Build and install (Linux)

```sh
git submodule update --init external/ti-revive
(cd external/vst3sdk && git submodule update --init base cmake pluginterfaces public.sdk vstgui4)
cmake -S . -B build -G Ninja && cmake --build build
cp -r build/VST3/Release/VirusTINative.vst3 ~/.vst3/
```

Windows: the CI artifact `VirusTINative-windows` from `.github/workflows/build.yml`.

Unit tests (no Virus needed): `ctest --test-dir build`.

## Using it in REAPER (Linux)

1. Close every other program that uses the Virus: only one session can own it.
2. Start REAPER with the right sample rate and buffer size:

   ```sh
   pw-metadata -n settings 0 clock.force-rate 44100
   pw-metadata -n settings 0 clock.force-quantum 512
   pw-jack reaper
   ```

3. Insert "Virus TI (native)". On the first start it syncs RAM A–D from the Virus. That takes about 8 minutes;
   the LCD shows the progress. The banks are cached in `~/.local/share/virus-ti-native/cache`; rename that folder
   to force a new sync.
4. Where the other banks come from:
   - ROM A–Z: the user's `firmware.bin`.
   - Personal Libraries: `Documents/Access Music/Virus TI/Patches`, including subfolders.

Environment settings for testing:
- **Timing:** `VTI_NATIVE_S` (stamp offset, default 5000), `VTI_NATIVE_M` (FE/F2 lead, default 1024),
  `VTI_NATIVE_PING`, `VTI_NATIVE_K`. See docs/status.md, R5.
- **Repaint log:** `VTI_NATIVE_DRAWTIME`. **Full repaints only** (fallback): `VTI_NATIVE_NO_PARTIAL`.

## Source layout

| path | content |
|---|---|
| `src/model` | `RemoteDevice`: parameters, MIDI/SysEx encode/decode, dumps, upgrader, automation map |
| `src/editor` | `EditorLayout`, `Conditions`, `EditorRenderer` (draws the original UI on any canvas) |
| `src/access` | Access files (DLL/bundle resources), firmware ROM banks, Personal Libraries (SMF) |
| `src/device` | broker client, shared-memory audio ring, memory sync, USB-MIDI packing |
| `plugin/source` | the VST3 plug-in (`VirusPlugin`) and its VSTGUI editor (`VirusEditor`) |
| `tools` | developer tools, see below |
| `tests` | unit tests (`vtitests`, run by ctest) |

## Tools

| tool | purpose |
|---|---|
| `vtimodel` | inspect the Access files: params, messages, dumps, categories, automation, ROM, libraries, init multi |
| `virusctl` | talk to the Virus through the broker: state, set, note, sysex, timing tests |
| `virussync` | bank sync without a host |
| `nativehost` | minimal VST3 host: states, params, notes, WAV, editor on X11 with click/drag/type/screenshot |
| `vtirender` | render the editor with cairo and compare it against screenshots of the original |
| `xclick`, `xtype` | drive the original under Wine/Xvfb |
| `DecompileAll.java`, `msgsummary.py` | Ghidra headless decompile into `work/`, trace summaries |
