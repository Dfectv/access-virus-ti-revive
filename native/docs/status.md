# Status

## 2026-10-12 (night)
- Repo created (private), submodules: external/ti-revive (TI Revive, public), external/vst3sdk (v3.8.1_build_84;
  base, cmake, pluginterfaces, public.sdk, vstgui4).
- Toolchain verified on Linux (Ubuntu 26.04): VST3 SDK + VSTGUI build, empty plug-in bundle builds.
- R0: trace recorder in the TI Revive broker (VTI_TRACE, scripts/vtitrace.py); first trace of the original (25 s,
  cached memory): F5 time stamps on cable 5, MIDI clock + CC + notes + SysEx on cable 2, control polls wIndex 1.
- R1 started: src/access (Access files from Virus Control.dll PE resources / Mac bundle, 445 files found in the Wine
  prefix), src/model (remotedevice.xml: 587 parameters, 128 value lists, 123 conditions, 12 message templates),
  tools/vtimodel (files/summary/params/encode). Encoding verified against the original for Global Master/Mode.
  docs/formats.md describes the XML semantics.
- automation.xml = the original's VST3 ParamIDs (2671 of 2672 + 32 aftertouch/pitchbend ids = the 2703 mapped);
  scripted automation of the original (vtihost VTI_PARAMS) confirms value = round(norm*127) and our encoding.
- editor.xml structure documented (36 control classes, 404 bitmaps, 20 top-level containers).
- R2 started: src/device/broker (C++ client of the TI Revive broker, POSIX), usbmidi packer, tools/virusctl
  (state / set <path> <value> / note).

## 2026-10-13 (night)
- Full-sync trace of the original (empty cache, 4 RAM banks, ~9 min): start-up sequence, bank info 39/14, single dump
  requests 30/10 at ~1 per second; docs/protocol.md, tools/msgsummary.py.
- virusctl state / set / note work against the Virus through the TI Revive broker (systemd socket).
- Dumps: RemoteDevice::decode_dump / update_dump / dump_size, vtimodel message|dump; checksum add7 verified, decode +
  re-encode byte-identical for all 528 recorded single dumps. Open: bank-info fingerprint function, multi dump checksum.
- Bank info (39/14) decoded from the decompile: 14-bit sum of dump bytes 13..521 minus 265 (512/512);
  replies come on EP 0x85 outside a plug-in session; virusctl sysex sends requests and saves the replies.
- Ghidra headless decompile of Virus Control.dll -> work/decomp/virus-control.c (tools/DecompileAll.java), for the
  fingerprint quirk and the remaining unknowns (39/14 compare, 74 01 7E poll, multi dump checksum).
- R2 sync engine: src/device/memsync (bank info -> fetch only programs whose fingerprint differs; strictly one request
  at a time - overlapping requests make the Virus truncate replies; ~1 s per request is the device's pace), cache in
  the original's file format; UsbMidiParser in usbmidi.h. tools/virussync: import of an Access cache + check of 4 banks
  = 3.4 s, 3 corrupted programs refetched and the bank restored byte-identical to the original's cache.
- Start-up multi dump = fixed table in the DLL (stale checksum, Virus accepts it); decompile pointers in protocol.md.

- Plug-in state = the start-up message list in the cache container (formats.md); vtihost VTI_SAVESTATE/LOADSTATE
  (TI Revive) captures the original's state. Original class ids + parameter list reproduced (2703 params, 2671/2671
  titles identical; automation.xml typo handled like the original).
- R3 started: plugin/ = "Virus TI (native)" (SingleComponentEffect, original buses, 2703 params, IMidiMapping, state in
  the original's format, parameter changes -> encoded messages -> broker, part dumps kept current). VST3 validator: 47
  passed / 0 failed. tools/nativehost: the original's state loads and getState returns it byte-identically; a
  Filter 1 Cutoff change updates exactly byte 49 + both checksums of the part 1 dump.

- R3: the native plug-in PLAYS the Virus on Linux: init messages + state upload, notes with F5 stamps, audio from
  the broker's shm ring (Out 1 = USB 1, Out 2 = USB 2, drift by frame drop/repeat). nativehost --notes --wav: init
  patch audible every second (peak ~0.09), ~170 ms latency for now.

- R3: a new instance builds the original's default project from the user's plug-in binary (init patch upgraded by
  a port of the original's format upgrader, MultiCtrl multi); non-part records byte-identical to the original's
  default state, parts differ in 17 legacy bytes (same sound). Plays without any loaded state (nativehost).
- Decompile findings: stamp rule + audio sync markers (protocol.md), patch format versions 1..12 (upgrade_single).

- R4 started: src/editor (EditorLayout from editor.xml, Conditions over remotedevice + editordevice incl. "outside"
  tests), tools/vtirender (cairo offscreen render with knob filmstrips / radio states / conditions). Easy page vs a
  screenshot of the original (Xvfb + PIL): 92-93 % identical pixels; missing: text (bitmap fonts FNT_*.png, patch
  names, value displays), filter curve displays (LP4Response etc.), part strip details; the big Filter 1 cutoff ring
  (MaskedKnob KNB_Cutoff_Rot_Uni) is not drawn by the original at rest - open why (other MaskedKnobs are).
- Device -> host knob feedback, realtime-safe event ring (R3).

- R4: the plug-in has its EDITOR on Linux (VSTGUI): the original layout from the user's files via the shared renderer,
  page switching by the header tabs, knob drags (vertical, full range over 200 px) -> performEdit for automatable
  parameters / direct SysEx otherwise, part dumps updated, repaint on device changes. tools/nativehost --editor
  (X11 window + IRunLoop + XEmbed mapping, --click/--drag via XTest, --shot): click OSC -> OSC page; drag Oscillator
  volume +60 px -> 64 -> 102, "+38" shown, part dump byte 45 + checksums updated.

- Editor fidelity vs screenshots of the original (vtihost under Xvfb, tabs clicked with tools/xclick.c): identical
  pixels per page: Easy 97.0, Browser 89.3, OSC 90.3, Filter 93.7, LFO 97.5, Matrix 86.6, Arp 97.0, FX 93.3, Common
  95.9, FX2 (page 11) 93.6 % (mean 93.4 %). Missing: custom displays (envelopes 0x180039df0.., filter curves,
  osc shapes, patch browser), some menus/popups state. Menus pop up with the valuelist and switch layouts.

## Next
- R3: device -> host (knobs on the Virus change parameters: decode incoming CC/poly pressure/SysEx with
  remotedevice.xml, update the part dumps, notify the host), realtime-safe event queue, exact F5 stamps (R5 rule).
- REAPER test with the user: "Virus TI (native)" instance (Linux VST3), and a project saved with the original
  (VTI_NATIVE_DROPIN build) - needs the user at the PC.
- R4: menus (popup with the valuelist), text edit (patch name), patch browser, custom displays (filter response,
  envelopes, osc shapes, arp step editor), part selection, LCD info panel (parameter name/value while dragging).
