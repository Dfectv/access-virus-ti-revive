#!/usr/bin/env python3
"""usbdecode.py CAPTURE.pcapng ADDR - decode non-iso Virus TI traffic from a usbmon capture.

Pairs URB submit/complete by urb_id, prints one line per transfer:
  time  EP  dir  status  [setup]  payload
and, for interrupt EPs 0x05/0x85/0x06/0x86, splits payloads into 4-byte USB-MIDI
event packets (cable<<4 | CIN) and reassembles SysEx per (EP, cable).
Run with: python3 -I scripts/usbdecode.py captures/X.pcapng 17
"""
import subprocess, sys, os

CIN_LEN = {0x2: 2, 0x3: 3, 0x4: 3, 0x5: 1, 0x6: 2, 0x7: 3, 0x8: 3, 0x9: 3,
           0xA: 3, 0xB: 3, 0xC: 2, 0xD: 2, 0xE: 3, 0xF: 1}
FIELDS = ["frame.time_relative", "usb.urb_id", "usb.endpoint_address", "usb.urb_type",
          "usb.urb_status", "usb.bmRequestType", "usb.setup.bRequest", "usb.setup.wValue",
          "usb.setup.wIndex", "usb.setup.wLength", "usb.capdata",
          "usb.data_fragment", "usb.control.Response"]


def rows(path, addr):
    ts = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ts.sh")
    cmd = [ts, path, "-Y", f"usb.device_address == {addr} && usb.transfer_type != 0x00", "-T", "fields"]
    for f in FIELDS:
        cmd += ["-e", f]
    # no check=True: tshark exits non-zero on a truncated (still-being-written) capture but still prints the frames
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    for line in out.splitlines():
        yield dict(zip(FIELDS, line.split("\t")))


def main():
    path, addr = sys.argv[1], sys.argv[2]
    pending, sysex = {}, {}
    for r in rows(path, addr):
        if r["usb.urb_type"] == "'S'":
            pending[r["usb.urb_id"]] = r
            continue
        s = pending.pop(r["usb.urb_id"], {})
        ep = r["usb.endpoint_address"]
        data = next((x for x in (s.get("usb.capdata"), s.get("usb.data_fragment"), r["usb.capdata"],
                                 r["usb.control.Response"]) if x), "").replace(":", "")
        setup = ""
        if s.get("usb.bmRequestType"):
            setup = "setup %s req=%s wV=%s wI=%s wL=%s " % tuple(
                s[k] for k in FIELDS[5:10])
        line = f"{float(r['frame.time_relative']):10.6f} ep={ep:<5} st={r['usb.urb_status']:<4} {setup}{data}"
        midi = []
        if data == "4e735201":
            midi.append("HANDSHAKE (raw, not USB-MIDI)")
        elif ep in ("0x05", "0x85", "0x06", "0x86") and data:
            b = bytes.fromhex(data)
            for i in range(0, len(b) - 3, 4):
                hdr = b[i]
                cab, cin = hdr >> 4, hdr & 0xF
                if hdr == 0:
                    continue
                ev = b[i + 1:i + 1 + CIN_LEN.get(cin, 3)]
                key = (ep, cab)
                if cin in (0x4, 0x5, 0x6, 0x7) or ev[:1] == b"\xf0":
                    sysex[key] = sysex.get(key, b"") + ev
                    if b"\xf7" in ev:
                        midi.append(f"c{cab} SYSEX {sysex.pop(key).hex(' ')}")
                else:
                    midi.append(f"c{cab} cin{cin:x} {ev.hex(' ')}")
        print(line + ("  | " + " ; ".join(midi) if midi else ""))


if __name__ == "__main__":
    main()
