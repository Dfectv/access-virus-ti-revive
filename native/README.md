# virus-ti-native (private)

Native rewrite of the Access Virus TI plug-in - stage 3 of TI Revive. Plan: [docs/plan.md](docs/plan.md),
progress: [docs/status.md](docs/status.md).

Rules: no Access material in git (graphics, XML, firmware, decompiled code) - the plug-in loads the skin and device
description at runtime from the user's own Access installation; device recordings stay in work/.

Build (Linux): `git submodule update --init external/ti-revive && (cd external/vst3sdk && git submodule update --init base cmake pluginterfaces public.sdk vstgui4) && cmake -S . -B build -G Ninja && cmake --build build`

Install (Linux): `cmake --build build --target VirusTINative && cp -r build/VST3/Release/VirusTINative.vst3 ~/.vst3/`
(Windows: CI artifact `VirusTINative-windows`, workflow `.github/workflows/build.yml`).

## Testing in REAPER (Linux)

1. The TI Revive broker must own the Virus (socket `/tmp/vtibroker-<uid>.sock`); close every other Virus host first.
2. `pw-metadata -n settings 0 clock.force-rate 44100; pw-metadata -n settings 0 clock.force-quantum 512; pw-jack reaper`
3. First start: the plug-in syncs all RAM banks from the Virus (~8 min, LCD shows the progress) and caches them in
   `~/.local/share/virus-ti-native/cache` (rename the folder to force a new sync). ROM banks come from the user's
   `firmware.bin`, Personal Libraries from `Documents/Access Music/Virus TI/Patches`.

Environment knobs: `VTI_NATIVE_S` (stamp offset, default 5000), `VTI_NATIVE_M` (FE/F2 lead, 1024), `VTI_NATIVE_PING`,
`VTI_NATIVE_K`, `VTI_NATIVE_DRAWTIME` (see docs/status.md, R5).

## Tools

| tool | purpose |
|---|---|
| `vtimodel` | inspect the Access files: params, messages, dumps, categories, automation, ROM, libraries |
| `virusctl` | talk to the Virus through the broker: state, set, note, sysex, timing tests |
| `virussync` | bank sync without a host |
| `nativehost` | minimal VST3 host: load state, set params, render WAV, open the editor (X11), click/drag/type, screenshot |
| `vtirender` | render the editor with cairo, compare against screenshots of the original |
| `xclick`, `xtype` | drive the original under Wine/Xvfb |
| `DecompileAll.java`, `msgsummary.py` | Ghidra headless decompile into work/, trace summaries |
