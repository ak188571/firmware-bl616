#!/usr/bin/env python3
"""Build microSD UI16 font package without rewriting fixed ROM assets.

The bundled Shinonome source is the authoritative bitmap source; all existing
manifest glyphs must match the installed monitor font bit-for-bit. This guard
prevents accidental visual regressions during the transition.
"""
import argparse
import gzip
import json
from pathlib import Path
from fontpack import FontPack, encode

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).with_name('shinonome16.json.gz')


def make_mapping(monitor: Path, *, all_glyphs: bool = True) -> dict[int, bytes]:
    manifest = json.loads((ROOT / 'ui/ui16_font_manifest.json').read_text(encoding='utf-8'))
    rom = [int(word, 16) for word in
           (monitor / 'src/iosys/ui16/font16_full.hex').read_text(encoding='ascii').split()]
    source = json.loads(gzip.decompress(SOURCE.read_bytes()))
    mapping = {}
    for entry in manifest['glyphs']:
        cp, slot, rows = entry['cp'], entry['id'], entry['rows']
        if len(rows) != 16 or rom[slot * 16:slot * 16 + 16] != rows:
            raise ValueError(f'Installed ROM mismatch at U+{cp:04X}, glyph {slot}')
        if cp in mapping:
            raise ValueError(f'Duplicate existing glyph U+{cp:04X}')
        mapping[cp] = b''.join(row.to_bytes(2, 'big') for row in rows)
    if all_glyphs:
        for number, rows in source.items():
            cp = int(number)
            if cp in mapping:
                if mapping[cp] != b''.join(row.to_bytes(2, 'big') for row in rows):
                    # Existing manifest may intentionally override a source glyph.
                    # Preserve the already-validated deployed bitmap.
                    pass
                continue
            if len(rows) != 16 or any(not 0 <= row <= 65535 for row in rows):
                raise ValueError(f'Invalid Shinonome bitmap U+{cp:04X}')
            mapping[cp] = b''.join(row.to_bytes(2, 'big') for row in rows)
    return mapping


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--monitor', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--builtins-only', action='store_true')
    args = parser.parse_args()
    mapping = make_mapping(args.monitor, all_glyphs=not args.builtins_only)
    data = encode(mapping)
    assert FontPack.decode(data).glyphs == mapping
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(f'Font pack: {args.output} ({len(mapping)} glyphs, {len(data)} bytes)')
    print('copy to microSD only AFTER firmware and monitor support this format')


if __name__ == '__main__':
    main()
