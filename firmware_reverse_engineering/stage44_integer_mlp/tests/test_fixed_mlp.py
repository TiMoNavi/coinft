#!/usr/bin/env python3
"""Verify Python quantization, float error, and the native C inference path."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys

import numpy as np


STAGE = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(STAGE / "tools"))
import generate_mlp_model as generator  # noqa: E402


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)
    print(f"PASS {message}")


def main() -> None:
    model = generator.load_quantized_model()
    captured = generator.captured_zeroed()
    integer, saturation = generator.integer_infer(model, captured)
    floating = generator.float_infer(model, captured)
    error = integer.astype(np.float64) / 1_000_000.0 - floating

    require(int(np.sum(saturation)) == 0,
            "2012 historical samples have no integer saturation")
    rmse = np.sqrt(np.mean(error * error, axis=0))
    maximum = np.max(np.abs(error), axis=0)
    require(rmse[0] < 0.01 and rmse[1] < 0.01 and rmse[2] < 0.02,
            "force RMSE stays below 0.01/0.01/0.02 N")
    require(maximum[0] < 0.05 and maximum[1] < 0.06 and maximum[2] < 0.15,
            "force max error stays below 0.05/0.06/0.15 N")
    require(np.all(rmse[3:] < 0.001) and np.all(maximum[3:] < 0.005),
            "torque error stays below 0.001 RMSE and 0.005 Nm max")

    golden = json.loads((STAGE / "tests/golden_vectors.json").read_text())
    vectors = np.asarray([item["zeroed"] for item in golden["vectors"]])
    expected = np.asarray([item["integer_output_micro"]
                           for item in golden["vectors"]])
    actual, _ = generator.integer_infer(model, vectors)
    require(np.array_equal(actual, expected), "generated Python golden vectors match")

    output = STAGE / "build/host/test_mlp_fixed"
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-DMLP_HOST_TEST",
        f"-I{STAGE / 'include'}", f"-I{STAGE / 'tests'}",
        str(STAGE / "src/mlp_fixed.c"),
        str(STAGE / "src/mlp_model_data.c"),
        str(STAGE / "tests/test_mlp_fixed.c"),
        "-o", str(output),
    ], check=True)
    subprocess.run([str(output)], check=True)
    print("float RMSE SI: " + " ".join(f"{value:.9f}" for value in rmse))
    print("float max  SI: " + " ".join(f"{value:.9f}" for value in maximum))


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, subprocess.CalledProcessError) as error:
        print(f"FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
