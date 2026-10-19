#!/usr/bin/env python3
"""Minimal example that reads CoinFT samples through the standard interface."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from host.coinft_interface import CoinFTInterface


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--no-start-command", action="store_true")
    args = parser.parse_args()

    try:
        with CoinFTInterface(args.port, args.baud, start_stream=not args.no_start_command) as coinft:
            while True:
                print(json.dumps(coinft.read_sample().as_dict(), separators=(",", ":")), flush=True)
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
