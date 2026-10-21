from __future__ import annotations

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from host.coinft_interface import (
    CalibrationFailedError,
    CoinFTInterface,
    ZeroCheckFailedError,
)
from host.protocol import encode_frame


class FakeSerial:
    def __init__(self, *args, **kwargs) -> None:
        self.writes: list[bytes] = []
        self.chunks: list[bytes] = []
        self.closed = False

    @property
    def in_waiting(self) -> int:
        return len(self.chunks[0]) if self.chunks else 0

    def write(self, value: bytes) -> int:
        self.writes.append(value)
        return len(value)

    def flush(self) -> None:
        pass

    def reset_input_buffer(self) -> None:
        self.chunks.clear()

    def read(self, count: int) -> bytes:
        if not self.chunks:
            return b""
        chunk = self.chunks.pop(0)
        return chunk[:count]

    def close(self) -> None:
        self.closed = True


class CoinFTInterfaceTest(unittest.TestCase):
    def test_start_read_sample_and_stop_commands(self) -> None:
        fake = FakeSerial()
        interface = CoinFTInterface(
            "/dev/fake",
            startup_delay=0,
            serial_factory=lambda *args, **kwargs: fake,
        )
        fake.chunks.append(
            encode_frame(41, 1234, (1, 2, 3, 4, 5, 6), (7, 8, 9, 10, 11, 12))
            + encode_frame(43, 2234, (0,) * 6, (0,) * 6)
        )

        first = interface.read_sample()
        second = interface.read_sample()
        interface.close()

        self.assertEqual(fake.writes, [b"i", b"s", b"i"])
        self.assertEqual(first.left_wrench, (1.0, 2.0, 3.0, 4.0, 5.0, 6.0))
        self.assertEqual(first.as_dict()["left"]["mx"], 4.0)
        self.assertEqual(second.dropped_before, 1)
        self.assertTrue(fake.closed)

    def test_attach_without_start_does_not_send_control_commands(self) -> None:
        fake = FakeSerial()
        interface = CoinFTInterface(
            "/dev/fake",
            start_stream=False,
            serial_factory=lambda *args, **kwargs: fake,
        )
        interface.close()
        self.assertEqual(fake.writes, [])
        self.assertTrue(fake.closed)

    def test_read_sample_timeout_reports_missing_frame(self) -> None:
        fake = FakeSerial()
        interface = CoinFTInterface(
            "/dev/fake",
            start_stream=False,
            serial_factory=lambda *args, **kwargs: fake,
        )
        with self.assertRaises(TimeoutError):
            interface.read_sample(timeout=0)
        interface.close()

    def test_restart_calibration_sends_stop_and_start(self) -> None:
        fake = FakeSerial()
        interface = CoinFTInterface(
            "/dev/fake",
            start_stream=False,
            serial_factory=lambda *args, **kwargs: fake,
        )
        interface.restart_calibration(restart_delay=0)
        interface.close()
        self.assertEqual(fake.writes, [b"i", b"s", b"i"])

    def test_wait_until_ready_skips_calibration_frames(self) -> None:
        fake = FakeSerial()
        interface = CoinFTInterface(
            "/dev/fake",
            start_stream=False,
            serial_factory=lambda *args, **kwargs: fake,
        )
        fake.chunks.append(
            encode_frame(1, 1000, (0,) * 6, (0,) * 6, calibration_status=6, wrench_valid=False)
            + encode_frame(2, 2000, (1, 2, 3, 4, 5, 6), (7, 8, 9, 10, 11, 12))
        )

        sample = interface.wait_until_ready(timeout=1)
        interface.close()

        self.assertTrue(sample.wrench_valid)
        self.assertEqual(sample.calibration_status, "ready")

    def test_wait_until_ready_returns_sample_with_zero_check_warning(self) -> None:
        fake = FakeSerial()
        interface = CoinFTInterface(
            "/dev/fake",
            start_stream=False,
            serial_factory=lambda *args, **kwargs: fake,
        )
        fake.chunks.append(
            encode_frame(1, 1000, (1,) * 6, (2,) * 6, zero_check_warning=True)
        )

        sample = interface.wait_until_ready(timeout=1)
        interface.close()

        self.assertTrue(sample.wrench_valid)
        self.assertTrue(sample.zero_check_warning)
        self.assertIn("warning", sample.as_dict())

    def test_wait_until_ready_raises_on_zero_check_failure(self) -> None:
        fake = FakeSerial()
        interface = CoinFTInterface(
            "/dev/fake",
            start_stream=False,
            serial_factory=lambda *args, **kwargs: fake,
        )
        fake.chunks.append(
            encode_frame(1, 1000, (0,) * 6, (0,) * 6, calibration_status=8, wrench_valid=False)
        )

        with self.assertRaises(ZeroCheckFailedError):
            interface.wait_until_ready(timeout=1)
        interface.close()

    def test_wait_until_ready_raises_on_firmware_error(self) -> None:
        fake = FakeSerial()
        interface = CoinFTInterface(
            "/dev/fake",
            start_stream=False,
            serial_factory=lambda *args, **kwargs: fake,
        )
        fake.chunks.append(
            encode_frame(1, 1000, (0,) * 6, (0,) * 6, calibration_status=9, wrench_valid=False)
        )

        with self.assertRaises(CalibrationFailedError):
            interface.wait_until_ready(timeout=1)
        interface.close()


if __name__ == "__main__":
    unittest.main()
