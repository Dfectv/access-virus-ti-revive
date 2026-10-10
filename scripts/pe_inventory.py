#!/usr/bin/env python3
"""pe_inventory.py OUTDIR FILE... - per-PE facts for findings/inventory.md.

Writes OUTDIR/<basename>.txt with full imports/exports/version/signers and prints one
compact summary line per file. Run with PYTHONPATH=~/tools/pylib (pefile).
"""
import hashlib, os, subprocess, sys, time
import pefile

MACH = {0x14c: "x86", 0x8664: "x64"}


def signers(pe, data):
    d = pe.OPTIONAL_HEADER.DATA_DIRECTORY[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_SECURITY"]]
    if not d.VirtualAddress:
        return []
    blob = data[d.VirtualAddress + 8:d.VirtualAddress + d.Size]  # skip WIN_CERTIFICATE header
    r = subprocess.run(["openssl", "pkcs7", "-inform", "DER", "-print_certs", "-noout"],
                       input=blob, capture_output=True)
    return [l.strip()
            for l in r.stdout.decode(errors="replace").splitlines() if l.startswith("subject=")]


def verinfo(pe):
    out = {}
    for fi in getattr(pe, "FileInfo", []) or []:
        for e in fi:
            for st in getattr(e, "StringTable", []):
                for k, v in st.entries.items():
                    out[k.decode(errors="replace")] = v.decode(errors="replace")
    return out


def main():
    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)
    for path in sys.argv[2:]:
        data = open(path, "rb").read()
        pe = pefile.PE(data=data)
        sha = hashlib.sha256(data).hexdigest()
        arch = MACH.get(pe.FILE_HEADER.Machine, hex(pe.FILE_HEADER.Machine))
        ts = time.strftime("%Y-%m-%d", time.gmtime(pe.FILE_HEADER.TimeDateStamp))
        vi = verinfo(pe)
        imps = {}
        for e in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
            imps[e.dll.decode()] = [(i.name.decode() if i.name else f"#{i.ordinal}") for i in e.imports]
        exps = [(s.name.decode() if s.name else f"#{s.ordinal}")
                for s in getattr(getattr(pe, "DIRECTORY_ENTRY_EXPORT", None), "symbols", [])]
        sig = signers(pe, data)
        name = os.path.basename(path)
        with open(os.path.join(outdir, name + ".txt"), "w") as f:
            f.write(f"path: {path}\nsha256: {sha}\nsize: {len(data)}\narch: {arch}\nlink_ts: {ts}\n")
            f.write("version:\n" + "".join(f"  {k}: {v}\n" for k, v in vi.items()))
            f.write("signers:\n" + "".join(f"  {s}\n" for s in sig))
            f.write(f"exports ({len(exps)}):\n" + "".join(f"  {x}\n" for x in exps))
            for dll, fns in imps.items():
                f.write(f"import {dll} ({len(fns)}):\n" + "".join(f"  {x}\n" for x in fns))
        print(f"{name}\t{sha[:16]}\t{arch}\t{ts}\t{vi.get('FileVersion', '-')}\t"
              f"{vi.get('CompanyName', '-')}\texp={len(exps)}\timp={','.join(imps)}")


if __name__ == "__main__":
    main()
