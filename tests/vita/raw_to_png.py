#!/usr/bin/env python3
"""Turn the renderer's frame dumps into PNGs.

A build made with FRAME_DUMP=<n> writes ux0:data/vitapoke/frameNNNNN.raw every n frames: both DS
screens in one file, 256x192 each, R,G,B,A bytes, the top screen first. This writes one PNG per file,
with the two screens side by side the way the Vita shows them.

    tests/vita/raw_to_png.py frame00000.raw [more.raw ...]

No dependencies: PNG is a zlib stream with a header, and zlib is in the standard library.
"""
import struct
import sys
import zlib
from pathlib import Path

W, H = 256, 192


def png(path: Path, width: int, height: int, rgba: bytes) -> None:
    raw = b"".join(b"\0" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 6))
                     + chunk(b"IEND", b""))


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2
    for name in argv[1:]:
        src = Path(name)
        data = src.read_bytes()
        if len(data) != 2 * W * H * 4:
            print(f"{src}: {len(data)} bytes, expected {2 * W * H * 4}")
            continue
        # Side by side, as the console lays them out: top screen left, touch screen right.
        rows = []
        for y in range(H):
            top = data[y * W * 4:(y + 1) * W * 4]
            bottom = data[(H + y) * W * 4:(H + y + 1) * W * 4]
            rows.append(top + bottom)
        out = src.with_suffix(".png")
        png(out, W * 2, H, b"".join(rows))
        print(f"{out} ({W * 2}x{H})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
