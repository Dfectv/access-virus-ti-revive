#!/usr/bin/env python3
"""make_test_patch.py INIT_BANK.mid OUT.mid - write a Virus TI bank with percussive timing-test singles.

Base is the factory " -INIT-   " single (taken from the user's own Access bank, e.g. "Virus TI2 Rom A.mid",
slot 127: nothing Access-made is shipped here). Changes: amp envelope attack 0 / short decay / sustain 0 /
short release, so every 16th note of timing-16ths-120bpm.mid is a separate click; FX sends are already 0 in Init.
Single dump (findings spec): F0 00 20 33 01 dd 10 bank prog <512 data: page A, page B, ...> cs F7,
cs = sum(dd .. last data byte) & 0x7F; name = page B 112..121. Bank .mid = SMF format 0, one SysEx per single.
"""
import re
import sys

PATCHES = [  # name (10 chars), amp env decay, release
    ("TI Click  ", 20, 0),
    ("TI Pluck  ", 45, 8),
]


def find_init(path):
    d = open(path, "rb").read()
    for m in re.finditer(rb"\x00\x20\x33\x01.\x10", d, re.S):
        b = d[m.start():m.start() + 523]
        if len(b) == 523 and b[-1] == 0xF7 and b[8 + 128 + 112:8 + 128 + 122] == b" -INIT-   ":
            return bytearray(b)
    sys.exit("no -INIT- single in " + path)


def vlq(n):
    out = [n & 0x7F]
    while n > 0x7F:
        n >>= 7
        out.insert(0, 0x80 | (n & 0x7F))
    return bytes(out)


def main():
    init = find_init(sys.argv[1])
    track = b""
    for prog, (name, decay, release) in enumerate(PATCHES):
        b = bytearray(init)
        b[6], b[7] = 1, prog  # bank A (edit-buffer independent), program
        a = 8  # page A starts after 00 20 33 01 dd 10 bank prog
        b[a + 59], b[a + 60], b[a + 61], b[a + 62], b[a + 63] = 0, decay, 0, 64, release
        b[a + 128 + 112:a + 128 + 122] = name.encode()
        b[521] = sum(b[4:521]) & 0x7F
        track += (b"\x00" if prog == 0 else b"\x78") + b"\xF0" + vlq(len(b)) + bytes(b)
    track += b"\x78\xFF\x2F\x00"
    smf = b"MThd" + (6).to_bytes(4, "big") + bytes([0, 0, 0, 1, 0x01, 0xE0])
    smf += b"MTrk" + len(track).to_bytes(4, "big") + track
    open(sys.argv[2], "wb").write(smf)
    print(f"{sys.argv[2]}: {len(PATCHES)} singles")


if __name__ == "__main__":
    main()
