#!/usr/bin/env python3
"""timing_analysis.py REC.wav [--bpm 120] [--div 4] - measure the timing of a recorded Virus TI loop.

Test (findings E067): a MIDI clip of short, percussive notes on a fixed grid (default 16th notes at 120 BPM)
plays the Virus through the plug-in; the plug-in output is recorded in the DAW. This script detects note onsets
and fits them to the grid:
  latency  constant offset of the first onset (relative to the file start: only meaningful between runs)
  drift    slope of (measured - expected) over time, in ppm and ms per minute (clock mismatch, e.g. 48k vs 44.1k)
  jitter   standard deviation of the residuals after the linear fit
  missing  grid slots without an onset / extra onsets
Reads 16/24/32-bit PCM and 32-bit float WAV (stdlib only).
"""
import argparse
import struct
import sys
import wave


def read_wav(path):
    """Mono float samples and the sample rate (WAVE_FORMAT_PCM and IEEE float)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        sys.exit("not a WAV file")
    pos, fmt, raw = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack_from("<I", data, pos + 4)[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            tag, ch, rate, _, align, bits = struct.unpack_from("<HHIIHH", body)
            if tag == 0xFFFE:  # extensible: real tag in the sub-format GUID
                tag = struct.unpack_from("<H", body, 24)[0]
            fmt = (tag, ch, rate, align, bits)
        elif cid == b"data":
            raw = body
        pos += 8 + size + (size & 1)
    tag, ch, rate, align, bits = fmt
    n = len(raw) // align
    out = []
    for i in range(n):
        frame = raw[i * align:(i + 1) * align]
        acc = 0.0
        for c in range(ch):
            b = frame[c * bits // 8:(c + 1) * bits // 8]
            if tag == 3 and bits == 32:
                v = struct.unpack("<f", b)[0]
            elif bits == 16:
                v = struct.unpack("<h", b)[0] / 32768.0
            elif bits == 24:
                v = int.from_bytes(b, "little", signed=True) / 8388608.0
            elif bits == 32:
                v = struct.unpack("<i", b)[0] / 2147483648.0
            else:
                sys.exit(f"unsupported format tag {tag} / {bits} bit")
            acc += v
        out.append(acc / ch)
    return out, rate


def onsets(x, rate, hold_ms=40.0):
    """Onsets = upward crossings of a threshold on a 1 ms RMS envelope, with a hold-off."""
    win = max(1, rate // 1000)
    env = []
    for i in range(0, len(x) - win, win):
        s = sum(v * v for v in x[i:i + win]) / win
        env.append(s ** 0.5)
    peak = max(env) if env else 0
    if peak <= 0:
        return []
    thr = peak * 0.2
    res, hold, armed = [], int(hold_ms), True
    i = 0
    while i < len(env):
        if armed and env[i] >= thr:
            # refine to the sample where |x| first exceeds 20 % of the peak within this millisecond
            base = i * win
            frac = 0
            for k in range(win):
                if abs(x[base + k]) >= thr:
                    frac = k
                    break
            res.append((base + frac) / rate)
            armed = False
            i += hold
            continue
        if env[i] < thr * 0.3:
            armed = True
        i += 1
    return res


def fit(on, period):
    """Number onsets by the gap to the previous one (robust against a large drift, where rounding against the
    nominal grid would mis-assign slots), then least-squares fit t = a + b * k."""
    pts = [(0, on[0])]
    for t in on[1:]:
        steps = round((t - pts[-1][1]) / period)
        if steps < 1:
            continue  # double trigger
        pts.append((pts[-1][0] + steps, t))
    n = len(pts)
    sk = sum(k for k, _ in pts)
    st = sum(t for _, t in pts)
    skk = sum(k * k for k, _ in pts)
    skt = sum(k * t for k, t in pts)
    b = (n * skt - sk * st) / (n * skk - sk * sk)
    a = (st - b * sk) / n
    res = [t - (a + b * k) for k, t in pts]
    sd = (sum(r * r for r in res) / n) ** 0.5
    span = pts[-1][0] + 1
    return a, b, sd, span - n, len(on) - n, res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--bpm", type=float, default=120.0)
    ap.add_argument("--div", type=int, default=4, help="notes per beat (4 = 16th notes)")
    a = ap.parse_args()
    x, rate = read_wav(a.wav)
    on = onsets(x, rate)
    if len(on) < 4:
        sys.exit(f"only {len(on)} onsets found")
    period = 60.0 / a.bpm / a.div
    off, per, sd, missing, extra, res = fit(on, period)
    drift_ppm = (per / period - 1) * 1e6
    print(f"file {a.wav}: {len(x) / rate:.1f} s at {rate} Hz, {len(on)} onsets")
    print(f"first onset         {on[0] * 1000:9.2f} ms")
    print(f"grid period         {period * 1000:9.3f} ms expected, {per * 1000:9.3f} ms measured")
    print(f"drift               {drift_ppm:+9.0f} ppm  ({drift_ppm * 60 / 1e3:+.1f} ms per minute)")
    print(f"jitter (std dev)    {sd * 1000:9.2f} ms   max |dev| {max(abs(r) for r in res) * 1000:.2f} ms")
    print(f"missing / extra     {missing} / {extra}")


if __name__ == "__main__":
    main()
