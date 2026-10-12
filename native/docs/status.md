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
