# HANDOFF

_Last updated: 2026-10-12 evening. Chronological notes until now: [HANDOFF-history.md](HANDOFF-history.md).
Evidence: [evidence.md](evidence.md) (E001-E125), protocol: [spec.md](spec.md)._

## Status

| Platform | State | Released | Key docs |
|---|---|---|---|
| Windows 10/11 | stable: WinUSB + user-mode emulation, broker with USB audio, ASIO "Virus TI (TI Revive)", Control Center via vtilaunch | MSI + setup.exe (Burn bundle) | design.md, design-wdm-bridge.md, design-midi-ports.md |
| macOS Apple Silicon | stable: native VST3/AUv2 bridge + Rosetta helper hosting the original, own libVirusUSB, broker (MIDI driver stand-in) | `-mac.zip` (pkg + scripts) | design-macos.md, design-macos-bridge.md |
| Linux (experimental) | works in REAPER: native broker owns the whole Virus (MIDI + iso audio), Windows wrapper under Wine/yabridge | `-linux.tar.gz` (install.sh) | design-linux.md |

Latest release: **v0.7.2** (2026-10-12, CI run 38291289944, automatically "Latest": CI no longer marks 0.x as
pre-release). 0.7.2 = macOS hides the Intel-only Access plug-ins + Rosetta check, Linux ring polling. Contents: All Notes Off when a client that played notes disconnects (all brokers), Linux
PipeWire-JACK docs, yabridge check in install.sh, Windows setup.exe, Mac AU, `tirevive-usbinfo.ps1` (Snow testers).

## Open items (order agreed with the user 2026-10-12: easy first; deferred stays deferred)

1. ~~Mark a release as Latest~~ - done (0.7.1).
2. ~~Tidy up HANDOFF~~ - done (this file).
3. ~~Windows setup.exe interactive dialogs~~ - done (E126).
4. BACKLOG - first notes at transport start (E127): same on Windows; the user remembers the same with the original
   Access driver (start playback slightly before the first note). Optional later: confirm with the win10 snapshot pre-winusb.
5. Occasional sync reports on Linux (E128): cause = wineserver's registry save ~30 s after the plug-in process starts
   (stalls the process ~25 ms -> plug-in re-anchors -> synth reports). Done: vtiaudio polls the ring under Wine (not
   released yet). Wine 11 tested (E131, paused by the user): WineHQ 11.0 stable / 11.19 devel (noble builds, unpacked in ~/.local/opt/wine11*, prefix copy ~/.local/opt/wine11/prefix) have NO ntsync support compiled in; same 30 s registry stall (61-64 ms). /dev/ntsync exists after `sudo modprobe ntsync` (until reboot). This PC is back to the plain 0.7.1 release (plug-in, broker, units).
6. macOS: Logic Pro test (AU), needs the user at the Mac.
6a. ~~macOS + Cubase blocklist~~ - resolved (E129): Cubase 15 loads TI Revive fine; the blocklisted plug-in is the
    original Intel "Virus TI.vst3". (a) DONE (not released): "Install Access Virus TI software.command" checks Rosetta and offers to install it,
    bridge log names a missing Rosetta (EBADARCH). (b) DONE (E130, not released): originals moved to /Library/Application Support/TI Revive/Access plug-ins;
    verified in Cubase 15 with a local 0.7.2 test package; uninstall moves them back (tested on the Mac).
6b. macOS universal bridge (E132, not released): DAWs under Rosetta can load TI Revive; tested with an x86_64 test
    host under Rosetta. Open: real test in a DAW under Rosetta with the Virus on the Mac (e.g. Cubase 15 'Open using Rosetta').
7. macOS: Control Center replacement (Access app is 32-bit) - biggest item.

Waiting: **Snow** (issue #3, bidmessmer) - the regular plug-in finds the Snow, the Snow plug-in does not; asked for
`tirevive-usbinfo.ps1` output after loading the Snow plug-in once (the vtiemu "device identity" line). Access'
VirusUSB.inf also lists PID 0816 (unlikely the cause since TI Revive reaches the device).
Deferred by the user: Windows MIDI ports (design-midi-ports.md), WDM sound card (design-wdm-bridge.md), latency
measurements.

## Release process

- Releases ONLY via version tags: `git tag -a vX.Y.Z -m ... && git push origin vX.Y.Z`; CI (.github/workflows/build.yml)
  builds binaries (mingw) -> MSI + setup.exe (windows-2022, WiX 5.0.2, `build.ps1 -Bundle`) -> mac zip (macos-15,
  scripts/mac-build-release.sh, VST3 pluginterfaces pinned 4f547e8e) -> linux tarball (scripts/linux-build-release.sh,
  static libusb without udev) -> release with all assets + scripts/tirevive-usbinfo.ps1.
- After CI: `gh release edit vX.Y.Z --notes-file work/release-notes/vX.Y.Z.md` (notes written beforehand).
- Identity: commits/tags only as `derpsn <callmederpsn@proton.me>`; never the old account/name/e-mail; never push to
  the old repo. Commit trailer: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Before releasing a new Windows wrapper: run src/wrapper/vtiloadtest.exe against it (E092).

## Environments

**This Linux PC** (Ubuntu 26.04, kernel 7.0, PipeWire; Virus on USB 1-9; udev rule 99-virus-ti.rules = plugdev):
- TI Revive installed like a user (0.7.0-test package + newer broker copied to ~/.local/lib/tirevive/vtibroker):
  systemd user socket `tirevive-broker.socket` (unix + 127.0.0.1:47615, token ~/.config/tirevive/broker.token),
  Wine prefix ~/.wine with Access software, yabridge MASTER build (5.1.1-57-gb580a9f7) in ~/.local/share/yabridge
  (official 5.1.1 backup: ~/.local/opt/dl/yabridge/share-5.1.1-backup). Broker log: `journalctl --user -u tirevive-broker`;
  verbose for a session: drop-in `ExecStart=%h/.local/lib/tirevive/vtibroker -v` (remove afterwards).
- REAPER 7.82 portable: ~/.local/opt/dl/reaper/reaper_linux_x86_64/REAPER/reaper; start `pw-jack .../reaper`
  after `pw-metadata -n settings 0 clock.force-rate 44100` + `clock.force-quantum 512` (PulseAudio backend = messy, E122/E123).
- User is in group pipewire (rtprio 95, memlock 4 GB). Ask the user to close REAPER before tests that need the Virus.
- Lab Wine without root: ~/.local/opt/wine-lab/bin/wine (bwrap overlays of the Ubuntu wine debs), prefix
  ~/.local/opt/wine-lab/prefix; test host `wine 'C:\vtihost.exe' '<plugin.vst3>' <s> 'C:\out.wav'` (VTI_BLOCK,
  VTI_EDITOR=C:\shot.bmp; Xvfb :77 + PIL ImageGrab for screenshots). Broker diagnostics: `-v` (notes, RX rates),
  `-vv` (RX dump), VTI_RECORD=<raw> (USB 1 audio).
- Gotcha: never `pkill -f <pattern>` in a tool call whose own command line contains the pattern (kills the shell).

**Windows VM** `win11` (libvirt, `VM=win11 scripts/vm.sh ps|push|pull`, user in ~/.config/tirevive/vm.env): the
current disk is build machine AND test machine (WiX 5.0.2 + .NET in C:\build, extensions UI/Util/
BootstrapperApplications - NOT Bal.wixext, that shows "damaged"; TI Revive 0.6.4 test build, Access 5.1.7, Ableton
Live 11). `bash scripts/win11/build-msi.sh <version>` builds MSI + setup.exe there. Virus passthrough:
`virsh detach-device/attach-device ... scripts/virus.xml` (never virt-manager USB redirection, E061). Shut the VM
down after use.

**Mac** (MacBook Air M3, host/user in ~/.config/tirevive/mac.env, `scripts/mac.sh ssh|push|pull`): Ableton Live 12
native, GarageBand; SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk; release builds via CI.

## Things that bit us (keep in mind)

- The Access plug-in needs constant host blocks of 64/128/256/512 at 44.1 kHz (Mac AU block splitting reverted, E111;
  Linux PulseAudio backend E122/E123).
- The plug-in writes its Virus memory cache (ProgramData/.../Cache/<id>) only on a clean shutdown; a killed DAW means
  a new full sync (~7 min on a fresh cache, ~1 patch/s like the Access driver, E031/E119/E123).
- One program at a time owns the Virus (POSIX brokers, E112); a second one shows "no Virus TI connected".
- Linux: snd-usb-audio cannot share the Virus; the broker takes all 5 interfaces and hands them back on exit by
  claim/release + port reset (E113/E116). Wine 10 (Ubuntu) has no AF_UNIX -> TCP + token, TCP_NODELAY (E117/E118).
- yabridge 5.1.1 + Wine >= 9.22: dead mouse input (E120); install.sh warns.
- After an editor was open, Virus Control.dll can hang in DLL_PROCESS_DETACH at process exit (E119); the FreeLibrary
  path (what DAWs do) is fine.
- WinUSB / Windows: MI_00 bound to WinUSB too (broker streams audio itself, E075-E078); never WASAPI clocks.
