#!/usr/bin/env python3
"""macho_syms.py THIN_MACHO [--exports | --imports [LIBSUBSTR]] - symbols of a thin 64-bit Mach-O (no lipo/nm
needed): --exports: defined external symbols; --imports: undefined symbols with the dylib they bind to
(two-level namespace library ordinal), optionally filtered by a substring of the dylib path."""
import struct
import sys

LC_SYMTAB, LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB, LC_LAZY_LOAD_DYLIB = 0x2, 0xC, 0x80000018, 0x8000001F, 0x20
N_TYPE, N_EXT, N_UNDF, N_SECT = 0x0E, 0x01, 0x0, 0xE


def main():
    data = open(sys.argv[1], "rb").read()
    magic, cpu, sub, ftype, ncmds, sizecmds, flags, _ = struct.unpack("<IiiIIIII", data[:32])
    if magic != 0xFEEDFACF:
        sys.exit("not a thin 64-bit Mach-O")
    off, dylibs, symtab = 32, [], None
    for _ in range(ncmds):
        cmd, size = struct.unpack("<II", data[off:off + 8])
        if cmd in (LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB, LC_LAZY_LOAD_DYLIB):
            name_off = struct.unpack("<I", data[off + 8:off + 12])[0]
            name = data[off + name_off:off + size].split(b"\0")[0].decode()
            dylibs.append(name + (" (weak)" if cmd == LC_LOAD_WEAK_DYLIB else ""))
        elif cmd == LC_SYMTAB:
            symtab = struct.unpack("<IIII", data[off + 8:off + 24])
        off += size
    mode = sys.argv[2] if len(sys.argv) > 2 else "--exports"
    filt = sys.argv[3] if len(sys.argv) > 3 else ""
    if mode == "--dylibs":
        for i, d in enumerate(dylibs, 1):
            print(i, d)
        return
    symoff, nsyms, stroff, strsize = symtab
    out = set()
    for i in range(nsyms):
        n_strx, n_type, n_sect, n_desc, n_value = struct.unpack("<IBBHQ", data[symoff + i * 16:symoff + i * 16 + 16])
        if not n_type & N_EXT or n_type & 0xE0:  # skip debug (stab) entries
            continue
        name = data[stroff + n_strx:data.index(b"\0", stroff + n_strx)].decode(errors="replace")
        if mode == "--exports" and (n_type & N_TYPE) == N_SECT:
            out.add(name)
        elif mode == "--imports" and (n_type & N_TYPE) == N_UNDF:
            ordinal = (n_desc >> 8) & 0xFF
            lib = dylibs[ordinal - 1] if 0 < ordinal <= len(dylibs) else f"ordinal {ordinal}"
            if filt in lib:
                out.add(f"{name}\t{lib.rsplit('/', 1)[-1]}")
    print("\n".join(sorted(out)))


if __name__ == "__main__":
    main()
