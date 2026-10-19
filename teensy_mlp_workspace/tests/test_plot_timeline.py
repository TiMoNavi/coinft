from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from visualization._live_plot import TeensyTimebase


class TeensyTimebaseTest(unittest.TestCase):
    def test_each_frame_gets_increasing_sample_time(self) -> None:
        clock = TeensyTimebase()
        stamps = [clock.update(value) for value in (1_000, 6_000, 11_000)]
        self.assertEqual(stamps, [0.0, 0.005, 0.01])

    def test_micros_rollover_keeps_time_increasing(self) -> None:
        clock = TeensyTimebase()
        first = clock.update(0xFFFFFF00)
        second = clock.update(0x000003E8)
        self.assertAlmostEqual(second - first, 0.001256)


if __name__ == "__main__":
    unittest.main()
