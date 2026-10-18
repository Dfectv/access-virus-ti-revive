# HANDOFF

_Last updated: 2026-10-18. CONTINUE HERE = **Round 0.8.26** (branch round-0.8.26; code change: per-monitor work area on Linux, E227): `72 00 03` safety rule (docs), Mac session mac-0.8.26-1 all pass (E225: no doubled notes in 7 takes, first sync ping answered after every broker idle exit; issue #7 finding 2 needs nothing from us). Virus: was on the Mac for that session (moves back to Linux for the next items). Linux session E226: PresenceWatch bounced branch PASS on hardware, E175 not reproduced (0/12). Linux = release 0.8.25 now. Before: **Released v0.8.25** (merge 95879c7 on main, tag v0.8.25, release CI run
39131364441 green, 6 assets; site rebuilt by hand, run 39131769375, live shows 0.8.25 / 220 tests / original VST3
projects). New: original VST3 projects open (second class with the original's id, E221-E223; VST2 not), Revert to /
Recently Used (E220), Create Empty Library (E217), Windows 10 menu look (E219), plug-in version 0.8.25. Test machines:
win11 VM = release (silent, 0.8.25c uninstalled first, plug-in 13:27 UTC); Linux: release unpacked in
~/Downloads/TI-Revive-0.8.25-release, user runs install.sh (~/.vst3 still dev build 9dcd74f until then); Mac: release
unzipped in ~/Downloads/TI-Revive-0.8.25-release-mac, user runs the sudo installer; real PC still 0.8.24 (0.8.25b setup
in C:\tirevive-test\0.8.25b, release not copied). **Next round: 0.8.26 started (branch round-0.8.26), see below.** Open: Snow original id 1B4C380B.. as a
third entry (only if Snow users ask), E175 (not reproduced in E226, watch), Mac double notes E209 (7) (not reproduced in E225, watch). Not relevant (user 2026-10-18): VST2 projects of the
original (no converter, no VST2 build). Chronological notes: [HANDOFF-history.md](HANDOFF-history.md).
Evidence: [evidence.md](evidence.md) (E001-E232), protocol: [spec.md](spec.md); the native plug-in:
[../native/docs/](../native/docs/architecture.md) (its development log: native/docs/status.md)._

## Round 0.8.26 (branch round-0.8.26, collecting; started 2026-10-18)

1. **Safety rule `72 00 03`** (issue #7 finding 1): never send the Multi parameter "Original Patch"; added to
   AGENTS.md rule 2 and the tirevive-test skill's safety list, pointing at issue #7 and E209 (their PR into
   claims.md is not in yet; point the rule at the claims entry once it is). Docs only, nothing to test.
2. Deleted the stray ignored file `--help` in the repo root (user 2026-10-18; it was a 907x513 PNG screenshot
   from 2026-10-17, written by a tool run by mistake).
3. **Mac double notes E209 (7): log analysis only** (work/timing/baseline-issue7/mac/M2-diag.log, session 3 =
   broker start 08:09:04, 16 s after the previous idle exit). klog K was 6024/6025 (= session 1, which also had
   5566 once), then 6382-6384 for the next 7 notes (+359 frames = 8.1 ms) from stamp ~1029117 on, with "timeline
   steps 0, anchors 0" in the stats: the plug-in did not move its timeline, so the Virus' stamp clock vs its USB 1
   output shifted by itself. Sync reports rose during playback and also with no sound for 20 s (6 at 08:10:06, 7 at
   08:10:18); sessions 1/2 had 0-1. Hypothesis (LOW, not tested): the Virus lost/re-took its sync in that session;
   the doubling itself is not visible in klog (it needs 2000 quiet frames; "no onset" lines are as frequent in the
   good sessions). Next: reproduce on the Mac with a recording the user makes in Live (resample track, no SSH
   permissions needed - user declined sudoers/TCC changes 2026-10-18), then compare onsets with timing_analysis.py.
4. **Issue #7 finding 2: code reading.** A missed ping is visible: after 5 s without a marker the log says "no sync
   marker - time stamps relative to the ring" (scan_audio), then up to 3 retries while quiet and not playing
   (process, marker_retries_). E209 (2) counted 2/2 Mac reopens after an idle exit with the marker on the first
   ping. TI Revive never sends CC120: the plug-in sends CC64 0 + CC123 per part at setActive(false), the broker
   CC64 0 + CC123 on 16 channels at a disconnect (vti_pack_notes_off), and host CCs are not passed on
   (getMidiControllerAssignment maps only aftertouch and pitch bend). So their exact trigger (CC120 + CC123) cannot
   come from TI Revive; remaining check: Mac, broker idle exit + reopen x3, look for the "no sync marker" line.
5. **Mac session checklist for 3 + 4:** https://claude.ai/artifact/Pg1eFZf4Fv7TCkJNmDA7a3 (spec
   scripts/checklists/mac-0.8.26-1.json: D0 recording track, D1 cold start, D2 three reopens after idle exit with
   a 30 s recording each, D3 if doubled: record + Stop/Play, D4 three more reopens). Mac on release 0.8.25 (pkgutil),
   diag.on + klog.on present. Before the user starts: rename the Mac's old diag.log; afterwards fetch diag.log,
   broker log and the recordings (project folder Samples/Recorded), run timing_analysis.py on each take.
   **Done 2026-10-18 (E225): all pass, D3 not needed.** 8 sessions, every first ping got its marker (Mac now 8/8
   reopens after an idle exit); 7 takes 480/480 onsets, 0 extra, jitter 0.16-0.29 ms; klog K constant, 1-2 sync
   reports per session. E209 (7) not reproduced (0/7), cause open: watch for it, no code change. Issue #7 finding 2:
   nothing to fix on our side (no CC120 sent, first ping answered). Files: work/timing/mac-0.8.26-1.
6. **Linux reconnects (E226, 2026-10-18, release 0.8.25 installed on Linux by install.sh):** new
   scripts/linux-usb-bounce.sh (sudo; sysfs authorized 0/1, optional --freeze PID). PresenceWatch bounced branch
   PASS on hardware (frozen host 3/3, 2/3 via the bounced branch; unfrozen 0.3 s drops 3/3, 2/3 bounced too);
   E175 cable 3/3 and power-off/on with a waiting host 3/3 recovered: E175 not reproduced (0/12). Likely why
   (LOW): since 0.8.21 the flash info read (~3 s) comes before the audio start, so the stream no longer starts
   while the Virus boots. E175 stays "watch", no code change.
7. **Per-monitor work area on Linux** (popup menus): _NET_WORKAREA is one box around all monitors (this PC under
   GNOME/XWayland: 67,32 4413x1408 over DP-3 2560x1440 at 0,0 and DP-2 1920x1080 at 2560,223), so a menu on the
   smaller monitor could open where it has no pixels. New native/src/editor/workarea.h (NativeRect moved there;
   work_area_at, parse_work_areas, clip_monitors; 5 tests, 225 in all) and popup_screen in popupwindow_x11.cpp:
   _GTK_WORKAREAS_D<desktop> (mutter: 67,32 2493x1408 and 2560,223 1920x1080) -> RandR 1.5 monitors cut to
   _NET_WORKAREA (KDE, Xfce, ...) -> _NET_WORKAREA -> the screen; the monitor at the anchor, else the nearest.
   Links xcb-randr (libxcb-randr0-dev installed here by the user, added to the CI apt list). Built + tests on Linux;
   dev build in ~/.vst3 (release 0.8.25 backed up in ~/.vst3-backup/0.8.25). **User test in REAPER PASS (E227):**
   menus stay on their monitor, submenu flips at the monitor border, no crash. RandR path untested (GNOME has the
   GTK property). Needs CI build (deps changed) before the round's test build. ctest installer_scripts "Not Run": stale PWSH_EXECUTABLE in the build
   cache (an old scratchpad), unrelated.

8. **Firmware analysis started** (E228-E230, 2026-10-18, user's idea: "decompile the firmware and check it against
   our implementation"). firmware.bin decoded (IFF FORM, 32 KB flash sectors with 3-byte addresses), chips named:
   host CPU = 8051 family with a 7-bit flash page register at XDATA 0x02E0, USB controller = TI TUSB3200A/TAS1020
   family (8052; its firmware F060 holds the USB descriptors and streams the audio), synth = DSP56300 (no
   disassembler, not analysed). USB-MIDI packets: USB chip 64-byte ring, holds the endpoint (NAK) below 32 free
   bytes, 9-bit UART with 4-byte frames to the host; host parks a packet when its ring has < 8 free bytes; cable
   5 (F5 stamps) goes straight to both DSPs, cable 2 to the parser ring and the DSPs, 3 to the parser ring, 0 to
   DIN out. Host rings 1 KB each; full ring = reset + "MIDI Buffer overflow". Tooling: REA cannot open 8051/56k,
   so Ghidra 12.1.4 headless with own scripts; everything in work/firmware/ (git-ignored, Access material).
   REA updated 6.3.0 -> 6.7.0 (setup --client claude_code, effective after a restart). Open: DSP side of a
   parameter flood (rule 2), handshake pins, sync event origin - see the memory note project-firmware-analysis.

9. **Send rates vs the firmware (E231) and a DSP disassembler (E232).** Our 250/s automation limit is far under the
   link's ~4 k packets/s (LOW); the hazard is bursts (one block's events go out in one write of up to 3072 bytes,
   each stamped message = 2 packets) and totals above the link rate, where the USB chip's NAK makes the broker's
   write block and the host CPU's parking rule drops frames. No code changed. DSP56300: the emulator core
   github.com/dsp56300/dsp56300 (GPL-3) has a standalone disassembler (built: work/firmware/tools/dsp56kDisassemble,
   listing work/firmware/out/dsp_main.asm, 126k lines) and the Ghidra module github.com/yatli/ghidra_dsp56k
   compiles with one fix and is installed as a user extension (DSP56K:LE:24:default, byte-swapped image). Next:
   the DSP's host-port receive handler (what happens to MIDI bytes when the host writes faster than the DSP
   drains) - the missing half of the rule-2 hang.

## Round 0.8.25 (released as v0.8.25 2026-10-18)

Same process as 0.8.24 (Linux checks only during the round; test build + one checklist later). In this round so far:
1. **Create Empty Library** (E217, commits a466a8b + 43e15de; user 2026-10-18: "replicate the original as close
   as possible"): entry last under "Personal Libraries" in LOAD, SAVE and the browser's bank menu; opens the
   original's save dialog (title "Save empty library as...", filter "Virus TI Patch Libraries" *.mid, library folder,
   "New Library.mid"), ".mid" added like the original, 26-byte empty SMF written, listed at once. Linux: VSTGUI's
   zenity/kdialog dialog (modal, blocks the host UI like every VSTGUI plug-in; without zenity/kdialog nothing happens).
   Xvfb check passed (dialog, OK, file byte-identical, listed). Windows/macOS: not built locally (CI later).
   Separator done with item 3.
   Checklist item later: LOAD > Personal Libraries > Create Empty Library (dialog, OK), SAVE into it, LOAD from it.
   Dev build NOT installed in ~/.vst3 (release 0.8.24 stays); user smoke test of the dev build still to do.
2. **Issue #7 finding 3 on Linux** (E218): 6 start-ups after broker idle exits with a full state restore, release
   0.8.24: ready 6.7-6.9 s, marker every time, normal sound, no hang (9/9 with E209). No code change.
3. **Menu look like Windows 10** (E219, commit b929bcb): measured in the user's logged-on win10 VM session with our own
   test menu (scratchpad sep/menumeasure.c): menus with columns = the older look we had (19 px rows, separator 9 px);
   single-column menus = the themed look (22 px rows, check band, chevron, separator 7 px). Ours now draws every
   one-column menu themed (LOAD/SAVE top level, Personal Libraries, value lists <= 32, categories) and has separator
   rows; Create Empty Library gets its separator. 217 tests; Xvfb check of LOAD > Personal Libraries (+ greyed
   "(empty)"). Not seen on screen: a checked entry in the themed look. Checklist items later (W especially: compare
   with the original's menus side by side if the user wants).
4. **Project compatibility test** (2026-10-18, with the user): two Ableton Live 11 projects saved with the ORIGINAL
   on the win10 VM (restore-test-vst3.als = original VST3, class id = VTI_CID_ORIGINAL; restore-test-vst2.als =
   original VST2 'VCP ' 0x56435020): parts 1-3 Fimplex HS / Acidia AV / Alead BC, MIDI clip, automation Cutoff part 1
   and Reso part 2 (Easy page). Copies: work/projcompat/ (not in git: contain the original's state), real PC
   C:\tirevive-test\projcompat, win11 VM same, Mac ~/Downloads/projcompat. Linux (nativehost, release 0.8.24): the
   state loads (244 records, part 1 Fimplex HS, getState identical). Checklist
   https://claude.ai/artifact/5qa9fHxYrLiPG1CnMBViVV (spec scripts/checklists/projcompat-1.json: PW1-3, PM1-3,
   PB1-3). Expectation for the VST2 project: "missing" (we declare no VST2 id) - untested.
5. **Revert to / Recently Used DONE** (E220, commit 9dcd74f) + themed menus' 3 px bottom strip. 220 tests.
6. **Live 11 does not open the original's projects** (E221): fix = second VST3 class under the original's id
   "TI Revive Virus Control (original projects)" (commit d58e175, test factory_classes; user: keep two entries for
   now, decide before the release whether to take over the id - after the Live 11 redo and the Live 12 data).
   CI run 39124266237 failed on Windows: my library_create_empty test kept a file open while deleting its folder
   (Windows throws -> abort 0xc0000409); fixed + test runner names each test first (9dcd74f). 0.8.25a (CI
   39124817970) still failed PW1: the "original's id" B49F90D9735F4305.. was the raw TUID bytes, never the FUID;
   the real one is D9909FB45F730543AC5397BE8958CD31 (E221 correction, 2a6d97c; also fixes the moduleinfo entry).
   **0.8.25b (CI 39125467481) on the win11 VM: PW1 + PW2 pass, PW3 (VST2) fails as expected (E222).** Plug-in
   version was still 0.8.23 (also in the 0.8.24 release) -> 0.8.25 (87eef8a) + site-check rule. **Mac (Live 12.4.6,
   0.8.25b installed): PM1 + PM2 pass, PM3 VST2 fails as expected (E223). Real PC skipped (user). Test complete.**
   Site check 2026-10-18 (round items only; full check before the release): "projects of the original open" was
   wrong for 0.8.24 in Live 11 -> now "VST3 projects open (since 0.8.25, second entry), VST2 do not" (site +
   README); TESTS 214 -> 220; menu look, Revert/Recently Used, Create Empty Library added (since 0.8.25). NOT live
   (site from main by hand). Still to do at the release: retake docs/images/plugin-menus.png (old look). User
   recommendation taken: keep two entries. Round checklist plan (user: UI on Linux is enough): Linux core + menus +
   Revert/Recently Used + Create Empty Library; VM and Mac core + Create Empty Library dialog only.
   **Round test build 0.8.25c = CI run 39130131007 (7b49ada). Checklist https://claude.ai/artifact/YV55z8yQ737u5JpLcNAx93
   (spec scripts/checklists/round-0.8.25.json: L1-L3, W1, M1; no core items and no real PC this round - user).
   Plan (user): ship 0.8.25 soon; live site stays until then.**
   **Round results (E224): L1-L3, W1, M1 all PASS. Ready for the release steps:** full site check, retake
   docs/images/plugin-menus.png (themed look), release notes in build.yml, merge round-0.8.25 -> main, tag v0.8.25,
   release CI, site rebuild by hand, install the release on Linux (user runs install.sh), VM (silent), Mac. Linux ~/.vst3 = dev build 9dcd74f (backup of 0.8.24 in
   ~/.vst3-backup/0.8.24). User smoke test of the earlier dev build (menus, Create Empty Library): ok.
win10 VM: 5 crash dumps since 2026-10-10 (bugcheck 0x139 at 13:06 today while Live + the Access driver ran with the
Virus passed through) - the VM is a reference machine only.

Not possible alone (need the user): PresenceWatch "bounced" branch on hardware (a sub-second USB drop: needs sudo
for /sys/bus/usb/devices/1-8/authorized, or a cable pull + replug within ~0.3 s); Linux replug known issue (E175,
needs a physical replug); per-monitor work area on Linux (xcb-randr: libxcb-randr0-dev not installed, needs sudo +
CI apt list); E180 pixel comparison (needs the 0.7.x wrapper rebuilt + installed in ~/.wine, see
native/tools/uicompare/README.md; "Virus TI (TI Revive).vst3" is no longer in the prefix); Mac double notes (E209 (7)).

## Round 0.8.24 (branch round-0.8.24, collecting; new test process 2026-10-18)

Process (agreed with the user 2026-10-18): fixes collect on this branch; I check on Linux only (tests, build, own
TI2 runs, user's short crash smoke test of a dev build); the round's test build + ONE checklist come when ~5-10
changes are in, something users wait for is ready, or the user asks. Checklist = core items from
scripts/checklists/core.json (checklist.py merges them with `"core": "core.json"`: cold start, reopen after broker
idle exit, export, Virus off/on, sleep; ~15-20 min per system) + one item per change on the systems it affects.
Failures go back into the batch; hotfixes only for crash / data loss / broken install.

In this round so far:
1. Sleep: a stall re-sends start-up messages + state like a reconnect (E210); Linux sleep works (E211, via the
   kernel's USB reset). Needs: Windows real PC + Mac sleep (core item C5).
2. StallWatch false alarm on every first connect fixed (E209 (5), test stallwatch_starting_stream_is_no_stall;
   Linux checked E211). Needs: core C1 on all systems (no "audio stood still" line).
3. Docs only (skill stocktake, 9cf52c3): tirevive-diagnose table refreshed (Snow works per E188, "latest release"
   instead of 0.8.0, Driver Extensions fix, replug and sleep rows, 0.7.x buffer row re-sourced to E111/E123);
   tirevive-pr (native first, real evidence.md columns, PR template, maintainer rules); tirevive-site-check
   (core.json / baseline-*.json, full style rules); Snow wording in AGENTS.md and CONTRIBUTING.md. No test needed.
**Fix for LC5 (E213, 2026-10-18): the Virus gone-and-back in 0.27 s at wake, unseen by the worker -> broker client
counts arrivals, PresenceWatch reconnects on a changed count. 214 tests. Dev build in ~/.vst3 (backup of 0.8.24a in
~/.vst3-backup/0.8.24a). L6 (LC5 redo, dev build) PASS (E214). CI test build 0.8.24b = run 39110354589 (version 0.8.24,
green): Linux installed (= CI), real PC setup in C:\tirevive-test\0.8.24b (old 0.8.24a setup removed), Mac zip pushed
+ unzipped in ~/Downloads/TI-Revive-0.8.24b-mac (user runs the sudo installer, then I check pkgutil), VM stays on
0.8.24a (WC1-WC4 passed, path untouched). E215: MC1-MC5 PASS on 0.8.24b (Mac sleep fix confirmed). Real PC ran
0.8.23 (setup never run) -> BC1-BC5 cleared, new item B0 (installed version check). ONLY OPEN before the release:
B0 + BC1-BC5 on the real PC with 0.8.24b (setup in C:\tirevive-test\0.8.24b). Then: site check, merge, tag v0.8.24.**
**Round complete (E216): all 21 items pass. Site check before the release (2026-10-18): tests 208 -> 214; sleep moved
from "Being tested now" (section removed) to the solved list (site + README); release notes "New in 0.8.24"; timing
summaries 0.01-0.04 -> 0.01-0.33 ms std. dev. (user: honest range; this round's exports 0.16-0.33 ms) + 0.8.24 Windows
PC row in both tables. Unchanged: Linux replug known issue (not retested), coverage 94 % (16 Oct).**
**Baseline results (E212, 2026-10-18): NOT releasable yet.** Linux LC1-LC4 pass (false alarm gone), LC5 sleep FAIL:
the stall path (no USB reset this time) sends set-up + state, Virus ready, but no sync marker afterwards -> timeline
never re-anchored, silent. Next: trace in code why no ping/marker follows the stall path (reconnect path E211 gets
one), test first. VM WC1-WC4 pass. Mac ran 0.8.23 (pkg not installed): redo MC1-MC5 with 0.8.24a (or the fix build).
Real PC BC1-BC5 not run: user does a bigger Windows run later.
**Plan (user 2026-10-18): test, merge to main, release 0.8.24, then the next round.**
Test build 0.8.24a = CI run 39105250660 (version 0.8.24, from sleep-reconnect fe7b196; the branch has no code
change since, only docs/checklist tooling, so no new CI run). Installed: Linux (install.sh, plug-in hash = CI), win11
VM (setup.exe /quiet, entry 0.8.24, VM left running, Virus not attached yet), Mac (zip pushed + unzipped in
~/Downloads/TI-Revive-0.8.24a-mac, user runs the sudo installer), real PC (setup.exe in C:\tirevive-test).
Checklist https://claude.ai/artifact/CPe7iSh2gaTduJHBxaXeeG (spec scripts/checklists/round-0.8.24.json = core only:
LC1-5, WC1-4, MC1-5, BC1-5). After all pass: merge round-0.8.24 into main, tag v0.8.24, release CI, site check +
site rebuild by hand, install the release on the machines.
Candidates for the round: Mac double notes after a reopen (E209 (7), watch); issue #7 PR review + `72 00 03` safety
rule when it comes; E180 pixel comparison, E175 (old list).

## Baseline round results (2026-10-18): E209, E210

All systems on release 0.8.23 (Linux installed by the user, Mac pkg installed by the user, Windows real PC).
Logs and renders: work/timing/baseline-issue7/{win,linux,mac}. Results: issue #7 findings 1 and 3 not seen, finding 2
not reproduced (6/6 reopens after the broker's idle exit got the marker on the first ping; E209). Windows catch-up
B4-B7 pass (export 0 ppm / sd 0.04 ms on the real PC). Open, in this order:
1. **B8 Windows sleep FAIL (E210):** after wake the plug-in re-anchors and gets markers, but the Virus stays silent
   until the plug-in closes. The stall path (E208) only raises the generation; a reconnect also sends the start-up
   messages + state. Fix candidate (test first): a stall = a reconnect (start-up messages, state, readiness probe).
   Then sleep on all three systems (Mac M3 of 0.8.23 and Linux L4 never ran with the fix).
2. **StallWatch false alarm on every first connect (E209 (5)):** the starting stream counts as a stall; reset the
   watch when a stream (re)starts. Same branch as 1.
3. macOS double notes after the last reopen (E209 (7)), 9 Sync Information Events; cause open, watch for it.
4. Not done as planned: three reopens per system (1-3 were done); B6 has no file. Repeat with the next round if needed.
Reply on issue #7 with the results once their PR is in (or before, if the user wants).

## Issue #7 (2026-10-18): findings from a separate macOS TI2 project (wirrareka), waiting for their PR

Four hardware reports from their own code (TI2, macOS 26, Apple Silicon), not reproduced by us. Replied twice (personal
+ technical, with E031/E112/E136/E139/E145/E149); asked for a PR into `findings/claims.md`. When it comes: review
(no Access material, claims format), then move what we reproduce to evidence.md. Our side, checked in code only:
1. Multi "Original Patch" write `72 00 03` -> all 16 edit buffers Init at the next power-up (2/2). TI Revive never sends
   it (ParamInit = part MIDI channels + Panel Part only; not in automation; bulk multi dump ignores the byte). To do
   after the PR: a safety rule "never send `72 00 03`" - DONE in round 0.8.26 (AGENTS.md rule 2, tirevive-test);
   still to do after the PR: point it at the claims entry.
2. Panic (CC120 + CC123) before close -> no sync marker in the next session (5/5). We send CC64 0 + CC123 on
   setActive(false), no CC120; Linux broker resets the port on close, macOS/Windows do not; marker retry (3x) would
   hide a missed first ping. Open check (Mac, read-only): first ping after a broker idle exit + reopen answered? If
   not, try without CC123.
3. Heavy restore after a reopen hangs the Virus (full-scale on all six inputs, until a power cycle). Our pacing (E145
   250 ms pause, automation rate limit, one readback E149) should avoid their trigger. Open check (Linux): several
   start-ups after broker reopens in one power-on, watch for a hang. They test pacing and report back.
4. Panel knob drift (Osc1 PW 0 -> 21/22 by itself). Nothing to do; note for unexplained readback differences.

Baseline checklist for checks 1-3, same items on all systems with release 0.8.23:
https://claude.ai/artifact/TsNG9uUTrf9Do1VMR441oz (spec scripts/checklists/baseline-issue7.json; B1-B3 bare-metal
Windows first + catch-up B4-B8 since 0.8.20: menus/REMOTE, export+freeze, aux track, Virus off during export, sleep; then L1-L3, M1-M3). Windows logs: C:\tirevive-test\B*-diag.log on the Windows partition
(nvme2n1p2, mount read-only from Linux after the reboot).

## Previous state (2026-10-17 late: branch round-0.8.23, offline render)

**Released v0.8.23** (tag on main bcec1ce, CI run 39060420044 all green incl. release; assets setup.exe, msi, mac zip, linux tarball, tirevive-usbinfo.ps1, libusb source). Website rebuilt by hand (site run 39060784148), live: Version 0.8.23, 208 tests, offline/sync error help, original-problems section. Test machines: win11 VM release installed silently (0.8.23, plug-in has the sleep fix); Linux: install.sh now asks sudo for the udev rule - user runs it (still the 0.8.23a test build without the sleep fix until then); Mac asleep, release zip in ~/Downloads/TI-Revive-0.8.23-release on this PC, push when awake. Next round: Mac sleep test (E208), L4. Before: **Round 0.8.23 done, release prepared (state 2026-10-17 21:xx).** Results E207 (all pass except M3 Mac sleep FAIL -> fix E208 StallWatch, untested, user: release with it, test with the next round; L4 Linux sleep not tested). Site check before the release: release notes say the Windows test was in the VM; sleep sentence fixed (the failing build was 0.8.23a, not 0.8.22); USB-port advice sourced in E205. Version 0.8.23, 208 tests. Earlier state: **Branch `round-0.8.23` (pushed; commit 8574a72 ...).** CI test build 0.8.23a = run 39057457787 (version 0.8.23; run 39057093527 was 0.0.1 by mistake, discarded); installed: Linux (install.sh), win11 VM (bundle uninstall 0.8.22d + setup.exe /quiet, one entry 0.8.23); Mac asleep, package in ~/Downloads/TI-Revive-0.8.23a-mac on this PC, push when awake. Checklist https://claude.ai/artifact/LC2ixtX9PCrDWfHP5e7cJs (spec scripts/checklists/round-0.8.23.json: L1-L4, W1-W2, M1-M3). Also done: sync error page like the original (E205, SyncAlert, 203 tests; user wanted it before the release); offline render + sync error as features on README/site; REAPER offline render 0 ppm / 0.01 ms (E204). Started from the user's web research on what owners
reported about the original plug-in (community claims C11-C18 in claims.md, with links). Done: (1) offline render
(E203): the original refuses it (warning, canDo offline = no); the native plug-in now paces offline blocks to the
Virus (native/src/device/offlinepace.h, VirusPlugin::wait_offline / report_offline; start wait once per render, anchor
in that block, 1 s timeout per block), 6 new unit tests (196), nativehost --offline T; measured on Linux with the TI2
(1.00x, onsets identical to real time). (2) README + site: section "Problems in the original Access plug-in" with
the community reports and their links (identical texts), "Export and freeze at real-time speed", the stale Windows
"sync error can appear once" line removed (the native plug-in never shows that page: it does not read the Virus'
Sync Information Event 0x54 - open question for the user whether to show/log it), test count 196.
Next: user dev smoke test on Linux (REAPER: render with "Full-speed offline" and "Online render"), then CI test build
and a checklist with: offline render per OS/DAW (Ableton export, REAPER, Logic offline bounce via AU), (1) sleep/wake
with the plug-in loaded (C17), (2) the plug-in on a return/aux track, delay compensation (C18), the 1 s timeout
(Virus off during an export). The site must not go live with the offline line before the release.

## Previous state (2026-10-17 evening: v0.8.22 released)

**Released v0.8.22** (tag on main 5116102, CI run 39051260777 all green incl. release; assets: setup.exe, msi, mac zip,
linux tarball, tirevive-usbinfo.ps1, libusb source). After the tag (user): menus framed as "reworked, modernized, the
same on Windows, macOS and Linux" in README, site and the GitHub release text (edited with gh release edit, only the
menu paragraph), screenshot docs/images/plugin-menus.png (LOAD > device > RAM A, Xvfb with a grey root, the installed
0.8.22d plug-in with the TI2) in README and on the site. Website rebuilt by hand (site run 39052054849), live: 0.8.22,
190 unit tests, "Menus, reworked" with the image. Test machines: Linux and win11 VM have 0.8.22d (same code as the
release), the Mac 0.8.22d (user installed). Virus on Linux. Next round candidates: from the older lists below (E180
pixel comparison, inactive text strength, bare-metal list, E175).

## Previous state (state 2026-10-17 night: round 0.8.22, popup windows done on Linux, CI for Windows/macOS)

**Results 0.8.22c (user, 2026-10-17):** L6 pass, L7 FAIL (Alt-Tab did not close the menu on Linux: no focus token
there -> fixed, _NET_ACTIVE_WINDOW, not tried yet), W4 pass, W5 pass, W3 pass, W2 FAIL ("Knob Name is working but the
dropdown not. Dropdown acts like a textbox." -> E202: the VM's Access DLL is the 32-bit one, our knob template finder
read only 64-bit pointers (0 templates, the arrow opened nothing); fixed (PE32, 35 of 35 identical), not re-tested).
macOS postponed by the user. Evidence now E001-E202. Dev smoke test of both fixes on Linux (Alt-Tab closes, no crash): user "working" -> CI test build 0.8.22d. CI run 39050163917 green: Linux installed (./install.sh as user, ~/Downloads/TI-Revive-0.8.22d-linux), win11 VM 0.8.22c uninstalled + 0.8.22d silent (plug-in 16:40 UTC build), Mac package downloaded to the scratchpad (macOS postponed). Checklist v8: L8, W6.
**Round 0.8.22 COMPLETE (user, 2026-10-17):** L8 pass (Alt-Tab), W6 pass (REMOTE knob templates with the 32-bit DLL), M5, M6, M2, M3, M4 pass on the Mac (0.8.22d). With L3, L5, L6, W3, W4, W5 every item passes. The Virus is on the Mac (VM entry detached, Linux broker socket started). Next: merge round-0.8.22 into main, release 0.8.22 (site check, release notes, website screenshot of a menu beyond the editor). Release 0.8.22 (user: "ship it"): VERSION 0.8.22, release notes in build.yml (menus, REMOTE page), site check 2026-10-17: TESTS 165 -> 190 (site + README), REMOTE page feature line added (E196-E201, L5/W6/M3/M4), Menus block (L6/L8/W4/W5/M5/M6), coverage 94 % (16 Oct) kept with its date, E175 still "being worked on"; menu screenshot for the site still open.

**Done on round-0.8.22 (after 838544a):** items 1 + 2 of the plan below. Popup menus are top-level windows of their
own on all platforms (native/plugin/source/popupwindow.h + _win.cpp / _mac.mm / _x11.cpp): one window per open
menu, never activated (Windows: WS_POPUP tool window, the VSTGUI child's button press bypasses its SetFocus;
macOS: non-activating NSPanel at NSPopUpMenuWindowLevel; X11: override-redirect, _NET_WM_WINDOW_TYPE_POPUP_MENU),
placed in the monitor's work area at the display scale (Windows: GetDpiForWindow, Linux: the host's content scale,
macOS: points). PopupView is now an invisible modal view over the editor (keys; a click into the editor closes) with
a 15 ms timer: submenu wait, a press outside menus + editor closes, a focus switch closes (Windows foreground
window, macOS app active; Linux: none). Submenu delay in PopupCascade (Windows' SPI_GETMENUSHOWDELAY, 400 ms on
macOS/Linux): highlight at once, submenus switch after the wait, reaching the open submenu keeps it. Patch/bank
menus back on our popup on Windows/macOS. Fallback inside the editor: VTI_NATIVE_INLINE_POPUP=1 or no window.
**REAPER crash fixed (dev build, user test 2026-10-17):** choosing a preset from LOAD crashed REAPER (SIGSEGV,
REAPER's main loop called a freed handler: rip 0, gdb on the real desktop). Cause: the menu closed from a timer
callback (Call::later / its 15 ms timer) and stopped timers / destroyed its X11 frames (their redraw timers) there;
REAPER keeps plain pointers to handlers (as in E183). Fix: PopupHost per editor = a pool of menu windows + one timer,
kept until VirusEditor::close; menus hide and reuse windows, a click is handled in the event (no Call::later).
nativehost now aborts when a handler is unregistered inside another handler's callback (VTI_HOST_LAX=1 turns it
off); the flow LOAD > choose (part 16) > reopen (pool reused, RAM B) > desktop click > editor click passes with it.
New rule (user): dev build on Linux first, CI only after the user's OK (memory). User OK on the fixed dev build in REAPER (2026-10-17: "It works") -> CI test build started. Order (user): dev phase = short crash smoke tests only; after CI the Linux CI build is installed first, and the checklist covers all platforms as before (CI run 39048281922 green, version 0.0.1: Linux unpacked ~/Downloads/TI-Revive-0.8.22c-linux/TI-Revive-0.0.1-linux, ./install.sh as normal user (never sudo: it refuses root); run 39048629581 with -f version=0.8.22 for the win11 VM MSI + Mac; checklist v6 same link: L6, L7, W4, W5, W2, W3, M5, M6, M2-M4). win11 VM: 0.8.22b uninstalled with its cached bundle (/uninstall /quiet), then TI-Revive-0.8.22c-setup.exe /quiet (same version number: no reinstall over it, E166); Virus attached to the VM (all interfaces OK); Mac push started (Mac may sleep). Installed: symbol dev build
(build-dbg, RelWithDebInfo, .so.dbg) in ~/.vst3; backup of 0.8.22b in the session scratchpad.
Earlier check on Linux (Xvfb + nativehost with the TI2): LOAD menu beyond the editor, RAM A ->
patches across RAM B keeps RAM A, desktop click and editor click close, Osc 2 list, 200 % editor (menus at 100 %),
editor closed with a menu open (exit 0). 189 unit tests. CI run 39046709950: Windows built, macOS failed (missing
include, fixed); next CI only after the user's dev-build OK.
Open: Linux work area is _NET_WORKAREA (spans all monitors, no RandR); ctest gui_duplicate_editors failed once in
4 runs right after a parallel build (flake?). Next: CI green -> test build, install on Linux/VM/Mac, checklist
items L4 redo + menu items for W and M (the Windows/macOS menus are new code: hover delay, outside click, Alt-Tab /
Cmd-Tab closes, HiDPI placement, a menu near the screen's right edge opens submenus on the left).

## Previous state (2026-10-17 evening: round 0.8.22 paused after the Linux tests)

**Results 0.8.22b (user, 2026-10-17):** L5 REMOTE page PASS (LCD on a double-click, text fields fit); L3 pass (earlier
build). L4 patch/bank menus: loading works but the menu is still not good: bound to the plug-in window; when moving
from RAM A to its patches the pointer crosses ROM W..Z and switches the patch list; moving to ROM A shifts the whole
cascade left over the editor. W1-W3 and M1-M4 not tested yet (paused by the user).
**User decision: our own popup on ALL platforms, but not bound to the plug-in window** (like the original's Windows
menus, which are windows of their own). Next round, in order:
1. Popup menus as their own top-level windows (Windows: a WS_POPUP window, macOS: a borderless NSWindow/NSPanel,
   Linux/X11: an override-redirect window), each hosting a VSTGUI frame with our PopupView/PopupCascade; positioned in
   screen coordinates, clamped to the monitor's work area instead of the editor; the patch/bank menus back to our
   popup on Windows/macOS too (this round they use the system menus, round-0.8.22 a7.. "system menus" commit).
2. Submenu behaviour like Windows menus: a short hover delay before a submenu switches and keeping the open submenu
   while the pointer moves towards it (no switching when crossing other entries on the way, e.g. ROM W..Z).
3. Then redo L4 and W1-W3, M1-M4 (checklist https://claude.ai/artifact/8Qi89czHHyzB79hQa5eSAq, spec
   scripts/checklists/round-0.8.22.json).
Build 0.8.22b is installed: Linux (user ran install.sh), win11 VM (silent), Mac pushed (not installed).

## Previous state (2026-10-17 evening: round 0.8.22 test build)

**Test round 0.8.22:** first results L1/L2 FAIL, L3 pass -> fixes (system menus on Win/macOS, Linux parent shift, LCD on template choice, edit field size); test build 0.8.22b = CI run 39044254597 (installed: win11 VM silent; Mac pushed ~/Downloads/TI-Revive-0.8.22b-mac; Linux unpacked ~/Downloads/TI-Revive-0.8.22b-linux, user runs install.sh); order this time Linux, VM, Mac; checklist
https://claude.ai/artifact/8Qi89czHHyzB79hQa5eSAq (spec scripts/checklists/round-0.8.22.json: macOS M1-M4 incl. the
Wavetable delay re-check, Linux L1-L3, win11 VM W1-W3). Next: install on Linux (unpack, user runs install.sh), win11 VM
(silent), Mac (push, user runs the installer), then the user's results; then merge + release.

## Previous state (2026-10-17 late afternoon: round 0.8.22 started)

**Branch `round-0.8.22` (not merged, no build yet, 171 tests):** (1) patch/bank menus in our own popup (E195):
PopupCascade (native/src/editor/popupmenu.*) with submenus - hover/click/Right/Return open, Left/Esc close the
deepest, left of the parent when no room on the right; LOAD/SAVE and the browser's bank menu use it, VSTGUI's
COptionMenu and the generic Linux menu theme are gone from the editor. Value lists now use the original's column rule
(entries / 4 above 32 entries, REA 0x180040f50) instead of 25 for all. Checked under Xvfb with the TI2 (LOAD > device >
RAM A 6 columns, Personal Libraries, browser bank menu, choice ROM A). Open: the original's "Create Empty Library"
entry; n of the original's patch submenus. (2) The Mac Wavetable delay re-check (655cf8c): user decision - with the
next major release test round, not now. Item 3 (REMOTE page) started 2026-10-17, user decision: editor first, NO sending. REA done (E196: record
layout, the original's cache file and when it sends). Done on round-0.8.22: full template model (records, edits,
cache file byte-identical to the original's), our RemoteSets file next to the bank cache (import of the original's
Windows cache once), Template Name / Knob Name editable, MIDI Channel Behaviour menu works, saved after each edit;
checked under Xvfb with the TI2 (rename, knob name, flag, reload after restart). MIDI String editable too (the original's
encoding, E196; Xvfb: "b0 7 x" -> "B0 07 *"). Nothing is sent to the Virus. Knob Template menu done (E197: read
from the user's binary at runtime by shape, Windows DLL and Mac binary, 35 entries; a choice fills the MIDI String).
MIDI String Info and Load/Save Template File
done (E198: *.vrt "RMOT"/"RSET" format, LOAD into the selected template, SAVE / Save as new; Xvfb-checked). The
REMOTE editor is complete except sending. Joint session 2026-10-17 (E199): capture of the original choosing/editing a
template (double-click chooses; Remote Patch 73 00 6C + 33 records on cable 3; no answer) and the Virus' own
Transmit MIDI Dump "Remote Patches" (16 factory-like knob records of template 0, no "TEST" from the original's
edit). User decision: send like the original. Done on round-0.8.22: choose (double-click) = Remote Patch + 33
records, edits = their record, LOAD = 33 records, cable 3; both template places in LOAD/SAVE (E200); hardware test
passed (E201: byte-identical to the original, Virus fine, its own dump unchanged). Still open: what the Virus does with
the records (same unknown as with the original) (a capture of
the original choosing/editing a template in the win10 VM with the user, then a test the user approves).

## Previous state (2026-10-17 afternoon: v0.8.21 released)

**Released v0.8.21** (tag on main 5aaef09, CI run 39031771705 all green incl. release; assets: setup.exe, msi, mac zip,
linux tarball, tirevive-usbinfo.ps1, libusb source; website rebuilt by hand, run 39032109744, live with 0.8.21 and 165
tests). main = round-0.8.21 (fast-forward). Installed: win11 VM release setup.exe silent (one entry 0.8.21); Linux
unpacked ~/Downloads/TI-Revive-0.8.21-release/TI-Revive-0.8.21-linux (user runs install.sh); Mac pushed
~/Downloads/TI-Revive-0.8.21-mac (user runs sudo installer). Virus on Linux. No site/README feature lines for the
popup menus / INFO box (user: parity fixes). Next round candidates: re-check the Mac Wavetable delay with the repaint
fix; patch/bank menus (submenus) in our own popup; REMOTE page sending/SAVE (E193); E180 pixel comparison; inactive
text strength; bare-metal list for one session.

## Previous state (2026-10-17 noon: round 0.8.21)

**Branch `round-0.8.21` (pushed, NOT merged; CI test build run 39018067599 all green, Pester tests ran on Linux + Windows runners; checklist https://claude.ai/artifact/AQAVVmwAfEjjdpu6FxXW8A; installed: win11 VM setup.exe 0.8.21 after Claude's W1 checks, VM shut down; Linux unpacked in ~/Downloads/TI-Revive-0.8.21-linux (user runs install.sh); Mac asleep: ~/Downloads/TI-Revive-0.8.21-mac.zip, push with scripts/mac.sh):** (1) E186 fixed: tirevive-bundles.ps1 (MSI
deferred CA, only without setup.exe = TIREVIVE_BUNDLE) removes stale TI Revive Burn bundle registrations; Pester tests
in src/installer/tests, run by ctest `installer_scripts` (skipped without pwsh + Pester 5; CI windows job installs
Pester; locally pwsh 7.4.6 from the scratchpad: cmake -DPWSH_EXECUTABLE=...). Cause read from the bare-metal
Windows registry/Burn logs (read-only mount). (2) E13: -Wall -Wextra on our sources, SDK/VSTGUI/pugixml headers as
system headers, -Werror in the native-linux CI job (VTI_WERROR); 2 warnings fixed. (3) osc-wavetables merged (E180);
Mac bundle path unit test; the table is found in the real 5.1.7 Mac binary (same as Windows). 143 vtitests.
Not done: E180 pixel comparison with the original (needs the original's editor at known OSC settings - test round,
with the user); E187 earlier crackle and E175: nothing to do until they show up again. Next: the user's checklist (Linux L1-L2, VM W1-W2, Mac M1-M3, bare metal B1-B2), then merge + release 0.8.21
(bump project VERSION in native/CMakeLists.txt). Issue #4 closed (E188).

**Round 0.8.21 work after the comparison (2026-10-17, branch round-0.8.21, pushed, 157 tests; user decisions: OS
version read from flash = yes, broker version in the driver rows, REMOTE page now (read-only first), keep our help
mode, plain menus first):** DONE: menus anchored + 25/column + right click through the ring (E189); out-of-range
values clamped (Bands "32"); INFO box: website text, "TI Revive <v>", broker version (VTIMSG_INFO, make VERSION=),
Virus OS read by the broker with the original's fixed read-only flash sequence (flashinfo.h, VTIMSG_FLASHINFO, owner
only, cached; tested on the TI2: "5.1.7.00", E191; skipped for brokers without INFO); HyperSaw display with the
original's rule, pixel-exact (E192); REMOTE page read-only, pixel-exact, nothing sent (E193; the original sends 0x13
records while browsing - not done, needs analysis + hardware test); FILTER LINK and page tabs: no difference
(E190; tabs identical). NOTE: commit 1d4ec04 did not build, fixed in 5492ec7. OPEN: REMOTE knob names/MIDI strings
and template files ("Personal Remote Templates"), sending/SAVE; inactive text strength (original Delay Clock text
105 vs ours 149, Reverb 186-205 vs 161), envelope graph ends, filter curve, saw edge anti-aliasing; then the
checklist. Tools: scratchpad probes (init.state with both checksums, diff.py/regions.py for orig vs vtirender),
captures/remote-page.pcapng (untracked). Second test build: CI run 39027398548 (all green); checklist (same link,
version 3) https://claude.ai/artifact/AQAVVmwAfEjjdpu6FxXW8A; installed: win11 VM setup.exe silent (one entry);
Linux unpacked ~/Downloads/TI-Revive-0.8.21b-linux (user runs install.sh); Mac unpacked ~/Downloads/
TI-Revive-0.8.21b-mac (user runs sudo installer). The Virus is on Linux; the win10 VM still has the original +
Access driver (snapshot before-original-driver = with TI Revive 0.8.17).

**Round 0.8.21 second build results (user, 2026-10-17):** Linux L4, L5 pass; VM W2, W3 pass; macOS M1, M3 pass.
L3/M2 FAIL = the Wavetable list is one long column on Linux and macOS: VSTGUI's columns (setNbItemsPerColumn) only
exist in its Windows menu (macOS NSMenu has none, the Linux menu ignores it) - needs our own popup view (the user's
"modern menu" idea), next step. DONE (2026-10-17, not in a build yet): our own popup (E194; native/src/editor/popupmenu.* +
PopupView in viruseditor.cpp, unit tests popup_*, 164 tests) for value lists, category filter and size menu on all
platforms: the original's Windows look measured from the screenshots (classic style with columns), 25 per column, at
the control's corner, inside the editor (moved up/left; more columns if taller), keys + Return + Esc. Checked under
Xvfb with nativehost (Wavetable 4 x 25, Osc 1 Mode, Return, size menu at 200 %; the editor needs a Virus to leave the
status page - a temporary local hack, not committed). The patch/bank menus (submenus) are still VSTGUI's native/generic
ones. Open: Esc and the Windows/macOS look in the next build; the "modern" look as an option (user idea) later. Third test build (0.8.21c): CI run 39030095040; checklist version 4 on the same link (redo items with new IDs: M4,
M5, L6, L7, W4; order macOS first, then Linux, VM; bare metal postponed). Installed: Mac pkg pushed (~/Downloads/TI-Revive-0.8.21c-mac,
build 39030095040 = version 0.0.1, same code); Linux and VM from run 39030413663 (version 0.8.21): Linux unpacked in
~/Downloads/TI-Revive-0.8.21c-linux, win11 VM setup.exe silent (one entry 0.8.21), Virus attached to win11. NOTE: after a
VM test always `VM=win11 scripts/vm.sh detach` - the Virus stayed in the VM after the last round and REAPER found nothing. RESULTS 0.8.21c (user, 2026-10-17): all pass - M4, M5 (macOS), L6, L7 (Linux), W4 (VM). Note M4: the Wavetable list
opens with a short delay on the Mac (likely: PopupView invalidates the whole frame-sized view on open and on every
hover, so the editor below repaints fully; plus 100 CoreText measurements) - idea for the next round: invalidate only
the menu / the two changed rows. Virus back on Linux (detached). Repaint fix done (655cf8c, popup_repaint_areas; Xvfb: a hover repaints 2 rows
instead of 907x513), not hardware-tested (Mac delay to re-check in the next round). Release 0.8.21 (user: "fix the
repaint first, then release"): VERSION 0.8.21, release notes in build.yml, 165 tests in README + site. Site check
2026-10-17: TESTS 165 updated; COVERAGE 94 % (16 Oct) kept with its date (not re-measured); E175 "being worked on" still
open (not seen since 0.8.18); Snow matrix ok (E188); no new feature lines on the site/README yet (popup columns, INFO
box are parity fixes - ask the user). L5 notes: the link must point to our website -> done (sysinfo website_url, click
opens the browser; not yet in a build); the REMOTE MIDI channel menu is locked on purpose (read-only page). Bare
metal (B1, B2): postponed by the user - collect a bigger bare-metal list for one session later.

**Round 0.8.21 POSTPONED (user, 2026-10-17 noon): W2 failed (Wavetable list), then a full comparison with the
original.** Reference: the original Virus Control 5.1.7 in the win10 VM with the Access driver (TI Revive removed
there, snapshot before-original-driver), 21 screenshots of every page taken with virsh screenshot (scratchpad only,
never in the repo; editor at screen offset 278,207, our vtirender output matches pixel-exact where equal). Our pages
rendered with the Init patch (scratch init.state: init_single upgraded, both checksums) differ only where listed:
- identical (apart from patch names/LCD): LFO, MATRIX, ARP, COMMON; FX 1 only LCD.
- popups: the original anchors them to the control - Menu at its top-left corner (Wavetable 296,146; Category
  722,54), HierarchicMenu below it (bank list at 212,73 under the 212,50 23 px bar); ours opens at the mouse.
  Wavetable list: opens on RIGHT click on the table name (user), 4 columns x 25; ours: the MaskedKnob ring
  (WaveTableIndex) covers the menu in hit_test, a click never reaches it (scratch probe). Rule for columns and
  right-click: REA open. User wants a more modern popup look as an option.
- HyperSaw display (OscHypersawShape) not drawn: lines = Density (ring), spacing = Local Detune (0 centre, 127 full
  width); exact rule: REA (KOscHypersawShape, strings 0x180160e20).
- PATCH UTILITY INFO: URLButton text (valuelist "Access Website List") not drawn; version fields empty. Original:
  Control version = its built-in string (FUN_180078060); TIOS = read from the Virus' flash (FUN_1800ac7c0: blocks at
  0x7fffa0/0x40ffa0 + 0x400009, "no TIOS" fallback) - our broker blocks vendor flash requests on purpose (E177,
  ctrlguard): user decision needed (narrow read-only exception or "-"); driver rows: user decision (broker version?).
- Vocoder Bands: Init stores 127 (range 0..31); original shows the clamped "32", ours "127".
- FILTER LINK: original on with Init, ours off (pixel-exact with Filter Link = 1); LFO1/2 Mod Link off in both. Where
  the original sets it: REA open (7 refs to 0x180163cd0; start-up FUN_18006ed70 touches all four links per part).
- FX 2: Delay Clock and Reverb Mode look inactive in ours, active in the original (not analysed yet).
- FILTER page: envelope graphs (release end / sustain) and the filter response curve differ slightly; Classic saw
  edge anti-aliased in the original (E180 pixel work).
- REMOTE page (remote templates): not implemented in ours (empty list, "0" for MIDI Channel Behaviour).
- Help "?": original opens a menu (Setup Guide, Documentation, Video Tutorials, Access homepage, Create timing
  profile, Contact support); ours opens the help mode.
Our renderer stays the measuring tool (vtirender + scratch init.state); a new checklist follows the fixes.

## Previous state (state 2026-10-17 09:05: v0.8.20 released)

**Released v0.8.20** (tag on main 31af3e9, CI run 39011780610 all green incl. release; assets: setup.exe, msi, mac zip,
linux tarball, tirevive-usbinfo.ps1, libusb-1.0.30.tar.bz2 (LGPL source); website rebuilt by hand, run 39012105808,
live with 0.8.20). main = round-0.8.20 (fast-forward). Round 0.8.20 passed on all four systems (macOS, Linux, win11
VM, bare-metal Windows 11; B6/B7 large buffers fine, E187). Posted the short findings update on
Give-me-a-signal/virus-ti-rework#5 (comment 6230987764). Installed: win11 VM (release setup.exe, silent; VM shut
down); Linux: release unpacked in ~/Downloads/TI-Revive-0.8.20-linux (user runs install.sh; test builds renamed
*-testbuild); Mac was asleep: ~/Downloads/TI-Revive-0.8.20-mac.zip on this PC, push with scripts/mac.sh when awake.
Checklists: scripts/checklist.py + scripts/checklists/round-X.json (user wants one before every release).
**Site/README check (2026-10-17, skill tirevive-site-check):** Windows timing row was E143 (0.8.0 test1, real PC,
304/304, <= 0.66 ms upper bound) -> now E186 (0.8.20 bare metal: 224/224, sd 0.24 ms, max 2.09 ms, 2 ppm); sidechain
"not yet on a real Windows PC" removed (B1 passed on bare metal); site limit "USB input not fed by the plug-in"
removed (sidechain since 0.8.14). Pushed (ac7fa87), site rebuilt (run 39014607805), live checked.
**Next round (0.8.21) candidates:** the stale "TI Revive 0.8.0" uninstall entry on bare-metal Windows (E186); the
earlier crackle at large buffers not reproduced (E187 open part); osc-wavetables branch (pixel comparison, macOS
bundle check); E175 not seen this round (capture lines in place); E13 warning flags.

## Previous state (state 2026-10-17 morning: round 0.8.20 built for the hardware tests)

**Round 0.8.19 macOS results (user, 2026-10-17):** M1 pass; M2 FAIL (Control Center refused while Ableton ran;
backup "permission denied"); M3 FAIL (duplicate looked usable). Causes: E181 (POSIX broker: first client of any kind
owned the Virus; waiting instance showed the normal page since 0.8.18) and E182 (~/Documents/Access Music made by
the Access installer as root). Round 0.8.19 is not tested further; its fixes all went into round 0.8.20.
**Branch `round-0.8.20` (from round-0.8.19, pushed, NOT merged):** src/broker/owner.h (one owner rule for both
brokers: first plug-in owns, plug-ins of other programs wait, every other client shares; POSIX decides at HELLO),
StatusChange (devicestatus.h) for the waiting instance, backup error with the way out, postinstall + setup .command
chown the root-owned Documents/Access Music entries, README/package readmes "Control Center works next to the DAW",
THIRD-PARTY-NOTICES.txt in every package + libusb 1.0.30 source attached to releases (user decision: keep MIT),
plug-in version 0.8.20 (CMake). TDD, 139 tests. CI test build: run 39003702169. Checklist (all 4 systems, the fixes
on each): https://claude.ai/artifact/RntLcBgj3aAifV54cNompD; installed: Mac unpacked in ~/Downloads (user runs sudo installer), Linux unpacked (user runs install.sh), win11 VM setup.exe silent (0.8.20, notices in the install folder). The user's Mac folder is left
root-owned on purpose (M1 checks the postinstall). After the tests: merge, site rebuild, release 0.8.20.

**Round 0.8.20 progress (2026-10-17 morning):** macOS M1-M4 pass; Linux L1, L2, L4 pass, L5 FAIL -> fixed (E183,
REAPER crash at a duplicated track: VSTGUI run loop adapter replaced by a second instance/editor; user: "reaper is
working now!"), no status page without a broker -> fixed; win11 VM W1-W4 pass, Control Center without the Access
look (E184, setup fixed, verified in the VM), Control Center window cut off/too big at start and Linux crash on
closing (E185, fixed; Linux verified in Xvfb). New: end-to-end GUI tests via ctest (gui_test.sh, xvfb; the duplicate
test skips without Access files, e.g. CI), CITATION.cff + "Using our findings" (attribution request). Second build
CI run 39007000903 (green) installed: Mac ~/Downloads/TI-Revive-0.8.20b-mac (user runs installer), Linux
~/Downloads/TI-Revive-0.8.20b-linux (user runs install.sh), VM setup.exe silent. Checklist (same link) has redo items
M5, L6, L7, W5, B5 - all PASS (user, 2026-10-17: macOS M1-M5, Linux L1, L2, L4-L7, VM W1-W5; L3 not needed,
E175 did not occur). Bare metal B1-B5 PASS (E186: render 224/224 on the grid, the VM's
gaps were the passthrough; packets clean; replug recovers). User question: crackling with a sound card at buffer
2048/4096 -> E187: 4096 re-anchored every block in the first seconds (fixed test-first, 140 tests), 2048 on Windows
not measured yet. User chose: investigate 2048 before the release. Third build CI run 39010110029 (green): the 4096
fix + diag.log line with host rate and block sizes (also with the editor closed). Checklist items B6/B7 (same link):
the user plays at 2048/4096 on bare metal; afterwards read C:\Users\<user>\AppData\Local\virus-ti-native\diag.log
from the Windows partition read-only (udisksctl mount -o ro). Then release 0.8.20. The Virus is in the VM. After merge: post the short findings update
on Give-me-a-signal/virus-ti-rework#5 (draft approved by the user: scratchpad rework-update.md - pointer to
findings/evidence.md, no credit request).

## Previous state (state 2026-10-16 late night: round 0.8.19 built, hardware tests 2026-10-17)

**Branch `round-0.8.19` (pushed, NOT merged; main has 3 local HANDOFF/findings commits that are also on this branch):**
CI test build 0.8.19 = run 38962757365, all 7 jobs green (packages say 0.8.19; the plug-in itself still reports
0.8.18 - project(VERSION) in native/CMakeLists.txt is bumped at release time). Contents, all TDD (134 tests): (G) both
brokers refuse every control request except standard reads (src/broker/ctrlguard.h, E177); (A) E175 capture: IN
packet-size statistics + "out of range since N ms" log lines on Linux and Windows (src/broker/pktstat.h; journal
analysis E176: stuck sessions get exactly 2815 good packets after each start, then only wrong sizes; the Access driver
has no recovery for the Virus either, E178); (B) scripts/vm.sh attach stops the Linux broker first (E179: the error 31
handover was the broker reopening the Virus while libvirt attached it); (C) greyed controls already ignore clicks
(code since 8e7077c, no change); (E) Linux library folder from the XDG documents dir (~/Dokumente on this PC - the
factory libraries there were never found before), install.sh same rule, README. E13 (compiler warning flags) skipped
on purpose. Installed: win11 VM (MSI 0.8.19 silent, VM shut down); Linux: unpacked in ~/Downloads/TI-Revive-0.8.19-linux
(user runs install.sh, needs sudo); Mac was unreachable - ~/Downloads/TI-Revive-0.8.19-mac.zip on this PC. The Virus is
on the Mac. Checklist (4 blocks: Linux, win11 VM, Windows 11 bare metal on this PC, macOS):
https://claude.ai/artifact/1gKwV9ZEiB8JiZ6GpzgFLj (2026-10-17: macOS first; Mac unpacked in ~/Downloads, win11 VM running with setup.exe 0.8.19 installed over the bundle). Bare-metal results go to a folder tirevive-0.8.19 on one of the
NTFS data disks (mount read-only from Linux afterwards). After the tests: read the broker journal for the new packet
lines (L2/L3), merge, README/site, release 0.8.19 (bump CMake version).
**Branch `osc-wavetables` (pushed, from round-0.8.19, NOT in the test build):** (D) classic OSC display with the
original's 64 waves (read at runtime by content) and mix (E180); vtirender images look right; open: pixel comparison
with the original, macOS bundle check. Next round.

## Previous state (2026-10-16 night, after the 0.8.18 release)

**Released v0.8.18** (Latest, CI run 38953060428, all jobs green; website rebuilt by hand, run 38953633854, live with
0.8.18 and the new screenshot docs/images/plugin-help-inactive.png): full project recall (E174), the "No Virus TI can
be found" page (E173), help notes for greyed-out controls. Round 0.8.18 passed on Linux (REAPER), win11 VM (Ableton 11)
and macOS (Ableton Live 12); known issue E175 (Linux only so far: silent after a cable replug / power-on with no DAW
running, a power cycle with the DAW running fixes it) in the README, site and release notes. 127 unit tests, vticore
coverage 93 %. Installed: win11 VM (release, /repair over the test build of the same version); Mac and Linux: release
unpacked in ~/Downloads (test builds renamed *-testbuild), the user installs (Linux needs sudo while the Virus is on
the Mac). The Virus is on the Mac.

**After the release (main, not in a release yet):** help mode framed an empty box next to the ? - the display-only
"Global Section Lock Indicator" got a frame since 0.8.18 includes disabled controls; its note removed (commit 0b12af5),
website screenshot corrected. Ships with the next version. Coverage pass (2026-10-16, commit 37011e9): test_decode got
decode_describe_with_model (hand-written model: Nerd mode's parameter names, global/part/action pages, CC) and the
truncated-message fallbacks; decode.cpp 90.2 -> 99.4 %, CI-like vticore 93.5 -> 94.1 % of 2696 lines (97.0 % with
header lines merged over the test objects); no file under 90 %. README/site: 128 tests, 94 % (site not rebuilt). Left
uncovered on purpose: controlcenter "did not answer"/"incomplete" (would wait out 3 x 3 s MemorySync retries) and
diag.cpp filesystem/platform branches. Dead-code pass (commit 9a2edd7): a separate build with -Wunused found only two
unused locals (editorrender.cpp osc_classic, nativehost.cpp), removed; no unused vti:: functions (checked against the
plug-in, CC app, tools, tests and the ASIO/AU sources). The native build itself has no warning flags (option for later).

**Next round:** (1) E175 on Linux: capture the IN stream in the stuck state (E112 pattern?), compare the broker's
open/stream timing with macOS and the VM (the VM got the Virus late and recovered), REA: the original's start-up;
(2) DONE: greyed controls already block clicks (see round 0.8.18 notes); (3) Linux -> VM handover left the
Virus answering error 31 until a power cycle (give-back reset?); (4) bare-metal Windows: the real PC is THIS PC
booted into Windows (user, 2026-10-16) - test everything possible in the win11 VM first, then one self-contained
bare-metal session (no Claude during it): sidechain, timing render (E141 gap pattern), reconnect without libvirt
re-attach (E175 on Windows?), ASIO latency; save logs/renders where Linux can read them. Bench against the original:
plan in [bench-original.md](bench-original.md) (stage B needs Windows 10 + the Access driver; that PC runs
Windows 11 - blocked; PARKED by the user 2026-10-16, curiosity only, not
part of "complete"; until then no performance comparison with the original anywhere).

## Round 0.8.18 notes (branch round-0.8.18, merged)

**Round 0.8.18 (branch round-0.8.18, TDD, not merged):** (1) disconnect page: the plug-in now sets SystemStatus/Error
and the three "Internal System Info/... Communication" values from the broker's presence (model/devicestatus.h, E173);
Linux TI2: Virus attached to the VM -> "No Virus TI can be found" page, back -> "Virus reconnected", ready after 3.9 s.
(2) recall bug found and fixed (E174): the state's part dumps were format 9, the Virus converts them and resets 20 newer
fields (Input Mode, unison, env 3/4, arp mode, chorus type ...); now upgraded to 12 on load and in the default state;
hardware per-field test all TAKEN. Projects saved before keep the Virus' converted values. (3) help notes for inactive
controls: Enabled="false" ("Inactive now: Chorus Type is Off.") and greyed by the parameter's own condition ("depends
on Input Mode, Input Atomizer."), Xvfb screenshots with the TI2. 127 unit tests, vticore coverage 93.2 % of 2768 lines
(changed files 94-98 %). Open for the user's test round: all three on Linux/macOS/win11, REAPER default pins 3/4 ->
Sidechain (E171), Windows sidechain on a real PC (no answer yet whether one exists). Test build 0.8.18 (CI run 38945354060, all green): installed on Linux and win11
(setup.exe on the Desktop), Mac unpacked in ~/Downloads (user runs sudo installer). Checklist: https://claude.ai/artifact/41uVXA5BRHVExjWpex23Ee. User test (Linux, in progress): disconnect page ok with power, but after a USB cable replug and after a
power-on with REAPER closed the Virus stayed silent on USB (MIDI fine) until a power cycle - E175, cause open, next round.
Recall confirmed: after that power cycle the reopened project played ARP Up.
Linux results (user, 2026-10-16): all pass except (a) tabs locked on the "No Virus" page = original behaviour (the tab
buttons exist twice, Enabled="false" under "Any Communication Error"; checklist expectation was wrong), (b) E175 twice,
(c) Chorus Type "Classic" in a new project = the Virus' conversion of the original's v9 Init patch (0.8.17 showed
"Off" from the wrong positions), (d) the user had pointed at the ENV FOLLOWER's Input menu (two menus labelled INPUT, checklist wording
fixed); on the INPUT box's Input select the "Inactive now: depends on Input Mode, Input Atomizer." note appears -
pass. Checked 2026-10-16 (code): greyed controls, menus included, already ignore clicks since 8e7077c (viruseditor.cpp, the param_active check runs before the menu branch), like the original under Wine (native/docs/status.md 2026-10-13); the "clickable" note came from the ENV FOLLOWER menu, which is not greyed. Hardware check in round 0.8.19. REAPER pins 3/4 -> Sidechain by default: pass (E171 closed).
Windows results (win11 VM, Ableton 11, user 2026-10-16): all pass, incl. USB cable replug (the VM gets the Virus only
on Claude's re-attach, seconds after it booted - supports the E175 hypothesis "stream starts while the Virus boots"),
recall (ARP Up, Unison 8 heard after power cycle), help notes, and the sidechain in Ableton (VM; real PC still not
available). Note: Chorus Type changed while a key is held applies from the next note - the Virus' own behaviour (user:
the hardware panel does the same), not a bug.
macOS results (Ableton Live 12, user 2026-10-16): all pass, incl. the USB cable replug and the power-on with the DAW
closed - so E175 (silent after replug / power-on) shows only on Linux so far. Round 0.8.18 is through on all three
OSes; open for release: README + site (disconnect page, full recall, help reasons; screenshot), then the release. Handover Linux -> VM: the Virus answered every WinUSB transfer with error 31 until a
power cycle (+ re-attach, the VM keeps the old USB address) - possibly the Linux give-back reset or E175. Note for the user: the original's
diagnostics page advises holding ARP EDIT while powering on - effect unknown (rule 2: we do not advise it).

## Previous state (2026-10-16 evening, after the 0.8.17 release)

**Released v0.8.17** (Latest, CI run 38939098874, all 7 jobs green; website rebuilt by hand, run 38940119942 - a CI-made
release does not start site.yml): sidechain input to the Virus (3 Outs / 1 Input; REAPER: Sidechain bus, Ableton: main
input, E167/E168/E171/E172), help mode behind ? (helptext.cpp), Linux reset guard as a tested header (E136), CI job time
limits. Tested in round 0.8.14-0.8.16 (checklist artifact https://claude.ai/artifact/8zqCEtreSPUbRGw86XQNc7): Linux all
pass, macOS all pass incl. Logic Pro AU, Windows (win11 VM) smoke + help mode pass, sidechain on Windows untested on a
real PC. 119 unit tests (111 run, 8 need Access files), vticore coverage 93.6 % of 2704 lines (sidechain 96.7 %,
helptext 96.1 %). Installed: win11 VM (0.8.17), win10 (0.8.17, no longer a test machine); Linux and Mac: packages
unpacked in ~/Downloads, the user installs (Linux needs sudo while the Virus is on the Mac; Mac: sudo installer ...).
The Virus is on the Mac.

**Next round (TDD, test rounds per [[release batching]]):** (1) disconnect page: the plug-in never sets "Internal
System Info/USB Communication" - look up in the original (REA) when it does; (2) recall bug: a reloaded project does not
send part 1's Input Mode (and maybe more parameters outside the part dumps) to the Virus (E172); (3) help notes also for
inactive controls (Chorus Type Off), with the reason; (4) sidechain on a real Windows PC with Ableton; (5) REAPER: check
that the stereo-only buses (0.8.15) map 3/4 -> Sidechain 1/2 by default (E171).

**0.8.12 RELEASED 2026-10-16** (Latest, CI run 38910873523, all 7 jobs green; setup.exe in Downloads on win11 + win10; Mac was unreachable - push the mac zip later): the sharper editor images + Linux reopen crash and zoomed menus fix (skins branch). Built + tests on Linux before tagging. 0.8.11 was only a CI test build (no tag).

**Released:** v0.8.10 (Latest, CI run 38890324901, all 7 jobs green, 2026-10-16): reconnect waits for the Virus and
finds a new marker (E163); the queue waits for a Virus that is not there yet (DAW started first, E166); Windows: one
plug-in owner across programs, the second DAW shows "Virus unavailable" and takes over (E166); Out 3 in "3 Outs /
1 Input" (E164). Tested: Linux (TI2: reconnect, absent start), Windows VM (Ableton + REAPER). v0.8.9: the third USB
output (E159, Linux/Windows/macOS). v0.8.8: soft knobs on the Virus change the sound. v0.8.7: presets chosen on the
Virus. All work branches merged into main (106 unit tests; CI-like vticore coverage 94.9 % of 2507 lines, measured 2026-10-16 after the encode_into/library/load-error tests, 98 run / 8 need Access files; CI runs them on Windows and Linux, README and site say so).
Two programs on macOS: tested 2026-10-16 (Ableton 12 + Cubase 15, E166). Open: the Virus' USB input in mode 2
(the plug-in sends no audio; the original sends its Sidechain bus there, E167; implemented on branch `sidechain` for Linux, works on the TI2 with nativehost, E168 - next: a REAPER test with a real sidechain route, then Windows (ASIO driver writes the same ring) and macOS (no output to the Virus yet)), the Snow reports on #4.

**Sharper images (merged into main 2026-10-16, CI test build 0.8.11 = run 38901544137; user tests: Linux REAPER, macOS and Windows 11 "works!"; ships with 0.8.12):** option A of the skin question:
sharper editor images on scaled displays, computed at runtime from the user's own Access bitmaps (Lanczos-3,
`native/src/editor/upscale.*`, 3 tests; editor integration in viruseditor.cpp, see native/docs/architecture.md).
Built + 102 tests on Linux; checked under Xvfb at VTI_NATIVE_ZOOM=2 and 1.5 (sync, Easy, Filter pages) and with
forced tiles (VTI_NATIVE_TILE=1000: identical pixels). NOT built on Windows/macOS (D2D/CG paths untested), not
tested in a DAW or on a real HiDPI screen. User decision 2026-10-16: we support it (goes into 0.8.12; CI test build 0.8.11 still to do - pushing the branch needs
the user's OK). Found while testing in REAPER on Linux, fixed on the branch: (1) reopening the editor crashed the
host (cairo-xcb-screen.c _get_screen_index assertion) - VSTGUI disconnects X without cairo_device_finish and the
next connection reuses the address, our static `ref` surface outlived its connection too; since 0.8.0, also in 0.8.10
(nativehost --reopen: 3 of 6 runs crashed, after the fix 48 reopens without one). (2) Linux menus at 150/200 % opened
at zoom x the click position (VSTGUI's generic menu adds the zoom twice) - menu_at() divides it out. User test in REAPER on Linux (2026-10-16): sharper images, reopening and the menus at 150/200 % work ("works"). Option C (recreated artwork): dropped by the user 2026-10-16 (rule 1). Website LIVE since 2026-10-16: https://derpsn.github.io/access-virus-ti-revive/ (one page + changelog, generator scripts/site/build.py, deploy .github/workflows/site.yml on each published release / site change on main / by hand; Pages source = GitHub Actions). Update TESTS/COVERAGE constants in build.py when they change. Since 2026-10-16 also: section + issue form "Problem in the original Access plug-in (Virus Control)" (label `original`) for problems of the original that we could fix, a Contributing section (AGENTS.md, skills), the virus.info download link (account needed) also in the README.

**Branch `sidechain` (pushed 2026-10-16, CI test build 0.8.13 = run 38919495024):** the Sidechain bus to the Virus' USB
input in "3 Outs / 1 Input" on all platforms (SidechainSender: rate-controlled windowed-sinc converter like the
original, E167/E168; Windows: the plug-in steps back while the ASIO driver writes the ring; macOS: the Core Audio
IOProc plays the ring's output part), Linux broker E136 guard (give-back resets at least 5 s apart). Tested: Linux
nativehost (first version, E168), unit tests (109). Next: one test session per OS (sidechain in a DAW, host rate
48 kHz, E136 guard on Linux with two quick restarts), then README + merge.

**Restore a backup (asked 2026-10-16):** the original Control Center restores by erasing and writing flash banks (E169) - not allowed (rule 2). User decision 2026-10-16: postponed (backups stay read-only; the single-dump alternative would need a one-slot hardware check first).

**Test round 0.8.14 (branch `round-0.8.14` = sidechain + help-mode, 2026-10-16):** one CI test build, one checklist
per OS (smoke test, sidechain in a DAW, 48 kHz, Linux reset guard, help mode). Release after it passed.
CI run 38922591289 (re-run after a stuck apt mirror; CI now has job time limits). Installed: this Linux PC
(install.sh), win11 VM (silent MSI + setup.exe, running), win10 VM (silent, off); Mac: unpacked in ~/Downloads, the user runs
`sudo installer -pkg ...`. Checklist page (private artifact): https://claude.ai/artifact/8zqCEtreSPUbRGw86XQNc7.
The Virus is on Linux; attach it to win11 with `VM=win11 scripts/vm.sh attach` (stops the Linux broker first, E179) when the user starts the Windows part.
Linux results (user, 2026-10-16): 22/22 pass. But the reset-guard steps could not trigger the guard: the broker stays
up while REAPER restarts (it exits only after 30 s without clients, IDLE_EXIT_S), so no reset happened at all. The guard
is now covered by a unit test instead (TDD: test_resetguard.cpp RED 08afb1f - header missing; GREEN - spacing, fail-open
on a missing stamp or a clock that went back, no symlink following; vticore.c uses src/broker/resetguard.h).
Windows (win11 VM): reconnect after power-cycling the Virus needed a re-attach (libvirt keeps the old USB address;
VM limit, not a plug-in bug) - then it played again (pass). NEW FINDING (user, Linux and Windows): when the Virus is
switched off the plug-in only writes "Virus disconnected" on the LCD; the layout's communication error page never
shows, because the plug-in never sets "Internal System Info/USB Communication" (conditions "... USB Communication
Error" / SystemStatus/Error). Next round, TDD: look up with REA when the original sets these, then implement.
Windows VM: the sidechain arrived silent at the Virus (E170, corrected: Input Mode Static silences part 1 when the input
is silent; all IO configurations play). Open: Ableton's sidechain buffers vs. the VM's iso OUT - check via the ASIO
driver (same OUT path). Next round (TDD): the stats line also reports the sidechain input level.
Windows results (user, win11 VM): smoke 4/4 (reconnect after a VM re-attach), help mode 8/8 (Esc, ?, 150 %),
sidechain open (E170). The ASIO device is no check in 3 Outs / 1 Input (the Virus is no sound card then; feedback).
Linux sidechain re-test (E171): WORKS in REAPER once the pins are Track 3 -> Sidechain 1, 4 -> Sidechain 2 (USB 1 peaks
~15000, rate +-30 ppm). REAPER mapped 4-pin buses wrongly by default. Next round (TDD): Sidechain bus strictly stereo;
stats line with the sidechain input level; disconnect page (above).
macOS (0.8.16, E172): the sidechain WORKS in Ableton Live 12 - Ableton puts it on the main input (active: 1 0), the plug-in
now reads that (USB 1 ~15000). Windows/Ableton had the same symptom (E170) - most likely fixed by 0.8.16 too, untested.
NEW BUG (next round, TDD): reloading a project does not send part 1's Input Mode to the Virus (editor Static, Virus off).
macOS results (user, 0.8.16): all pass - smoke 4/4 (no disconnect page, sound comes back), sidechain 8/8 incl. 48 kHz,
help mode 8/8, and the Audio Unit in LOGIC PRO loads and plays (first Logic test; open item 6 done). Note: with Chorus
Type Off the chorus controls are inactive and show no help note - next round: notes for inactive controls too.
Builds of this round: 0.8.14 (round), 0.8.15 (bus lookup + stereo + log), 0.8.16 (main input for Ableton); Mac and
win11 are on 0.8.16/0.8.15, Linux on 0.8.14 (its install needs sudo while the Virus is away).

**Branch `help-mode` (2026-10-16):** the "?" button (the original's Help Menu, which listed manuals; ours showed
numbers 0-127) toggles a help mode: frames around every explained control, a note for the control under the mouse
(click pins it, clicks change nothing; page tabs, part strip and envelope select still work), Esc or "?" leaves.
Texts: native/src/editor/helptext.cpp, our own words written from the user's Virus TI Reference PDF (read locally,
not in the repo - rule 1) and the editor model; 501 of the 520 control paths covered (the rest: welcome page, progress
bars). Hover needs a frame mouse observer (VSTGUI passes moves to views only while a button is down). Tested: unit
tests (help texts, visible_controls), Xvfb screenshots at 100 % and 150 % with the TI2 connected. Not tested: Esc,
Windows, macOS, a DAW.

**Test builds:** always a NEW version number (a forced reinstall of the same version removed the WinUSB driver in the VM, E166).

**Replies on #4 (2026-10-16):** 6208931585 (nasguile, 0.8.7), 6208931949 (Manu, 0.8.7, logs asked), 6210139464
(both: correction - use 0.8.8, soft knobs on the Virus now change the sound). Waiting for their test results/logs.

**This PC:** ~/.vst3 holds a main build (0.8.8 code + the E157 clean-up); the user's old 0.8.3 copy is in the session
scratchpad (vst3-backup) - the 0.8.8 Linux package can be installed over it. Coverage measurement: a scratch build
`cmake -S native -B <scratch>/cov -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage -O0"` (NEVER with
FETCHCONTENT_BASE_DIR=build-native/_deps - it stays in the cache and breaks build-native's pugixml build on every
later coverage build; the session's cov2 has its own _deps), run vtitests with HOME = an empty
folder and XDG_CONFIG_HOME/WINEPREFIX/VTI_ACCESS_DIR unset, gcov over CMakeFiles/vticore.dir, LC_ALL=C.

**Open, in order of value:**
1. DONE 2026-10-16: Windows check of 0.8.8 (E155).
2. DONE 2026-10-16: E154 (3) explained (E157) - in plug-in mode the Virus changes only name + origin of the edit
   buffer when a program is chosen on the panel, so a read-back cannot supply a patch missing from the cache; the
   read-back code is removed (branch `readback-experiment`, merged). ROM banks need firmware.bin (normal install).
3. #4 ManuAnilkumar23: RAM banks "Init" on the Snow. The Snow description = TI description (E151). Possibly
   related to E152 (if he switches presets on the Snow, the plug-in stayed on "-INIT-") - ask him after 0.8.7;
   else his diag.log ("RAM A sync" lines) and broker log.
4. #4 nasguile: FX 1 changes only after re-triggering - NOT reproduced on the TI2 (E160: CC, pp and SysEx Bank C
   FX parameters all reach a held note at once). Needs his answer: which knobs, Snow/macOS/Ableton, same on the Snow
   itself? The editor path (Chorus/Distortion with types, user test in REAPER) works live too.
5. DONE 2026-10-16: CI-like coverage 92.3 % (test_firmware, test_diag, test_protocol). Lowest now: library.cpp 85 %,
   remotedevice.cpp 90 %.
6. DONE 2026-10-16 (E161): the original drops host notes + clock while busy (start-up queue, bank sync, requests)
   and sends All Notes Off when it becomes busy; ours holds notes until ready and pauses the sync - kept.
7. Older items unchanged: (USB 3: released in 0.8.9, E159; mode 2 measured E164, Out 3 on main, not released; the plug-in sends no audio to the Virus input - the original sends the Sidechain bus (VST3 input bus 1) there, E167; next: implement it), (reconnect path: fixed on main, E163, not released), (macOS duplicate track: works, E165), (two DAWs on Windows: fixed on branch offline-queue, E166), Snow "officially" (record a test report).

**Tools:** `virusmon` (read-only broker observer, decoded traffic) is in the repo: native/tools/virusmon.cpp,
target `virusmon` (build-native/bin/Release/virusmon, `--hex` for SysEx bytes).

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

Latest release: **v0.8.18** (2026-10-16, full project recall E174, "No Virus TI" page E173, help reasons; earlier: v0.8.10 reconnect + absent start + two DAWs on Windows, E163/E166; 0.8.9: the third USB output, E159; 0.8.8: soft knobs on the Virus change the sound, E154; 0.8.7: presets chosen on the Virus + soft knobs, E150/E152; 0.8.6: start-up waits for the Virus, E149; 0.8.5: one plug-in instance per process, issue #6). The 0.8.x
releases are summarised in the sections above; 0.7.x and older in [HANDOFF-history.md](HANDOFF-history.md).
Virus TI Snow: works with the native plug-in per user reports (macOS, 0.8.3+, USB 3 and soft knobs with 0.8.20, E188; issue #4 closed 2026-10-17); v0.7.4 stays available for Windows.

## Open items (order agreed with the user 2026-10-12: easy first; deferred stays deferred)

1. ~~Mark a release as Latest~~ - done (0.7.1).
2. ~~Tidy up HANDOFF~~ - done (this file).
3. ~~Windows setup.exe interactive dialogs~~ - done (E126).
4. CLOSED for the native plug-in (checked 2026-10-16): E127 was measured on the classic 0.7.x path; the native plug-in
   starts exactly on Linux/REAPER and macOS/Ableton (first onset 2000.00 ms, E140/E141). Windows only measured in the VM
   (E141's open gap pattern) - recheck on a real PC. Old note: first notes at transport start (E127): same on Windows; the user remembers the same with the original
   Access driver (start playback slightly before the first note). Optional later: confirm with the win10 snapshot pre-winusb.
5. OBSOLETE since 0.8.0 (checked 2026-10-16): only the 0.7.x Wine path; the native plug-in on Linux runs without Wine.
   Old note: occasional sync reports on Linux (E128): cause = wineserver's registry save ~30 s after the plug-in process starts
   (stalls the process ~25 ms -> plug-in re-anchors -> synth reports). Done: vtiaudio polls the ring under Wine (released
   in 0.7.2). Wine 11 tested (E131, paused by the user): WineHQ 11.0 stable / 11.19 devel (noble builds, unpacked in ~/.local/opt/wine11*, prefix copy ~/.local/opt/wine11/prefix) have NO ntsync support compiled in; same 30 s registry stall (61-64 ms). /dev/ntsync exists after `sudo modprobe ntsync` (until reboot). This PC is back to the plain 0.7.1 release (plug-in, broker, units).
6. DONE 2026-10-16: Logic Pro test of the native AU (0.8.16 test build: loads and plays, user).
6a. ~~macOS + Cubase blocklist~~ - resolved (E129): Cubase 15 loads TI Revive fine; the blocklisted plug-in is the
    original Intel "Virus TI.vst3". (a) DONE (released in 0.7.2): "Install Access Virus TI software.command" checks Rosetta and offers to install it,
    bridge log names a missing Rosetta (EBADARCH). (b) DONE (E130, released in 0.7.2): originals moved to /Library/Application Support/TI Revive/Access plug-ins;
    verified in Cubase 15 with a local 0.7.2 test package; uninstall moves them back (tested on the Mac).
6b. macOS universal bridge (E132, RELEASED in v0.7.3, release package verified universal): DAWs under Rosetta can load TI Revive; tested with an x86_64 test
    host under Rosetta. Real test done (E133: Cubase 15 under Rosetta plays). Reddit Cubase 14 user: 'not connected' = LIBUSB_ERROR_ACCESS (E134): old Access drivers from a full install hold the Virus; told him to remove them + restart; install script now detects/disables them (not released). CONFIRMED: Aura Plugins' Virus Editor USB MIDI driver held the Virus; parked -> connected and syncing. Install script now also detects Aura's driver and offers to park it (default No). RELEASED in v0.7.4.
6c. OBSOLETE since 0.8.1 (checked 2026-10-16): the native AU/VST3 has no Rosetta helper round trip. Old note: macOS CPU (Reddit user, Cubase 14 under Rosetta, M4 Max): Cubase's CPU meter jumps only while STOPPED, smooth during
    playback -> ASIO-Guard real-time path for the selected/armed instrument track (per-block round trip to the helper,
    translated). Harmless unless dropouts; advice: buffer 512, deselect the track. Measured on our M3 (Cubase 15 under Rosetta,
    30 s play/stop/play): Cubase 23-28 %, vtihelper ~2.5 %, vtibroker ~4 % of one core, NO change between playing and
    stopped -> the ASIO-Guard jump at stop is Cubase moving the selected track to the real-time path, not extra work.
    Optional later: editor mirror copies only changed frames.
7. DONE in 0.8.2: TI Revive Control Center (all platforms, also `tirevive-cc`).

8. STILL WATCHING: E136 (Virus hung after rapid give-back resets during tests on 2026-10-13); if it repeats,
   rate-limit the reset in vti_close (src/broker/vticore.c, unchanged). Kernel log of this PC checked 2026-10-16: no
   'error -71' / 'not accepting address' since (the original event is not in the log either, so weak evidence).

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

- win10 VM (original + Access driver): Windows crashes with bug check 0xCE (a driver unloaded with pending
  operations) when the Virus disappears or resets under the Access driver - a Virus power cycle or re-plug while it
  is attached, or a libvirt attach right after it re-enumerated (2026-10-17, twice). Close the DAW before a detach,
  never power-cycle the Virus while it is in the VM. Reinstalling the Access driver starts the firmware updater after
  the reboot, which switches the Virus into update mode; the re-enumerated device is not passed to the VM, so nothing
  is flashed - close the updater, power-cycle the Virus (E199).

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
