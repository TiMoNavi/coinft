#!/usr/bin/env python3
"""Receive Teensy-side six-axis results and write one JSON object per line."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from host.coinft_interface import CoinFTInterface


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output", type=Path, help="JSONL path; defaults to stdout")
    parser.add_argument("--no-start-command", action="store_true", help="do not send i/s to the Teensy")
    parser.add_argument("--startup-delay", type=float, default=1.0, help="wait after opening USB serial")
    return parser


def run(args: argparse.Namespace) -> int:
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
    output = args.output.open("w", encoding="utf-8") if args.output else sys.stdout
    try:
        with CoinFTInterface(
            args.port,
            args.baud,
            start_stream=not args.no_start_command,
            startup_delay=args.startup_delay,
        ) as coinft:
            print(f"Receiving CoinFT MLP frames on {args.port}", file=sys.stderr, flush=True)
            while True:
                row = coinft.read_sample().as_dict()
                output.write(json.dumps(row, separators=(",", ":")) + "\n")
                output.flush()
    except KeyboardInterrupt:
        return 0
    finally:
        if output is not sys.stdout:
            output.close()


if __name__ == "__main__":
    raise SystemExit(run(build_parser().parse_args()))
