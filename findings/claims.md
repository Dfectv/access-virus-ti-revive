# Claims tracker

Status: UNTESTED / CONFIRMED / REFUTED / PARTIAL. Evidence column references IDs in evidence.md.

| ID | Claim (source) | Status | Evidence | Notes |
|----|----------------|--------|----------|-------|
| C1 | MI_03/MI_04 carry standard USB-MIDI 4-byte event packets (reddit) | UNTESTED | | |
| C2 | Enable handshake 4E 73 52 01 to EP 0x05; present in Linux sound/usb/quirks.c (reddit) | UNTESTED | | |
| C3 | VirusUSB.sys is a Ploytec OEM driver (reddit) | UNTESTED | | |
| C4 | Audio routing depends on Global SysEx "Input and Output Configuration" (bank 0x73, idx 0x09); driver also does USB audio config steps (reddit) | UNTESTED | | |
| C5 | REMOTEDEVICE.XML in the Virus Control installer lists parameters (reddit) | UNTESTED | | |
| C6 | Device emits 73 00 54 01 keepalive events (reddit) | UNTESTED | | |
| C7 | TI mode needs MIDI clock + F5 position messages on EP 0x06 (reddit, one commenter) | UNTESTED | | |
| C8 | Old VirusUSB.sys blocked on Win11 by CI cross-cert exceptions policy (user, verified on host) | CONFIRMED (user) | user report: CodeIntegrity 3076/3077, Code 39 | not re-tested here |
