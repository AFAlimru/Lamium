"""Independent LMR2 encoder/reader for the cross-language fixture test.

Requires Python's zstandard package. Run --write to regenerate the golden
file, or pass a C++-written .lm2 to verify the same data independently.
"""
import argparse
import pathlib
import struct
import zlib
import zstandard

FIXTURE = pathlib.Path(__file__).resolve().parents[1] / "tests/fixtures/lmr2/r.-2.3.lm2"
N = 256 * 256


def values():
    colors = [0 if i % 17 == 0 else 0xFF000000 | ((i * 7919) & 0xFFFFFF) for i in range(N)]
    heights = [0 if c == 0 else ((i * 32767 + (i // 256) * 123) & 65535) - 32768
               for i, c in enumerate(colors)]
    stamps = [(1700000000000 + i * 12345, i % 4) for i in range(256)]
    return colors, heights, stamps


def encode():
    colors, heights, stamps = values()
    palette = list(dict.fromkeys(colors))
    lookup = {c: i for i, c in enumerate(palette)}
    indices = [lookup[c] for c in colors]
    colr = struct.pack("<I", len(palette)) + struct.pack(f"<{len(palette)}I", *palette)
    colr += bytes(i & 255 for i in indices) + bytes(i >> 8 for i in indices)
    deltas = []
    for i, h in enumerate(heights):
        prev = heights[i - 1] if i % 256 else heights[i - 256] if i >= 256 else 0
        delta = (h - prev) & 65535
        deltas.append(((delta << 1) ^ (65535 if delta & 32768 else 0)) & 65535)
    hght = bytes(d & 255 for d in deltas) + bytes(d >> 8 for d in deltas)
    sections = [(b"CHNK", b"".join(struct.pack("<QH", *s) for s in stamps)), (b"COLR", colr), (b"HGHT", hght)]
    bodies = [zstandard.ZstdCompressor(level=3).compress(raw) for _, raw in sections]
    table = b"".join(struct.pack("<4sBBIII", tag, 1, 1, len(raw), len(body), zlib.crc32(body))
                     for (tag, raw), body in zip(sections, bodies))
    return struct.pack("<4sHHiiH", b"LMR2", 1, 0, -2, 3, len(sections)) + table + b"".join(bodies)


def verify(path):
    data = path.read_bytes()
    magic, version, flags, x, z, count = struct.unpack_from("<4sHHiiH", data)
    assert (magic, version, flags, x, z) == (b"LMR2", 1, 0, -2, 3)
    at = 18 + 18 * count
    raw = {}
    for i in range(count):
        tag, codec, _, size, stored, crc = struct.unpack_from("<4sBBIII", data, 18 + 18 * i)
        body = data[at:at + stored]
        at += stored
        assert zlib.crc32(body) == crc
        raw[tag] = body if codec == 0 else zstandard.ZstdDecompressor().decompress(body)
        assert len(raw[tag]) == size
    assert at == len(data)
    colors, heights, stamps = values()
    assert [struct.unpack_from("<QH", raw[b"CHNK"], i * 10) for i in range(256)] == stamps
    p, = struct.unpack_from("<I", raw[b"COLR"])
    palette = struct.unpack_from(f"<{p}I", raw[b"COLR"], 4)
    plane = raw[b"COLR"][4 + 4 * p:]
    decoded_colors = [palette[plane[i] | (plane[N + i] << 8 if p > 256 else 0)] for i in range(N)]
    assert decoded_colors == colors
    decoded_heights = []
    for i in range(N):
        v = raw[b"HGHT"][i] | raw[b"HGHT"][N + i] << 8
        delta = (v >> 1) ^ (65535 if v & 1 else 0)
        prev = decoded_heights[i - 1] if i % 256 else decoded_heights[i - 256] if i >= 256 else 0
        decoded_heights.append(((prev + delta + 32768) & 65535) - 32768)
    assert decoded_heights == heights
    print(f"Verified LMR2 fixture: {path}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("path", nargs="?", type=pathlib.Path, default=FIXTURE)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    if args.write:
        args.path.parent.mkdir(parents=True, exist_ok=True)
        args.path.write_bytes(encode())
    verify(args.path)
