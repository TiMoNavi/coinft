"""Per-sensor ONNX inference for the standard CoinFT receiver."""

from __future__ import annotations

from collections import deque
from pathlib import Path
import json
from typing import Any

import numpy as np


class CoinFTModel:
    """Apply one physical sensor's normalization, ONNX model, and smoothing."""

    def __init__(
        self,
        model_path: str | Path,
        norm_path: str | Path,
        moving_average_window: int = 5,
        axis_map: tuple[int, ...] | list[int] = (0, 1, 2, 3, 4, 5),
        axis_sign: tuple[float, ...] | list[float] = (1, 1, 1, 1, 1, 1),
    ) -> None:
        self.model_path = Path(model_path).expanduser().resolve()
        self.norm_path = Path(norm_path).expanduser().resolve()
        if not self.model_path.is_file():
            raise FileNotFoundError(f"CoinFT model does not exist: {self.model_path}")
        if not self.norm_path.is_file():
            raise FileNotFoundError(f"CoinFT norm does not exist: {self.norm_path}")
        try:
            import onnxruntime as ort
        except ImportError as exc:  # pragma: no cover - environment dependent
            raise RuntimeError("onnxruntime is required for CoinFT model inference") from exc

        with self.norm_path.open("r", encoding="utf-8") as handle:
            raw = json.load(handle)
        self.norm = {
            key: np.asarray(raw[key], dtype=np.float32)
            for key in ("mu_x", "sd_x", "mu_y", "sd_y")
        }
        if self.norm["mu_x"].shape != (12,) or self.norm["sd_x"].shape != (12,):
            raise ValueError(f"CoinFT input norm must have 12 values: {self.norm_path}")
        if self.norm["mu_y"].shape != (6,) or self.norm["sd_y"].shape != (6,):
            raise ValueError(f"CoinFT output norm must have 6 values: {self.norm_path}")
        if np.any(self.norm["sd_x"] == 0) or np.any(self.norm["sd_y"] == 0):
            raise ValueError(f"CoinFT norm contains zero scale: {self.norm_path}")

        self.session = ort.InferenceSession(str(self.model_path))
        self.input_name = self.session.get_inputs()[0].name
        self.axis_map = np.asarray(axis_map, dtype=np.int64)
        self.axis_sign = np.asarray(axis_sign, dtype=np.float64)
        if self.axis_map.shape != (6,) or sorted(self.axis_map.tolist()) != list(range(6)):
            raise ValueError("CoinFT axis_map must be a permutation of 0..5")
        if self.axis_sign.shape != (6,):
            raise ValueError("CoinFT axis_sign must contain 6 values")
        self.offset: np.ndarray | None = None
        self.ft_bias: np.ndarray | None = None
        self.filter: deque[np.ndarray] = deque(maxlen=max(1, int(moving_average_window)))

    def set_offset(self, offset: np.ndarray) -> None:
        self.offset = np.asarray(offset, dtype=np.float64).copy()
        self.filter.clear()

    def set_ft_bias(self, bias: np.ndarray) -> None:
        self.ft_bias = np.asarray(bias, dtype=np.float64).copy()
        self.filter.clear()

    def reset_filter(self) -> None:
        self.filter.clear()

    def zero_raw(self, raw: Any) -> np.ndarray:
        values = np.asarray(raw, dtype=np.float64)
        return values if self.offset is None else values - self.offset

    def predict_uncorrected(self, raw: Any) -> np.ndarray:
        zeroed = self.zero_raw(raw).astype(np.float32)
        normalized = (zeroed - self.norm["mu_x"]) / self.norm["sd_x"]
        output = self.session.run(None, {self.input_name: normalized.reshape(1, 12)})[0].flatten()
        wrench = output * self.norm["sd_y"] + self.norm["mu_y"]
        return (wrench[self.axis_map] * self.axis_sign).astype(np.float64)

    def predict(self, raw: Any) -> np.ndarray:
        value = self.predict_uncorrected(raw)
        if self.ft_bias is not None:
            value = value - self.ft_bias
        self.filter.append(value)
        return np.mean(np.asarray(self.filter), axis=0)
