# Plan: "Virus TI" rewrite - an own, native plug-in nearly identical to the original (stage 3)

_User request 2026-10-12. main keeps the current approach (original Access plug-in + TI Revive driver emulation);
the rewrite lives on the feature branch `rewrite`._

## Goal

Give Virus TI owners the complete Total Integration experience with a **native** plug-in that does not need the
Intel-only Access plug-in (no Rosetta on the Mac, no Wine on Linux): same behaviour, same look, same automation IDs
and project state as the original, so existing DAW projects open with it. Access will not publish the sources (they
were written by another company), so everything is reconstructed from the shipped files and observed behaviour.

## What we start from

- Device side: the USB protocol (spec.md, E001-E135), brokers with USB audio on Windows/macOS/Linux, the plug-in
  channel framing (USB-MIDI packets on EP 0x06/0x86, F5 time stamps, MIDI clock), memory sync sequence (E031),
  sync information events (E072-E078), the Mac/Windows/Linux driver emulations as a working reference.
- The original is largely **data-driven**. In the Mac bundle (Virus Control.bundle/Contents/Resources; embedded in
  Virus Control.dll on Windows, E013):
  - `remotedevice.xml` (600 KB): device model - 79 parameter sets, 587 parameters (ranges, defaults, value lists,
    MIDI controller indices, conditions), 128 value lists, 128 mappings, 1089 SysEx byte layouts, init sequences.
    `remotedevicePR.xml` = the same for another model family ("PR"); `editordevice.xml` = editor-side parameters.
  - `editor.xml` (600 KB): the complete UI - containers, 298 knobs, 158 radio buttons, 109 buttons, 91 menus,
    static texts, each with position, bitmap, condition; `editorPR.xml` for the other family.
  - `automation.xml`: DAW automation index -> parameter/part (project compatibility).
  - 465 PNG bitmaps (pages, knob filmstrips, buttons, displays).
- Source map from strings: `Sources/CVSTEditor.cpp`, `KMenu.cpp`, `KViewObject.cpp`, `RemoteDevice.cpp`,
  `VirusTI/VirusDevices.cpp`, `VirusFirmwarePack.cpp`, `Common/issue.cpp`, VSTGUI 4.0 (the UI toolkit).
- Oracle: the original runs on this PC under Wine with our broker (vtihost, E118) and on the Mac (helper) - every
  behaviour can be recorded (USB traffic with time stamps, audio, screenshots) and compared.

## Ground rules

- **No Access files in the repository** (it is public, also every branch): no PNG/XML from Access, no firmware, no
  decompiled code pasted into sources. The rewrite **loads the skin, device description and firmware package at
  runtime from the user's own Access installation** (as TI Revive already requires it) - this gives the identical
  look without distributing Access' work. Reverse-engineering results go into findings/ as our own descriptions
  (formats, sequences, semantics). Later decision (user): own artwork as an alternative skin.
- Same VST3 class/parameter IDs, bus layout and state format as the original (projects load), names marked as
  TI Revive where shown to the user.
- Every step has an automatic comparison against the original (traces, audio, screenshots) - no "looks right".

## Platform to start: Linux (this PC)

Chosen because I can work almost without input: the Virus is attached here, the native broker streams its USB
audio, the original runs here as the oracle (Wine + vtihost + broker logs), Xvfb gives headless screenshots for
pixel comparisons, REAPER is installed for host tests. The code is portable from day one (CMake, C++17, VST3 SDK
(MIT) + VSTGUI 4 (BSD, the toolkit the original uses), CI builds for Windows and macOS on every phase; Windows/Mac
tests in the VM / on the MacBook at the phase ends.

## Phases

| | Phase | Content | Done when | Needs the user |
|---|---|---|---|---|
| R0 | Setup | branch `rewrite`, tree `rewrite/` (CMake: core lib, VST3 plug-in, CLI tools, tests), REA/Ghidra projects of Virus Control (Mac x86_64 + Windows x64), **trace recorder**: broker logs every USB-MIDI packet both ways with device frame time, scripted oracle runs (vtihost under Wine) -> golden traces | a golden trace of start-up + sync + note + parameter change from the original exists | - |
| R1 | Data model | parsers for remotedevice.xml / editor.xml / automation.xml (loaded from the Access install); document their semantics (conditions, value lists, mappings, byte layouts, init); unit tests: every parameter -> SysEx bytes identical to the original's traces | the CLI prints any patch parameter and builds the same SysEx as the original | - |
| R2 | Device engine (headless) | connect via the brokers (vtiproto), handshake, memory sync (RAM/ROM banks, firmware package for ROM names), patch model (singles/multis/parts), parameter send/receive, MIDI with F5 time stamps + MIDI clock, audio in/out through the broker ring, latency/re-anchoring like the original (decompile the timing code) | CLI "virusctl": sync, select patch, play notes, record audio; traces equal to the original's | - |
| R3 | Plug-in shell | VST3 with the original's class ID(s), buses, 10404 parameters (IDs from automation.xml + RE), DAW state chunk format (RE of getState/setState), transport/sync handling, Snow class | REAPER (Linux) loads a project saved with the original and it sounds the same; automation works | listen test |
| R4 | Editor | VSTGUI front-end built from editor.xml + PNGs: containers/pages, knobs (filmstrips), buttons, menus, displays, conditions, value text; patch browser, Patch Utility, part selection | Xvfb screenshots of every page pixel-compared with the original; all controls change the Virus | a look/feel check |
| R5 | Completeness | multi mode, arpeggiator editor, MIDI learn/mapping, preferences, sync diagnostics, model variants (Desktop/Polar/Keyboard/Snow, "PR" family), error/info pages | feature checklist from editor.xml + manual complete | - |
| R6 | Windows + macOS | Windows: broker pipe backend; macOS: native arm64 + x86_64, broker for MIDI, Core Audio for audio (as libVirusUSB does today) or the broker, AU wrapper; CI release artifacts | same tests pass on all three | Mac/Windows tests |
| R7 | Validation + beta | A/B against the original (timing like E078/E125, audio, projects, long sessions), beta through GitHub releases | testers confirm | release decision |
| R8 | UI/legal review | decide what of the Access look stays runtime-loaded vs. own artwork, names/trademarks | user decision | yes |

## Risks

1. Timing/sync logic (F5 anchors, latency, re-anchoring) - the hardest part; decompile the original's code paths
   (VirusDevices / AudioSM consumers) and compare traces sample-accurately.
2. DAW state format: must be byte-compatible to load old projects; RE of the chunk + tests with saved projects.
3. Hidden semantics in the XML (conditions, "PR" models) - covered by trace comparison per page.
4. Size: the original is ~14 MB (Windows) / 4.9 MB (Mac, x86_64+i386, stripped) - work page by page, always shippable
   on the branch.

## First steps (R0)

1. Branch `rewrite`, `rewrite/` skeleton with CMake, VST3 SDK + VSTGUI as pinned submodules/fetch, CI job.
2. Trace recorder in the Linux broker (all EP 0x06 / 0x86 packets + device frame + wall time, binary log + decoder).
3. Oracle script: vtihost under Wine with scripted actions (load, wait for sync, notes, parameter changes via
   IEditController, program change) -> golden traces in work/ (not committed - contain device data).
4. REA: open Virus Control (Mac x86_64 slice + Windows x64), map the XML loaders (RemoteDevice.cpp) and the
   device/timing classes (VirusDevices.cpp), record findings in findings/rewrite-re.md.
