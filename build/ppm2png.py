#!/usr/bin/env python3
"""Convert the test's PPM dumps to PNG so they can be eyeballed.

Usage:  python build/ppm2png.py build/shots/gf_scene.ppm [more.ppm ...]
        python build/ppm2png.py build/shots/*.ppm

Binary P6 only, 8 bits per channel -- which is exactly what tests/test_guofeng.cpp
and tests/test_warfire.cpp write.  Hand-rolled rather than pulled from Pillow so
the repository keeps its "no third-party packages" property.
"""
import struct
import sys
import zlib


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()

    # header: P6 <ws> w <ws> h <ws> maxval <single ws> then raw RGB
    if not data.startswith(b"P6"):
        raise ValueError("%s: not a binary PPM" % path)

    pos = 2
    fields = []
    while len(fields) < 3:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    pos += 1                                    # the single whitespace byte

    w, h, maxval = fields
    if maxval != 255:
        raise ValueError("%s: only 8-bit PPMs are supported" % path)

    return w, h, data[pos:pos + w * h * 3]


def write_png(path, w, h, rgb):
    raw = bytearray()
    stride = w * 3
    for y in range(h):
        raw.append(0)                           # filter type 0 (None)
        raw += rgb[y * stride:(y + 1) * stride]

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")

    with open(path, "wb") as f:
        f.write(png)


def main(argv):
    if not argv:
        print(__doc__)
        return 1
    for src in argv:
        w, h, rgb = read_ppm(src)
        dst = src.rsplit(".", 1)[0] + ".png"
        write_png(dst, w, h, rgb)
        print("%s -> %s  (%dx%d)" % (src, dst, w, h))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
