# Plan: benchmark against the original Access driver and plug-in (2026-10-16)

Status: **planned, not possible for now** - stage B needs Windows 10 with the Access driver on bare metal; the only
real Windows PC (this Linux PC dual-booted, user 2026-10-16) runs Windows 11, where the Access driver is blocked. Nothing measured yet.
Until stage B has results, README, website, issues and release notes make **no** performance comparison with the
original (timing, latency, stability, CPU). What they compare today is the native plug-in against our own classic
0.7.x plug-in (README "Performance and improvements", E139-E143). This plan details step R7 of
[../native/docs/plan.md](../native/docs/plan.md).

## Question

How does "TI Revive Virus Control" (with the broker, and the ASIO driver on Windows) compare with the original
"Virus Control" on the Access driver (`VirusUSB.sys`) - timing, latency, stability, load, start-up, behaviour - in a
setup where neither side has an advantage the other does not have.

## Which environment is fair

| Environment | Original runs? | Fair for | Not fair for |
|---|---|---|---|
| Linux (this PC) | no: the Access driver does not exist for Linux; the 0.7.x Wine path runs the original plug-in on our own driver emulation (`vtiemu`), so it measures us | USB capture of a VM (usbmon), external recorder | any comparison with the original |
| win10 VM, snapshot `pre-winusb` (libvirt, Virus passed through) | yes (Access driver + Ableton 11) | behaviour (stage A): messages, start-up order, recall, presets, soft knobs; values each side reports (e.g. latency) | timing, latency, dropouts, CPU: USB passthrough adds its own effects (E141: ping answered up to 2 s late, 85/256 notes missing in a repeating pattern, never seen on Linux/macOS) and hits a kernel driver and our user-mode WinUSB broker differently; guest CPU/DPC figures mostly measure the host |
| Bare-metal Windows 10 22H2 on a real PC | yes (last Windows the Access driver supports properly) | everything (stage B) | - |
| Bare-metal Windows 11 (TI Revive) vs Windows 10 (original), same PC | partly | the user's question "is the switch worth it" | a like-for-like result (the OS differs too); optional extra, never instead of stage B |

## Parked (user decision 2026-10-16)

Optional, out of curiosity: nobody asks for it and it is **not** part of "complete". The plan stays here for the day a
suitable machine turns up. Machine options discussed on 2026-10-16:

- **Windows 10 22H2 on a spare SSD/partition of the Linux PC** (preferred): same CPU and USB controller (xHCI) as the
  Windows 11 tests; two Windows 10 installations (or one + a disk image restored between the phases); OpenSSH server
  with key login so Claude can run the scripted parts.
- **MacBook 2009 with Windows 10 via Boot Camp** (fallback, extra data point only): x64 Core 2 Duo (our builds need
  no CPU flags beyond the x64 default), but Windows 10 is not supported by Apple's Boot Camp drivers for that model
  (legacy BIOS install, driver guesswork), and its USB 2.0 controller is EHCI - our WinUSB isochronous stream has never
  run on EHCI, so results would mix controller and design effects and not generalise.
- For both: REAPER on both sides (command-line render, started through a scheduled task in the logged-in session, since
  SSH sessions have no desktop/audio); Virus directly on the port, no hub; the Access software version must match the
  Virus OS (a mismatch may offer an OS update - never confirm it); the user is needed for power cycles and listening.

## Stage A - behaviour in the win10 VM (possible, not scheduled)

- VM `win10`: snapshot `pre-winusb` (Access driver, Ableton 11) against a snapshot with the current TI Revive release.
  Revert between runs; never swap drivers inside one snapshot.
- Capture: usbmon on the Linux host (read-only, outside the system under test) for both runs.
- Compare: start-up messages and their order, time to "ready", bank sync requests, project save/recall (state bytes
  and the dumps sent; E174: what the original does with part dumps of format 9 is not known yet), preset chosen on
  the Virus (E152), soft knobs (E150/E154), reported latency per buffer size (E141: ours 4224 at 512, the original
  4352), behaviour after a Virus power cycle.
- Every result is labelled "VM". No timing numbers from this stage go into README or website.

## Stage B - performance on bare metal (blocked: no Windows 10 installation on bare metal)

Setup:
1. One PC, two system images (or two SSDs): Windows 10 22H2 with the Access driver and the Virus TI Software Suite;
   the same Windows 10 with TI Revive (current release). No driver swapping in one installation (a reinstall left the
   interfaces without WinUSB once, E166).
2. Equal for both: Ableton Live 11 (same version), the same project, 44.1 kHz, buffer 256 and 512, the same USB port
   without a hub, the same Virus (TI2 Desktop) and Virus OS, power plan and background services the same.
3. Measure from outside: the Virus' analog outputs recorded by an independent interface (e.g. on the Linux PC), next
   to the DAW's own recording. Recording only through the driver under test measures that path with itself.

Metrics (existing tools where possible):

| Metric | Method |
|---|---|
| Note timing (onsets on the grid, jitter, max deviation, drift, first note after Play) | 16ths at 120 BPM, live and render; `scripts/timing_analysis.py` (method of E127/E140/E141) |
| Round-trip latency, measured vs reported | click out -> Virus -> analog in / external recorder; compare with the latency each driver reports to the DAW |
| Stability | 60 min dense project: dropouts, sync errors (plug-in display and the Virus' sync reports) |
| Load | DAW CPU meter; LatencyMon (DPC/ISR) on the same machine |
| Start-up | time to ready, bank sync duration, project recall checked against a read-back dump |
| Protocol differences | read-only capture (USBPcap on the PC) |
| ASIO (Virus as sound card) | Access ASIO driver vs "TI Revive Virus Audio": input/output latency, level, timing |

## Safety (AGENTS.md rule 2)

No parameter floods to "stress" either driver: only the bounded rates a real project produces. No firmware update,
no writes to stored patches or flash on either side. A disposable project per run.

## Result table (to fill in)

| Metric | Original (Access driver) | TI Revive | Notes / evidence |
|---|---|---|---|
| | | | |

Results become evidence entries (next free E-number, source, confidence). Only after that may README/website say
anything comparative, and only what the table shows.
