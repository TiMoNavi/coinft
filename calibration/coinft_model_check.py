#!/usr/bin/env python3
"""Run the CoinFT ONNX calibration model on a raw capture JSON."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

import numpy as np
import onnxruntime as ort


def load_capture(path: str) -> np.ndarray:
    record = json.loads(pathlib.Path(path).read_text(encoding="ascii"))
    if record.get("schema") != "coinft-raw-v1":
        raise ValueError("unsupported capture schema")
    data = np.asarray(record.get("frames"), dtype=np.float32)
    if data.ndim != 2 or data.shape[1] != 12 or len(data) < 2:
        raise ValueError("capture must contain at least two 12-channel frames")
    return data


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--capture", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--norm", required=True)
    parser.add_argument("--offset-capture")
    parser.add_argument("--tare-frames", type=int, default=100)
    parser.add_argument("--output")
    args = parser.parse_args()
    frames = load_capture(args.capture)
    if args.tare_frames < 1 or args.tare_frames > len(frames):
        raise ValueError("tare-frames must fit inside capture")
    offset_frames = load_capture(args.offset_capture) if args.offset_capture else frames
    if args.tare_frames > len(offset_frames):
        raise ValueError("tare-frames must fit inside offset capture")
    norm = json.loads(pathlib.Path(args.norm).read_text(encoding="ascii"))
    mu_x = np.asarray(norm["mu_x"], dtype=np.float32)
    sd_x = np.asarray(norm["sd_x"], dtype=np.float32)
    mu_y = np.asarray(norm["mu_y"], dtype=np.float32)
    sd_y = np.asarray(norm["sd_y"], dtype=np.float32)
    if any(array.shape != (12,) for array in (mu_x, sd_x)):
        raise ValueError("input normalization must contain 12 values")
    if any(array.shape != (6,) for array in (mu_y, sd_y)):
        raise ValueError("output normalization must contain 6 values")
    session = ort.InferenceSession(args.model)
    input_name = session.get_inputs()[0].name
    offset = np.mean(offset_frames[:args.tare_frames], axis=0)
    outputs = []
    for frame in frames:
        model_input = ((frame - offset - mu_x) / sd_x).reshape(1, 12).astype(np.float32)
        outputs.append(session.run(None, {input_name: model_input})[0].reshape(6) * sd_y + mu_y)
    result = np.asarray(outputs, dtype=np.float64)
    report = {
        "schema": "coinft-model-check-v1",
        "capture": args.capture,
        "offset_capture": args.offset_capture or args.capture,
        "frame_count": int(len(frames)),
        "tare_frames": args.tare_frames,
        "model": args.model,
        "norm": args.norm,
        "finite": bool(np.isfinite(result).all()),
        "median": [float(v) for v in np.median(result, axis=0)],
        "std": [float(v) for v in np.std(result, axis=0, ddof=1)],
        "span": [float(v) for v in np.ptp(result, axis=0)],
    }
    encoded = json.dumps(report, indent=2) + "\n"
    if args.output:
        pathlib.Path(args.output).write_text(encoded, encoding="ascii")
        print(f"report: {args.output}")
    print("finite:", report["finite"])
    print("median:", " ".join(f"{v:.6g}" for v in report["median"]))
    print("span:", " ".join(f"{v:.6g}" for v in report["span"]))
    return 0 if report["finite"] else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
