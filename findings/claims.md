# Claims tracker

Status: UNTESTED / CONFIRMED / REFUTED / PARTIAL. Evidence column references IDs in evidence.md.

| ID | Claim (source) | Status | Evidence | Notes |
|----|----------------|--------|----------|-------|
| C1 | MI_03/MI_04 carry standard USB-MIDI 4-byte event packets (reddit) | CONFIRMED (amended) | E023, E024 | device->host EP 0x85 cable 1; host->device EP 0x06 cable 2 (+cable 5 for F5); EPs are interrupt |
| C2 | Enable handshake 4E 73 52 01 to EP 0x05; present in Linux sound/usb/quirks.c (reddit) | CONFIRMED | E016, E022, E028 | Windows plugin path sends it on open; host Linux snd-usb-audio sent the same bytes after re-enumeration; kernel source not read |
| C3 | VirusUSB.sys is a Ploytec OEM driver (reddit) | PARTIAL (supported) | E009, E012, E018 | usb-audio.de + pt_/PGAsio/UH7K markers; confirm in VirusUSB.sys code |
| C4 | Audio routing depends on Global SysEx "Input and Output Configuration" (bank 0x73, idx 0x09); driver also does USB audio config steps (reddit) | PARTIAL | E014, E012 | param exists as "Virus Control USB IO Configuration" 0..2; effect + driver-side config steps untested |
| C5 | REMOTEDEVICE.XML in the Virus Control installer lists parameters (reddit) | CONFIRMED (amended) | E013 | embedded in Virus Control.dll, not a file |
| C6 | Device emits 73 00 54 01 keepalive events (reddit) | REFUTED as keepalive / PARTIAL | E025 | keepalive is host->device Active Sensing FE (~0.3 s); device sent 73 40 54 01 (global 0x54 Sync Information Event=1) once, not periodically |
| C7 | TI mode needs MIDI clock + F5 position messages on EP 0x06 (reddit, one commenter) | PARTIAL | E025 | F5 xx yy (cable 5) + F8 (cable 2) observed periodically on EP 0x06; "needs" (device behavior without them) untested |
| C8 | Old VirusUSB.sys blocked on Win11 by CI cross-cert exceptions policy (user, verified on host) | CONFIRMED (user) | user report: CodeIntegrity 3076/3077, Code 39 | not re-tested here |
| C9 | Windows inbox driver handles playback (user known-fact) | CONTRADICTED in VM | E006 | with Access stack installed audio is vtiaudio; likely true only without VirusUSB.sys (e.g. Win11 host?) — ask user |
