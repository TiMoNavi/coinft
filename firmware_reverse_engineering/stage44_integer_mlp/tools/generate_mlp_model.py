#!/usr/bin/env python3
"""Generate the integer-only CoinFT 0019 MLP constants and golden vectors."""

from __future__ import annotations

import hashlib
import json
import pathlib
import struct
from dataclasses import dataclass

import numpy as np
import onnx


STAGE = pathlib.Path(__file__).resolve().parents[1]
CORE = STAGE.parents[1]
MODEL_DIR = CORE / "08_mlp/coinft_2606602_0019"
ONNX_PATH = MODEL_DIR / "coinft_2606602_0019_MLP.onnx"
EXTERNAL_PATH = MODEL_DIR / "CFT24_MLP.onnx.data"
NORM_PATH = MODEL_DIR / "coinft_2606602_0019_norm.json"
CAPTURE_PATH = CORE / "07_flash_and_probe/capture_sample/raw_coinft_packets.bin"
RUNTIME_PATH = CORE / "07_flash_and_probe/capture_sample/coinft_calibration_runtime.json"

ACTIVATION_SCALES = (4096, 2048, 1024, 2048, 1024, 2048)
INPUT_SHIFT = 8
REQUANT_SHIFT = 24
OUTPUT_SHIFT = 20


@dataclass
class QuantizedModel:
    input_multiplier: np.ndarray
    input_offset: np.ndarray
    weights: list[np.ndarray]
    multipliers: list[np.ndarray]
    biases: list[np.ndarray]
    norm: dict[str, list[float]]
    float_weights: list[np.ndarray]
    float_biases: list[np.ndarray]


def round_away(values: np.ndarray) -> np.ndarray:
    values = np.asarray(values, dtype=np.float64)
    return np.where(values >= 0, np.floor(values + 0.5),
                    np.ceil(values - 0.5)).astype(np.int64)


def round_shift(values: np.ndarray, shift: int) -> np.ndarray:
    values = np.asarray(values, dtype=np.int64)
    half = 1 << (shift - 1)
    return np.where(values >= 0, (values + half) >> shift,
                    -(((-values) + half) >> shift))


def load_quantized_model() -> QuantizedModel:
    graph = onnx.load(str(ONNX_PATH), load_external_data=True)
    tensors = {
        item.name: onnx.numpy_helper.to_array(item).astype(np.float64)
        for item in graph.graph.initializer
    }
    norm = json.loads(NORM_PATH.read_text(encoding="ascii"))
    float_weights = [tensors[f"net.{index}.weight"] for index in range(0, 12, 2)]
    float_biases = [tensors[f"net.{index}.bias"] for index in range(0, 12, 2)]

    sd_x = np.asarray(norm["sd_x"], dtype=np.float64)
    mu_x = np.asarray(norm["mu_x"], dtype=np.float64)
    input_multiplier = round_away(
        ACTIVATION_SCALES[0] / sd_x * (1 << INPUT_SHIFT))
    input_offset = round_away(
        -mu_x / sd_x * ACTIVATION_SCALES[0] * (1 << INPUT_SHIFT))

    weights: list[np.ndarray] = []
    multipliers: list[np.ndarray] = []
    biases: list[np.ndarray] = []
    for layer, (weight, bias) in enumerate(zip(float_weights, float_biases)):
        weight_scale = np.max(np.abs(weight), axis=1) / 127.0
        weight_scale[weight_scale == 0.0] = 1.0 / 127.0
        weights.append(round_away(weight / weight_scale[:, None]).astype(np.int8))

        if layer < 5:
            multiplier = (weight_scale * ACTIVATION_SCALES[layer + 1] /
                          ACTIVATION_SCALES[layer])
            multipliers.append(round_away(multiplier * (1 << REQUANT_SHIFT)))
            biases.append(round_away(bias * ACTIVATION_SCALES[layer + 1]))
        else:
            sd_y = np.asarray(norm["sd_y"], dtype=np.float64)
            mu_y = np.asarray(norm["mu_y"], dtype=np.float64)
            multiplier = (weight_scale * sd_y * 1_000_000.0 /
                          ACTIVATION_SCALES[layer])
            multipliers.append(round_away(multiplier * (1 << OUTPUT_SHIFT)))
            biases.append(round_away((bias * sd_y + mu_y) * 1_000_000.0))

    return QuantizedModel(input_multiplier, input_offset, weights,
                          multipliers, biases, norm,
                          float_weights, float_biases)


def integer_infer(model: QuantizedModel, zeroed: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    zeroed = np.atleast_2d(np.asarray(zeroed, dtype=np.int64))
    activation = round_shift(
        zeroed * model.input_multiplier + model.input_offset, INPUT_SHIFT)
    saturation = np.sum((activation < -32768) | (activation > 32767), axis=1)
    activation = np.clip(activation, -32768, 32767)

    for layer in range(5):
        accumulator = activation @ model.weights[layer].astype(np.int64).T
        value = round_shift(
            accumulator * model.multipliers[layer], REQUANT_SHIFT)
        value += model.biases[layer]
        saturation += np.sum(value > 32767, axis=1)
        activation = np.clip(value, 0, 32767)

    accumulator = activation @ model.weights[5].astype(np.int64).T
    output = round_shift(
        accumulator * model.multipliers[5], OUTPUT_SHIFT)
    output += model.biases[5]
    return output.astype(np.int32), saturation.astype(np.uint16)


def float_infer(model: QuantizedModel, zeroed: np.ndarray) -> np.ndarray:
    zeroed = np.atleast_2d(np.asarray(zeroed, dtype=np.float64))
    activation = ((zeroed - np.asarray(model.norm["mu_x"])) /
                  np.asarray(model.norm["sd_x"]))
    for layer, (weight, bias) in enumerate(
            zip(model.float_weights, model.float_biases)):
        activation = activation @ weight.T + bias
        if layer < 5:
            activation = np.maximum(activation, 0.0)
    return (activation * np.asarray(model.norm["sd_y"]) +
            np.asarray(model.norm["mu_y"]))


def c_array(name: str, c_type: str, values: np.ndarray) -> str:
    flat = np.asarray(values).reshape(-1)
    width = 16 if c_type == "int8_t" else 8
    lines = []
    for start in range(0, len(flat), width):
        lines.append("    " + ", ".join(str(int(value)) for value in
                                         flat[start:start + width]) + ",")
    return (f"MODEL_DATA const {c_type} {name}[{len(flat)}] = {{\n" +
            "\n".join(lines) + "\n};\n")


def write_model_source(model: QuantizedModel) -> None:
    chunks = [
        '#include "mlp_model.h"\n\n',
        '#if defined(MLP_HOST_TEST)\n'
        '#define MODEL_DATA\n'
        '#else\n'
        '#define MODEL_DATA __attribute__((section(".mlp_model"), used, aligned(4)))\n'
        '#endif\n\n',
        c_array("mlp_input_multiplier", "int32_t", model.input_multiplier),
        c_array("mlp_input_offset", "int32_t", model.input_offset),
    ]
    for layer in range(6):
        chunks.extend((
            c_array(f"mlp_w{layer}", "int8_t", model.weights[layer]),
            c_array(f"mlp_m{layer}", "int32_t", model.multipliers[layer]),
            c_array(f"mlp_b{layer}", "int32_t", model.biases[layer]),
        ))
    (STAGE / "src/mlp_model_data.c").write_text(
        "\n".join(chunks), encoding="ascii")


def captured_zeroed() -> np.ndarray:
    data = CAPTURE_PATH.read_bytes()
    if len(data) % 58 != 0:
        raise ValueError("capture is not an integer number of 58-byte packets")
    raw = np.asarray([
        struct.unpack_from("<12H", data, offset + 10)
        for offset in range(0, len(data), 58)
    ], dtype=np.float64)
    runtime = json.loads(RUNTIME_PATH.read_text(encoding="ascii"))
    tare = np.asarray(runtime["slots"]["left"]["runtime_offset"])
    return round_away(raw - tare)


def write_golden_vectors(model: QuantizedModel, captured: np.ndarray) -> None:
    selected = [np.zeros(12, dtype=np.int64)]
    selected.extend(captured[index] for index in (0, 1, 50, 250, 500, 1000, 1500, 2011))
    selected.extend((
        np.arange(-110, 130, 20, dtype=np.int64),
        np.arange(165, -195, -30, dtype=np.int64),
    ))
    vectors = np.asarray(selected, dtype=np.int64)
    output, saturation = integer_infer(model, vectors)
    float_output = float_infer(model, vectors)
    records = []
    for index in range(len(vectors)):
        records.append({
            "zeroed": vectors[index].astype(int).tolist(),
            "integer_output_micro": output[index].astype(int).tolist(),
            "saturation_count": int(saturation[index]),
            "float_output_si": float_output[index].tolist(),
        })
    payload = {"schema": "coinft-mlp-golden-v1", "vectors": records}
    (STAGE / "tests/golden_vectors.json").write_text(
        json.dumps(payload, indent=2) + "\n", encoding="ascii")

    def matrix(name: str, c_type: str, matrix_values: np.ndarray) -> str:
        rows = ["    {" + ", ".join(str(int(value)) for value in row) + "},"
                for row in matrix_values]
        return (f"static const {c_type} {name}[{len(matrix_values)}]"
                f"[{matrix_values.shape[1]}] = {{\n" + "\n".join(rows) + "\n};\n")

    header = [
        "#ifndef COINFT_MLP_GOLDEN_VECTORS_H\n#define COINFT_MLP_GOLDEN_VECTORS_H\n\n#include <stdint.h>\n",
        f"#define GOLDEN_VECTOR_COUNT ({len(vectors)}u)\n",
        matrix("golden_input", "int32_t", vectors),
        matrix("golden_output", "int32_t", output),
        "static const uint16_t golden_saturation[GOLDEN_VECTOR_COUNT] = {\n    " +
        ", ".join(str(int(value)) for value in saturation) + ",\n};\n",
        "#endif\n",
    ]
    (STAGE / "tests/golden_vectors.h").write_text(
        "\n".join(header), encoding="ascii")


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_manifest(model: QuantizedModel, captured: np.ndarray) -> None:
    integer, saturation = integer_infer(model, captured)
    floating = float_infer(model, captured)
    error = integer.astype(np.float64) / 1_000_000.0 - floating
    axes = ("Fx", "Fy", "Fz", "Mx", "My", "Mz")
    metrics = {
        axis: {
            "rmse_si": float(np.sqrt(np.mean(error[:, index] ** 2))),
            "max_abs_si": float(np.max(np.abs(error[:, index]))),
        }
        for index, axis in enumerate(axes)
    }
    manifest = {
        "schema": "coinft-fixed-mlp-v1",
        "physical_coinft_id": "coinft_2606602_0019",
        "architecture": [12, 128, 64, 36, 24, 12, 6],
        "weight_format": "symmetric per-output-channel int8",
        "activation_format": "signed int16 with per-layer power-of-two scale",
        "accumulator_format": "signed int32",
        "activation_scales": list(ACTIVATION_SCALES),
        "input_requant_shift": INPUT_SHIFT,
        "hidden_requant_shift": REQUANT_SHIFT,
        "output_requant_shift": OUTPUT_SHIFT,
        "output_units": ["uN", "uN", "uN", "uNm", "uNm", "uNm"],
        "source_sha256": {
            ONNX_PATH.name: sha256(ONNX_PATH),
            EXTERNAL_PATH.name: sha256(EXTERNAL_PATH),
            NORM_PATH.name: sha256(NORM_PATH),
        },
        "historical_capture": {
            "samples": len(captured),
            "total_saturations": int(np.sum(saturation)),
            "float_error": metrics,
        },
    }
    (STAGE / "quantization_manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="ascii")


def main() -> None:
    model = load_quantized_model()
    captured = captured_zeroed()
    write_model_source(model)
    write_golden_vectors(model, captured)
    write_manifest(model, captured)
    print(f"generated int8 model for {sum(item.size for item in model.weights)} weights")
    print(f"wrote {STAGE / 'src/mlp_model_data.c'}")


if __name__ == "__main__":
    main()
