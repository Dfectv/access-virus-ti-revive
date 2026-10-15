#!/usr/bin/env python3
"""msgsummary.py TRACE - which Virus SysEx commands a broker trace contains, when (first/last), how often.

Groups messages by direction, cable and command byte (byte 6 after F0 00 20 33 01 <dev>) plus, for memory
commands, the bank. Uses the decoder of TI Revive (external/ti-revive/scripts/vtitrace.py).
"""
import os
import sys
from collections import OrderedDict

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "external", "ti-revive", "scripts"))
import vtitrace  # noqa: E402

MEMORY = {0x10, 0x11, 0x14, 0x30, 0x31, 0x32, 0x34, 0x39}


def key(kind, cable, msg):
    d = vtitrace.KIND[kind]
    if not msg:
        return None
    if msg[0] == 0xF0 and len(msg) > 8 and msg[1:5] == b"\x00\x20\x33\x01":
        cmd = msg[6]
        k = "%s c%d %02x" % (d, cable, cmd)
        if cmd in MEMORY:
            k += " bank %02x" % msg[7]
        elif cmd in (0x72, 0x73, 0x74, 0x6E, 0x6F, 0x70, 0x71):
            k += " part %02x" % msg[7]
        return k
    if msg[0] in (0xF8, 0xF5):
        return None
    return "%s c%d %02x" % (d, cable, msg[0] & 0xF0 if msg[0] < 0xF0 else msg[0])


def main():
    rows = OrderedDict()
    t0 = None
    for ns, frame, kind, ep, cable, msg in vtitrace.messages(vtitrace.records(sys.argv[1])):
        if kind == 2:
            continue
        t0 = ns if t0 is None else t0
        k = key(kind, cable, msg)
        if not k:
            continue
        r = rows.setdefault(k, [ns, ns, 0, len(msg)])
        r[1] = ns
        r[2] += 1
    for k, (a, b, n, ln) in rows.items():
        print("%8.1f .. %8.1f s  %6dx  %4d B  %s" % ((a - t0) / 1e9, (b - t0) / 1e9, n, ln, k))


if __name__ == "__main__":
    main()
