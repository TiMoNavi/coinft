#!/usr/bin/env python3
"""Compile the portable Teensy inference core and compare it to ONNX Runtime."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import numpy as np
import onnxruntime as ort

from generate_model_header import MODEL_SPECS, _load_norm


WORKSPACE = Path(__file__).resolve().parents[1]
REPO_ROOT = WORKSPACE.parent
SKETCH_DIR = WORKSPACE / "firmware" / "teensy_coinft_mlp"
sys.path.insert(0, str(WORKSPACE / "host"))
sys.path.insert(0, str(REPO_ROOT))


def compile_runner(compiler: str, build_dir: Path) -> Path:
    binary = build_dir / "firmware_runner"
    command = [
        compiler,
        "-std=c++11",
        "-O2",
        "-ffp-contract=off",
        "-Wall",
        "-Wextra",
        str(SKETCH_DIR / "calibration.cpp"),
        "-I",
        str(SKETCH_DIR),
        str(SKETCH_DIR / "inference.cpp"),
        str(SKETCH_DIR / "output_protocol.cpp"),
        str(WORKSPACE / "tests" / "firmware_runner.cpp"),
        "-o",
        str(binary),
    ]
    subprocess.run(command, check=True)
    sketch_command = [
        compiler,
        "-std=c++11",
        "-Wall",
        "-Wextra",
        "-fsyntax-only",
        "-x",
        "c++",
        "-I",
        str(WORKSPACE / "tests" / "arduino_stub"),
        "-I",
        str(SKETCH_DIR),
        str(SKETCH_DIR / "teensy_coinft_mlp.ino"),
    ]
    subprocess.run(sketch_command, check=True)
    return binary


def compare_model(binary: Path, repo_root: Path, side: str, sample_count: int, seed: int) -> dict[str, float | int | str]:
    model_dir = repo_root / "models (3) (1)" / MODEL_SPECS[side]
    onnx_path = model_dir / "CFT24_MLP.onnx"
    norm = _load_norm(model_dir / "CFT24_norm.json")
    session = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
    input_name = session.get_inputs()[0].name

    rng = np.random.default_rng(seed)
    # Include learned-input neighborhoods and the complete unsigned sensor range.
    raw = rng.integers(0, 4096, size=(sample_count, 12), dtype=np.uint16).astype(np.float32)
    raw[: sample_count // 2] = np.clip(
        rng.normal(norm["mu_x"], norm["sd_x"] * 1.5, size=(sample_count // 2, 12)), 0, 65535
    ).astype(np.float32)
    normalized = (raw - norm["mu_x"]) / norm["sd_x"]
    expected = np.stack(
        [session.run(None, {input_name: sample.reshape(1, 12)})[0][0] for sample in normalized],
        axis=0,
    )
    expected = expected * norm["sd_y"] + norm["mu_y"]

    rows = ["I " + side + " " + " ".join(format(float(value), ".9g") for value in sample) for sample in raw]
    output = subprocess.run(
        [str(binary)],
        input="\n".join(rows) + "\n",
        text=True,
        capture_output=True,
        check=True,
    ).stdout.splitlines()
    actual = np.asarray([[float(value) for value in row.split()] for row in output], dtype=np.float32)
    error = np.abs(expected.astype(np.float32) - actual)
    denominator = np.maximum(np.abs(expected), 1e-5)
    relative = error / denominator
    return {
        "side": side,
        "samples": sample_count,
        "max_abs_error": float(np.max(error)),
        "max_relative_error": float(np.max(relative)),
        "rmse": float(np.sqrt(np.mean(error**2))),
    }


def verify_protocol(binary: Path) -> None:
    from protocol import decode_frame, encode_frame

    left = (1.25, -2.5, 3.75, -0.0125, 0.025, -0.0375)
    right = (-4.25, 5.5, -6.75, 0.0625, -0.075, 0.0875)
    for warning in (False, True):
        request = "P 7 1 4294967295 305419896 " + " ".join(map(str, left + right)) + f" {int(warning)}\n"
        hex_frame = subprocess.run([str(binary)], input=request, text=True, capture_output=True, check=True).stdout.strip()
        cpp_bytes = bytes.fromhex(hex_frame)
        py_bytes = encode_frame(
            0xFFFFFFFF, 0x12345678, left, right,
            calibration_status=7, wrench_valid=True, zero_check_warning=warning,
        )
        if cpp_bytes != py_bytes:
            raise AssertionError("C++ and Python frame encoders produced different bytes")
        decoded = decode_frame(cpp_bytes)
        if decoded.sequence != 0xFFFFFFFF or decoded.teensy_time_us != 0x12345678:
            raise AssertionError("C++ frame metadata did not round-trip")
        if decoded.zero_check_warning != warning or not decoded.wrench_valid:
            raise AssertionError("zero-check warning changed wrench validity")


def verify_calibration(binary: Path, sample_count: int = 500) -> None:
    from receiver.models import CoinFTModel
    from receiver.warmup import CoinFTWarmup, WarmupConfig

    models = {
        side: CoinFTModel(
            REPO_ROOT / "models (3) (1)" / MODEL_SPECS[side] / "CFT24_MLP.onnx",
            REPO_ROOT / "models (3) (1)" / MODEL_SPECS[side] / "CFT24_norm.json",
        )
        for side in ("left", "right")
    }
    warmup = CoinFTWarmup(models, WarmupConfig())
    status_ids = {
        "warming_up": 0,
        "delayed": 1,
        "provisional_tare": 2,
        "runtime_settling": 3,
        "final_tare": 4,
        "bias_calibration": 5,
        "zero_check": 6,
        "ready": 7,
        "zero_check_failed": 8,
        "error": 9,
    }
    base = {
        "left": np.arange(1000, 1012, dtype=np.int32),
        "right": np.arange(1200, 1212, dtype=np.int32),
    }
    timestamps_us = [sample * 50000 for sample in range(sample_count)]
    raw_samples: list[dict[str, np.ndarray]] = []
    expected_statuses: list[int] = []
    expected_valid: list[bool] = []
    expected_wrenches: list[tuple[np.ndarray, np.ndarray] | None] = []
    for sample, timestamp_us in enumerate(timestamps_us):
        noise = 45 if sample < 100 and sample % 2 == 0 else -45 if sample < 100 else 0
        motion = ((sample // 2) % 7) - 3 if sample >= 400 else 0
        raws = {
            side: (values + noise + motion * np.where(np.arange(12) % 2, -1, 1)).astype(np.uint16)
            for side, values in base.items()
        }
        result = warmup.process(raws, timestamp_us / 1_000_000.0)
        raw_samples.append(raws)
        expected_statuses.append(status_ids[result["calibration_status"]])
        expected_valid.append(bool(result["calibration_ready"]))
        if result["calibration_ready"]:
            expected_wrenches.append(
                (
                    np.asarray(result["left_wrench"], dtype=np.float32),
                    np.asarray(result["right_wrench"], dtype=np.float32),
                )
            )
        else:
            expected_wrenches.append(None)

    if not warmup.ready:
        raise AssertionError(f"Python reference did not reach ready: {warmup.status}")
    if not {0, 1, 3, 4, 5, 6, 7}.issubset(set(expected_statuses)):
        raise AssertionError(f"calibration sequence missed expected states: {set(expected_statuses)}")
    request = "".join(
        "S " + str(timestamp_us) + " "
        + " ".join(str(int(value)) for value in np.concatenate((raws["left"], raws["right"])))
        + "\n"
        for timestamp_us, raws in zip(timestamps_us, raw_samples)
    )
    output = subprocess.run([str(binary)], input=request, text=True, capture_output=True, check=True).stdout.splitlines()
    if len(output) != sample_count:
        raise AssertionError(f"C++ calibration returned {len(output)} rows for {sample_count} samples")

    max_parity_error = 0.0
    max_force = 0.0
    for sample, line in enumerate(output):
        values = line.split()
        if len(values) != 15:
            raise AssertionError(f"malformed C++ calibration row {sample}: {line}")
        status, valid, warning = map(int, values[:3])
        if status != expected_statuses[sample]:
            raise AssertionError(
                f"calibration state mismatch at sample {sample}: C++={status}, Python={expected_statuses[sample]}"
            )
        if bool(valid) != expected_valid[sample]:
            raise AssertionError(
                f"wrench validity mismatch at sample {sample}: C++={valid}, Python={expected_valid[sample]}"
            )
        if warning:
            raise AssertionError(f"unexpected zero-check warning in stable calibration sample {sample}")
        actual_left = np.asarray([float(value) for value in values[3:9]], dtype=np.float32)
        actual_right = np.asarray([float(value) for value in values[9:15]], dtype=np.float32)
        if expected_wrenches[sample] is not None:
            expected_left, expected_right = expected_wrenches[sample]
            max_parity_error = max(
                max_parity_error,
                float(np.max(np.abs(actual_left - expected_left))),
                float(np.max(np.abs(actual_right - expected_right))),
            )
            max_force = max(
                max_force,
                float(np.max(np.abs(actual_left[:3]))),
                float(np.max(np.abs(actual_right[:3]))),
            )
    if max_parity_error > 2e-4:
        raise AssertionError(f"Teensy tare/bias/filter parity exceeded 2e-4: {max_parity_error}")
    if max_force > 0.15:
        raise AssertionError(f"stationary zero-check force exceeds 0.15 N: {max_force}")
    warning_result = subprocess.run(
        [str(binary)], input="W\n", text=True, capture_output=True, check=True
    ).stdout.split()
    if tuple(map(int, warning_result)) != (7, 1, 1):
        raise AssertionError(f"zero-check disturbance must warn and keep output valid: {warning_result}")
    lifecycle = subprocess.run(
        [str(binary)], input="Q 700 120 50000\n", text=True, capture_output=True, check=True
    ).stdout.split()
    first_ready_us, quick_ready_us = map(int, lifecycle)
    if not 20_000_000 <= first_ready_us <= 35_000_000:
        raise AssertionError(f"first startup calibration took an unexpected time: {first_ready_us} us")
    if not 2_000_000 <= quick_ready_us <= 4_000_000:
        raise AssertionError(f"subsequent quick tare took an unexpected time: {quick_ready_us} us")
    wrap_result = subprocess.run(
        [str(binary)], input="B 14000 1000\n", text=True, capture_output=True, check=True
    ).stdout.split()
    if list(map(int, wrap_result)) != [7, 1]:
        raise AssertionError(f"1 kHz calibration failed after ring-buffer wrap: {wrap_result}")
    print(
        json.dumps(
            {
                "calibration_status": "ready",
                "samples": sample_count,
                "max_force_n": max_force,
                "max_python_parity_error": max_parity_error,
                "first_startup_ready_s": first_ready_us / 1_000_000.0,
                "subsequent_tare_ready_s": quick_ready_us / 1_000_000.0,
                "states_checked": sorted(set(expected_statuses)),
                "ring_wrap_1khz": "ready",
            },
            sort_keys=True,
        )
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
    parser.add_argument("--samples", type=int, default=700)
    parser.add_argument("--compiler", default=os.environ.get("CXX", "g++"))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="coinft-mlp-check-") as temporary:
        binary = compile_runner(args.compiler, Path(temporary))
        results = [
            compare_model(binary, args.repo_root.resolve(), side, args.samples, 260804 + index)
            for index, side in enumerate(("left", "right"))
        ]
        verify_protocol(binary)
        verify_calibration(binary)
    for result in results:
        print(json.dumps(result, sort_keys=True))
        if result["max_abs_error"] > 1e-4:
            raise SystemExit(f"{result['side']} model parity exceeded 1e-4 absolute error")
    print("C++/Python protocol encoding matches byte-for-byte")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
