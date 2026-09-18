#!/usr/bin/env python3
"""extract_assets.py ROM OUTDIR : unpack a DS ROM's filesystem for a native build.

The Vita build does not read the ROM on the console. The game asks its filesystem for files by
path -- `FS_OpenFile("poketool/personal/pms.narc")`, and every one of the 2621 NARC_* calls in the
game goes through one table of such paths -- so the files can simply be files, laid out the way the
game asks for them, and packed into the VPK at build time. That is what this writes.

It produces, under OUTDIR:

    <the ROM's own filesystem>   340 files for Platinum, about 96 MB, at their own paths
    nitrofs.idx                  everything else the port needs from the ROM, which is small

nitrofs.idx holds three things that are not files in the ROM's filesystem:

  - the 512-byte cartridge header. The game reads its own header: CheckForMemoryTampering compares
    the maker code against Nintendo's and calls OS_Terminate if it does not match, and CARD_Init
    copies the header into the DS's hardware header buffers where the game looks for it.
  - the ARM9 overlay table, 32 bytes per module. The port links every overlay's code in, but the
    game still reads each module's original address and size back out of this table as metadata.
  - the path of every file, by file id, so that the port can answer FS_ConvertPathToFileID and
    FS_OpenFileFast without the ROM's own name and allocation tables.

Nothing here is redistributable, which is the point of doing it at build time from a ROM the person
building already owns: the repository has no game data in it, and neither does a build made without
--rom.
"""
import os
import sys


def u16(b, o):
    return b[o] | (b[o + 1] << 8)


def u32(b, o):
    return u16(b, o) | (u16(b, o + 2) << 16)


def main(argv):
    if len(argv) != 3:
        sys.exit("usage: extract_assets.py ROM OUTDIR")
    romPath, out = argv[1], argv[2]
    rom = open(romPath, "rb").read()
    if len(rom) < 512:
        sys.exit("%s is too short to be a DS ROM" % romPath)

    # The header names where the two filesystem tables live. FNT is the directory tree and the
    # names; FAT is one 8-byte start/end pair per file, indexed by file id.
    fntOff, fntLen = u32(rom, 0x40), u32(rom, 0x44)
    fatOff, fatLen = u32(rom, 0x48), u32(rom, 0x4C)
    if fntLen < 8 or fatLen % 8 or fntOff + fntLen > len(rom) or fatOff + fatLen > len(rom):
        sys.exit("%s does not have a readable filesystem (is it a DS ROM?)" % romPath)
    files = fatLen // 8

    # Walk the directory tree. Directory ids start at 0xF000, and entry 0 is the root, whose
    # "parent" field is the number of directories rather than a parent id.
    def walk(dirId, prefix, written):
        entry = fntOff + (dirId & 0xFFF) * 8
        p = fntOff + u32(rom, entry)
        fileId = u16(rom, entry + 4)
        while True:
            length = rom[p]
            p += 1
            if length == 0:
                break
            isDir = (length & 0x80) != 0
            name = rom[p:p + (length & 0x7F)].decode("ascii", "replace")
            p += length & 0x7F
            if isDir:
                sub = u16(rom, p)
                p += 2
                walk(sub, prefix + name + "/", written)
            else:
                written.append((prefix + name, fileId))
                fileId += 1

    written = []
    walk(0xF000, "", written)
    if not written:
        sys.exit("%s has an empty filesystem" % romPath)

    total = 0
    for path, fileId in written:
        if fileId >= files:
            sys.exit("the filesystem names file %u but the table has %u" % (fileId, files))
        start = u32(rom, fatOff + fileId * 8)
        end = u32(rom, fatOff + fileId * 8 + 4)
        if end < start or end > len(rom):
            sys.exit("file %s (%u) is outside the ROM" % (path, fileId))
        dest = os.path.join(out, path)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as f:
            f.write(rom[start:end])
        total += end - start

    # The ARM9 overlay table: the header says where it is and how long, 32 bytes a module.
    ovtOff, ovtLen = u32(rom, 0x50), u32(rom, 0x54)
    if ovtLen % 32 or ovtOff + ovtLen > len(rom):
        sys.exit("%s has no readable ARM9 overlay table" % romPath)

    # nitrofs.idx. Little-endian throughout, to match the console it is read on.
    #
    #   "VPOKFS1\0"  u32 fileCount  u32 overlayBytes
    #   512 bytes of cartridge header
    #   overlayBytes of overlay table
    #   fileCount times: u16 pathLength, pathLength bytes of path (no terminator, no leading slash)
    #
    # Files with no name in the ROM's filesystem -- the overlay images, which this port does not
    # need, because their code is linked in -- get a zero length and are skipped above.
    byId = dict((fileId, path) for path, fileId in written)
    idx = bytearray()
    idx += b"VPOKFS1\0"
    idx += files.to_bytes(4, "little")
    idx += ovtLen.to_bytes(4, "little")
    idx += rom[:512]
    idx += rom[ovtOff:ovtOff + ovtLen]
    for fileId in range(files):
        path = byId.get(fileId, "").encode("ascii")
        idx += len(path).to_bytes(2, "little")
        idx += path
    with open(os.path.join(out, "nitrofs.idx"), "wb") as f:
        f.write(idx)

    print("    %u files, %.1f MB, from %s" % (len(written), total / 1048576.0,
                                              os.path.basename(romPath)))
    print("    nitrofs.idx: %u file ids, %u overlay modules, %u bytes"
          % (files, ovtLen // 32, len(idx)))
    print("    game code %s, maker code %s" % (rom[0x0C:0x10].decode("ascii", "replace"),
                                               rom[0x10:0x12].decode("ascii", "replace")))


if __name__ == "__main__":
    main(sys.argv)
