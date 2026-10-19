#!/usr/bin/env python3
"""Compare left and right CoinFT values on six shared-axis waveforms."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from visualization._live_plot import build_parser, run_axis_compare


if __name__ == "__main__":
    args = build_parser(__doc__).parse_args()
    if args.history <= 0:
        raise SystemExit("--history must be positive")
    raise SystemExit(run_axis_compare(args))
