"""Builds noite's icon from the hand-drawn pixel grids next to this script (M8b).

noite-16.txt and noite-32.txt hold one character per pixel (palette below). Sizes 16 and
32 are drawn as they are; 48 = 16 x 3, 64 = 32 x 2, 128 = 32 x 4, 256 = 32 x 8, scaled
nearest-neighbour so the pixels stay crisp. Writes noite.ico (every size, PNG-encoded; the
exe's icon) and noite-<size>.png (the app's QIcon and the README) here. With --preview DIR,
also writes the PNGs to DIR.

Run with `python -I packaging/icon/make_icon.py`. Standard library only.
"""

import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))

PALETTE = {
    ".": (0x0E, 0x1A, 0x40),  # sky, dark blue
    "#": (0xFF, 0xF1, 0xA6),  # moon, light yellow
    "+": (0xE8, 0xD2, 0x7A),  # moon shade, along the inner edge
    "*": (0x3F, 0xB8, 0xFF),  # star, bright blue
    "W": (0xC8, 0xEC, 0xFF),  # star centre, near white
}

# (size, grid, factor)
SIZES = [(16, 16, 1), (32, 32, 1), (48, 16, 3), (64, 32, 2), (128, 32, 4), (256, 32, 8)]


def read_grid(n):
    path = os.path.join(HERE, "noite-%d.txt" % n)
    with open(path, encoding="ascii") as f:
        rows = [line.rstrip("\r\n") for line in f if line.strip()]
    if len(rows) != n or any(len(r) != n for r in rows):
        sys.exit("%s: expected %d rows of %d characters" % (path, n, n))
    for r in rows:
        for c in r:
            if c not in PALETTE:
                sys.exit("%s: unknown pixel %r" % (path, c))
    return rows


def png(rows, factor):
    size = len(rows) * factor
    raw = bytearray()
    for r in rows:
        line = bytearray([0])  # filter: none
        for c in r:
            line += bytes(PALETTE[c] + (0xFF,)) * factor
        raw += bytes(line) * factor
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)  # 8-bit RGBA
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")


def ico(images):
    """images: list of (size, png bytes)."""
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for size, data in images:
        side = 0 if size >= 256 else size  # 0 means 256
        out += struct.pack("<BBBBHHII", side, side, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for _, data in images:
        out += data
    return out


def main():
    preview = None
    if len(sys.argv) == 3 and sys.argv[1] == "--preview":
        preview = sys.argv[2]
    elif len(sys.argv) != 1:
        sys.exit("usage: make_icon.py [--preview DIR]")
    grids = {16: read_grid(16), 32: read_grid(32)}
    images = [(size, png(grids[grid], factor)) for size, grid, factor in SIZES]
    with open(os.path.join(HERE, "noite.ico"), "wb") as f:
        f.write(ico(images))
    # One PNG per size: the app's QIcon (a Qt resource) and the README use these.
    for folder in [HERE] + ([preview] if preview else []):
        os.makedirs(folder, exist_ok=True)
        for size, data in images:
            with open(os.path.join(folder, "noite-%d.png" % size), "wb") as f:
                f.write(data)
    print("noite.ico: %s" % ", ".join(str(s) for s, _ in images))


if __name__ == "__main__":
    main()
