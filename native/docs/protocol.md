# Plug-in <-> Virus protocol as the original uses it (from broker traces, tools/msgsummary.py)

Transport: USB-MIDI packets on EP 0x06 (host -> Virus) / 0x86 (Virus -> host); cable 2 = parameters, notes,
requests; cable 3 = bulk dumps; cable 5 = `F5 lo hi` time stamps (14-bit frame) before timed events; MIDI clock F8
on cable 2. SysEx: `F0 00 20 33 01 <dev=0x10> <cmd> <a> <b> ... F7`. USB control requests in parallel (spec.md):
wIndex 0x01 status poll (~1 per 50 ms), 0x12 address writes, 0x30 16-byte reads, 0x22 (stalls, as with Access).

## Start-up with an empty cache (original-fullsync.trace, Virus TI2 Desktop, 2026-10-13)

| t | dir | command | meaning (hypothesis where marked ?) |
|---|---|---|---|
| 7.1 s | out | 25x `73 00 <id> <v>` | global parameters (remotedevice.xml GlobalInit) |
| 7.2 s | out | `11 00` 267 B | multi dump of the plug-in into the edit buffer |
| 7.2 s | out | `72 <part> <id> <v>` part 0..15 | multi part parameters (ParamInit?) |
| 7.2 s | out | `F2` song position | transport |
| 7.3 s | out | `6F <part> ...` part 0..15 | a page-D single parameter per part (?) |
| 7.4-9.4 s | out c3 | 16x `10 00 <part>` 524 B | the plug-in's 16 single patches into the part edit buffers (project restore) |
| 9.4 s | out | 224 CC | 14 controllers x 16 channels |
| 9.6-14 s | out/in | `39 <bank>` -> `14 <bank>` 267 B | per RAM bank 1-4: bank info (?) |
| 13.5-546 s | out/in | `30 <bank> <prog>` -> c3 `10 <bank> <prog>` 524 B | 4 RAM banks x 128 single dumps, one request per ~1.01 s, answer ~1 s later |
| 9.6-546 s | out | `74 01 7E` ~1/s | poll during the sync (?) |
| 546 s | | | sync done, notes play (vtihost) |
| 607 s | in c1 | `6F <part>` x16 | after the plug-in closed (reset echo?) |

ROM banks are not transferred over USB: the plug-in builds them from the firmware package (cache files "ROM A".."Z").
Open: is 1 dump/s the Virus' pace or the plug-in's (try faster requests in the rewrite); meaning of 39/14, 74 01 7E,
6F per part; cached start (only the start-up part, no 30/10).

## Parameter changes (automation, original-automation.trace)
CC / poly pressure / parameter SysEx exactly as remotedevice.xml describes (docs/formats.md), value = round(norm*127).
On close the original resets globals (73 00 3B/3A/3C/7A).

## Dump formats (verified on 528 recorded single dumps, tools: `vtimodel message <id>`, `vtimodel dump <file.syx>`)
- remotedevice.xml dump templates: the byte `index` is the offset inside the SysEx message (incl. F0).
- Single dump `10 <bank> <prog>`: 524 bytes; header 9 bytes, data 9..520 (two 256-byte pages A/B), byte 522 = add7 checksum
  (7-bit sum of bytes 5..521), F7. VirusTI_Singledump describes 435 parameter bytes (+ bit fields), the patch name is
  `Single Patch Name/Character 01..10` (bytes 249-258). Bank 0 = edit buffer (part = program byte). decode + re-encode
  of all 528 dumps is byte-identical (unknown bytes are kept: update_dump patches an existing dump).
- Multi dump `11 00 00` (267 B, the plug-in sends one at start): template VirusTI_MultiDump (add7 5..264), but the
  plug-in's own dump has checksum 0x26 where add7 gives 0x24 - no other byte range matches. Open (the Virus seems to accept it?).
- Bank info `39 <bank>` -> `14 <bank> 00` + 256 data bytes (267 B, add7 checksum OK): 128 x (lo, hi) = one 14-bit
  value per program. Formula (510 of 512 recorded programs): sum of the single dump's data bytes 9..520 WITHOUT bytes
  9, 11, 12 (page A offsets 0, 2, 3: flags?, Original Bank, Original Patch) and 265 (page B offset 0), mod 16384.
  The 2 exceptions (RAM A 3 and RAM A 106) are exactly +100 - both are the only patches with byte 117 (Chorus/Delay)
  = 8 and byte 239 (unnamed) = 3; device table and dumps re-read live are unchanged (stable quirk of the firmware's
  internal format). The cache files (RAM A-D: "MIDI" 00 01 08 00 + 128 x [u32 BE 524 + single dump]) do not store
  the table, and a cached start of the original requests 39 for all banks but no single dump - so the original
  recomputes the value from its cache including the quirk -> find the function in the decompile (work/decomp).
- Replies arrive on EP 0x86 (plug-in interface, cable 3) during a plug-in session and on EP 0x85 (MIDI port, cable 1)
  otherwise (virusctl sysex). The Virus needs ~1 s per request in both cases.
