#!/usr/bin/env python3
"""Exercise Flowis decoding with synthetic bitstreams, without an SDR.

The monthly layout was reverse engineered from FLOWIS+ traffic. These fixtures
use an invented meter ID and generated CRCs; they are not RF recordings.
"""
import json
import struct
import subprocess
import sys
import unittest

RTL_433 = sys.argv.pop(1)
PREAMBLE = bytes.fromhex("aaaad391d391")
METER_ID = 123456789


def frame(message_type, payload):
    body = bytes([message_type]) + struct.pack("<I", METER_ID) + payload
    data = bytes([len(body)]) + body
    crc = 0xffff
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x8005 if crc & 0x8000 else 0)) & 0xffff
    return PREAMBLE + data + struct.pack(">H", crc)


class FlowisTest(unittest.TestCase):
    def decode(self, data, bits=None):
        code = "{%d}%s" % (len(data) * 8 if bits is None else bits, data.hex())
        result = subprocess.run(
            [RTL_433, "-c", "0", "-R", "237", "-F", "json", "-y", code],
            capture_output=True, text=True, timeout=10)
        self.assertIn(result.returncode, (0, 1), result.stderr)
        return [json.loads(line) for line in result.stdout.splitlines() if line.strip()]

    def test_monthly_values(self):
        # Non-symmetric values detect byte swaps; zero and uint16 max are valid.
        values = (699, 7343, 0, 1, 256, 12345, 65535)
        packets = self.decode(frame(2, struct.pack("<7H", *values)))
        self.assertEqual(len(packets), 1)
        packet = packets[0]
        self.assertEqual(packet["model"], "Flowis")
        self.assertEqual(packet["id"], METER_ID)
        self.assertEqual(packet["msg_type"], 2)
        self.assertEqual(packet["mic"], "CRC")
        for month, litres in enumerate(values):
            self.assertAlmostEqual(packet["consumption_month_%d_m3" % month], litres / 1000)
        for key in ("volume_m3", "device_time", "alarm", "backflow"):
            self.assertNotIn(key, packet)

    def test_observed_month_rollover(self):
        # Payloads reported in rtl_433 issue #2317. ID and CRC are generated.
        october = bytes.fromhex("8105 7d10 f82b da2f 0525 1824 6005")
        november = bytes.fromhex("0000 8105 7d10 f82b da2f 0525 1824")
        before = self.decode(frame(2, october))[0]
        after = self.decode(frame(2, november))[0]
        expected = (1.409, 4.221, 11.256, 12.250, 9.477, 9.240, 1.376)
        for month, volume in enumerate(expected):
            self.assertAlmostEqual(before["consumption_month_%d_m3" % month], volume)
        self.assertEqual(after["consumption_month_0_m3"], 0)
        for month in range(1, 7):
            self.assertEqual(after["consumption_month_%d_m3" % month],
                             before["consumption_month_%d_m3" % (month - 1)])

    def test_zero_consumption(self):
        packet = self.decode(frame(2, bytes(14)))[0]
        for month in range(7):
            self.assertEqual(packet["consumption_month_%d_m3" % month], 0)

    def test_extended_type_2(self):
        packet = self.decode(frame(2, struct.pack("<7H", *range(7)) + b"\xaa\xbb"))[0]
        self.assertAlmostEqual(packet["consumption_month_6_m3"], 0.006)

    def test_type_1_unchanged(self):
        # Byte 6 unknown, timestamp at 7..10, volume at 11..13,
        # backflow at 14, alarm at 15; remaining bytes are unknown.
        year, month, day, hour, minute, second = 26, 11, 7, 13, 42, 30
        timestamp = bytes([
            second | ((minute & 3) << 6),
            (minute >> 2) | ((hour & 15) << 4),
            (hour >> 4) | (day << 1) | ((month & 3) << 6),
            (month >> 2) | (year << 2)])
        payload = b"\x00" + timestamp + (123456).to_bytes(3, "little") + b"\x02\x04" + bytes(6)
        packet = self.decode(frame(1, payload))[0]
        self.assertEqual(packet["id"], METER_ID)
        self.assertEqual(packet["msg_type"], 1)
        self.assertAlmostEqual(packet["volume_m3"], 123.456)
        self.assertEqual(packet["device_time"], "2026-11-07T13:42:30")
        self.assertEqual(packet["backflow"], 2)
        self.assertEqual(packet["alarm"], 4)
        self.assertFalse(any(key.startswith("consumption_month_") for key in packet))

    def test_bad_crc(self):
        data = bytearray(frame(2, bytes(14)))
        data[-1] ^= 1
        self.assertEqual(self.decode(data), [])

    def test_truncated_frames(self):
        data = frame(2, bytes(14))
        for length in range(len(PREAMBLE), len(data)):
            with self.subTest(length=length):
                self.assertEqual(self.decode(data[:length]), [])
        for missing_bits in range(1, 8):
            with self.subTest(missing_bits=missing_bits):
                self.assertEqual(self.decode(data, len(data) * 8 - missing_bits), [])

    def test_short_payloads_with_valid_crc(self):
        for message_type, minimum in ((1, 10), (2, 14)):
            for length in range(minimum):
                with self.subTest(message_type=message_type, length=length):
                    self.assertEqual(self.decode(frame(message_type, bytes(length))), [])

    def test_unknown_types(self):
        for message_type in (0, 3, 4, 5, 255):
            with self.subTest(message_type=message_type):
                self.assertEqual(self.decode(frame(message_type, bytes(14))), [])


if __name__ == "__main__":
    unittest.main()
