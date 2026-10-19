from __future__ import annotations

import struct
import unittest

import numpy as np

from receiver.protocol_v2 import CoinFTPacket, CoinFTStreamParser, decode_packet, sequence_gap
from receiver.warmup import CoinFTWarmup, WarmupConfig


class FakeSerial:
    def __init__(self, data: bytes) -> None:
        self.data = bytearray(data)

    def read(self, count: int) -> bytes:
        if not self.data:
            return b""
        out = bytes(self.data[:count])
        del self.data[:count]
        return out


class FakeModel:
    def __init__(self, base: float) -> None:
        self.base = base
        self.offset = None
        self.bias = None
        self.history: list[np.ndarray] = []

    def set_offset(self, value: np.ndarray) -> None:
        self.offset = np.asarray(value, dtype=float)

    def set_ft_bias(self, value: np.ndarray) -> None:
        self.bias = np.asarray(value, dtype=float)

    def reset_filter(self) -> None:
        self.history.clear()

    def zero_raw(self, raw: np.ndarray) -> np.ndarray:
        return np.asarray(raw, dtype=float) - (self.offset if self.offset is not None else 0.0)

    def predict_uncorrected(self, raw: np.ndarray) -> np.ndarray:
        value = self.base + float(np.mean(self.zero_raw(raw))) * 0.01
        return np.asarray([value, value, value, 0.0, 0.0, 0.0])

    def predict(self, raw: np.ndarray) -> np.ndarray:
        value = self.predict_uncorrected(raw)
        if self.bias is not None:
            value = value - self.bias
        self.history.append(value)
        return np.mean(np.asarray(self.history), axis=0)


class ReceiverV2Test(unittest.TestCase):
    def test_dual_v2_packet_decodes_metadata_and_sides(self) -> None:
        left = tuple(range(100, 112))
        right = tuple(range(200, 212))
        raw = b"\x00\x00" + struct.pack("<II", 7, 123456) + struct.pack("<24H", *(left + right))
        packet = decode_packet(raw, "dual-v2")
        self.assertEqual(packet, CoinFTPacket(7, 123456, (left, right), raw, "dual-v2"))

    def test_parser_resynchronizes_dual_packet(self) -> None:
        left = tuple(range(12))
        right = tuple(range(12, 24))
        raw = b"garbage" + b"\x00\x00" + struct.pack("<II", 1, 2) + struct.pack("<24H", *(left + right))
        parser = CoinFTStreamParser("dual-v2")
        packet = parser.read_packet(FakeSerial(raw), 0.1)
        self.assertEqual(packet.sequence_id, 1)
        self.assertEqual(packet.sensors, (left, right))
        self.assertGreater(parser.skipped_bytes, 0)

    def test_single_legacy_packet_is_explicit(self) -> None:
        values = tuple(range(12))
        packet = CoinFTStreamParser("single-legacy24").read_packet(FakeSerial(struct.pack("<12H", *values)), 0.1)
        self.assertIsNone(packet.sequence_id)
        self.assertEqual(packet.sensors, (values,))

    def test_sequence_gap_and_wrap(self) -> None:
        self.assertEqual(sequence_gap(10, 12), (1, False))
        self.assertEqual(sequence_gap(2**32 - 1, 0), (0, False))
        self.assertTrue(sequence_gap(10, 2**32 - 1)[1])

    def test_two_side_warmup_reaches_ready_and_zeroes_force(self) -> None:
        models = {"left": FakeModel(0.4), "right": FakeModel(-0.2)}
        config = WarmupConfig(
            raw_stability_window_s=0.2,
            raw_stability_min_wait_s=0.5,
            raw_stability_check_interval_s=0.1,
            raw_stability_mean_delta_counts=8.0,
            raw_stability_noise_std_counts=20.0,
            raw_stability_required_passes=2,
            runtime_settling_s=0.3,
            final_tare_window_s=0.2,
            bias_window_s=0.2,
            zero_check_window_s=0.2,
        )
        runtime = CoinFTWarmup(models, config)
        result = {}
        timestamp = 0.0
        for _ in range(100):
            level = 1000.0 if runtime.status in {"warming_up", "delayed"} else 1020.0
            result = runtime.process({"left": np.full(12, level), "right": np.full(12, level)}, timestamp)
            timestamp += 0.05
            if runtime.ready:
                break
        self.assertTrue(runtime.ready)
        self.assertEqual(runtime.status, "ready")
        self.assertLessEqual(result["zero_force_max_abs_n"], 0.15)
        self.assertEqual(len(result["left_wrench"]), 6)
        self.assertEqual(len(result["right_wrench"]), 6)


if __name__ == "__main__":
    unittest.main()
