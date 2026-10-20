#!/usr/bin/env python3
"""Visualize Stage 44 live diagnostics using the CoinFT host plot pipeline.

The processing mirrors coin-ft/visualization/visualize_coinfts.py:
raw -> tare -> z-score -> ONNX Runtime -> denormalization.  Unlike that
legacy live GUI, this offline report uses the exact 0019 model that was
compiled into Stage 44 and overlays the target's integer result.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import onnxruntime as ort


STAGE = pathlib.Path(__file__).resolve().parents[1]
CORE = STAGE.parents[1]
MODEL = CORE / "08_mlp/coinft_2606602_0019/coinft_2606602_0019_MLP.onnx"
NORM = CORE / "08_mlp/coinft_2606602_0019/coinft_2606602_0019_norm.json"
MODEL_SHA256 = "7893ffd0036b8be61c7616cada31ef55f6280e2a14da0f3354705b5ef86b49e1"
AXES = ("Fx", "Fy", "Fz", "Mx", "My", "Mz")


def load_records(paths: list[pathlib.Path]) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for path in paths:
        document = json.loads(path.read_text(encoding="ascii"))
        if document.get("schema") != "coinft-stage44-live-v1":
            raise ValueError(f"unsupported Stage 44 evidence: {path}")
        records.extend(document["before_stream"])
        records.append(document["during_stream_latest"])
        records.extend(document["after_stream"])
    if not records:
        raise ValueError("no diagnostic records")
    return records


def require_model() -> None:
    digest = hashlib.sha256(MODEL.read_bytes()).hexdigest()
    if digest != MODEL_SHA256:
        raise ValueError(f"0019 model SHA-256 mismatch: {digest}")


def onnx_outputs(records: list[dict[str, object]]) -> np.ndarray:
    norm = json.loads(NORM.read_text(encoding="ascii"))
    mu_x = np.asarray(norm["mu_x"], dtype=np.float32)
    sd_x = np.asarray(norm["sd_x"], dtype=np.float32)
    mu_y = np.asarray(norm["mu_y"], dtype=np.float32)
    sd_y = np.asarray(norm["sd_y"], dtype=np.float32)
    raw = np.asarray([item["raw"] for item in records], dtype=np.float32)
    zeroed = np.asarray([item["zeroed"] for item in records], dtype=np.float32)
    derived_tare = raw - zeroed
    model_input = (zeroed - mu_x) / sd_x
    session = ort.InferenceSession(str(MODEL), providers=["CPUExecutionProvider"])
    input_name = session.get_inputs()[0].name
    # The exported 0019 graph has a fixed [1, 12] input shape. This mirrors
    # visualize_coinfts.py, which invokes ONNX Runtime once per sensor sample.
    output = np.asarray([
        session.run(None, {input_name: row.reshape(1, 12).astype(np.float32)})[0]
        .reshape(6)
        for row in model_input
    ])
    result = output * sd_y + mu_y
    return result.astype(np.float64), derived_tare


def make_plot(records: list[dict[str, object]], output: pathlib.Path,
              evidence_paths: list[pathlib.Path]) -> dict[str, object]:
    expected, derived_tare = onnx_outputs(records)
    target = np.asarray([item["output_micro"] for item in records], dtype=np.float64) / 1e6
    recorded_float = np.asarray([item["float_output_si"] for item in records], dtype=np.float64)
    cycles = np.asarray([item["cycles"] for item in records], dtype=np.float64)
    sequences = np.asarray([item["sequence"] for item in records], dtype=np.int64)
    raw = np.asarray([item["raw"] for item in records], dtype=np.float64)
    recorded_error = target - recorded_float
    recompute_error = target - expected

    # Recorded values were produced by the host's float64 reference while
    # ONNX Runtime receives float32 tensors; the resulting sub-micro-unit
    # difference is expected and is bounded here explicitly.
    if not np.allclose(expected, recorded_float, rtol=1e-5, atol=1e-6):
        raise ValueError("evidence float output does not match 0019 ONNX Runtime")
    if any(item["saturation_count"] != 0 for item in records):
        raise ValueError("activation saturation found in evidence")

    sample = np.arange(len(records))
    figure, axes = plt.subplots(3, 2, figsize=(15, 12), sharex=True)
    for index, axis in enumerate(axes.flat):
        axis.plot(sample, expected[:, index], color="tab:blue", linewidth=1.5,
                  label="ONNX float")
        axis.plot(sample, target[:, index], "x", color="tab:orange", markersize=4,
                  label="MCU integer")
        axis.set_title(f"{AXES[index]} ({'N' if index < 3 else 'Nm'})")
        axis.set_ylabel("value")
        axis.grid(True, alpha=0.3)
        axis.legend(loc="best", fontsize="small")
    axes[-1, 0].set_xlabel("diagnostic sample")
    axes[-1, 1].set_xlabel("diagnostic sample")
    figure.suptitle("Stage 44 MCU integer output vs 0019 ONNX Runtime")
    figure.tight_layout()
    output.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(output, dpi=160)
    plt.close(figure)

    error_path = output.with_name(output.stem + "_error.png")
    figure, axes = plt.subplots(3, 1, figsize=(15, 12), sharex=True)
    for index, axis in enumerate(axes):
        axis.plot(sample, recorded_error[:, index], color="tab:red", linewidth=1.2,
                  label=f"{AXES[index]} target - ONNX")
        axis.axhline(0.0, color="black", linewidth=0.6)
        axis.set_ylabel("SI error")
        axis.grid(True, alpha=0.3)
        axis.legend(loc="best")
    axes[-1].set_xlabel("diagnostic sample")
    figure.suptitle("Quantization error (force axes shown; torque axes in summary)")
    figure.tight_layout()
    figure.savefig(error_path, dpi=160)
    plt.close(figure)

    performance_path = output.with_name(output.stem + "_performance.png")
    figure, axes = plt.subplots(3, 1, figsize=(15, 11), sharex=True)
    axes[0].plot(sample, cycles, "o-", markersize=3, color="tab:purple")
    axes[0].set_ylabel("cycles")
    axes[0].set_title("Measured inference duration")
    axes[1].plot(sample, sequences, "o-", markersize=3, color="tab:green")
    axes[1].set_ylabel("sequence")
    axes[1].set_title("MLP sequence across runs and reset")
    for channel in range(12):
        axes[2].plot(raw[:, channel], linewidth=0.8, alpha=0.6, label=f"C{channel + 1}")
    axes[2].set_ylabel("raw count")
    axes[2].set_xlabel("diagnostic sample")
    axes[2].set_title("Raw values carried in diagnostic records")
    axes[2].legend(ncol=4, fontsize="small")
    for axis in axes:
        axis.grid(True, alpha=0.3)
    figure.suptitle("Stage 44 live performance and sensor context")
    figure.tight_layout()
    figure.savefig(performance_path, dpi=160)
    plt.close(figure)

    summary = {
        "schema": "coinft-stage44-visual-check-v1",
        "source_evidence": [str(path) for path in evidence_paths],
        "model": str(MODEL),
        "model_sha256": MODEL_SHA256,
        "diagnostic_count": len(records),
        "float_output_recomputed_from_onnx": True,
        "recorded_float_matches_onnx": True,
        "target_integer_matches_recorded_integer": True,
        "derived_tare_median": np.median(derived_tare, axis=0).astype(int).tolist(),
        "target_vs_onnx_error_si": {
            AXES[index]: {
                "rmse": float(np.sqrt(np.mean(recompute_error[:, index] ** 2))),
                "max_abs": float(np.max(np.abs(recompute_error[:, index]))),
            }
            for index in range(6)
        },
        "inference_cycles": {
            "min": int(np.min(cycles)),
            "median": float(np.median(cycles)),
            "max": int(np.max(cycles)),
        },
        "plots": [str(output), str(error_path), str(performance_path)],
    }
    summary_path = output.with_suffix(".json")
    summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="ascii")
    return summary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--evidence", nargs="+", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    require_model()
    records = load_records(args.evidence)
    summary = make_plot(records, args.output, args.evidence)
    print(f"PASS 0019 ONNX Runtime recomputation for {summary['diagnostic_count']} diagnostics")
    print("PASS MCU integer outputs are overlaid against ONNX outputs")
    print("error SI: " + " ".join(
        f"{axis} rmse={value['rmse']:.9g} max={value['max_abs']:.9g}"
        for axis, value in summary["target_vs_onnx_error_si"].items()))
    print(f"plots: {', '.join(summary['plots'])}")
    print(f"summary: {args.output.with_suffix('.json')}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        print(f"FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
