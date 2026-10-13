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
  95.9, FX2 (page 11) 93.6 % (mean 93.4 %); with nine-part tiled bitmaps (Bitmaps Tile/TileInsetRect, menu text inside the insets):
  Easy 97.0, Browser 91.7, OSC 94.7, Filter 93.7, LFO 98.3, Matrix 92.9, Arp 97.2, FX 94.4, Common 98.9, FX2 94.2 (mean 95.3 %). Missing: custom displays (envelopes 0x180039df0.., filter curves,
  osc shapes, patch browser), some menus/popups state. Menus pop up with the valuelist and switch layouts.

- R4: patch browser works in the plug-in: bank tables (97.3 % identical to the original), click selects (inverse
  font), double click loads the patch into the current part (dump to the Virus edit buffer + state + parameters).
  Part selection, menus, page tabs, knob/slider drags, buttons work; the editor follows device/automation changes.

- Plug-in memory sync: after activation a background thread checks RAM A-D (bank info) through the plug-in's own
  broker connection (MemorySync embedded: send function + feed of replies) and fetches only changed programs into
  ~/.local/share/virus-ti-native/cache (initially imported from the Access cache). Verified: corrupted RAM C program
  -> 1 fetched -> file identical to the original's cache again.

- Timing: the plug-in keeps a Virus timeline (newest ring frame - S), sends stamped MIDI clock at the host tempo, a
  stamped FE + song position at each anchor and stamps every event on the timeline; the Virus then honours the stamps
  with a constant per-session offset (measured with broker trace + raw recording, scratch measure.py). Open: runtime
  offset, clean start, host latency.

- Timing start (2026-10-13 05:20): the timeline anchors only after the state upload has drained (DeviceOut::busy) and
  0.5 s of steady audio, then follows drift by 1 frame/block -> sync reports 13 -> 1. Without stale start-up messages
  the stamps count as "late" (immediate, ~340 frames) - with a stale start they were honoured (K 3982, ~2000 frames
  after sending). Hypothesis: the Virus anchors on the song position's ARRIVAL; the F2/FE are now stamped M frames
  early (VTI_NATIVE_M, default 1024) so later events lie M frames in its future. UNTESTED: the Virus dropped off USB
  at 05:22 (port 1-8: resets, "device not accepting address, error -71") right after a broker give-back -> needs a
  power cycle by the user. Watch whether this repeats (broker reset at exit, or our stream hanging the firmware).

- Editor displays: ADSTR envelopes, filter responses (LP/HP/BP/BS), classic oscillator shape (all measured on the
  original), big rotating rings only while held, inactive controls use their DisabledBack strip. Pixel match per page:
  Easy 98.3, Browser 97.3, OSC 97.4, Filter 97.4, LFO 99.0, Matrix 96.2, Arp 97.4, FX 95.9, Common 98.9, FX2 94.1
  (mean 97.2 %). Open: FX2 menus that the original shows active although our state says inactive (Reverb Send 0 -
  the original probably uses the Virus' upgraded edit buffer values), Hypersaw/Wavetable displays, step sequencer.

- Editor: patch rename (double click on a part's name, VSTGUI text edit -> dump + Virus), LCD shows the edited
  parameter (display name, value text, part). process() is now free of blocking locks (automation -> lock-free ring ->
  device worker updates the part dumps). Virus still off USB (E136 in TI Revive) - device tests wait for the user.

- R6 start: device layer compiles for Windows (mingw, scripts/win-cross-tools.sh): broker client over the named
  pipe (overlapped I/O, reader + writers concurrently), audio ring via the named mapping, named event for the
  Windows broker's audio request. vtimodel.exe under Wine finds the Access installation (C:\Program Files\...).
  Untested against the Windows broker. The plug-in for Windows needs MSVC (CI).

- MILESTONE 2026-10-13: first DAW test by the user - REAPER 7.82 on Linux (pw-jack), "Virus TI (native)" from
  ~/.vst3: "everything is working", latency sounds good, only minor UI issues (to be listed by the user). The Virus
  was power cycled after the E136 hang and works again.

- User feedback round (2026-10-13): first start without a cache = full RAM sync like the original (512 patches ~8 min,
  identical to the original's cache; Virus display message + LCD progress); ROM A-Z from firmware.bin by model;
  browser / LOAD / SAVE / bank menu rebuilt after screenshots of the original (work/orig-ui: device > RAM/ROM banks,
  Personal Libraries, "000 Name", selection frame, category menu with check); big rings visible again; editor lag:
  partial repaints (~8 ms per drag step). Personal Libraries: the .mid patch files in Documents/Access Music/Virus TI/Patches (src/access/library; Linux
  ~/Documents/..., else the Wine prefix's) in the bank menu, LOAD menu and browser. Virus C (267-byte) dumps are upgraded
  with the default page B from the DLL; SAVE writes into library files (overwrite / append). Open: search display in the original
  (typing into the original under Wine did not reach the field), whether the original loads on double click.

- CI (.github/workflows/build.yml): Windows (MSVC, VirusTINative.vst3 artifact) + Linux builds on every push - both
  green (run 38367374934). The Windows build loads under Wine (vtihost: initialize, 3 out / 2 in buses, Access files
  found, editor attached; drawing not visible under Wine = Direct2D); it cannot reach the Linux broker from Wine (pipe;
  the Wine route is TCP + token like TI Revive's vticore_sock) - real test: the win11 VM with Virus passthrough.
- Questions for the user (could not be captured from the original under Wine): how the original's search shows
  results; whether a double click in the browser loads the patch (ours: double click loads, click selects).

## 2026-10-13 (midday) - user test feedback, browser fixes

User test in REAPER: CATEGORY text garbled, search field floating/not working, Piano filter dimmed everything,
part strip bar always full. Fixed (2ca76d0): built-in CATEGORY conditions + "No Label", filter index -> stored
category value, live search field at the layout position, VU = MIDI activity meter (decompile: ShowMIDIActivityMeters,
per-channel max velocity, -1 per idle tick). Verified in nativehost/Xvfb (search "pi", meters part 1 only). Category
popup itself not testable headless - waiting for the user's retest.

## 2026-10-13 (afternoon) - browser like the original, performance

- Search/category: one "Search Results" bank in the UPPER browser (Browser 0), sorted by name, pages of 128, the
  original's titles; sources RAM + ROM + all Personal Libraries incl. subfolders, de-duplicated by page A[2:]
  (reproduces the original's Piano list exactly: 70 entries, same 9 doubles). Libraries recursive, folders as
  submenus. Category menu tick fixed (generic menu uses the menu value). LCD shows the category + [original value].
- MIDI activity meters repainted 16x per tick -> REAPER sluggish while the editor was open; now one merged repaint
  of the changed meters (31bf493). Waiting for the user's REAPER retest.
- Open: generic Linux menus feel slower/bigger than the original's native menus (VSTGUI theme not configurable
  on X11 without an SDK patch - own menu view possible); first category search: categories now decoded in the background after the sync (~0.5-0.7 s).

## 2026-10-13 (afternoon) - Windows port started

- win11 VM: TI Revive 0.7.4 installed (MSI, silent), CI build of the plug-in + tools (nativehost.exe now in CI).
- Fixed: the plug-in starts vtibroker.exe on demand (was: connect only); bank sync cancellable on terminate.
- Verified with the Virus passed through: Access files + libraries found, TI2 detected, audio streaming with notes
  (peak ~0.07 like Linux), first sync running. Next: user test in REAPER/Ableton in the VM (editor on Windows
  untested so far - nativehost has no editor on Windows).
- Linux broker stopped for the VM test: afterwards `scripts/vm.sh detach`, `systemctl --user start tirevive-broker.socket`.

- Windows user test (Ableton 11.3 in the VM): plug-in found after a rescan (first scan had crashed with an older
  build; WER LocalDumps for "Ableton Plugin Scanner.exe" -> C:\dumps enabled, CI now uploads PDB + map as
  symbols-windows), sound OK, UI draws correctly, menus/browser OK. Fixed after the test: sliders track the mouse,
  inactive controls locked. Open: retest of those two, timing check.

## 2026-10-13 (evening) - macOS port

- CI macOS job: universal VST3 (arm64 + x86_64) + tools. Core Audio input backend (Virus = class-compliant
  "Virus TI", 6 in @ 44.1 kHz), broker started on demand (posix_spawn like libVirusUSB), double-connect bug fixed.
- MacBook (macOS 26.6, arm64): plug-in installed in ~/Library/Audio/Plug-Ins/VST3; Access files, firmware ROM
  (26 banks) and libraries found; notes reach the Virus, Core Audio frames advance but are all zero when run over
  SSH (no microphone permission). User test in Ableton 12 on the Mac: everything works, UI responsive.
- Afterwards: the Virus goes back to Linux (open Linux items need the original next to ours: Hypersaw display,
  ArpEdit, FX2 menus, timing R5).

## 2026-10-13 (night) - remaining displays

- HyperSaw display (3 lines, spacing from DetuneSpread) and the arpeggiator user pattern (ArpEdit: bars, span over
  off steps, mouse editing of velocity/length) - both pixel-identical to the original (captures via vtihost +
  VTI_LOADSTATE with states built by `vtimodel setdump`; scripts in the session scratchpad, procedure: base state
  from VTI_SAVESTATE, change part 0's dump, load, click the tab with xclick, ImageGrab).
- Stored values outside a parameter's range are clamped on display (Vocoder/Bands 127 -> 32 like the original).
- Page diff against the original (all tabs, same state): LFO, MATRIX, COMMON, FX1 identical; ARP/FX2 < 0.1 %
  (inactive menu text brightness differs in some places - the original is inconsistent there); EASY/OSC/FILTER
  ~2-3 % = mostly the big knob rings (shown permanently since the user's feedback; the original under Wine shows
  them only on hover), plus the curves below.
- OSC classic display: default saw pixel-identical (1 px per column at floor(y), 2:1 steps, extra column);
  mixed shapes (sine..saw, pulse) still differ: the original interpolates 64-point wave tables from the DLL
  (FUN_180048470, DAT_1801ac340) - to do: read those tables at runtime.
- FX2 "Reverb/Type" active with Reverb/Send 0 in the original while Reverb/Mode (same condition) is greyed: an
  inconsistency of the original (no special case in the decompile); ours follows the XML condition for both.

## UI to-do (paused 2026-10-13 evening, continue here)

Procedure: capture the original with `vtihost` + VTI_LOADSTATE (state built from VTI_SAVESTATE base + `vtimodel
setdump` changes on part 0's dump), click the tab with work/bin/xclick (editor at screen offset 4,30), ImageGrab
:77; render ours with nativehost --state <component state> in `bwrap --tmpfs /tmp` (no Virus access); diff pixels
(threshold 24) per page. Scripts were in the session scratchpad (capa.py / allpages.py / fcmp.sh) - re-create.
Current page diffs (base state): LFO, MATRIX, COMMON, FX1 0 %; ARP 0.04 %; FX2 0.08 %; EASY 1.65 %; OSC 2.29 %;
FILTER 2.70 % (mostly the big knob rings, which we show permanently).
1. OSC classic display: mixed shapes (sine..saw, pulse) - the original interpolates 64-point wave tables from the
   DLL (FUN_180048470, DAT_1801ac340, second table at object+0x1c0): read the tables at runtime.
2. Filter displays: highpass resonance height (original ~2 px higher at resonance 100, non-linear), HP within 2 px.
3. Envelope displays: handles up to 1 px off for the base patch (measured geometry was from the init patch).
4. Inactive menu text brightness differs in some places (ARP/FX2 vs MATRIX - the original is inconsistent).
5. Editor speed in REAPER (field diagnostics, diag.on): a full repaint costs ~77 ms in REAPER (20 ms in nativehost
   on Xvfb), a partial ~5 ms; knob drags used up to 25 % of REAPER's main thread. LCD-only changes now repaint
   only the LCD. Done (2026-10-13): static layer cache + X pixmap copies of all bitmaps on Linux -> full repaint
   1-7 ms, knob step < 1 ms on XWayland (was ~70 / ~5 ms); page switch 30-45 ms (cache rebuild, CPU render).
6. Big knob rings: shown permanently (user wanted them); the original under Wine shows them on hover only.
7. Meta engine: tested reverb switch; LFO clock menus and filter/vocoder links untested with the Virus.

## 2026-10-13 (night) - stress tests

- Open/close 20x: every second re-open had no audio (broker refuses the start right after a session) -> retry
  every 500 ms (eea2b7e). Endurance 10 min with notes: 0 drops, distance steady ~1000-1150 frames (no drift).
- MIDI load (notes on 16 channels + 32 automated parameters per block = ~5500 msgs/s): the send queue starved ->
  8 real-time messages then one queued (dedd4e0); bank sync completes under load.
- Virus hang: the Virus' audio goes digitally silent (MIDI still answers, needs a power cycle) under a parameter
  flood: unthrottled automation 32 lanes/block (~2750/s) hangs it within 4 min; notes alone at ~2750/s are fine;
  automation throttled to 1000/s alone fine, but together with the notes it still hung (full-scale burst first).
  Automation is now coalesced per parameter and sent within a budget (AUTOMATION_RATE, now 250/s) - retest of the
  full load passed at 250/s (4 min notes on 16 channels + 32 lanes, Virus fine afterwards).
- Block sizes: 64 had a ring re-anchor (click) every ~30 s -> audio distance at least 768 frames (0 in 60 s);
  128..2048 fine. Open (R5): the plug-in reports no latency to the host yet (getLatencySamples) - needs the real
  note-to-audio latency (variable K), else delay compensation would be wrong.
- Tooling: nativehost --stress / --stress-notes / --stress-params / --block N; VTI_NATIVE_STATS.
- User tests in REAPER: (a) pages showed part 1's values for every part (containers' Part="0" was inherited) ->
  fixed (bbfb44b); (b) USB unplug during playback not detected, no sound after reconnect -> the worker follows the
  broker's device state: "Virus disconnected" on the LCD, on return start-up messages + state again, audio retry,
  timeline re-anchor (a64743b) - user: works. (c) selected part jumped back to 1 after a patch load: non-dump values
  (selected part, part volume/pan, globals) are now kept in the state (9adf045) - user: works. (d) linked knob
  (LFO link) followed only on release -> repaint of changed params during drags (8bb3a5f). (e) LFO link lost after
  project reload -> editor-only link buttons saved as F0 7D 'VTN' records (ea08936). (f) notes hung while the
  start-up bank sync ran -> sync pauses while the host plays (8119d36) - user: works. (g) editor drawing slow
  (REAPER lag/dropouts) -> static layer cache + X pixmaps on Linux (7c045ea, c5c8645) - user: "snappy". (h) HyperSaw
  display ignored Density -> lines = saws, re-captured and pixel-identical (5617ddf); waiting: does the Density knob
  audibly change the sound (CC 17)? Note: after power cycles the original under Wine resyncs its memory (~10 min,
  let vtihost exit normally so its cache is written) before captures work again.

## Next
- R5 timing: (1) anchor the timeline once the audio stream runs steadily (no re-anchors -> no Sync Information
  Events), (2) the runtime offset between stamps and audio: the original anchors on out-of-range audio samples
  (0x1800a8900) - no such samples on any of the 6 ring channels in our session; check whether the original writes the
  host->Virus audio stream (broker OUT ring) and whether the markers appear only then; else calibrate with a probe note
  on a part routed to an unused USB output, (3) read the audio at timeline + offset and report the latency to the host.
- R4: custom displays (KEnvelope 0x180039df0 + stage setup in the control factory, filter responses = KXYControl?,
  KOscClassicShape/KOscHypersawShape, KStepSequencer), patch name editing, info panel / LCD texts, remaining 2-6 %
  pixel differences (scripted page captures: tools/xclick.c + vtihost under Xvfb, see the orig-page*.png procedure).
- REAPER test with the user (Linux VST3 "Virus TI (native)"; drop-in build for projects saved with the original).
- R6: Windows (broker pipe client, WinUSB shm ring) and macOS (Core Audio path) ports of src/device.
