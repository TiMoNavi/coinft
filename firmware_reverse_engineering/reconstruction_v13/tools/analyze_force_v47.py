#!/usr/bin/env python3
"""v47: analyse the stage B force experiment.

Reproduces every number quoted in restoration_progress_v47.md from the raw
capture, so the conclusions can be re-checked rather than trusted.
"""

from __future__ import annotations

import json
import struct
from pathlib import Path

SNAPSHOT = "force_experiment_v47_20260830"
RECORD_COUNT = 13
UART_CHANNELS = 12

# Static pin table recovered from the firmware's widget pin records and the
# schematic (07_analysis/schematic_capsense_topology_v1.md).  The force
# experiment is an independent test of this table.
NET = {0: "RU1", 1: "RU2", 2: "LD2", 3: "LD1", 4: "LU2", 5: "LU1",
       6: "RD2", 7: "RD1", 8: "RU1", 9: "LD2", 10: "LU2", 11: "RD2"}
QUAD = {i: NET[i][:2] for i in NET}

# Operator-frame position -> expected schematic quadrant, as confirmed below.
EXPECTED = {"右上角": "RU", "左上角": "LU", "左下角": "LD", "右下角": "RD"}

# Channels sharing an MCU pin with a partner, and the one context field that
# differs between them.
PIN_PAIRS = [(0, 8, "RU1/P3.5"), (2, 9, "LD2/P0.2"),
             (4, 10, "LU2/P0.0"), (6, 11, "RD2/P3.0")]


def decode(words: list[int]) -> list[tuple[int, ...]]:
    raw = b"".join(struct.pack("<I", w) for w in words)
    return [struct.unpack_from("<5H", raw, i * 10) for i in range(RECORD_COUNT)]


def series(phase: dict) -> list[list[tuple[int, ...]]]:
    return [decode(s["words"]) for s in phase["samples"] if s["ok"]]


def mean(rows: list[list[tuple[int, ...]]], ch: int, field: int) -> float:
    return sum(r[ch][field] for r in rows) / len(rows)


def main() -> int:
    root = Path(__file__).resolve().parents[2] / "07_analysis" / SNAPSHOT
    path = root / "force_experiment.json"
    if not path.exists():
        print("force_experiment=missing")
        return 1
    data = json.loads(path.read_text())

    total = ok = 0
    for pos in data["positions"]:
        for phase in pos["phases"].values():
            total += len(phase["samples"])
            ok += sum(1 for s in phase["samples"] if s["ok"])
    print(f"positions={len(data['positions'])} swd_reads_ok={ok}/{total} "
          f"uart_frames={data['uart_frames']}")

    # 1. field2 / field3 never leave zero, even under 30 s of sustained load.
    nz2 = nz3 = seen = 0
    for pos in data["positions"]:
        for phase in pos["phases"].values():
            for rows in series(phase):
                for rec in rows:
                    seen += 1
                    nz2 += 1 if rec[2] else 0
                    nz3 += 1 if rec[3] else 0
    print(f"field2_nonzero={nz2}/{seen} field3_nonzero={nz3}/{seen}")

    # 2. Baseline is frozen for the whole session.
    distinct = {i: set() for i in range(RECORD_COUNT)}
    for pos in data["positions"]:
        for phase in pos["phases"].values():
            for rows in series(phase):
                for i, rec in enumerate(rows):
                    distinct[i].add(rec[1])
    frozen = sum(1 for i in distinct if len(distinct[i]) == 1)
    print(f"baseline_frozen_records={frozen}/{RECORD_COUNT} "
          f"(distinct values per record: "
          f"{sorted({len(v) for v in distinct.values()})})")

    # 3. Quadrant mapping: the three strongest channels per position.
    hits = 0
    checked = 0
    for pos in data["positions"]:
        unloaded = series(pos["phases"]["unloaded"])
        press = series(pos["phases"]["press"])
        if not unloaded or not press:
            continue
        steady = press[len(press) // 2:]
        delta = [mean(steady, i, 0) - mean(unloaded, i, 0)
                 for i in range(UART_CHANNELS)]
        top = sorted(range(UART_CHANNELS), key=lambda i: -delta[i])[:3]
        quads = {QUAD[i] for i in top}
        label = pos["position"]
        detail = ", ".join(f"ch{i}={NET[i]}(+{delta[i]:.0f})" for i in top)
        if label in EXPECTED:
            checked += 1
            good = quads == {EXPECTED[label]}
            hits += 1 if good else 0
            print(f"  {label}: {detail} -> {quads} "
                  f"{'OK' if good else 'MISMATCH'}")
        else:
            spread = max(delta) - min(delta)
            print(f"  {label}: {detail} -> all-channel load, "
                  f"min={min(delta):.0f} max={max(delta):.0f} spread={spread:.0f}")
    print(f"quadrant_mapping_confirmed={hits}/{checked}")

    # 4. Polarity and recovery.
    for pos in data["positions"][:1]:
        unloaded = series(pos["phases"]["unloaded"])
        press = series(pos["phases"]["press"])
        release = series(pos["phases"]["release"])
        steady = press[len(press) // 2:]
        delta = [mean(steady, i, 0) - mean(unloaded, i, 0)
                 for i in range(UART_CHANNELS)]
        ch = max(range(UART_CHANNELS), key=lambda i: delta[i])
        print(f"polarity: {pos['position']} ch{ch} "
              f"unloaded={mean(unloaded, ch, 0):.0f} "
              f"held={mean(steady, ch, 0):.0f} "
              f"released={mean(release[len(release) // 2:], ch, 0):.0f} "
              f"baseline={unloaded[0][ch][1]}")

    # 5. Pin-sharing pairs are not redundant copies.
    print("pin_pair_response_ratios (partner / primary):")
    for pos in data["positions"]:
        unloaded = series(pos["phases"]["unloaded"])
        press = series(pos["phases"]["press"])
        steady = press[len(press) // 2:]
        delta = [mean(steady, i, 0) - mean(unloaded, i, 0)
                 for i in range(UART_CHANNELS)]
        parts = []
        for a, b, net in PIN_PAIRS:
            if delta[a] > 200 or delta[b] > 200:
                ratio = delta[b] / delta[a] if delta[a] else float("nan")
                parts.append(f"{net} ch{a}/ch{b}={ratio:.2f}")
        if parts:
            print(f"  {pos['position']}: " + " | ".join(parts))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
