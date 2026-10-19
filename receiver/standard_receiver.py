#!/usr/bin/env python3
"""Standard live receiver for the revised Teensy/CoinFT protocol.

Examples:

  Dual sensor, with the two production models::

    python receiver/standard_receiver.py --port /dev/cu.usbmodemXXXX \
      --models-json /path/to/coinft_models.json

  One sensor using the current revised firmware's bare 24-byte output::

    python receiver/standard_receiver.py --port /dev/cu.usbmodemXXXX \
      --sensor-count 1 --single-side left --single-format single-legacy24 \
      --models-json /path/to/coinft_models.json

The receiver never sends the ``t`` command.  Tare is performed from the raw
stream so the same behavior is used for one and two sensors.
"""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
import sys
import time
from typing import Any

if __package__ in {None, ""}:  # Allow ``python receiver/standard_receiver.py``.
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

try:
    import serial
except ImportError:  # pragma: no cover - environment dependent
    serial = None  # type: ignore[assignment]

from receiver.models import CoinFTModel
from receiver.protocol_v2 import CoinFTPacket, CoinFTStreamParser, sequence_gap
from receiver.warmup import AXES, CoinFTWarmup, SIDES, WarmupConfig


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MODEL = REPO_ROOT / "hardware_configs" / "CFT24_MLP.onnx"
DEFAULT_NORM = REPO_ROOT / "hardware_configs" / "CFT24_norm.json"
DEFAULT_MODEL_CONFIG = REPO_ROOT / "configs" / "coinft_models_0019_0012.json"


def _path(value: str, base: Path | None = None) -> Path:
    path = Path(value).expanduser()
    return (base / path).resolve() if base is not None and not path.is_absolute() else path.resolve()


def load_model_paths(args: argparse.Namespace, sides: tuple[str, ...]) -> dict[str, dict[str, Any]]:
    specs: dict[str, dict[str, Any]] = {}
    config_path_value = args.models_json or (str(DEFAULT_MODEL_CONFIG) if DEFAULT_MODEL_CONFIG.is_file() else None)
    if config_path_value:
        config_path = _path(config_path_value)
        with config_path.open("r", encoding="utf-8") as handle:
            payload = json.load(handle)
        raw_slots = payload.get("slots", payload)
        if not isinstance(raw_slots, dict):
            raise ValueError("models JSON must contain an object or a slots object")
        for side in sides:
            item = raw_slots.get(side)
            if not isinstance(item, dict):
                raise ValueError(f"models JSON has no {side} slot")
            specs[side] = {
                "model": _path(str(item["model"] if "model" in item else item["model_path"]), config_path.parent),
                "norm": _path(str(item["norm"] if "norm" in item else item["norm_path"]), config_path.parent),
                "axis_map": item.get("axis_map", [0, 1, 2, 3, 4, 5]),
                "axis_sign": item.get("axis_sign", [1, 1, 1, 1, 1, 1]),
            }
    for side, model_arg, norm_arg in (
        ("left", args.left_model, args.left_norm),
        ("right", args.right_model, args.right_norm),
    ):
        if side not in sides:
            continue
        current = specs.setdefault(side, {})
        if model_arg:
            current["model"] = _path(model_arg)
        if norm_arg:
            current["norm"] = _path(norm_arg)
        current.setdefault("model", DEFAULT_MODEL)
        current.setdefault("norm", DEFAULT_NORM)
        if "model" not in current or "norm" not in current:
            raise ValueError(f"missing model and norm paths for {side}")
    return {side: specs[side] for side in sides}


def packet_row(packet: CoinFTPacket, host_receive_ns: int, result: dict[str, Any], sides: tuple[str, ...]) -> dict[str, Any]:
    row: dict[str, Any] = {
        "schema": "coinft-teensy-v2-sample",
        "host_receive_monotonic_ns": host_receive_ns,
        "sequence_id": packet.sequence_id,
        "teensy_time_us": packet.teensy_time_us,
        "wire_format": packet.wire_format,
        "sensor_count": packet.sensor_count,
        "side_order": list(sides),
        "raw": {side: list(packet.sensors[index]) for index, side in enumerate(sides)},
        "wrench_axes": list(AXES),
    }
    row.update(result)
    row["wrench"] = [result.get(f"{side}_wrench") for side in sides]
    return row


def _csv_fields(sides: tuple[str, ...]) -> list[str]:
    fields = [
        "host_receive_monotonic_ns",
        "sequence_id",
        "teensy_time_us",
        "wire_format",
        "sensor_count",
        "calibration_status",
        "calibration_ready",
        "calibration_error",
        "zero_force_max_abs_n",
        "zero_force_limit_n",
    ]
    for side in sides:
        fields.extend(f"{side}_c{index}" for index in range(1, 13))
        fields.extend(f"{side}_{axis}" for axis in AXES)
    return fields


def _csv_row(row: dict[str, Any], sides: tuple[str, ...]) -> dict[str, Any]:
    out = {key: row.get(key) for key in _csv_fields(sides)}
    for side in sides:
        raw = row["raw"][side]
        wrench = row.get(f"{side}_wrench") or []
        for index, value in enumerate(raw, start=1):
            out[f"{side}_c{index}"] = value
        for axis, value in zip(AXES, wrench):
            out[f"{side}_{axis}"] = value
    return out


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--sensor-count", type=int, choices=(1, 2), default=2)
    parser.add_argument("--single-side", choices=SIDES, default="left")
    parser.add_argument(
        "--single-format",
        choices=("single-v2", "single-framed24", "single-legacy24"),
        default="single-legacy24",
        help="single-sensor wire format; the revised current firmware uses single-legacy24",
    )
    parser.add_argument(
        "--models-json",
        help="JSON with left/right model and norm paths; defaults to configs/coinft_models_0019_0012.json",
    )
    parser.add_argument("--left-model")
    parser.add_argument("--left-norm")
    parser.add_argument("--right-model")
    parser.add_argument("--right-norm")
    parser.add_argument("--output", type=Path, default=Path("coinft_v2_samples.jsonl"))
    parser.add_argument("--csv-output", type=Path, default=Path("coinft_v2_samples.csv"))
    parser.add_argument("--plot", action="store_true", help="show dual-sensor force/moment waveforms")
    parser.add_argument("--max-samples", type=int, default=0, help="0 means run until Ctrl-C")
    parser.add_argument("--packet-timeout", type=float, default=1.0)
    parser.add_argument("--moving-average-window", type=int, default=5)
    parser.add_argument("--raw-stability-min-wait-s", type=float, default=5.0)
    parser.add_argument("--runtime-settling-s", type=float, default=5.0)
    parser.add_argument("--final-tare-window-s", type=float, default=2.0)
    parser.add_argument("--bias-window-s", type=float, default=1.0)
    parser.add_argument("--zero-check-window-s", type=float, default=1.0)
    parser.add_argument("--zero-force-limit-n", type=float, default=0.15)
    return parser


def run(args: argparse.Namespace) -> int:
    if serial is None:
        raise RuntimeError("pyserial is required; install it from environment_unix.yml")
    if args.max_samples < 0:
        raise ValueError("--max-samples must be non-negative")
    if args.plot and args.sensor_count != 2:
        raise ValueError("--plot is available only for dual-sensor mode")
    sides = SIDES if args.sensor_count == 2 else (args.single_side,)
    model_paths = load_model_paths(args, tuple(sides))
    models = {
        side: CoinFTModel(
            spec["model"],
            spec["norm"],
            moving_average_window=args.moving_average_window,
            axis_map=spec.get("axis_map", [0, 1, 2, 3, 4, 5]),
            axis_sign=spec.get("axis_sign", [1, 1, 1, 1, 1, 1]),
        )
        for side, spec in model_paths.items()
    }
    warmup = CoinFTWarmup(
        models,
        WarmupConfig(
            moving_average_window=args.moving_average_window,
            raw_stability_min_wait_s=args.raw_stability_min_wait_s,
            runtime_settling_s=args.runtime_settling_s,
            final_tare_window_s=args.final_tare_window_s,
            bias_window_s=args.bias_window_s,
            zero_check_window_s=args.zero_check_window_s,
            zero_force_limit_n=args.zero_force_limit_n,
        ),
    )
    wire_format = "dual-v2" if args.sensor_count == 2 else args.single_format
    parser = CoinFTStreamParser(wire_format)
    plotter = DualWrenchPlot() if args.plot else None
    args.output.parent.mkdir(parents=True, exist_ok=True)
    csv_handle = None
    csv_writer = None
    if args.csv_output:
        args.csv_output.parent.mkdir(parents=True, exist_ok=True)
        csv_handle = args.csv_output.open("w", encoding="utf-8", newline="")
        csv_writer = csv.DictWriter(csv_handle, fieldnames=_csv_fields(tuple(sides)))
        csv_writer.writeheader()

    previous_sequence: int | None = None
    dropped_sequences = 0
    sample_count = 0
    metadata_path = args.output.with_suffix(".meta.json")
    try:
        with args.output.open("w", encoding="utf-8") as output:
            with serial.Serial(args.port, args.baud, timeout=0.02, write_timeout=1.0) as serial_port:
                serial_port.write(b"i")
                serial_port.flush()
                time.sleep(0.2)
                serial_port.reset_input_buffer()
                serial_port.write(b"s")
                serial_port.flush()
                print(f"CoinFT stream started: {args.port}, format={wire_format}, sides={sides}", flush=True)
                while args.max_samples == 0 or sample_count < args.max_samples:
                    try:
                        packet = parser.read_packet(serial_port, args.packet_timeout)
                    except TimeoutError as exc:
                        print(f"timeout: {exc}", file=sys.stderr, flush=True)
                        continue
                    gap, out_of_order = sequence_gap(previous_sequence, packet.sequence_id)
                    if out_of_order:
                        previous_sequence = packet.sequence_id
                        print("discarded packet with out-of-order sequence", file=sys.stderr, flush=True)
                        continue
                    previous_sequence = packet.sequence_id if packet.sequence_id is not None else previous_sequence
                    dropped_sequences += gap
                    host_receive_ns = time.monotonic_ns()
                    raw_by_side = {side: packet.sensors[index] for index, side in enumerate(sides)}
                    result = warmup.process(raw_by_side, host_receive_ns / 1e9)
                    row = packet_row(packet, host_receive_ns, result, tuple(sides))
                    output.write(json.dumps(row, separators=(",", ":")) + "\n")
                    output.flush()
                    if csv_writer is not None:
                        csv_writer.writerow(_csv_row(row, tuple(sides)))
                        csv_handle.flush()
                    if plotter is not None:
                        plotter.update(row)
                    sample_count += 1
                    if sample_count == 1 or sample_count % 50 == 0:
                        print(
                            f"samples={sample_count} status={warmup.status} ready={warmup.ready} "
                            f"dropped={dropped_sequences} skipped_bytes={parser.skipped_bytes}",
                            flush=True,
                        )
    except KeyboardInterrupt:
        print("\nStopping CoinFT receiver", flush=True)
    finally:
        if csv_handle is not None:
            csv_handle.close()
        metadata = {
            "schema": "coinft-teensy-v2-run",
            "wire_format": wire_format,
            "sensor_count": args.sensor_count,
            "side_order": list(sides),
            "models": {
                side: {
                    "model": str(spec["model"]),
                    "norm": str(spec["norm"]),
                    "axis_map": list(spec.get("axis_map", [0, 1, 2, 3, 4, 5])),
                    "axis_sign": list(spec.get("axis_sign", [1, 1, 1, 1, 1, 1])),
                }
                for side, spec in model_paths.items()
            },
            "sample_count": sample_count,
            "dropped_sequences": dropped_sequences,
            "skipped_bytes": parser.skipped_bytes,
            "calibration_status": warmup.status,
            "calibration_ready": warmup.ready,
            "calibration_error": warmup.error,
            "provisional_offsets": {side: value.tolist() for side, value in warmup.provisional_offsets.items()},
            "final_offsets": {side: value.tolist() for side, value in warmup.final_offsets.items()},
            "ft_biases": {side: value.tolist() for side, value in warmup.ft_biases.items()},
        }
        metadata_path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return 0


class DualWrenchPlot:
    """Small live plot matching the official dual-sensor visualizer."""

    def __init__(self) -> None:
        try:
            import matplotlib.pyplot as plt
        except ImportError as exc:  # pragma: no cover - environment dependent
            raise RuntimeError("matplotlib is required for --plot") from exc
        self.plt = plt
        self.figure, self.axes = plt.subplots(2, 2, figsize=(12, 7), squeeze=False)
        self.history: dict[str, list[tuple[float, list[float]]]] = {side: [] for side in SIDES}
        self.lines: dict[str, list[Any]] = {}
        self.start_s: float | None = None
        colors = ("#d62728", "#2ca02c", "#1f77b4")
        for column, side in enumerate(SIDES):
            force_axis = self.axes[0][column]
            moment_axis = self.axes[1][column]
            force_axis.set_title(f"{side.title()} CoinFT - Force")
            moment_axis.set_title(f"{side.title()} CoinFT - Moment")
            force_axis.set_ylabel("N")
            moment_axis.set_ylabel("Nm")
            force_axis.grid(True, alpha=0.3)
            moment_axis.grid(True, alpha=0.3)
            force_lines = [force_axis.plot([], [], color=color, label=axis.upper())[0] for color, axis in zip(colors, AXES[:3])]
            moment_lines = [moment_axis.plot([], [], color=color, linestyle="--", label=axis.upper())[0] for color, axis in zip(colors, AXES[3:])]
            force_axis.legend(loc="upper right")
            moment_axis.legend(loc="upper right")
            self.lines[side] = force_lines + moment_lines
        self.figure.tight_layout()
        plt.ion()
        plt.show(block=False)

    def update(self, row: dict[str, Any]) -> None:
        stamp = float(row["host_receive_monotonic_ns"]) / 1e9
        if self.start_s is None:
            self.start_s = stamp
        for side in SIDES:
            wrench = row.get(f"{side}_wrench")
            if not isinstance(wrench, list) or len(wrench) != 6:
                continue
            history = self.history[side]
            history.append((stamp - self.start_s, [float(value) for value in wrench]))
            cutoff = history[-1][0] - 10.0
            while history and history[0][0] < cutoff:
                history.pop(0)
            times = [item[0] for item in history]
            for axis_index, line in enumerate(self.lines[side]):
                line.set_data(times, [item[1][axis_index] for item in history])
            column = 0 if side == "left" else 1
            for axis in (self.axes[0][column], self.axes[1][column]):
                axis.relim()
                axis.autoscale_view()
            self.axes[1][column].set_xlabel("Time [s]")
        self.figure.canvas.draw_idle()
        self.figure.canvas.flush_events()
        self.plt.pause(0.001)


def main() -> int:
    args = build_parser().parse_args()
    return run(args)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
