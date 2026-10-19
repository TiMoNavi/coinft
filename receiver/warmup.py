"""Runtime warmup, tare, model-bias, and zero-check state machine."""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass
from typing import Any, Mapping

import numpy as np

from .models import CoinFTModel


SIDES = ("left", "right")
AXES = ("fx", "fy", "fz", "mx", "my", "mz")


@dataclass(frozen=True)
class WarmupConfig:
    raw_stability_window_s: float = 2.0
    raw_stability_min_wait_s: float = 5.0
    raw_stability_check_interval_s: float = 0.5
    raw_stability_mean_delta_counts: float = 8.0
    raw_stability_noise_std_counts: float = 20.0
    raw_stability_required_passes: int = 3
    runtime_settling_s: float = 5.0
    final_tare_window_s: float = 2.0
    bias_window_s: float = 1.0
    zero_check_window_s: float = 1.0
    zero_force_limit_n: float = 0.15
    moving_average_window: int = 5


class CoinFTWarmup:
    """Calibrate one or two sensor models from a live raw stream."""

    def __init__(self, models: Mapping[str, CoinFTModel], config: WarmupConfig | None = None) -> None:
        if set(models) - set(SIDES) or not models:
            raise ValueError("models must contain left and/or right")
        self.models = dict(models)
        self.sides = tuple(side for side in SIDES if side in self.models)
        self.config = config or WarmupConfig()
        self.status = "warming_up"
        self.error: str | None = None
        self.raw_history: deque[tuple[float, dict[str, np.ndarray]]] = deque()
        self.bias_history: deque[tuple[float, dict[str, np.ndarray]]] = deque()
        self.zero_history: deque[tuple[float, dict[str, np.ndarray]]] = deque()
        self.started_s: float | None = None
        self.settling_started_s: float | None = None
        self.bias_started_s: float | None = None
        self.zero_started_s: float | None = None
        self.last_stability_check_s: float | None = None
        self.stability_passes = 0
        self.stability_mean_delta_counts: float | None = None
        self.stability_noise_std_counts: float | None = None
        self.zero_force_max_abs_n: float | None = None
        self.provisional_offsets: dict[str, np.ndarray] = {}
        self.final_offsets: dict[str, np.ndarray] = {}
        self.ft_biases: dict[str, np.ndarray] = {}

    @property
    def ready(self) -> bool:
        return self.status == "ready"

    def process(self, raw_by_side: Mapping[str, Any], timestamp_s: float) -> dict[str, Any]:
        if set(raw_by_side) != set(self.sides):
            raise ValueError(f"expected raw sides {self.sides}, got {tuple(raw_by_side)}")
        raws = {side: np.asarray(raw_by_side[side], dtype=np.float64) for side in self.sides}
        if any(value.shape != (12,) for value in raws.values()):
            raise ValueError("each CoinFT raw sample must contain 12 channels")
        timestamp_s = float(timestamp_s)
        if self.started_s is None:
            self.started_s = timestamp_s
        self._append_raw(timestamp_s, raws)

        try:
            if self.status in {"warming_up", "delayed"}:
                self._advance_stability(timestamp_s)
                if self.status in {"warming_up", "delayed"}:
                    return self._status_result()
            if self.status == "provisional_tare":
                self.status = "runtime_settling"
                self.settling_started_s = timestamp_s
                return self._status_result()
            if self.status == "runtime_settling":
                self._advance_final_tare(timestamp_s)
                return self._status_result()
            if self.status == "final_tare":
                self.status = "bias_calibration"
                self.bias_started_s = timestamp_s
                self.bias_history.clear()
            if self.status == "bias_calibration":
                self._advance_bias(timestamp_s, raws)
                return self._status_result()

            if self.status == "zero_check_failed":
                return self._status_result()

            output = self._wrench_result(raws)
            if self.status == "zero_check":
                self._advance_zero_check(timestamp_s, output)
                output.update(self._status_result())
                return output
            output.update(self._status_result())
            return output
        except Exception as exc:  # noqa: BLE001
            self.status = "error"
            self.error = str(exc)
            return self._status_result()

    def _append_raw(self, timestamp_s: float, raws: dict[str, np.ndarray]) -> None:
        self.raw_history.append((timestamp_s, {side: value.copy() for side, value in raws.items()}))
        keep_s = max(self.config.raw_stability_window_s * 2, self.config.final_tare_window_s) + 1.0
        while self.raw_history and self.raw_history[0][0] < timestamp_s - keep_s:
            self.raw_history.popleft()

    def _window(self, start_s: float, end_s: float) -> dict[str, np.ndarray] | None:
        rows = [(stamp, values) for stamp, values in self.raw_history if start_s <= stamp <= end_s]
        required_span = max(0.0, end_s - start_s) * 0.7
        if len(rows) < 2 or rows[-1][0] - rows[0][0] < required_span:
            return None
        return {side: np.stack([values[side] for _, values in rows]) for side in self.sides}

    def _advance_stability(self, timestamp_s: float) -> None:
        assert self.started_s is not None
        if timestamp_s - self.started_s < self.config.raw_stability_min_wait_s:
            self.status = "warming_up"
            return
        if self.last_stability_check_s is not None and timestamp_s - self.last_stability_check_s < self.config.raw_stability_check_interval_s:
            return
        self.last_stability_check_s = timestamp_s
        window_s = self.config.raw_stability_window_s
        previous = self._window(timestamp_s - 2 * window_s, timestamp_s - window_s)
        current = self._window(timestamp_s - window_s, timestamp_s)
        if previous is None or current is None:
            self.status = "delayed"
            self.stability_passes = 0
            return
        previous_all = np.concatenate([previous[side] for side in self.sides], axis=1)
        current_all = np.concatenate([current[side] for side in self.sides], axis=1)
        self.stability_mean_delta_counts = float(np.max(np.abs(np.mean(current_all, axis=0) - np.mean(previous_all, axis=0))))
        self.stability_noise_std_counts = float(np.median(np.std(current_all, axis=0)))
        stable = self.stability_mean_delta_counts <= self.config.raw_stability_mean_delta_counts and self.stability_noise_std_counts <= self.config.raw_stability_noise_std_counts
        self.stability_passes = self.stability_passes + 1 if stable else 0
        if self.stability_passes < max(1, self.config.raw_stability_required_passes):
            self.status = "delayed"
            return
        for side in self.sides:
            offset = np.mean(current[side], axis=0)
            self.provisional_offsets[side] = offset
            self.models[side].set_offset(offset)
        self.status = "provisional_tare"

    def _advance_final_tare(self, timestamp_s: float) -> None:
        assert self.settling_started_s is not None
        if timestamp_s - self.settling_started_s < self.config.runtime_settling_s:
            return
        window = self._window(timestamp_s - self.config.final_tare_window_s, timestamp_s)
        if window is None:
            return
        for side in self.sides:
            final_offset = np.mean(window[side], axis=0)
            self.final_offsets[side] = final_offset
            self.models[side].set_offset(final_offset)
            self.models[side].reset_filter()
        self.raw_history.clear()
        self.status = "final_tare"

    def _advance_bias(self, timestamp_s: float, raws: dict[str, np.ndarray]) -> None:
        self.bias_history.append((timestamp_s, {side: self.models[side].predict_uncorrected(raws[side]) for side in self.sides}))
        assert self.bias_started_s is not None
        if timestamp_s - self.bias_started_s < self.config.bias_window_s:
            return
        for side in self.sides:
            bias = np.mean(np.stack([row[side] for _, row in self.bias_history]), axis=0)
            self.ft_biases[side] = bias
            self.models[side].set_ft_bias(bias)
            self.models[side].reset_filter()
        self.bias_history.clear()
        self.zero_history.clear()
        self.zero_started_s = timestamp_s
        self.status = "zero_check"

    def _advance_zero_check(self, timestamp_s: float, output: dict[str, Any]) -> None:
        values = {side: np.asarray(output[f"{side}_wrench"], dtype=np.float64) for side in self.sides}
        self.zero_history.append((timestamp_s, values))
        if self.zero_started_s is None:
            self.zero_started_s = timestamp_s
        if timestamp_s - self.zero_started_s < self.config.zero_check_window_s:
            return
        force_means = np.stack([np.mean(np.stack([row[side][:3] for _, row in self.zero_history]), axis=0) for side in self.sides])
        self.zero_force_max_abs_n = float(np.max(np.abs(force_means)))
        if self.zero_force_max_abs_n <= self.config.zero_force_limit_n:
            self.status = "ready"
            self.error = None
        else:
            self.status = "zero_check_failed"
            self.error = f"unloaded zero check failed: {self.zero_force_max_abs_n:.3f} N exceeds {self.config.zero_force_limit_n:.3f} N"

    def _wrench_result(self, raws: dict[str, np.ndarray]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for side in self.sides:
            result[f"{side}_raw_zeroed"] = self.models[side].zero_raw(raws[side]).tolist()
            result[f"{side}_wrench"] = self.models[side].predict(raws[side]).tolist()
        return result

    def _status_result(self) -> dict[str, Any]:
        return {
            "calibration_status": self.status,
            "calibration_ready": self.ready,
            "calibration_error": self.error,
            "stability_passes": self.stability_passes,
            "stability_required_passes": self.config.raw_stability_required_passes,
            "stability_mean_delta_counts": self.stability_mean_delta_counts,
            "stability_noise_std_counts": self.stability_noise_std_counts,
            "zero_force_max_abs_n": self.zero_force_max_abs_n,
            "zero_force_limit_n": self.config.zero_force_limit_n,
        }
