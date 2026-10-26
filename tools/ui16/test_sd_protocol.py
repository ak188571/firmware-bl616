#!/usr/bin/env python3
"""Independent *behavioral* audit of actual BL616 UART frames.

This is NOT an RTL simulation. It verifies that the C++ sender emits bytes
satisfying the documented protocol and the blank/upload/28-full-rows/show
barrier. Fault injection exercises expected fail-safe decisions.
"""
import pathlib
import sys
import unittest


def crc8(data):
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c << 1) ^ (0x07 if c & 0x80 else 0)
            c &= 0xFF
    return c


def packet(cmd, body):
    length = 1 + len(body)
    return bytes((0xAA, length >> 8, length & 255, cmd)) + body


def split_packets(data):
    packets = []
    offset = 0
    while offset < len(data):
        if len(data) - offset < 4 or data[offset] != 0xAA:
            raise ValueError(f"framing at byte {offset}")
        length = (data[offset + 1] << 8) | data[offset + 2]
        if not length or length > 2047 or offset + 3 + length > len(data):
            raise ValueError(f"length at byte {offset}")
        packets.append((data[offset + 3], data[offset + 4:offset + 3 + length]))
        offset += 3 + length
    return packets


class MonitorModel:
    """Minimal fail-safe state model; deliberately independent of Verilog."""
    def __init__(self):
        self.visible = True
        self.good = False
        self.rows = set()
        self.font_slots = set()
        self.committed_rows = set()
        self.show_accepted = False

    def run(self, packets):
        for cmd, body in packets:
            if cmd == 0x13:
                if len(body) != 2 or crc8(body[:1]) != body[1]:
                    if not self.visible:
                        self.good = False
                    continue
                if body[0] == 0:
                    self.visible = False
                    self.good = True
                    self.rows.clear()
                    self.committed_rows.clear()
                elif body[0] == 1 and not self.visible and self.good and len(self.rows) == 28:
                    self.visible = True
                    self.show_accepted = True
            elif cmd == 0x12:
                valid = (len(body) == 35 and crc8(body[:34]) == body[34]
                         and body[1] < 4)
                slot = body[0] | body[1] << 8 if len(body) >= 2 else 0
                if valid and not self.visible and self.good and slot < 1024:
                    self.font_slots.add(slot)
                elif not self.visible:
                    self.good = False
            elif cmd == 0x14:
                valid = (len(body) >= 6 and crc8(body[:-1]) == body[-1]
                         and body[2] != 0 and len(body) == 4 + 2 * body[2]
                         and body[0] < 28 and body[1] + body[2] <= 32)
                # A valid protected row is written ONLY when BLANK was accepted.
                if valid and not self.visible:
                    self.committed_rows.add(body[0])
                    if body[1] == 0 and body[2] == 32:
                        self.rows.add(body[0])
                elif not self.visible:
                    self.good = False
        return self


class ProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.stream = pathlib.Path(sys.argv[1]).read_bytes()
        cls.frames = split_packets(cls.stream)

    def test_real_sender_packets(self):
        commands = [cmd for cmd, _ in self.frames]
        self.assertEqual(commands.count(0x13), 2)
        self.assertEqual(commands.count(0x12), 3)
        self.assertEqual(commands.count(0x14), 28)
        self.assertEqual(commands.count(0x0F), 0)
        result = MonitorModel().run(self.frames)
        self.assertTrue(result.visible and result.show_accepted)
        self.assertEqual(len(result.font_slots), 3)
        self.assertEqual(result.rows, set(range(28)))

    def patched(self, idx, payload):
        q = list(self.frames)
        cmd, _ = q[idx]
        q[idx] = (cmd, payload)
        return q

    def test_bad_font_crc_holds_blank(self):
        cmd, body = self.frames[1]
        self.assertEqual(cmd, 0x12)
        bad = bytearray(body)
        bad[10] ^= 1
        result = MonitorModel().run(self.patched(1, bytes(bad)))
        self.assertFalse(result.visible)
        self.assertFalse(result.show_accepted)

    def test_bad_row_crc_holds_blank(self):
        idx = next(i for i, (cmd, _) in enumerate(self.frames) if cmd == 0x14)
        bad = bytearray(self.frames[idx][1]);bad[10] ^= 1
        result = MonitorModel().run(self.patched(idx, bytes(bad)))
        self.assertFalse(result.visible)
        self.assertFalse(result.show_accepted)

    def test_missing_row_holds_blank(self):
        idx = next(i for i, (cmd, data) in enumerate(self.frames)
                   if cmd == 0x14 and data[0] == 27)
        result = MonitorModel().run(self.frames[:idx] + self.frames[idx + 1:])
        self.assertFalse(result.visible)
        self.assertFalse(result.show_accepted)

    def test_partial_row_never_satisfies_barrier(self):
        idx = next(i for i, (cmd, data) in enumerate(self.frames)
                   if cmd == 0x14 and data[0] == 27)
        new = bytes((27, 31, 1, 0, 0))
        result = MonitorModel().run(self.patched(idx, new + bytes((crc8(new),))))
        self.assertFalse(result.visible)
        self.assertFalse(result.show_accepted)

    def test_bad_blank_disables_font_and_protected_rows(self):
        cmd, body = self.frames[0]
        self.assertEqual(cmd, 0x13)
        bad = bytearray(body);bad[-1] ^= 1
        result = MonitorModel().run(self.patched(0, bytes(bad)))
        self.assertTrue(result.visible)
        self.assertFalse(result.show_accepted)
        self.assertFalse(result.font_slots)
        self.assertFalse(result.committed_rows)

    def test_bad_show_crc_holds_blank(self):
        idx = len(self.frames) - 1
        cmd, body = self.frames[idx]
        self.assertEqual(cmd, 0x13)
        bad = bytearray(body);bad[-1] ^= 1
        result = MonitorModel().run(self.patched(idx, bytes(bad)))
        self.assertFalse(result.visible)
        self.assertFalse(result.show_accepted)

    def test_frame_length_and_crc(self):
        reconstructed = b''.join(packet(cmd, body) for cmd, body in self.frames)
        self.assertEqual(reconstructed, self.stream)
        for cmd, body in self.frames:
            if cmd in (0x12, 0x13, 0x14):
                self.assertEqual(crc8(body[:-1]), body[-1])


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: test_sd_protocol.py <BL616-stream.bin>')
    unittest.main(argv=[sys.argv[0]],verbosity=2)
