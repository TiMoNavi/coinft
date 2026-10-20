#!/usr/bin/env python3
"""Exercise Stage 44 raw streaming and fixed-point MLP on real hardware."""

from __future__ import annotations

import argparse
import json
import pathlib
import statistics
import struct
import sys
import time

import numpy as np
import serial
from serial.tools import list_ports

STAGE = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(STAGE / "tools"))
import generate_mlp_model as generator  # noqa: E402

BAUD = 1_000_000
RAW_FRAME_BYTES = 26
DIAGNOSTIC_BYTES = 114


def find_port() -> str:
    matches = [item.device for item in list_ports.comports()
               if "KitProg3 USB-UART" in (item.description or "")]
    if len(matches) != 1:
        raise RuntimeError("expected one KitProg3 USB-UART; found: " +
                           (", ".join(matches) if matches else "none"))
    return matches[0]


def read_exact(port: serial.Serial, size: int, timeout: float) -> bytes:
    data = bytearray()
    deadline = time.monotonic() + timeout
    while len(data) < size and time.monotonic() < deadline:
        data.extend(port.read(size - len(data)))
    if len(data) != size:
        raise TimeoutError(f"received {len(data)} of {size} bytes")
    return bytes(data)


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 \
                  else (crc << 1) & 0xFFFF
    return crc


def check_q(port: serial.Serial) -> None:
    port.reset_input_buffer()
    port.write(b"q")
    port.flush()
    if read_exact(port, 1, 1.0) != b"\x1a":
        raise RuntimeError("q response mismatch")


def zero(port: serial.Serial) -> None:
    port.reset_input_buffer()
    port.write(b"z")
    port.flush()
    if read_exact(port, 1, 2.0) != b"Z":
        raise RuntimeError("z response mismatch")


def parse_diagnostic(encoded: bytes) -> dict[str, object]:
    if len(encoded) != DIAGNOSTIC_BYTES:
        raise ValueError("diagnostic frame length mismatch")
    if encoded[:4] != bytes((0xA5, 0x5A, 1, DIAGNOSTIC_BYTES)):
        raise ValueError(f"diagnostic header mismatch: {encoded[:4].hex()}")
    expected_crc = struct.unpack_from("<H", encoded, 112)[0]
    if crc16_ccitt(encoded[:112]) != expected_crc:
        raise ValueError("diagnostic CRC mismatch")
    return {
        "frame_hex": encoded.hex(),
        "sequence": struct.unpack_from("<I", encoded, 4)[0],
        "raw": list(struct.unpack_from("<12H", encoded, 8)),
        "zeroed": list(struct.unpack_from("<12i", encoded, 32)),
        "output_micro": list(struct.unpack_from("<6i", encoded, 80)),
        "cycles": struct.unpack_from("<I", encoded, 104)[0],
        "saturation_count": struct.unpack_from("<H", encoded, 108)[0],
        "flags": struct.unpack_from("<H", encoded, 110)[0],
    }


def diagnostic(port: serial.Serial, command: bytes) -> dict[str, object]:
    port.reset_input_buffer()
    port.write(command)
    port.flush()
    return parse_diagnostic(read_exact(port, DIAGNOSTIC_BYTES, 3.0))


class RawReader:
    def __init__(self, port: serial.Serial) -> None:
        self.port = port
        self.buffer = bytearray()

    def next(self, deadline: float) -> tuple[list[int], float]:
        while time.monotonic() < deadline:
            start = self.buffer.find(b"\x02")
            if start < 0:
                self.buffer.clear()
            elif start > 0:
                del self.buffer[:start]
            if len(self.buffer) >= RAW_FRAME_BYTES:
                candidate = bytes(self.buffer[:RAW_FRAME_BYTES])
                if candidate[-1] == 0x03:
                    del self.buffer[:RAW_FRAME_BYTES]
                    return list(struct.unpack("<12H", candidate[1:-1])), time.monotonic()
                del self.buffer[0]
                continue
            chunk = self.port.read(1)
            if chunk:
                self.buffer.extend(chunk)
        raise TimeoutError("raw frame timeout")


def capture_stream(port: serial.Serial, count: int) -> dict[str, object]:
    port.reset_input_buffer()
    reader = RawReader(port)
    port.write(b"s")
    port.flush()
    frames: list[list[int]] = []
    stamps: list[float] = []
    deadline = time.monotonic() + max(10.0, count / 100.0)
    for _ in range(count):
        raw, stamp = reader.next(deadline)
        frames.append(raw)
        stamps.append(stamp)
    port.write(b"i")
    port.flush()

    tail = bytearray()
    stop_deadline = time.monotonic() + 1.5
    last_data = time.monotonic()
    while time.monotonic() < stop_deadline:
        chunk = port.read(256)
        if chunk:
            tail.extend(chunk)
            last_data = time.monotonic()
        elif time.monotonic() - last_data >= 0.15:
            break
    else:
        raise RuntimeError("raw stream did not stop")

    duration = stamps[-1] - stamps[0]
    rate = (len(stamps) - 1) / duration
    return {
        "frame_count": count,
        "rate_hz": rate,
        "median": [statistics.median(row[channel] for row in frames)
                   for channel in range(12)],
        "span": [max(row[channel] for row in frames) -
                 min(row[channel] for row in frames) for channel in range(12)],
        "post_stop_tail_bytes": len(tail),
    }


def validate_diagnostic(model: generator.QuantizedModel,
                        record: dict[str, object]) -> None:
    zeroed = np.asarray(record["zeroed"], dtype=np.int64)
    expected, saturation = generator.integer_infer(model, zeroed)
    actual = np.asarray(record["output_micro"], dtype=np.int32)
    if not np.array_equal(expected[0], actual):
        raise RuntimeError(f"target/Python integer mismatch: {actual} != {expected[0]}")
    if int(saturation[0]) != record["saturation_count"]:
        raise RuntimeError("target/Python saturation mismatch")
    raw = np.asarray(record["raw"], dtype=np.int64)
    tare = raw - zeroed
    record["derived_tare"] = tare.astype(int).tolist()
    floating = generator.float_infer(model, zeroed)[0]
    record["float_output_si"] = floating.tolist()
    record["integer_error_si"] = (actual / 1_000_000.0 - floating).tolist()
    record["elapsed_us_at_48mhz"] = record["cycles"] / 48.0


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port")
    parser.add_argument("--output", required=True)
    parser.add_argument("--frames", type=int, default=200)
    parser.add_argument("--diagnostics", type=int, default=8)
    args = parser.parse_args()
    if args.frames < 2 or args.diagnostics < 1:
        raise ValueError("frames must be >= 2 and diagnostics >= 1")

    device = args.port or find_port()
    model = generator.load_quantized_model()
    with serial.Serial(device, BAUD, timeout=0.02, write_timeout=1.0) as port:
        check_q(port)
        zero(port)
        before = [diagnostic(port, b"p") for _ in range(args.diagnostics)]
        for item in before:
            validate_diagnostic(model, item)
        stream = capture_stream(port, args.frames)
        during = diagnostic(port, b"r")
        validate_diagnostic(model, during)
        after = [diagnostic(port, b"p") for _ in range(args.diagnostics)]
        for item in after:
            validate_diagnostic(model, item)
        check_q(port)

    if during["sequence"] <= before[-1]["sequence"]:
        raise RuntimeError("MLP sequence did not advance during raw streaming")
    if not 180.0 <= stream["rate_hz"] <= 220.0:
        raise RuntimeError(f"raw stream rate out of range: {stream['rate_hz']}")
    all_diagnostics = before + [during] + after
    if any(item["saturation_count"] != 0 for item in all_diagnostics):
        raise RuntimeError("MLP activation saturation observed")

    output = {
        "schema": "coinft-stage44-live-v1",
        "port": device,
        "baud": BAUD,
        "stream": stream,
        "sequence_advance_during_stream":
            during["sequence"] - before[-1]["sequence"],
        "before_stream": before,
        "during_stream_latest": during,
        "after_stream": after,
        "inference_cycles": {
            "minimum": min(item["cycles"] for item in all_diagnostics),
            "median": statistics.median(item["cycles"] for item in all_diagnostics),
            "maximum": max(item["cycles"] for item in all_diagnostics),
        },
    }
    path = pathlib.Path(args.output)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(output, indent=2) + "\n", encoding="ascii")
    print(f"PASS target/Python integer match for {len(all_diagnostics)} diagnostics")
    print(f"PASS MLP advanced {output['sequence_advance_during_stream']} times during raw stream")
    print(f"PASS raw rate {stream['rate_hz']:.3f} Hz for {args.frames} frames")
    print("inference cycles min/median/max: " +
          f"{output['inference_cycles']['minimum']}/" +
          f"{output['inference_cycles']['median']}/" +
          f"{output['inference_cycles']['maximum']}")
    print(f"evidence: {path}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, TimeoutError, ValueError) as error:
        print(f"FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
