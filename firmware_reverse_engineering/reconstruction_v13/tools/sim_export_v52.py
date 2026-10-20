#!/usr/bin/env python3
"""v52 stage J: pair recorded SWD record reads with the UART frames the device
actually transmitted, and export them for the C simulator to replay.

Pairing rule, and why it is conservative: a 33-word SWD read is not atomic with
respect to a 201 Hz scan loop, so a read can straddle a firmware update and
disagree with every transmitted frame.  Rather than pick the nearest frame in
time and count the mismatches as simulator failures, this exporter keeps a case
only when some frame inside a tight window matches the read exactly.  That makes
the exported set a test of the *encoding*, not of read atomicity -- which is
what stage J is trying to verify.  The rejected fraction is reported so the
selection stays visible.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

ANALYSIS = Path(__file__).resolve().parents[2] / "07_analysis"
RECORDS = 13
CHANNELS = 12


def decode_records(words: list[int]) -> list[int]:
    blob = b"".join(struct.pack("<I", w) for w in words)
    return [struct.unpack_from("<H", blob, i * 10)[0] for i in range(RECORDS)]


def load_force(path: Path) -> tuple[list[tuple[list[int], float]], list]:
    data = json.loads(path.read_text())
    reads: list[tuple[list[int], float]] = []
    for pos in data["positions"]:
        for phase in pos["phases"].values():
            for s in phase["samples"]:
                if s["ok"]:
                    reads.append((decode_records(s["words"]),
                                  (s["t_before"] + s["t_after"]) / 2.0))
    frames = [(f["t"], f["values"]) for f in data["frames"]]
    return reads, frames


def load_sync(path: Path) -> tuple[list[tuple[list[int], float]], list]:
    data = json.loads(path.read_text())
    reads = []
    for s in data["samples"]:
        r = s["regions"]["sensor_records"]
        if r["ok"]:
            reads.append((decode_records(r["words"]),
                          (r["t_before"] + r["t_after"]) / 2.0))
    frames = [(f["t"], f["values"]) for f in data["frames"]]
    return reads, frames


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--window", type=float, default=0.05)
    args = ap.parse_args()

    sources = [
        (ANALYSIS / "force_experiment_v47_20260830" / "force_experiment.json",
         load_force),
        (ANALYSIS / "runtime_snapshots_v46_20260830" / "sync_stream.json",
         load_sync),
    ]

    out = bytearray()
    kept = considered = 0
    for path, loader in sources:
        if not path.exists():
            print(f"  {path.name}: missing, skipped")
            continue
        reads, frames = loader(path)
        src_kept = 0
        for recs, t in reads:
            considered += 1
            window = [v for ft, v in frames if abs(ft - t) < args.window]
            # Keep only reads that some in-window frame reproduces exactly.
            match = next((v for v in window if v == recs[:CHANNELS]), None)
            if match is None:
                continue
            kept += 1
            src_kept += 1
            out += struct.pack("<13H", *recs)
            out += bytes([0x02]) + struct.pack("<12H", *match) + bytes([0x03])
        print(f"  {path.name}: {len(reads)} reads, {len(frames)} frames "
              f"-> {src_kept} exact-match cases")

    Path(args.out).write_bytes(out)
    print(f"cases_exported={kept}/{considered} "
          f"({100.0 * kept / considered:.1f}% of reads had an exact in-window "
          f"frame)" if considered else "no reads")
    print(f"wrote {args.out} ({len(out)} bytes)")
    return 0 if kept else 1


if __name__ == "__main__":
    sys.exit(main())
