# Instructions for AI coding agents

You are working on **TI Revive**: it makes the Access Virus TI (Desktop, Polar, Keyboard; the Snow with 0.7.4) work
on Windows 11, macOS and Linux without the old Access drivers. Since 0.8.0 its plug-in is "TI Revive Virus Control"
(`native/`), a rewrite that looks and works like the original "Virus Control" and loads the Access files from the
user's own installer at runtime.
Read this file completely before changing anything. Human contributors: see [CONTRIBUTING.md](CONTRIBUTING.md).

## Hard rules (never break these)

1. **No Access material in the repository**: no Access binaries, installers, plug-ins, firmware images, skin PNGs,
   XML resources, patch banks, and no decompiled or disassembled Access code (not even excerpts in comments).
   Describe behaviour in your own words; function addresses/offsets are fine. If a task seems to need Access
   material in git, stop and ask the human. The one exception (decided by the maintainer, 2026-10-15, widened to
   the website 2026-10-16): screenshots of the running plug-in and Control Center for the README (`docs/images/`)
   and the project website (`site/`), always next to the "not affiliated" notice and never as the site's logo or
   branding - never the skin files themselves.
2. **Do not risk the user's hardware.** Never send untested USB vendor requests or SysEx to a Virus, never trigger
   firmware/OS updates, never write to the Virus' flash. Keep MIDI rates bounded (a parameter flood can hang the
   Virus' audio until a power cycle — see evidence on automation limits). Prefer read-only diagnostics.
3. **Evidence before claims.** Do not invent protocol details. Every claim about the device or the Access software
   needs a source (capture, log, hardware test, static analysis) and goes into `findings/evidence.md` with a
   confidence level. If you are guessing, say so in the PR and mark the entry `open` or `LOW`.
4. **Never claim a test you did not run.** State exactly what was built and what was tested on which hardware.
   "Compiles" is not "works".
5. **No secrets, no personal data** in code, logs or commits (user names, serial numbers, home paths from logs).
6. **No history rewriting** on shared branches (no force pushes to `main`), no mass reformatting, no unrelated
   refactors in a bug-fix PR.

## Map of the repository

| Path | What it is |
|---|---|
| `findings/spec.md` | the USB/MIDI protocol of the Virus TI as far as it is known |
| `findings/evidence.md` | numbered observations E001.. with source and confidence — search here first |
| `findings/claims.md` | community claims and how they were checked |
| `findings/design*.md` | design per platform (Windows, macOS bridge, Linux, MIDI ports) |
| `findings/HANDOFF.md` | the maintainers' working log: current state, open problems, next steps |
| `native/` | **the plug-in since 0.8.0**: "TI Revive Virus Control" (VST3, all platforms) and the Windows ASIO driver "TI Revive Virus Audio"; `native/docs/architecture.md`, `native/docs/protocol.md` (timing, stamp clock), `native/docs/formats.md` |
| `src/broker/` | device access + broker: owns the Virus' USB interfaces, serves plug-in clients (WinUSB on Windows, libusb on macOS/Linux) |
| `src/wrapper/` | the classic Windows plug-in of 0.7.x (emulation of the Access driver interface `vtiemu`, VST3 wrapper, ASIO wrapper); not shipped since 0.8.0, kept for reference |
| `src/inf/`, `src/installer/` | Windows driver package (WinUSB) and MSI |
| `src/mac/` | macOS: `libvirususb/` (replacement libVirusUSB), `bridge/` (Audio Unit for Logic/GarageBand; its VST3 is replaced by the native plug-in), `helper/` (Rosetta host of the original plug-in) |
| `src/installer-mac/` | macOS package and helper scripts (driver conflicts, Access installer) |
| `scripts/` | diagnostics (`tirevive-usbinfo.ps1`), capture/decode tools, VM and build scripts |

## Before you change code

1. Search `findings/evidence.md` and `findings/HANDOFF.md` for the symptom (error text, USB request, model name).
   Many problems are known: e.g. another driver holding the interfaces (`LIBUSB_ERROR_ACCESS`, macOS
   `UsbExclusiveOwner = MIDIServer`, Windows `VIRUSUSB` service), wrong model detection (Snow vs TI, E115), part
   output not on USB ("connected but silent").
2. Find the path involved: since 0.8.0 the native plug-in (`native/`) on all platforms; the macOS Audio Unit still
   uses the bridge/helper; reports about "Virus TI (TI Revive)" VST3 come from 0.7.x or older.
3. Read the code around the change and match its style: C/C++, short functions, comments that explain *why*.
4. **Unsure how the original Virus Control does something? Look it up, do not guess.** Use the REA MCP (skill
   `reverse-engineer-anything`) for static analysis of the user's own Access files (e.g. `Virus Control.dll`), not
   ad-hoc disassembly in a shell. Then implement that behaviour in the most efficient and reliable way, with
   performance (no extra work on the audio thread) and safety (rule 2) in mind. Rule 1 still applies: the repo gets
   the behaviour in your own words plus function addresses (an evidence entry), never decompiled code.

## Building and checking

- Windows parts: cross-compile on Linux with mingw-w64: `make -C src/broker`, `make -C src/wrapper`. MSI: WiX on
  Windows (`src/installer/build.ps1`).
- macOS parts: on a Mac, `scripts/mac-build-bridge.sh <repo> --install` (development) or
  `scripts/mac-build-release.sh <repo> <version>`.
- Linux: the broker builds with `make -C src/broker` (libusb).
- Native plug-in (all platforms): `git submodule update --init --recursive native/external/vst3sdk`, then
  `cmake -S native -B build-native && cmake --build build-native && ctest --test-dir build-native` (MSVC on Windows,
  Xcode on macOS; see `native/README.md`). Never commit Access files: the plug-in loads them at runtime.
- CI (`.github/workflows/build.yml`) builds releases only on version tags; build locally for PRs.
- Without the hardware you can still: build, run the static checks you have, and reason from logs. Say that you did
  exactly that.

## Helping a user test their setup

Use the `tirevive-test` skill (`.claude/skills/tirevive-test/`): safety rules first (no firmware updates, no writing
stored patches, no untested SysEx, a disposable DAW project), then the test stages from USB detection to project
recall, and a report with pass / fail / not tested per stage for the "Test report" issue template. Untested models
(Virus TI Snow) and setups are exactly where these reports are needed.

## Working from a user's report

Use the `tirevive-diagnose` skill (`.claude/skills/tirevive-diagnose/`) or follow it by hand: collect the platform
logs listed in CONTRIBUTING.md, match the lines against known evidence, and answer with the cause, the evidence
entry and concrete steps. Quote log lines exactly.

## Preparing a pull request

Use the `tirevive-pr` skill (`.claude/skills/tirevive-pr/`) or its checklist:

- [ ] one focused change; no Access material; no personal data
- [ ] builds for the platforms it touches (say which you built)
- [ ] tested on hardware, or clearly marked "not tested on hardware"
- [ ] new device/protocol knowledge added to `findings/evidence.md` (next free E-number, date, source, confidence)
- [ ] README updated if users see different behaviour
- [ ] PR description: problem, cause (with evidence), change, test, and one line that an AI assistant helped

## Tone in issues and PRs

Be concrete and friendly, base answers on the logs, and add a short note at the end when an answer was written with
AI assistance.
