# Control Center (phase 1: read-only)

Access' "Virus Control Center" was a separate app next to the plug-in: backup and restore of the Virus' RAM banks,
"Burn To Flash", OS updates and an "Advanced" menu (factory reset, device name). It needs the old Access driver, so it
does not work with TI Revive. Its replacement is built in steps: a core library, a command line tool and an app.

## Scope

| Feature | State |
|---|---|
| Device info (USB identity, RAM banks compared with the cache) | app (Refresh), `tirevive-cc info` |
| Backup of the RAM banks as patch libraries (`RAM A.mid` .. `RAM D.mid`) | app, `tirevive-cc backup` |
| List / convert patch libraries (.mid <-> .syx), no Virus needed | app (Convert library), `tirevive-cc list`, `export` |
| Restore a backup into the Virus' RAM | later, after a separate review (writes stored patches) |
| Firmware / OS update, Burn To Flash, factory reset, device name | **not supported, on purpose** |

The last row stays out for good: these functions write the Virus' flash, and a failure there can brick the device
(AGENTS.md rule 2). If you really need them, use Access' original software on a system that still runs it.

## Where it is installed

| | App "TI Revive Control Center" | Command line |
|---|---|---|
| Windows (MSI) | `Program Files\TI Revive\TI Revive Control Center.exe`, Start menu "TI Revive" | `Program Files\TI Revive\tirevive-cc.exe` |
| macOS (pkg) | `/Applications/TI Revive Control Center.app` | `/Library/Application Support/TI Revive/tirevive-cc` |
| Linux (install.sh) | `~/.local/bin/tirevive-control-center` + menu entry (needs gtkmm 3) | `~/.local/bin/tirevive-cc` |

The uninstallers remove them. The release workflow builds both with the plug-in on each platform.

## The app

`ccapp/ccapp.cpp`, VSTGUI's standalone library (the toolkit of the plug-in's editor; on Linux it uses GTK 3 / gtkmm
for the window and file dialogs). One window: the device line and the four RAM banks (Refresh), the backup folder
(Change...), "Back up RAM banks" with progress and Cancel, "Convert library..." (open a .mid/.syx, save as the other).
The work runs on a worker thread; the app connects to the broker only while an action runs.

## How it works

- `src/cc/controlcenter.*` (in `vticore`), `tools/tirevive-cc.cpp` and `ccapp/ccapp.cpp`. They talk to the Virus through the TI Revive
  broker like the plug-in (`src/device/broker.*`), so the broker must be installed; it is started on demand.
- Only requests documented in `findings/evidence.md` are sent: standard USB GET_DESCRIPTOR (device, manufacturer and
  product string; not the serial number, which would end up in bug reports) (E115), bank info `39` -> `14` and
  single dump request `30` -> `10` (E027, E031, E037). Nothing is written to the Virus.
- Backups reuse the plug-in's bank cache (`MemorySync`): programs whose fingerprint still matches come from the cache,
  the others are read at the Virus' pace of about 1 s each (a full bank: about 2 minutes). The cache is updated on
  the way.
- **Close the plug-in (or the DAW) first.** The broker serves one program at a time; while the plug-in has the
  Virus, the tool reports "no Virus". The tool connects only while a command runs, so it never blocks the plug-in.
- Backups go to `<Documents>/Access Music/Virus TI/Backups/<date time>/` (next to `Patches`, so the plug-in's
  browser does not list every backup). The files are in the original's library format: copy one into `Patches` to
  open it in the plug-in's browser; Access' Virus Control reads them too.
- `.syx` export: the single dumps back to back, as patch librarians expect. `.mid` export renumbers the patches by
  position (bank byte 1, program = position) and recomputes the checksums, like the plug-in's library save.

## Tested

- 2026-10-15, MacBook Air M3, Virus TI2 Desktop (built with clang++ from the sources, not yet in a package):
  `info` (identity 133e:0815, bcdDevice 3.00, "Access Music" / "Virus TI"; 4 x 128 programs current in the cache,
  4 s); `backup` from a current cache (3.5 s); `backup --banks 4` with the RAM D cache moved aside: 128 programs read
  in 2 min 9 s, the library byte-identical to the one from the cache.
- 2026-10-15, Linux, Virus TI2 Desktop: the app on a virtual X display (Xvfb, clicks through XTest): Refresh shows
  the device and 4 x 128 programs current in the cache; "Back up RAM banks" writes RAM A..D.mid (progress, Cancel
  active while running, done message), libraries readable with `tirevive-cc list`. File dialogs not tried yet.
- Linux: builds, `list`/`export` round trips on factory libraries (patch data unchanged; only program byte and
  checksums differ). Windows: cross-compiles with mingw-w64.
- Packages (CI run 38709651579, manual, test version 0.0.1): the pkg's binary is universal and signed and runs
  `info` on the M3 with the TI2 (not installed, run from the expanded pkg); the MSI's `tirevive-cc.exe` runs in a
  Windows 11 VM (`list`, `export`; `info` without a Virus: "no Virus", exit 1); the Linux tarball contains it.
