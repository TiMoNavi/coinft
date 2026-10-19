#!/usr/bin/env python3
"""Timestamped direct CoinFT capture and ATI synchronization utilities.

The direct PSoC protocol is 1 Mbaud: q -> 0x1A, s, then
0x02 + twelve little-endian uint16 values + 0x03. The align subcommand joins
such a capture with an ATI CSV using host timestamps; it never fabricates a
periodic synchronization signal.
"""

from __future__ import annotations

import argparse
import csv
import json
import pathlib
import statistics
import sys
import time

import numpy as np
import serial
from serial.tools import list_ports


BAUD = 1_000_000
FRAME_SIZE = 26
CHANNELS = 12


def find_port() -> str:
    matches = [p.device for p in list_ports.comports()
               if "KitProg3 USB-UART" in (p.description or "")]
    if len(matches) != 1:
        raise RuntimeError("expected one KitProg3 USB-UART; found: " +
                           (", ".join(matches) if matches else "none"))
    return matches[0]


def read_frame(port: serial.Serial, buffer: bytearray, deadline: float):
    while time.monotonic() < deadline:
        start = buffer.find(b"\x02")
        if start < 0:
            buffer.clear()
        elif start:
            del buffer[:start]
        if len(buffer) >= FRAME_SIZE:
            candidate = bytes(buffer[:FRAME_SIZE])
            if candidate[-1] == 0x03:
                del buffer[:FRAME_SIZE]
                values = np.frombuffer(candidate[1:-1], dtype="<u2").astype(int).tolist()
                return values, time.monotonic()
            del buffer[0]
            continue
        chunk = port.read(1)
        if chunk:
            buffer.extend(chunk)
    raise TimeoutError("timed out waiting for a complete direct CoinFT frame")


def stop_and_check(port: serial.Serial) -> str:
    port.write(b"i")
    port.flush()
    deadline = time.monotonic() + 1.0
    last_data = time.monotonic()
    drained = bytearray()
    while time.monotonic() < deadline:
        chunk = port.read(256)
        if chunk:
            drained.extend(chunk)
            last_data = time.monotonic()
            continue
        if time.monotonic() - last_data >= 0.12:
            return drained.hex()
    raise RuntimeError("direct stream remained active after i")


def capture(args: argparse.Namespace) -> int:
    device = args.port or find_port()
    with serial.Serial(device, BAUD, timeout=0.02, write_timeout=1.0) as port:
        port.reset_input_buffer()
        port.write(b"q")
        if port.read(1) != b"\x1a":
            raise RuntimeError("q did not return 0x1A")
        port.write(b"s")
        buffer = bytearray()
        for _ in range(args.warmup):
            read_frame(port, buffer, time.monotonic() + args.timeout)
        frames = []
        timestamps = []
        capture_start_unix = None
        for _ in range(args.frames):
            values, timestamp = read_frame(port, buffer, time.monotonic() + args.timeout)
            if capture_start_unix is None:
                capture_start_unix = time.time()
            frames.append(values)
            timestamps.append(timestamp)
        discarded = stop_and_check(port)
    assert capture_start_unix is not None
    origin = timestamps[0]
    relative = [value - origin for value in timestamps]
    duration = relative[-1] if len(relative) > 1 else 0.0
    record = {
        "schema": "coinft-raw-v1",
        "label": args.label,
        "port": device,
        "baud": BAUD,
        "frame_count": len(frames),
        "frame_size": FRAME_SIZE,
        "frames": frames,
        "timestamps_s": relative,
        "capture_start_unix_s": capture_start_unix,
        "rate_hz": (len(frames) - 1) / duration if duration > 0 else None,
        "median": [statistics.median(frame[c] for frame in frames)
                   for c in range(CHANNELS)],
        "q_response_hex": "1a",
        "post_stop_discarded_hex": discarded,
        "post_stop_quiet_hex": "",
    }
    output = pathlib.Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(record, indent=2) + "\n", encoding="ascii")
    print(f"capture: {output}")
    print(f"frames: {len(frames)}, rate: {record['rate_hz']}")
    return 0


def read_ati(path: str):
    with pathlib.Path(path).open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise ValueError("ATI CSV is empty")
    names = rows[0].keys()
    time_name = next((name for name in ("timestamp_s", "time_s", "Time", "time")
                      if name in names), None)
    if time_name is None:
        raise ValueError("ATI CSV needs timestamp_s, time_s, Time, or time")
    aliases = [["Fx", "ati0", "ATI_Fx"], ["Fy", "ati1", "ATI_Fy"],
               ["Fz", "ati2", "ATI_Fz"], ["Mx", "ati3", "ATI_Mx"],
               ["My", "ati4", "ATI_My"], ["Mz", "ati5", "ATI_Mz"]]
    value_names = []
    for options in aliases:
        value_names.append(next((name for name in options if name in names), None))
    if any(name is None for name in value_names):
        raise ValueError("ATI CSV needs six force/moment columns")
    times = np.asarray([float(row[time_name]) for row in rows])
    values = np.asarray([[float(row[name]) for name in value_names] for row in rows])
    if not np.all(np.diff(times) > 0):
        raise ValueError("ATI timestamps must be strictly increasing")
    sync = None
    for name in ("sync", "Sync", "sync_daq"):
        if name in names:
            sync = np.asarray([float(row[name]) for row in rows])
            break
    return times, values, sync


def align(args: argparse.Namespace) -> int:
    record = json.loads(pathlib.Path(args.capture).read_text(encoding="ascii"))
    if record.get("schema") != "coinft-raw-v1":
        raise ValueError("unsupported CoinFT capture schema")
    sensor_t = np.asarray(record["timestamps_s"], dtype=float)
    sensor = np.asarray(record["frames"], dtype=float)
    ati_t, ati, sync = read_ati(args.ati_csv)
    capture_start = record.get("capture_start_unix_s")
    if capture_start is not None and np.max(ati_t) > 1.0e8:
        # Both loggers used wall-clock epoch timestamps. Keep that common clock.
        sensor_t = sensor_t + float(capture_start)
    else:
        # Relative ATI files need an explicit offset: ATI time at the first
        # CoinFT frame. Do not silently align two independent start times.
        sensor_t = sensor_t - sensor_t[0]
        ati_t = ati_t - ati_t[0] + args.ati_offset_s
    ati_at_sensor = np.column_stack([np.interp(sensor_t, ati_t, ati[:, c])
                                     for c in range(6)])
    left = np.searchsorted(ati_t, sensor_t, side="right") - 1
    left = np.clip(left, 0, len(ati_t) - 1)
    right = np.clip(left + 1, 0, len(ati_t) - 1)
    skew = np.minimum(np.abs(sensor_t - ati_t[left]),
                      np.abs(sensor_t - ati_t[right]))
    max_skew = float(np.max(skew))
    if max_skew > args.max_skew_ms / 1000.0:
        raise ValueError(f"maximum nearest ATI sample skew {max_skew * 1000:.3f} ms "
                         f"> {args.max_skew_ms:.3f} ms")
    output = pathlib.Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    fields = ["time_s"] + [f"raw{i}" for i in range(CHANNELS)] + \
             ["ati_Fx", "ati_Fy", "ati_Fz", "ati_Mx", "ati_My", "ati_Mz", "skew_ms"]
    with output.open("w", newline="", encoding="ascii") as handle:
        writer = csv.writer(handle)
        writer.writerow(fields)
        for i in range(len(sensor_t)):
            writer.writerow([f"{sensor_t[i]:.9f}"] + [int(v) for v in sensor[i]] +
                            [f"{v:.9g}" for v in ati_at_sensor[i]] +
                            [f"{skew[i] * 1000:.6f}"])
    summary = {
        "schema": "coinft-ati-alignment-v1",
        "capture": args.capture,
        "ati_csv": args.ati_csv,
        "ati_offset_s": args.ati_offset_s,
        "frame_count": int(len(sensor_t)),
        "max_nearest_skew_ms": max_skew * 1000.0,
        "mean_nearest_skew_ms": float(np.mean(skew) * 1000.0),
        "sync_column_present": sync is not None,
        "baseline_median_raw": [float(v) for v in np.median(sensor[:args.baseline_frames], axis=0)],
    }
    summary_path = output.with_suffix(".json")
    summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="ascii")
    print(f"aligned: {output}")
    print(f"max nearest ATI skew: {summary['max_nearest_skew_ms']:.3f} ms")
    if sync is None:
        print("sync: no digital sync column; alignment used timestamps")
    else:
        print("sync: digital sync column present; inspect transitions alongside timestamps")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    cap = sub.add_parser("capture")
    cap.add_argument("--port")
    cap.add_argument("--label", required=True)
    cap.add_argument("--output", required=True)
    cap.add_argument("--frames", type=int, default=1000)
    cap.add_argument("--warmup", type=int, default=20)
    cap.add_argument("--timeout", type=float, default=10.0)
    cap.set_defaults(handler=capture)
    join = sub.add_parser("align")
    join.add_argument("--capture", required=True)
    join.add_argument("--ati-csv", required=True)
    join.add_argument("--output", required=True)
    join.add_argument("--max-skew-ms", type=float, default=3.0)
    join.add_argument("--ati-offset-s", type=float, default=0.0,
                      help="ATI relative time at first CoinFT frame")
    join.add_argument("--baseline-frames", type=int, default=100)
    join.set_defaults(handler=align)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.command == "capture" and args.frames < 3:
        raise ValueError("frames must be >= 3")
    if args.command == "align" and args.baseline_frames < 1:
        raise ValueError("baseline-frames must be positive")
    return args.handler(args)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, TimeoutError, serial.SerialException,
            json.JSONDecodeError) as error:
        print(f"FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
