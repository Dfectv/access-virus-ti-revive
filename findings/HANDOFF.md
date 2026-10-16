# HANDOFF

_Last updated: 2026-10-16 (CONTINUE HERE below; 0.8.7 released). Chronological notes until now: [HANDOFF-history.md](HANDOFF-history.md).
Evidence: [evidence.md](evidence.md) (E001-E154), protocol: [spec.md](spec.md); the native plug-in:
[../native/docs/](../native/docs/architecture.md) (its development log: native/docs/status.md)._

## CONTINUE HERE (state 2026-10-16, after the 0.8.7 release)

**Released:** v0.8.7 (Latest, CI run 38856677383, all 7 jobs green, 2026-10-16): presets chosen on the Virus itself
are loaded (were ignored since 0.8.0), soft knobs work in both directions (editor -> destination parameter, E150;
knob on the Virus -> plug-in follows, E152). Branches `softknobs` and `tests-coverage` merged into main (62 unit
tests, ctest green on Linux). Tested on the TI2 under Linux only (nativehost + editor on the desktop; REAPER showed
the bug with the plug-in installed in ~/.vst3, moduleinfo 0.8.3). Not tested: Windows, macOS, a DAW with 0.8.7, the
Snow, parts other than 1.

**Replies posted 2026-10-16 on #4:** nasguile (comment 6208931585: soft knobs fixed, the hardware preset bug, FX 1
question still open) and ManuAnilkumar23 (6208931949: 0.8.7 may relate to his "Init"; caveat that a wrong bank copy
also breaks presets chosen on the Snow; asked for diag.log with "RAM A sync" + "preset chosen on the Virus" lines
and the broker log).

**Local branch `preset-followups` (from main after 0.8.7, NOT pushed/merged; ctest green on Linux, 88 tests):**
- FIX, tested in REAPER on the TI2 (E154): soft knobs turned on the Virus now change the SOUND - in plug-in mode the
  Virus only reports `Value SoftknobN`; the plug-in sends the destination parameter (clamped), like the original.
  0.8.7 moves the knob in the editor but the sound stays: worth a 0.8.8.
- after a preset chosen on the Virus is loaded, the 13 controllers of the original follow (tested: sent within 30 ms).
- cache miss (patch not in the bank cache): the edit-buffer read-back is built (worker thread, retry while busy,
  stale-reply check) but SWITCHED OFF (`READ_EDIT_BUFFER_ON_MISS` in load_reported_presets): the Virus ignored the
  dump written back (E154 (3), open). Normal installations have ROM banks from firmware.bin: tested fast.
- the decisions live in `vti::PanelFollow` (native/src/model/panelfollow.*) with 7 unit tests.
- This PC: ~/.vst3 holds the branch build now (REAPER tests); the user's old 0.8.3 copy is in the session scratchpad
  (vst3-backup) - install the next release package over it.
- `native/tools/virusmon.cpp`: the read-only traffic monitor, now a CMake target.
- tests: the editor renderer on a hand-written layout (test_editor, 12 tests) and AccessFiles on fake installations
  incl. a PE image built in the test (test_accessfiles, 7 tests, POSIX). CI-like vticore line coverage (no Access
  files, measured with gcov in a scratch --coverage build, HOME pointed to an empty folder): 53.5 -> 81.8 %.
- E153: the original's start-up timer only drives a progress display; no note hold found there (bounded search).
- Prepared, NOT applied (README is user-facing, needs the maintainer's OK): the Snow lines in README.md ("not
  supported by 0.8.0 yet ... stay with 0.7.4") are outdated - nasguile runs a Snow with 0.8.3 on macOS (#4), Manu's
  Snow is recognised (RAM banks "Init" open). Proposed: "**Virus TI Snow:** works with the native plug-in according
  to first user reports (macOS, 0.8.3+); not tested by us (we have no Snow). Please report what works and what not
  in a GitHub issue; 0.7.4 stays available for its classic plug-in." and in the testers box "(first reports: it
  works - your test results help us make it official)".

**Open, in order of value:**
1. Windows check of 0.8.7 (VM, Ableton): preset switched on the Virus, soft knobs both directions; the plug-in in
   ~/.vst3 on this PC (REAPER) is 0.8.3 - update it with the 0.8.7 Linux package.
2. E152/E154: merge `preset-followups` + release 0.8.8 (soft knobs change the sound). Open: the cache-miss
   read-back (E154 (3)); not tested: parts other than 1, Snow, macOS/Windows.
3. #4 ManuAnilkumar23: RAM banks "Init" on the Snow. The Snow description = TI description (E151). Possibly
   related to E152 (if he switches presets on the Snow, the plug-in stayed on "-INIT-") - ask him after 0.8.7;
   else his diag.log ("RAM A sync" lines) and broker log.
4. #4 nasguile: FX 1 changes only after re-triggering. Asked which knobs. Then: REA, how the original sends those
   parameters, compared with ours (vtimodel encode <path>).
5. CI coverage: 81.8 % on `preset-followups` (was 53.5 %). Lowest now: firmware.cpp 26 %, diag.cpp 40 %,
   remotedevice.cpp 66 % (synthetic tests for SysEx encode/decode, upgrade_single, automation maps would help).
6. Readiness probe (E149) vs the original: partly checked (E153) - the original's timer only shows progress; its
   audio/MIDI path from the host is not examined.
7. Older items unchanged: USB 3 output (C10), reconnect path without note hold, macOS duplicate-track check, two DAWs
   sharing the Virus on Windows, Snow "officially" (record a test report).

**Tools this session (scratchpad only, recreate if needed):** `virusmon` - a read-only broker observer (protocol 4,
`Broker::observe` + `cc::TrafficDecoder`, prints one decoded line per message; built against build-native's
libvticore.a + libpugixml.a with -DNDEBUG; rebuild vticore on the current branch first, a library from another branch
crashed it - Broker layout differs on tests-coverage). Worth adding as `native/tools/virusmon.cpp` later.

**Environment:** the Virus is on the Linux PC; win11 VM off. REA analysis of Virus Control.dll:
work/rea/virus-control-5.1.7-x64.ghidra-snapshot (git-ignored; open the copy in
~/.local/share/tirevive/access/... (sha256 a622f9a2...) with provider_id "ghidra" + snapshot_path). The user's
plug-in in ~/.vst3 is 0.8.3 (moduleinfo).

**Replies posted:** #4 on 2026-10-15 (fixes in the next release, logs asked). Memory: always REA for RE.

## 0.8.5 (2026-10-15): issue #6, duplicated track puts the Virus out of time (merged from branch single-instance)

- Cause (E148, MEDIUM): on Windows a second plug-in instance (Ableton: duplicate track) got the Virus too, sent its own
  sync ping and reset the Virus' only stamp clock; the original kept its old marker -> its stamps off by 13312 frames
  until a DAW restart. Fix: one instance per process (DeviceOut::claim_owner), others wait (LCD "Virus in use / by
  another instance"), takeover with state + new ping when the owner is removed; waiting instances drop their notes.
  Also: stats "virus frames" per instance. Built + ctest on Linux only.
- Test 1 (2026-10-15, Win11 VM, Ableton 11, TI Desktop, CI run 38791531643): original stays in time with a duplicate
  (user); the duplicate showed the LCD text only; takeover worked but notes were up to ~1.9 s late (klog) for ~25 s:
  the 529 queued start-up messages went out while notes played (E144) and the ping found no marker until the user
  stopped playing; once the marker came, K = 2559/2560 like the original. Changes after test 1: waiting instance shows
  the original's "Virus unavailable" page (SystemStatus/Error = 2, checked with vtirender), and after a takeover the
  sync thread runs the start-up again (start page, 4 s note hold, bank sync).
- Test 2 (CI run 38793336069): works (user); the first ping after a takeover still unanswered, marker ~13 s later
  (open, minor). Merged to main, released as 0.8.5. Open: macOS/Linux quick check of a duplicate.
- Not covered: two different DAWs on Windows still share the Virus (broker without owner on Windows).
- **0.8.6** (merged from branch `virus-ready`, E149): the fixed 4 s note hold is replaced by a readiness probe (single dump
  request `30 00 00` queued behind the start-up messages + state; its reply = the Virus is through); the timeline
  anchors and pings only after it, notes wait up to 3 s for the marker. Cause of the unanswered first ping: the
  Virus was still digesting 530 queued messages (two state uploads queued while waiting). nativehost `--duplicate T`
  = takeover test. Tested on Linux (TI Desktop): first start + takeover, marker on the first ping, K 6024 for all
  notes. Windows (VM, Ableton 11, CI run 38797985200, package 0.8.7): first start + takeover playing/stopped,
  ready after 3.9 / 6.9 / 6.8 s, marker on every first ping, works (user). Then: a state upload replaces the
  queued records of an earlier one (takeover queue 285 instead of ~530; Linux only, nativehost duplicate with
  setState). Open: the reconnect path (device back) still has no note hold; macOS not tested.

## 0.8.4 (2026-10-15): editor sizes (merged from branch ui-scaling)

- Plug-in: display scale from the host (IPlugViewContentScaleSupport) x user size (right-click menu 100-200 %, file
  ui-zoom), resizeView; tested on Linux with nativehost (--rclick added; its window follows resizeView): 150 %/200 %
  rendering, menu, runtime resize, saved choice. Control Center: View > Size + right-click in the Access look
  (preference "Zoom"), tested on Xvfb. User tests 2026-10-15: Linux (REAPER + Control Center) works; macOS with
  the CI package of run 38754600760 works in Ableton (VST3) and GarageBand (AU). Windows: Win11 VM at 150 %
  display scaling, Ableton Live 11 - works (user). Released as 0.8.4.

## 0.8.3 (2026-10-15, RELEASED: CI run 38750678863, all jobs green, Latest)

- The fixes from issue #5 (scan, state growth E147, first-sync estimate, dev tools). Open: the user's Windows check
  with a Virus (0.8.2 + 0.8.3 parts: Control Center, Nerd mode, plug-in).

## 0.8.2 (2026-10-15, RELEASED: CI run 38734447522, all jobs green, Latest)

- Open after the release: Windows check with a Virus (Control Center, Nerd mode while Ableton plays) - the user
  checks it later.
- After 0.8.2 (findings from issue #5, collaboration with virus-ti-rework - findings only, never code): plug-in scan
  no longer reaches the Virus, state growth fixed (E147), start page shows the time left for a long first sync,
  autest --no-notes / no crash after --png, nativehost --scan; their third-output finding recorded as C10 (UNTESTED).
  Replied in #5; our findings posted in their repo: Give-me-a-signal/virus-ti-rework#5 (allowed by the user).

- Clean-up after 0.8: the 0.7.x migration kit and test-package scripts in `native/` removed, broker headers only from
  `src/broker`; Linux broker without the TCP listener (opt-in `VTI_BROKER_TCP=1`, socket unit has no TCP port, install.sh
  removes `broker.token`; checked: no listener on 47615). `native/tools/autest.mm` (AU test host).
- Bug reports: template and `tirevive-usbinfo.ps1` collect the plug-in's `diag.log` (only "Virus TI native:" lines) and
  the broker log (checked in the win11 VM). Release workflow builds only what ships (broker `vtibroker.exe`, no mingw
  in the Linux job) - rehearse before the next tag.
- Issue #4 (Snow on macOS): status reply posted, both users asked to run the first test stages with 0.8.1.
  2026-10-15: nasguile (comment on #5) reports the Snow "recognized and fully works", total recall works, USB 1/2 ok,
  USB 3 silent (expected: Out 3 is silent by design, needs USB IO Configuration = 3 Outs, C10). Version not stated;
  asked on #4 for version, DAW, macOS and logs before recording evidence.
- Merged to main and pushed (1e88e73).
- **Control Center phase 1** (branch `control-center`): `native/src/cc/` + `native/tools/tirevive-cc` (info, backup,
  list, export), read-only, docs in native/docs/controlcenter.md. Tested on the M3 + TI2 (built there with clang++
  and `-isysroot .../MacOSX26.sdk`: the CLT linker rejects the default 27 SDK; no cmake on the Mac): info, backup
  from cache, full RAM D fetch byte-identical (2 min 9 s). Shipped in all packages (MSI: Program Files\TI Revive,
  pkg: /Library/Application Support/TI Revive, Linux: ~/.local/bin via install.sh); README "Backups" section.
  **App "TI Revive Control Center"** (native/ccapp, VSTGUI standalone, the user chose it 2026-10-15): device +
  banks, backup with progress/cancel, library conversion; packaged (MSI + Start menu, /Applications, Linux
  ~/.local/bin + desktop entry, needs gtkmm 3). Tested: Linux with the TI2 (Xvfb + XTest), Windows VM start (no
  Virus). Open: start on macOS (user), file dialogs on Windows/macOS. RAM restore only after a separate review.
- **Nerd mode** (user's idea 2026-10-15, agreed plan): (1) DONE logs + debug switches (page of the main window,
  `tirevive-cc logs` / `debug on|off`), tested on Linux (Xvfb). (2)+(3) DONE for the POSIX broker: protocol 4
  observer role (vtiproto.h), decoder src/cc/decode, Live tab, `tirevive-cc watch`; tested on Linux with the TI2
  (installed 0.8.1 broker stopped for the test and restored). (4) Windows broker observer DONE (VM test without Virus),
  macOS broker + CLI built on the M3 and tested without Virus (Virus is on the Linux PC), CI run 38718444950 green,
  merged. Open: the app's Nerd mode page on Windows/macOS only built, not looked at; Live with the Virus on
  Windows/macOS. Later ideas: timing view (klog as a switch), filter by part/kind, save the live view.
- **Access look of the Control Center** (branch `cc-skin`, user's wish 2026-10-15): layout + images of Access'
  Control Center exe at runtime (native/docs/controlcenter.md "The Access look"), tested on Linux under Xvfb.
  Setup scripts copy `Virus Control Center.exe` (Linux/Windows). Open: Windows look check, macOS source of the
  images, model choice (Desktop/Keyboard/Polar), real folder dialog check on a desktop. MERGED to main.
  Then: macOS images (setup command copies Contents/Resources PNG+XML; loading checked on the M3), model choice by
  clicking the picture (VSTGUI preferences), Nerd mode label for page-74 actions. macOS checked by the user
  (2026-10-15, test pkg 0.0.7): Control Center works, sound fine with the new broker. Version 0.8.2 prepared.
- README screenshots (docs/images/: plug-in filter page, Control Center Access look, Nerd mode): the maintainer
  allowed screenshots as the only exception to AGENTS.md rule 1 (2026-10-15); made with nativehost --editor --shot
  and the app on Xvfb (dbus-run-session, no paths / user names in the images). No "send SysEx" console (rule 2).
- Mac broker: the MIDI driver stand-in (`vtimidi_start`, VirusTICommMem, E100) only served the original plug-in of
  the classic path; it is now opt-in (`VTI_BROKER_MIDISHIM=1`). Without a reader its ring overflowed ("MDR ring full,
  dropping" in 0.8.1 broker logs). Tested on the M3 + TI2 with a broker built there: autest (native AU) connects,
  VTI_TRACE shows 434 writes to EP 0x06 and 129 reads from EP 0x86, tirevive-cc info works; no stand-in log lines.
  Audio could not be checked over SSH (no microphone permission: peak 0 also with the installed 0.8.1 broker) -
  a DAW check by the user is still open (the user will test sound later). The Virus is back on the Linux PC.
- `build.yml` can now run by hand: `gh workflow run build --ref <branch> -f version=0.0.1` builds all packages as
  artifacts (the release job still needs a tag). Use it instead of the rehearsal repo before a tag.
- Later: Linux `library_dir()` and install.sh use `~/Documents` even where the XDG documents folder is localized
  (e.g. `~/Dokumente`); consistent today, nicer with `xdg-user-dir DOCUMENTS`. USB3 output (Global USB controller 9); real readiness instead of the fixed 4 s
  note hold. **No flash writes, no OS update, ever** (AGENTS.md rule 2).

## 0.8.1 (2026-10-15): native Audio Unit, Mac without Rosetta

- `native/au/tirevive_au.mm`: the plug-in as AUv2 for Logic / GarageBand (Steinberg's wrapper, AudioUnitSDK 1.4 built
  by CMake), with the classic Audio Unit's codes; its old projects open (E146). `auval` passes, user-tested.
- Mac package: VST3 + AU + broker only; `Set up Access Virus TI files.command` unpacks the Access `.pkg`; the classic
  bridge, Rosetta helper and libVirusUSB are gone (src/mac/ kept for reference). Workflow `dev-mac` (manual) builds
  the Mac plug-in and package from any branch.

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
- Next: Snow in the native plug-in (needs a tester), OSC display for mixed shapes (wave tables), possibly the third USB return (cf. github.com/Give-me-a-signal/virus-ti-rework: Global USB controller 9)
  and RAM restore on exit; contact with that project pending.

## Status

| Platform | State | Released | Key docs |
|---|---|---|---|
| Windows 10/11 | native plug-in (VST3) + ASIO "TI Revive Virus Audio", WinUSB binding, broker with USB audio, Control Center app | `-setup.exe` (Burn bundle) + `.msi` | native/docs/architecture.md, design.md |
| macOS 14.4+ | native plug-in (VST3 + AUv2, universal, no Rosetta since 0.8.1), broker, Control Center app | `-mac.zip` (pkg + setup scripts) | native/docs/architecture.md, design-macos*.md |
| Linux | native plug-in (VST3), native broker owns the whole Virus (MIDI + iso audio), Control Center app | `-linux.tar.gz` (install.sh) | native/docs/architecture.md, design-linux.md |

Latest release: **v0.8.7** (2026-10-16, presets chosen on the Virus + soft knobs, E150/E152; 0.8.6: start-up waits for the Virus, E149; 0.8.5: one plug-in instance per process, issue #6). The 0.8.x
releases are summarised in the sections above; 0.7.x and older in [HANDOFF-history.md](HANDOFF-history.md).
Virus TI Snow owners stay on **v0.7.4** (classic plug-in) until the native plug-in supports the Snow.

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

**This Linux PC** (Ubuntu 26.04, kernel 7.0, PipeWire; Virus TI Desktop (TI2) on USB; udev rule 99-virus-ti.rules = plugdev):
- TI Revive installed like a user: broker ~/.local/lib/tirevive/vtibroker (copied 2026-10-15 13:44) started on demand
  by the systemd user socket `tirevive-broker.socket` (Unix socket /tmp/vtibroker-<uid>.sock only, no TCP since 0.8.x);
  the native plug-in in ~/.vst3 ("TI Revive Virus Control.vst3", moduleinfo 0.8.3 - older than the release);
  Control Center ~/.local/bin/tirevive-control-center, CLI tirevive-cc. Broker log: `journalctl --user -u
  tirevive-broker`. Access files (the user's): ~/.local/share/tirevive/access/... (Virus Control.dll 5.1.7, sha256
  a622f9a2..., also used by REA); the classic Wine prefix ~/.wine + yabridge are only for the 0.7.x path.
- Development tests without a DAW: `build-native/bin/Release/nativehost "<plugin.vst3>" [--editor] [--seconds N]`
  (DISPLAY=:0 shows the editor on the user's desktop; Xvfb for headless shots), `VTI_NATIVE_DEBUG=1` for the log;
  `build-native/bin/Release/virusmon [s]` shows the traffic read-only next to it.
- REAPER 7.82 portable: ~/.local/opt/dl/reaper/reaper_linux_x86_64/REAPER/reaper; start `pw-jack .../reaper`
  after `pw-metadata -n settings 0 clock.force-rate 44100` + `clock.force-quantum 512` (PulseAudio backend = messy, E122/E123).
- User is in group pipewire (rtprio 95, memlock 4 GB). Ask the user to close REAPER before tests that need the Virus
  (one owner at a time; a second plug-in instance waits).
- Gotcha: never `pkill -f <pattern>` in a tool call whose own command line contains the pattern (kills the shell).
- Gotcha: tools built against build-native/lib must use the headers of the same branch (Broker's layout differs
  between branches -> crash in the reader thread); rebuild vticore after switching branches.

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
