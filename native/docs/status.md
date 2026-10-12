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
- Bank info (39/14) decoded: 14-bit sum of the patch data minus 4 header bytes (510/512, two +100 quirks open);
  replies come on EP 0x85 outside a plug-in session; virusctl sysex sends requests and saves the replies.
- Ghidra headless decompile of Virus Control.dll -> work/decomp/virus-control.c (tools/DecompileAll.java), for the
  fingerprint quirk and the remaining unknowns (39/14 compare, 74 01 7E poll, multi dump checksum).
