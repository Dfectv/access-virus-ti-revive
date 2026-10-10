# Claims tracker

Status: UNTESTED / CONFIRMED / REFUTED / PARTIAL. Evidence column references IDs in evidence.md.

| ID | Claim (source) | Status | Evidence | Notes |
|----|----------------|--------|----------|-------|
| C1 | MI_03/MI_04 carry standard USB-MIDI 4-byte event packets (reddit) | UNTESTED | | |
| C2 | Enable handshake 4E 73 52 01 to EP 0x05; present in Linux sound/usb/quirks.c (reddit) | UNTESTED (lead strengthened) | E016 | host kernel has a Virus TI quirk (2 MIDI ports on vendor ifaces); handshake bytes not yet checked in kernel source |
| C3 | VirusUSB.sys is a Ploytec OEM driver (reddit) | PARTIAL (supported) | E009, E012 | usb-audio.de + pt_/PGAsio/UH7K markers; confirm in VirusUSB.sys code |
| C4 | Audio routing depends on Global SysEx "Input and Output Configuration" (bank 0x73, idx 0x09); driver also does USB audio config steps (reddit) | PARTIAL | E014, E012 | param exists as "Virus Control USB IO Configuration" 0..2; effect + driver-side config steps untested |
| C5 | REMOTEDEVICE.XML in the Virus Control installer lists parameters (reddit) | CONFIRMED (amended) | E013 | embedded in Virus Control.dll, not a file |
| C6 | Device emits 73 00 54 01 keepalive events (reddit) | UNTESTED | | |
| C7 | TI mode needs MIDI clock + F5 position messages on EP 0x06 (reddit, one commenter) | UNTESTED | | |
| C8 | Old VirusUSB.sys blocked on Win11 by CI cross-cert exceptions policy (user, verified on host) | CONFIRMED (user) | user report: CodeIntegrity 3076/3077, Code 39 | not re-tested here |
| C9 | Windows inbox driver handles playback (user known-fact) | CONTRADICTED in VM | E006 | with Access stack installed audio is vtiaudio; likely true only without VirusUSB.sys (e.g. Win11 host?) — ask user |
