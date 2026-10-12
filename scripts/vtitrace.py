#!/usr/bin/env python3
"""vtitrace.py TRACE [--stats] [--no-clock] [--kinds out,in,ctl] - decode a broker trace (VTI_TRACE=<file>, vtibroker_posix.c).

Every record: u32 size, u8 kind (0 host->device, 1 device->host, 2 control request), u8 endpoint, u16 reserved,
u64 monotonic ns, i64 synth frame (-1 if unknown), payload. Plug-in channel payloads are USB-MIDI packets (4 bytes:
cable << 4 | CIN, 3 data bytes); this tool reassembles them per direction and cable into messages:
  SysEx "F0 .. F7", channel events, MIDI clock (F8, usually preceded by an F5 time stamp: "F5 lo hi" = 14-bit frame).
Output lines: time (ms since the first record), synth frame, direction, cable, message.
"""
import argparse
import struct
import sys
from collections import Counter

CIN_LEN = [0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1]
KIND = {0: "out", 1: "in", 2: "ctl"}


def records(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"VTTRACE1":
        sys.exit("not a VTTRACE1 file")
    pos = 8
    while pos + 4 <= len(data):
        (size,) = struct.unpack_from("<I", data, pos)
        body = data[pos + 4:pos + 4 + size]
        pos += 4 + size
        if len(body) < 20:
            break
        kind, ep, _, ns, frame = struct.unpack_from("<BBHQq", body)
        yield kind, ep, ns, frame, body[20:]


def messages(recs):
    """(ns, frame, kind, ep, cable, bytes) per complete message; control requests as one item each."""
    sysex = {}
    for kind, ep, ns, frame, pay in recs:
        if kind == 2:
            yield ns, frame, kind, ep, -1, pay
            continue
        for i in range(0, len(pay) - 3, 4):
            hdr = pay[i]
            if hdr == 0:
                continue
            cable, cin = hdr >> 4, hdr & 15
            b = pay[i + 1:i + 1 + CIN_LEN[cin]]
            key = (kind, ep, cable)
            if cin == 4 or (cin in (5, 6, 7) and key in sysex) or (cin in (5, 6, 7) and b[:1] == b"\xf0"):
                buf = sysex.setdefault(key, bytearray())
                buf += b
                if cin != 4:
                    yield ns, frame, kind, ep, cable, bytes(sysex.pop(key))
            else:
                yield ns, frame, kind, ep, cable, bytes(b)


def label(msg):
    if not msg:
        return "(empty)"
    s = msg[0]
    if s == 0xF0:
        return "SysEx " + msg.hex(" ")
    if s == 0xF8:
        return "clock"
    if s == 0xF5 and len(msg) >= 3:
        return "F5 t=%d" % (msg[1] | msg[2] << 7)
    names = {0x80: "note-off", 0x90: "note-on", 0xA0: "poly-at", 0xB0: "cc", 0xC0: "program", 0xD0: "ch-at", 0xE0: "bend"}
    if s & 0xF0 in names:
        return "%s ch%d %s" % (names[s & 0xF0], (s & 15) + 1, msg[1:].hex(" "))
    return msg.hex(" ")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("trace")
    ap.add_argument("--stats", action="store_true", help="message counts per direction/cable/type instead of a listing")
    ap.add_argument("--no-clock", action="store_true", help="hide MIDI clock and F5 time stamps")
    ap.add_argument("--kinds", default="out,in,ctl")
    a = ap.parse_args()
    want = {k for k, v in KIND.items() if v in a.kinds.split(",")}
    t0 = None
    stats = Counter()
    for ns, frame, kind, ep, cable, msg in messages(records(a.trace)):
        if kind not in want:
            continue
        t0 = ns if t0 is None else t0
        if kind == 2:
            setup, res = msg[:8], struct.unpack_from("<i", msg, 8)[0] if len(msg) >= 12 else None
            text = "ctl %s -> %s %s" % (setup.hex(" "), res, msg[12:].hex(" "))
            typ = "ctl %02x/%02x idx %02x%02x" % (setup[0], setup[1], setup[5], setup[4])
        else:
            text = label(msg)
            typ = text.split(" ")[0] if not text.startswith("SysEx") else "SysEx " + msg[1:7].hex(" ") + (" " + msg[7:9].hex(" ") if len(msg) > 8 else "")
        if a.no_clock and text.split(" ")[0] in ("clock", "F5"):
            continue
        if a.stats:
            stats[(KIND[kind], ep, cable, typ)] += 1
        else:
            print("%10.3f %9s %-3s ep%02x c%-2s %s" % ((ns - t0) / 1e6, frame if frame >= 0 else "-", KIND[kind], ep,
                                                      cable if cable >= 0 else "-", text))
    for (k, ep, cable, typ), n in sorted(stats.items(), key=lambda x: -x[1]):
        print("%7d  %-3s ep%02x c%-2s %s" % (n, k, ep, cable if cable >= 0 else "-", typ))


if __name__ == "__main__":
    main()
