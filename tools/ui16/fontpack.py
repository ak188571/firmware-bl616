"""UI16 microSD font file proposal v1 (host reference, not firmware integration).

All fixed-size integers little-endian, glyph rows big-endian 16-bit words.
Header: <8sHBBIIIII (32 bytes); entry: <I + 32 bytes (36 bytes).
"""
from __future__ import annotations
import argparse
import bisect
import pathlib
import struct
import zlib
from dataclasses import dataclass

MAGIC = b"UI16FNT\0"
VERSION = 1
HEADER = struct.Struct('<8sHBBIIIII')
GLYPH_CP = struct.Struct('<I')
GLYPH_BYTES = 32
RECORD_BYTES = 4 + GLYPH_BYTES
MAX_RECORDS = 65535  # Provisional limit, subject to BL616 RAM/time measurements.
REPLACEMENT = 0xFFFD


class FontError(ValueError):
    pass


def valid_scalar(cp: int) -> bool:
    return 0 <= cp <= 0x10FFFF and not 0xD800 <= cp <= 0xDFFF


def validate_bitmap(bitmap: bytes) -> None:
    if not isinstance(bitmap, bytes) or len(bitmap) != GLYPH_BYTES:
        raise FontError("Each 16x16 monochrome glyph must be 32 bytes")


def encode(mapping: dict[int, bytes]) -> bytes:
    if len(mapping) > MAX_RECORDS:
        raise FontError("Too many glyphs")
    body = bytearray()
    for cp in sorted(mapping):
        if not valid_scalar(cp):
            raise FontError(f"Invalid Unicode scalar U+{cp:04X}")
        bitmap = mapping[cp]
        validate_bitmap(bitmap)
        body += GLYPH_CP.pack(cp) + bitmap
    header = HEADER.pack(MAGIC, VERSION, 16, 16, len(mapping),
                         zlib.crc32(body), HEADER.size, 0, 0)
    return header + body


@dataclass(frozen=True)
class FontPack:
    glyphs: dict[int, bytes]

    @classmethod
    def decode(cls, data: bytes) -> 'FontPack':
        if len(data) < HEADER.size:
            raise FontError('File shorter than header')
        (magic, version, width, height, count, checksum, offset, flags, reserved) = HEADER.unpack_from(data)
        if magic != MAGIC or version != VERSION or (width, height) != (16, 16):
            raise FontError('Unsupported font pack header')
        if offset != HEADER.size or flags != 0 or reserved != 0 or count > MAX_RECORDS:
            raise FontError('Invalid header fields')
        expected = offset + count * RECORD_BYTES
        if len(data) != expected:
            raise FontError('Incorrect file size')
        payload = data[offset:]
        if zlib.crc32(payload) != checksum:
            raise FontError('CRC32 mismatch')
        glyphs = {}
        prev = -1
        for n in range(count):
            i = n * RECORD_BYTES
            (cp,) = GLYPH_CP.unpack_from(payload, i)
            if not valid_scalar(cp) or cp <= prev:
                raise FontError('Unicode table not strictly ordered or invalid')
            prev = cp
            glyphs[cp] = payload[i + 4:i + RECORD_BYTES]
        return cls(glyphs)

    @classmethod
    def load(cls, filename: str | pathlib.Path) -> 'FontPack':
        return cls.decode(pathlib.Path(filename).read_bytes())

    def get(self, cp: int) -> bytes | None:
        return self.glyphs.get(cp)


def load_with_fallback(filename: str | pathlib.Path, builtin: 'FontPack') -> tuple['FontPack', str | None]:
    """Use known-good built-in font when file missing, unreadable or damaged."""
    try:
        return FontPack.load(filename), None
    except (OSError, FontError) as exc:
        return builtin, f'{type(exc).__name__}: {exc}'


def sample_bitmap(cp: int) -> bytes:
    """Synthetic TEST GLYPH. Not a readable Japanese font."""
    return b''.join(((cp ^ (r * 0x1111)) & 0xFFFF).to_bytes(2, 'big') for r in range(16))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('output')
    parser.add_argument('--sample-count', type=int, default=1100)
    args = parser.parse_args()
    if not 1 <= args.sample_count <= MAX_RECORDS:
        parser.error('sample-count outside 1..65535')
    mapping = {0x20 + i: sample_bitmap(0x20 + i) for i in range(args.sample_count)}
    pathlib.Path(args.output).write_bytes(encode(mapping))
    loaded = FontPack.load(args.output)
    print(f'Generated TEST ONLY font: {len(loaded.glyphs)} glyphs, {pathlib.Path(args.output).stat().st_size} bytes')


if __name__ == '__main__':
    main()
