# HANDOFF

_Last updated: 2026-10-15 (0.8.0). Chronological notes until now: [HANDOFF-history.md](HANDOFF-history.md).
Evidence: [evidence.md](evidence.md) (E001-E145), protocol: [spec.md](spec.md); the native plug-in:
[../native/docs/](../native/docs/architecture.md) (its development log: native/docs/status.md)._

## 0.8.0 (2026-10-15): the native plug-in replaces the classic one

- **"TI Revive Virus Control"** (`native/`, moved in from the private workshop repo virus-ti-native with its full
  history, native/docs/migration.md): VST3 on Windows (MSVC), macOS (universal) and Linux (Ubuntu 22.04 build); the
  Windows ASIO driver **"TI Revive Virus Audio"** (6 in / 2 out, no Access driver). Timing sample accurate with
  reported latency (E139-E141, E143), any host sample rate (resampler), start-up/sync behaviour (E144, E145).
- It takes over the class id of the classic plug-in (Windows/Linux wrapper id, macOS bridge id; moduleinfo
  Compatibility for the others), so projects of "Virus TI (TI Revive)" open with it. Vendor stays "TI Revive".
- Installers: Windows MSI without wrapper / ASIO wrapper / Control Center launcher; `tirevive-access.ps1` only unpacks
  the Access `.msi` (msiexec /a) and offers to remove a full Access install of 0.7.x (files kept, dead Access ASIO
  entry removed). Linux `install.sh` without Wine/yabridge (msiextract or Wine unpack). Mac pkg without the bridge
  VST3 (the Audio Unit for Logic stays, the Access software is still installed for it).
- Tested by the user on real hardware: Windows 11 PC (Ableton 11, ASIO), MacBook Air M3 (Ableton 12), Linux REAPER
  (44.1/48 kHz). Release workflow rehearsed in a private repo (all jobs green).
- Not in 0.8.0: Virus TI Snow (owners stay on 0.7.4), Virus Control Center, Windows MIDI ports.
- Next: Snow in the native plug-in (needs a tester), OSC display for mixed shapes (wave tables), AU of the native
  plug-in, possibly the third USB return (cf. github.com/Give-me-a-signal/virus-ti-rework: Global USB controller 9)
  and RAM restore on exit; contact with that project pending.

## Status

| Platform | State | Released | Key docs |
|---|---|---|---|
| Windows 10/11 | 0.8.0: native plug-in + ASIO "TI Revive Virus Audio", WinUSB binding, broker with USB audio | MSI + setup.exe (Burn bundle) | native/docs/architecture.md, design.md |
| macOS | 0.8.0: native plug-in (VST3, universal); Audio Unit = bridge + Rosetta helper; own libVirusUSB, broker | `-mac.zip` (pkg + scripts) | native/docs/architecture.md, design-macos*.md |
| Linux | 0.8.0: native plug-in (VST3); native broker owns the whole Virus (MIDI + iso audio) | `-linux.tar.gz` (install.sh) | native/docs/architecture.md, design-linux.md |

Latest release: **v0.7.4** (2026-10-12, macOS driver-conflict detection; 0.7.3 = universal macOS bridge; 0.7.2 CI run 38291289944, automatically "Latest": CI no longer marks 0.x as
pre-release). 0.7.2 = macOS hides the Intel-only Access plug-ins + Rosetta check, Linux ring polling. Contents: All Notes Off when a client that played notes disconnects (all brokers), Linux
PipeWire-JACK docs, yabridge check in install.sh, Windows setup.exe, Mac AU, `tirevive-usbinfo.ps1` (Snow testers).

## Open items (order agreed with the user 2026-10-12: easy first; deferred stays deferred)

1. ~~Mark a release as Latest~~ - done (0.7.1).
2. ~~Tidy up HANDOFF~~ - done (this file).
3. ~~Windows setup.exe interactive dialogs~~ - done (E126).
4. BACKLOG - first notes at transport start (E127): same on Windows; the user remembers the same with the original
   Access driver (start playback slightly before the first note). Optional later: confirm with the win10 snapshot pre-winusb.
5. Occasional sync reports on Linux (E128): cause = wineserver's registry save ~30 s after the plug-in process starts
   (stalls the process ~25 ms -> plug-in re-anchors -> synth reports). Done: vtiaudio polls the ring under Wine (released
   in 0.7.2). Wine 11 tested (E131, paused by the user): WineHQ 11.0 stable / 11.19 devel (noble builds, unpacked in ~/.local/opt/wine11*, prefix copy ~/.local/opt/wine11/prefix) have NO ntsync support compiled in; same 30 s registry stall (61-64 ms). /dev/ntsync exists after `sudo modprobe ntsync` (until reboot). This PC is back to the plain 0.7.1 release (plug-in, broker, units).
6. macOS: Logic Pro test (AU), needs the user at the Mac.
6a. ~~macOS + Cubase blocklist~~ - resolved (E129): Cubase 15 loads TI Revive fine; the blocklisted plug-in is the
    original Intel "Virus TI.vst3". (a) DONE (released in 0.7.2): "Install Access Virus TI software.command" checks Rosetta and offers to install it,
    bridge log names a missing Rosetta (EBADARCH). (b) DONE (E130, released in 0.7.2): originals moved to /Library/Application Support/TI Revive/Access plug-ins;
    verified in Cubase 15 with a local 0.7.2 test package; uninstall moves them back (tested on the Mac).
6b. macOS universal bridge (E132, RELEASED in v0.7.3, release package verified universal): DAWs under Rosetta can load TI Revive; tested with an x86_64 test
    host under Rosetta. Real test done (E133: Cubase 15 under Rosetta plays). Reddit Cubase 14 user: 'not connected' = LIBUSB_ERROR_ACCESS (E134): old Access drivers from a full install hold the Virus; told him to remove them + restart; install script now detects/disables them (not released). CONFIRMED: Aura Plugins' Virus Editor USB MIDI driver held the Virus; parked -> connected and syncing. Install script now also detects Aura's driver and offers to park it (default No). RELEASED in v0.7.4.
6c. macOS CPU (Reddit user, Cubase 14 under Rosetta, M4 Max): Cubase's CPU meter jumps only while STOPPED, smooth during
    playback -> ASIO-Guard real-time path for the selected/armed instrument track (per-block round trip to the helper,
    translated). Harmless unless dropouts; advice: buffer 512, deselect the track. Measured on our M3 (Cubase 15 under Rosetta,
    30 s play/stop/play): Cubase 23-28 %, vtihelper ~2.5 %, vtibroker ~4 % of one core, NO change between playing and
    stopped -> the ASIO-Guard jump at stop is Cubase moving the selected track to the real-time path, not extra work.
    Optional later: editor mirror copies only changed frames.
7. macOS: Control Center replacement (Access app is 32-bit) - biggest item.

8. Linux: watch for E136 (Virus hung after rapid give-back resets during tests on 2026-10-13); if it repeats,
   rate-limit the reset in vti_close.

**Snow on Windows works** (issue #3, E138, 2026-10-14): after removing the old Access USB driver (oem8.inf) and
0.7.4 the Snow plug-in works in Ableton 12.4.6 / Windows 11 25H2 (user confirmed, WinUSB on MI_00/03/04).
Waiting: **Snow on macOS** (issue #4) - MIDIServer driver holds the Virus; instructions posted, waiting for the user.
Stage 3 (native rewrite of the plug-in) is developed in the PRIVATE repo derpsn/virus-ti-native (~/virus-ti-native,
plan docs/plan.md, status docs/status.md); TI Revive is its submodule. State 2026-10-13 (evening): the native VST3 works on Linux (REAPER),
Windows (Ableton 11, win11 VM) and macOS (Ableton 12 on the M3, universal, Core Audio input) - user tests: sound,
timing, editor from the user's Access files, browser/search like the original. Night 2026-10-14: stability and
performance pass (thread races fixed via TSan/ASan, no lost note-offs, idle CPU 5 % -> 1.4 %, partial repaints for
automation, unit tests in CI); the device tests of it are on the list in docs/status.md there. Open there: a few
displays, an Ableton test with several M4L LFOs. Timing R5 solved 2026-10-14 (E139: the sync ping's
marker on USB 2 = the Virus' stamp clock 0 -> same latency in every session, reported to the host). Verified
2026-10-14: Linux/REAPER and macOS/Ableton 12 renders sample-exact (E140, E141), Windows only in the VM (open gap
pattern); macOS M4L test with three LFOs ~40 min stable (0 drops). Contributor/agent guidelines added (CONTRIBUTING.md,
AGENTS.md, .claude/skills). Issue #3 (Snow, Windows) solved; #4 (Snow, macOS): not supported by the Mac helper yet
(answer posted, Snow VST2 hosting would be needed - or the native plug-in).
Broker side from it: Mac broker logs "MDR ring full" once per episode (079904d, next release).
Kemper (2026-10-13, reply to the user's request): the Virus TI software will not be open-sourced, no plans; the
wish is forwarded to management. -> stage 3 stays on its rule: no Access material in any repo, everything loaded
from the user's own installation.
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
