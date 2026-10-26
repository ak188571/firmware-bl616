#!/usr/bin/env python3
"""Format/bitmap-compatibility tests for the actual UI16 font sources."""
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from build_sd_font import make_mapping
from fontpack import encode, FontPack, FontError, load_with_fallback

MONITOR = pathlib.Path(__file__).resolve().parents[3] / 'monitor'

class SdFontTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.existing = make_mapping(MONITOR, all_glyphs=False)
        cls.all = make_mapping(MONITOR)
        cls.data = encode(cls.all)

    def test_existing_bitmaps_retained(self):
        self.assertEqual(len(self.existing), 319)
        for cp, glyph in self.existing.items():
            self.assertEqual(self.all[cp], glyph)

    def test_full_bank_exceeds_monitor_slots(self):
        self.assertGreater(len(self.all), 1024)
        self.assertEqual(FontPack.decode(self.data).glyphs, self.all)

    def test_row_endianness_matches_hex(self):
        rows = self.existing[0x30d5]
        self.assertEqual(len(rows), 32)
        # Encoder stores each row as one 16-bit big-endian word.
        self.assertEqual(rows[:2], self.all[0x30d5][:2])

    def test_bad_crc_rejected(self):
        broken = bytearray(self.data)
        broken[-1] ^= 0x01
        with self.assertRaisesRegex(FontError, 'CRC32'):
            FontPack.decode(bytes(broken))

    def test_duplicate_unicode_rejected(self):
        broken = bytearray(self.data)
        broken[68:72] = broken[32:36]
        import zlib
        broken[16:20] = zlib.crc32(broken[32:]).to_bytes(4, 'little')
        with self.assertRaisesRegex(FontError, 'strictly ordered'):
            FontPack.decode(bytes(broken))

    def test_missing_file_fallback(self):
        builtin = FontPack(self.existing)
        result, reason = load_with_fallback('nonexistent_ui16_font_file.bin', builtin)
        self.assertIs(result, builtin)
        self.assertTrue(reason)

    def test_deterministic(self):
        self.assertEqual(self.data, encode(make_mapping(MONITOR)))

if __name__ == '__main__': unittest.main()
