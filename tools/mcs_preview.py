#!/usr/bin/env python3
"""mcs_preview.py - dump the Mednafen savestate's built-in preview image.

WHY: a Saturn savestate (.mc0/.mcs) embeds a small RGB snapshot of the
framebuffer at the moment of capture (mednafen-git src/state.cpp:614-625):
  header = 8 magic + 8 ts + 4 ver + 4 size/flags + 4 prev_w + 4 prev_h
  then prev_w*prev_h*3 bytes of RGB888 at offset 0x20.
This is the fastest reliable way to SEE what a captured frame rendered,
without driving an emulator UI. Used to confirm post-title-card GHZ
gameplay actually renders (the #192 visual-crash check).
"""
import gzip
import struct
import sys

MAGIC = b"MDFNSVST"


def main(argv=None):
    argv = argv if argv is not None else sys.argv[1:]
    if len(argv) != 2:
        sys.stderr.write("usage: mcs_preview.py state.mcs out.png\n")
        return 2
    raw = open(argv[0], "rb").read()
    dec = gzip.decompress(raw) if raw[:2] == b"\x1f\x8b" else raw
    if dec[:8] != MAGIC:
        sys.stderr.write("not a Mednafen savestate (bad magic %r)\n" % dec[:8])
        return 2
    w = struct.unpack("<I", dec[24:28])[0]
    h = struct.unpack("<I", dec[28:32])[0]
    npix = w * h
    rgb = dec[32:32 + npix * 3]
    if len(rgb) < npix * 3:
        sys.stderr.write("preview truncated: have %d need %d\n"
                         % (len(rgb), npix * 3))
        return 1
    print("preview %dx%d (%d bytes RGB)" % (w, h, npix * 3))
    _write_png(argv[1], w, h, rgb)
    print("wrote %s" % argv[1])
    return 0


def _write_png(path, w, h, rgb):
    import zlib
    # RGB888 -> PNG, filter 0 per scanline.
    rows = bytearray()
    stride = w * 3
    for y in range(h):
        rows.append(0)
        rows += rgb[y * stride:(y + 1) * stride]
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(bytes(rows), 9))
           + chunk(b"IEND", b""))
    open(path, "wb").write(png)


if __name__ == "__main__":
    sys.exit(main())
