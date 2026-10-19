from __future__ import annotations

import math
from pathlib import Path
import sys
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from host.protocol import (
    FRAME_SIZE,
    ProtocolError,
    StreamParser,
    decode_frame,
    encode_frame,
    sample_to_json,
    sequence_gap,
)


class ProtocolTest(unittest.TestCase):
    def test_round_trip_and_fixed_size(self) -> None:
        left = (1.0, -2.0, 3.5, 0.01, -0.02, 0.03)
        right = (-4.0, 5.0, -6.5, -0.04, 0.05, -0.06)
        raw = encode_frame(0xFFFFFFFF, 123456789, left, right)
        self.assertEqual(len(raw), FRAME_SIZE)
        decoded = decode_frame(raw)
        self.assertEqual(decoded.sequence, 0xFFFFFFFF)
        self.assertEqual(decoded.teensy_time_us, 123456789)
        self.assertEqual(decoded.calibration_status, 7)
        self.assertTrue(decoded.wrench_valid)
        np.testing.assert_allclose(decoded.left, left, rtol=1e-7, atol=1e-8)
        np.testing.assert_allclose(decoded.right, right, rtol=1e-7, atol=1e-8)

    def test_crc_known_vector(self) -> None:
        from host.protocol import crc16_ccitt_false

        self.assertEqual(crc16_ccitt_false(b"123456789"), 0x29B1)

    def test_json_uses_left_right_and_coinft_axis_order(self) -> None:
        frame = decode_frame(encode_frame(3, 4, (1, 2, 3, 4, 5, 6), (7, 8, 9, 10, 11, 12)))
        row = sample_to_json(frame, 123, 0)
        self.assertEqual(row["side_order"], ["left", "right"])
        self.assertEqual(row["wrench_axes"], ["fx", "fy", "fz", "mx", "my", "mz"])
        self.assertEqual(row["left"]["mx"], 4.0)
        self.assertEqual(row["right"]["fz"], 9.0)

    def test_invalid_during_calibration_is_not_reported_as_zero_force(self) -> None:
        raw = encode_frame(1, 2, (0.0,) * 6, (0.0,) * 6, calibration_status=0, wrench_valid=False)
        row = sample_to_json(decode_frame(raw), 123, 0)
        self.assertFalse(row["calibration_ready"])
        self.assertFalse(row["wrench_valid"])
        self.assertIsNone(row["left_wrench"])
        self.assertIsNone(row["right"])

    def test_rejects_wrench_validity_outside_ready_state(self) -> None:
        raw = encode_frame(1, 2, (0.0,) * 6, (0.0,) * 6, calibration_status=6, wrench_valid=True)
        with self.assertRaises(ProtocolError):
            decode_frame(raw)

    def test_stream_recovers_from_noise_and_crc_failure(self) -> None:
        first = encode_frame(7, 10, (1.0,) * 6, (2.0,) * 6)
        damaged = bytearray(encode_frame(8, 20, (3.0,) * 6, (4.0,) * 6))
        damaged[22] ^= 0x20
        last = encode_frame(9, 30, (5.0,) * 6, (6.0,) * 6)
        parser = StreamParser()
        result = []
        wire = b"noise" + first + bytes(damaged) + last
        for offset in range(0, len(wire), 11):
            result.extend(parser.feed(wire[offset : offset + 11]))
        self.assertEqual([frame.sequence for frame in result], [7, 9])
        self.assertEqual(parser.bad_frames, 1)
        self.assertGreaterEqual(parser.discarded_bytes, 6)

    def test_rejects_wrong_header_version_crc_and_non_finite(self) -> None:
        raw = encode_frame(1, 2, (0.0,) * 6, (0.0,) * 6)
        with self.assertRaises(ProtocolError):
            decode_frame(raw[:-1])
        with self.assertRaises(ProtocolError):
            decode_frame(b"NOPE" + raw[4:])
        changed_version = bytearray(raw)
        changed_version[4] = 3
        with self.assertRaises(ProtocolError):
            decode_frame(bytes(changed_version))
        invalid_flags = bytearray(raw)
        invalid_flags[6] = 0x80
        with self.assertRaises(ProtocolError):
            decode_frame(bytes(invalid_flags))
        changed_crc = bytearray(raw)
        changed_crc[-1] ^= 1
        with self.assertRaises(ProtocolError):
            decode_frame(bytes(changed_crc))
        non_finite = list((0.0,) * 6)
        non_finite[0] = math.inf
        with self.assertRaises(ProtocolError):
            encode_frame(1, 2, tuple(non_finite), (0.0,) * 6)

    def test_sequence_wrap(self) -> None:
        self.assertEqual(sequence_gap(None, 2), 0)
        self.assertEqual(sequence_gap(10, 12), 1)
        self.assertEqual(sequence_gap(0xFFFFFFFF, 0), 0)
        self.assertEqual(sequence_gap(12, 12), 0)
        self.assertEqual(sequence_gap(12, 10), 0)


if __name__ == "__main__":
    unittest.main()
