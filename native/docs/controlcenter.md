# Control Center (phase 1: read-only)

Access' "Virus Control Center" was a separate app next to the plug-in: backup and restore of the Virus' RAM banks,
"Burn To Flash", OS updates and an "Advanced" menu (factory reset, device name). It needs the old Access driver, so it
does not work with TI Revive. Its replacement is built in steps: first a core library with a command line tool, a UI
later.

## Scope

| Feature | State |
|---|---|
| Device info (USB identity, RAM banks compared with the cache) | `tirevive-cc info` |
| Backup of the RAM banks as patch libraries (`RAM A.mid` .. `RAM D.mid`) | `tirevive-cc backup` |
| List / convert patch libraries (.mid <-> .syx), no Virus needed | `tirevive-cc list`, `export` |
| Restore a backup into the Virus' RAM | later, after a separate review (writes stored patches) |
| Firmware / OS update, Burn To Flash, factory reset, device name | **not supported, on purpose** |

The last row stays out for good: these functions write the Virus' flash, and a failure there can brick the device
(AGENTS.md rule 2). If you really need them, use Access' original software on a system that still runs it.

## How it works

- `src/cc/controlcenter.*` (in `vticore`) and `tools/tirevive-cc.cpp`. They talk to the Virus through the TI Revive
  broker like the plug-in (`src/device/broker.*`), so the broker must be installed; it is started on demand.
- Only requests documented in `findings/evidence.md` are sent: standard USB GET_DESCRIPTOR (device, manufacturer and
  product string; not the serial number, which would end up in bug reports) (E115), bank info `39` -> `14` and
  single dump request `30` -> `10` (E027, E031, E037). Nothing is written to the Virus.
- Backups reuse the plug-in's bank cache (`MemorySync`): programs whose fingerprint still matches come from the cache,
  the others are read at the Virus' pace of about 1 s each (a full bank: about 2 minutes). The cache is updated on
  the way.
- **Close the plug-in (or the DAW) first.** The Virus answers one request at a time; requests of the plug-in and the
  tool would overlap and the Virus truncates its replies (E145).
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
- Linux: builds, `list`/`export` round trips on factory libraries (patch data unchanged; only program byte and
  checksums differ). Windows: not built yet.
