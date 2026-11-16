#!/usr/bin/env python3
"""Generate small, synthetic packages for manual and automated testing.

Every file produced here is created from scratch by this script. No commercial
or copyrighted content is included, which keeps the test corpus reproducible
and legally safe.

Usage: python3 tools/make_test_packages.py samples
"""

from __future__ import annotations

import os
import struct
import sys
import zipfile
from pathlib import Path


def write_elf(path: Path) -> None:
    ident = b"\x7fELF" + bytes([2, 1, 1, 0]) + b"\x00" * 8
    interp = b"/lib64/ld-linux-x86-64.so.2\x00"
    interp_offset = 0x100
    header = ident + struct.pack(
        "<HHIQQQIHHHHHH",
        2,        # ET_EXEC
        0x3E,     # EM_X86_64
        1,        # version
        0x401000, # entry
        0x40,     # program header offset
        0x800,    # section header offset
        0,        # flags
        64,       # ehsize
        56,       # phentsize
        1,        # phnum
        64,       # shentsize
        2,        # shnum
        0,        # shstrndx
    )
    phdr = struct.pack(
        "<IIQQQQQQ",
        3,              # PT_INTERP
        4,              # flags
        interp_offset,  # offset
        0, 0,
        len(interp),    # filesz
        len(interp),    # memsz
        1,              # align
    )
    data = bytearray(0x200)
    data[0:len(header)] = header
    data[0x40:0x40 + len(phdr)] = phdr
    data[interp_offset:interp_offset + len(interp)] = interp
    path.write_bytes(bytes(data))


def write_pe(path: Path) -> None:
    e_lfanew = 0x80
    optional_size = 240
    sections = 2
    dos = bytearray(0x40)
    dos[0:2] = b"MZ"
    struct.pack_into("<I", dos, 0x3C, e_lfanew)

    coff = struct.pack(
        "<HHIIIHH",
        0x8664,     # machine x64
        sections,
        0x60000000, # timestamp
        0, 0,
        optional_size,
        0x0022,     # executable + large address aware
    )
    optional = bytearray(optional_size)
    struct.pack_into("<H", optional, 0, 0x20B)          # PE32+
    struct.pack_into("<I", optional, 16, 0x1000)        # entry RVA
    struct.pack_into("<Q", optional, 24, 0x140000000)   # image base
    struct.pack_into("<H", optional, 68, 3)             # console subsystem

    def section(name: bytes, vsize: int, vaddr: int, raw_size: int, raw_ptr: int) -> bytes:
        entry = bytearray(40)
        entry[0:len(name)] = name
        struct.pack_into("<IIII", entry, 8, vsize, vaddr, raw_size, raw_ptr)
        return bytes(entry)

    table = section(b".text", 0x200, 0x1000, 0x200, 0x400) + section(
        b".data", 0x100, 0x2000, 0x100, 0x600
    )

    data = bytearray(0x800)
    data[0:0x40] = dos
    data[e_lfanew:e_lfanew + 4] = b"PE\x00\x00"
    data[e_lfanew + 4:e_lfanew + 24] = coff
    data[e_lfanew + 24:e_lfanew + 24 + optional_size] = optional
    start = e_lfanew + 24 + optional_size
    data[start:start + len(table)] = table
    data[0x400:0x410] = b"PKG Studio test"
    data[0x600:0x610] = b"data section"
    path.write_bytes(bytes(data))


def write_apk(path: Path) -> None:
    manifest = bytes.fromhex("03000800") + b"PKG Studio synthetic manifest"
    dex = b"dex\n035\x00" + b"\x00" * 108
    with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as archive:
        archive.writestr("AndroidManifest.xml", manifest)
        archive.writestr("classes.dex", dex)
        archive.writestr("META-INF/MANIFEST.MF", b"Manifest-Version: 1.0\n")
        archive.writestr("assets/readme.txt", b"synthetic test asset\n")


def write_vita_pkg(path: Path) -> None:
    # 36 character content id, NUL padded to the 0x30 byte header field.
    content_id = b"UP0000-PCSG00001_00-0000000000000001".ljust(0x30, b"\x00")
    assert len(content_id) == 0x30
    data_offset = 0x1000
    payload = b"\x00" * 0x200
    total = data_offset + len(payload)

    header = struct.pack(
        ">IHHIIIIQQQ",
        0x7F504B47,
        0x8000,    # finalized
        2,         # PSP / PS Vita
        0x280,     # metadata offset
        8,         # metadata entries
        0x160,     # metadata size
        3,         # file items
        total,
        data_offset,
        len(payload),
    )
    header += content_id
    header += b"\x00" * 0x10   # digest
    header += b"\x00" * 0x10   # data iv
    header += b"\x00" * 0x40   # signatures

    data = bytearray(total)
    data[0:len(header)] = header
    data[data_offset:] = payload
    path.write_bytes(bytes(data))


def write_ps4_pkg(path: Path) -> None:
    content_id = b"UP0000-CUSA00001_00-0000000000000001"
    assert len(content_id) == 0x24
    total = 0x3800
    header = bytearray(0x1000)
    struct.pack_into(">I", header, 0x00, 0x7F434E54)
    struct.pack_into(">I", header, 0x04, 0x1A)
    struct.pack_into(">I", header, 0x08, 0)
    struct.pack_into(">I", header, 0x0C, 5)
    struct.pack_into(">I", header, 0x10, 5)
    struct.pack_into(">H", header, 0x14, 0)
    struct.pack_into(">H", header, 0x16, 5)
    struct.pack_into(">I", header, 0x18, 0x1000)
    struct.pack_into(">I", header, 0x1C, 0x500)
    struct.pack_into(">Q", header, 0x20, 0x2000)
    struct.pack_into(">Q", header, 0x28, 0x1000)
    struct.pack_into(">Q", header, 0x30, 0x3000)
    struct.pack_into(">Q", header, 0x38, 0x800)
    header[0x40:0x40 + len(content_id)] = content_id
    struct.pack_into(">I", header, 0x70, 1)      # drm type
    struct.pack_into(">I", header, 0x74, 0x1A)   # content type
    struct.pack_into(">I", header, 0x78, 0)      # content flags
    struct.pack_into(">I", header, 0x80, 0x20200101)
    struct.pack_into(">I", header, 0x84, 0x12345678)
    struct.pack_into(">I", header, 0x404, 1)     # pfs image count
    struct.pack_into(">Q", header, 0x410, 0x3000)
    struct.pack_into(">Q", header, 0x418, 0x800)
    struct.pack_into(">Q", header, 0x430, total)

    data = bytearray(total)
    data[0:0x1000] = header
    data[0x2000:0x2100] = b"PKG Studio synthetic entry table" + b"\x00" * (0x100 - 28)
    data[0x3000:0x3800] = b"\xAA" * 0x800
    path.write_bytes(bytes(data))


def write_plain(path: Path) -> None:
    path.write_bytes(bytes((i * 7 + 13) & 0xFF for i in range(1024)))


def main() -> int:
    destination = Path(sys.argv[1] if len(sys.argv) > 1 else "samples")
    destination.mkdir(parents=True, exist_ok=True)

    write_elf(destination / "sample.elf")
    write_pe(destination / "sample.exe")
    write_apk(destination / "sample.apk")
    write_vita_pkg(destination / "sample_vita.pkg")
    write_ps4_pkg(destination / "sample_ps4.pkg")
    write_plain(destination / "unrecognized.bin")

    for entry in sorted(destination.iterdir()):
        if entry.is_file():
            print(f"{entry}  {entry.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
