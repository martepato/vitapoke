#!/usr/bin/env python3
"""Write a DS ROM's scaffolding -- and nothing else -- so the port's startup can be tested.

The port cannot be tested without a ROM, and this repository will never contain one. But most of what
happens before the game's first frame does not need the game's data: it needs a file shaped like a DS
ROM. That is what this writes.

    tests/vita/make_probe_rom.py probe.nds

What is in it: a 512-byte DS header, an ARM9 overlay table with one record per module (the port reads
this at startup and checks it against what it was built for), and an empty file name table and file
allocation table, so that the port's file system initialises and then reports that every file it asks
for is missing.

What is not in it: any of the game. No code, no graphics, no text, no music. Nothing here came from a
cartridge, and a build given this file will start up and then stop, because the first thing the game
does is read a file.

So this tests exactly the part of the port that is not the game: the module loading, the platform
layer, the arena, the overlay table reader, the save file, the renderer coming up and the first
frame. Those are the things that fail first and are hardest to see any other way.
"""
import json
import struct
import sys
from pathlib import Path

HEADER = 512
MODULES = 122          # Platinum's ARM9 overlay count; checked against modules.json when present


def module_count() -> int:
    """Take the count from the build tree if it is there, so this cannot drift from the port."""
    generated = Path(__file__).resolve().parents[2] / ".work/vita/test_out/native-audio-app/overlays/modules.json"
    if generated.is_file():
        return len(json.loads(generated.read_text()))
    return MODULES


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    count = module_count()

    # One FAT entry (start, end) for file 0, and a file name table with a single root directory.
    fat = struct.pack("<II", 0, 0)
    fnt = struct.pack("<IHH", 8, 0, 1) + b"\0\0\0\0"     # subtable offset, first file id, 1 directory

    overlay_offset = HEADER
    overlay = b"".join(
        struct.pack("<8I", i, 0x02000000, 0, 0, 0, 0, i, 0) for i in range(count)
    )
    fnt_offset = overlay_offset + len(overlay)
    fat_offset = fnt_offset + len(fnt)
    total = fat_offset + len(fat)

    header = bytearray(HEADER)
    header[0x00:0x0C] = b"VITAPOKEPROB"                 # game title, so nobody mistakes this for a dump
    header[0x0C:0x10] = b"PRBE"                         # game code
    # The game refuses to start on a cartridge whose maker code is not Nintendo's: see
    # CheckForMemoryTampering in the decompilation's boot.c, which calls OS_Terminate otherwise. Two
    # bytes of header, so that startup can be tested at all. There is still no game data here.
    header[0x10:0x12] = b"01"
    struct.pack_into("<I", header, 0x40, fnt_offset)
    struct.pack_into("<I", header, 0x44, len(fnt))
    struct.pack_into("<I", header, 0x48, fat_offset)
    struct.pack_into("<I", header, 0x4C, len(fat))
    struct.pack_into("<I", header, 0x50, overlay_offset)
    struct.pack_into("<I", header, 0x54, len(overlay))

    Path(sys.argv[1]).write_bytes(bytes(header) + overlay + fnt + fat)
    print(f"wrote {sys.argv[1]}: {total} bytes, {count} overlay modules, no game data")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
