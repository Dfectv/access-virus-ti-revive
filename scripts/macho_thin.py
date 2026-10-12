#!/usr/bin/env python3
"""macho_thin.py FAT OUT [arch] - extract one architecture slice from a universal (fat) Mach-O, like
`lipo -thin`, for tools that cannot pick a slice (Ghidra via REA). arch: x86_64 (default), i386, arm64."""
import struct
import sys

CPU = {"i386": 7, "x86_64": 0x01000007, "arm64": 0x0100000C}


def main():
    src, dst = sys.argv[1], sys.argv[2]
    want = CPU[sys.argv[3] if len(sys.argv) > 3 else "x86_64"]
    data = open(src, "rb").read()
    magic, n = struct.unpack(">II", data[:8])
    if magic not in (0xCAFEBABE, 0xCAFEBABF):
        sys.exit("not a universal Mach-O")
    wide = magic == 0xCAFEBABF  # fat_arch_64
    for i in range(n):
        if wide:
            cpu, sub, off, size, align, _ = struct.unpack(">iiQQII", data[8 + i * 32:8 + i * 32 + 32])
        else:
            cpu, sub, off, size, align = struct.unpack(">iiIII", data[8 + i * 20:8 + i * 20 + 20])
        if cpu == want:
            open(dst, "wb").write(data[off:off + size])
            print(f"slice cpu 0x{cpu:x}: {size} bytes -> {dst}")
            return
    sys.exit("architecture not in file")


if __name__ == "__main__":
    main()
