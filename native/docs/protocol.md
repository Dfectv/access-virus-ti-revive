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
