#!/usr/bin/env python3
"""v51 stage I: watch the baseline update rule, unattended.

v47 proved the baseline never follows raw *upward* (30 s holds moved raw by
~3000 and the baseline did not budge).  v47 section 11 then disproved both
candidate rules for when it *does* move.  The remaining hypothesis is downward
tracking driven by the widget context's lowBslnRst field, which v44 recovered
as 30.

This tool records raw and baseline for all 13 records over a long idle stream
and reports every baseline change: its direction, magnitude, and the raw-minus-
baseline value that preceded it.  No force is applied and nothing is written to
the target beyond the stock `i` / `s` characters.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from sync_capture_v46 import OpenOcdTcl, RawSerial, UART_BAUD  # noqa: E402

RECORD_ADDR = 0x2000045C
RECORD_WORDS = 33
RECORD_COUNT = 13
UART_CHANNELS = 12

# ch0's scan parameter was overwritten by the v48 `t` probes and has not been
# restored by a power cycle, so its absolute value is not the stock
# configuration.  Its baseline *behaviour* is still informative, but any
# conclusion that rests on ch0 alone must say so.
PERTURBED_CHANNELS = {0}


def decode(words: list[int]) -> list[tuple[int, int]]:
    raw = b"".join(struct.pack("<I", w) for w in words)
    return [struct.unpack_from("<2H", raw, i * 10) for i in range(RECORD_COUNT)]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--port", default="/dev/cu.usbmodem14403")
    ap.add_argument("--minutes", type=float, default=20.0)
    ap.add_argument("--poll-interval", type=float, default=0.05)
    ap.add_argument("--snapshot-every", type=float, default=0.5,
                    help="seconds between stored trend samples")
    args = ap.parse_args()

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    port = RawSerial(args.port, UART_BAUD)
    port.write(b"i")
    time.sleep(0.5)
    ocd = OpenOcdTcl()
    # INTR pulses rather than latching, so a single read can legitimately land
    # between causes.  Retry, and re-send `s` once, before declaring failure.
    intr_val = 0
    for attempt in range(12):
        port.write(b"s")
        time.sleep(0.6)
        for _ in range(8):
            got = ocd.read_words(0x40290170, 1)
            if got and got[0] != 0:
                intr_val = got[0]
                break
            time.sleep(0.15)
        if intr_val:
            break
    if not intr_val:
        print("! MSC INTR stayed 0 across retries; firmware is not scanning. "
              "Check the UART wiring. Aborting.")
        ocd.close()
        port.close()
        return 2
    intr = [intr_val]
    print(f"INTR={intr[0]:#010x}; scanning. Watching for "
          f"{args.minutes:.0f} min.", flush=True)

    trend: list[dict] = []
    events: list[dict] = []
    prev: list[tuple[int, int]] | None = None
    last_snapshot = 0.0
    reads = ok = 0
    t0 = time.time()
    end = t0 + args.minutes * 60.0

    try:
        while time.time() < end:
            words = ocd.read_words(RECORD_ADDR, RECORD_WORDS)
            reads += 1
            now = time.time()
            if words is None:
                time.sleep(args.poll_interval * 4.0)
                continue
            ok += 1
            recs = decode(words)

            if prev is not None:
                for i, (raw, bsln) in enumerate(recs):
                    if bsln != prev[i][1]:
                        events.append({
                            "t": now - t0,
                            "channel": i,
                            "baseline_from": prev[i][1],
                            "baseline_to": bsln,
                            "delta": bsln - prev[i][1],
                            "raw_at_change": raw,
                            "raw_before": prev[i][0],
                            # The quantity the hypothesis is about.
                            "raw_minus_baseline_before":
                                prev[i][0] - prev[i][1],
                        })
                        print(f"  [{now - t0:7.1f}s] ch{i:2d} baseline "
                              f"{prev[i][1]} -> {bsln} "
                              f"({bsln - prev[i][1]:+d}), raw was "
                              f"{prev[i][0]} (raw-bsln="
                              f"{prev[i][0] - prev[i][1]:+d})", flush=True)
            prev = recs

            if now - last_snapshot >= args.snapshot_every:
                last_snapshot = now
                trend.append({
                    "t": now - t0,
                    "raw": [r for r, _ in recs],
                    "baseline": [b for _, b in recs],
                })
            time.sleep(args.poll_interval)
    finally:
        ocd.close()
        time.sleep(0.3)
        port.write(b"i")
        time.sleep(0.3)
        port.close()

    result = {
        "minutes": args.minutes,
        "reads": reads,
        "reads_ok": ok,
        "perturbed_channels": sorted(PERTURBED_CHANNELS),
        "trend": trend,
        "events": events,
    }
    (outdir / "baseline_watch.json").write_text(json.dumps(result))

    print(f"\nreads_ok={ok}/{reads} trend_samples={len(trend)} "
          f"baseline_events={len(events)}")
    if events:
        down = sum(1 for e in events if e["delta"] < 0)
        up = sum(1 for e in events if e["delta"] > 0)
        print(f"  downward={down} upward={up}")
        mags = sorted({abs(e["delta"]) for e in events})
        print(f"  distinct magnitudes: {mags[:12]}")
        pre = [e["raw_minus_baseline_before"] for e in events]
        print(f"  raw-baseline before a change: min={min(pre)} "
              f"max={max(pre)}")
    else:
        print("  no baseline change observed in this window")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
