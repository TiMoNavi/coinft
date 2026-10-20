#!/usr/bin/env python3
"""Recompute the FUN_00002574 duration terms from captured runtime values."""

from __future__ import annotations


def estimate(shared: dict[str, int], widget: dict[str, int]) -> tuple[int, int, int]:
    cycles_per_us = shared["cpu_clock_hz"] // shared["clock_divider"] // 1_000_000
    widget_cycles = widget["sensor_clock_divider"] * (
        shared["num_subconversions"] + widget["cdac_comp_divider"]
    )
    if (widget["mode_bits"] & 3) == 2:
        widget_cycles *= 2
    total = (
        shared["sense_cycles_a"]
        + shared["sense_cycles_b"]
        + widget["route_factor"]
        * (
            shared["precharge_cycles"]
            + shared["cdac_comp_cycles_a"]
            + shared["cdac_comp_cycles_b"]
            + widget_cycles
        )
    )
    duration5 = (total // cycles_per_us) * 5 if cycles_per_us else 0
    timeout = ((shared["cpu_clock_hz"] // 1_000_000) * duration5) // 5
    return widget_cycles, duration5, timeout


def main() -> None:
    shared = {
        "cpu_clock_hz": 48_000_000,
        "clock_divider": 1,
        "num_subconversions": 3,
        "sense_cycles_a": 200,
        "sense_cycles_b": 200,
        "precharge_cycles": 0,
        "cdac_comp_cycles_a": 1023,
        "cdac_comp_cycles_b": 1023,
    }
    for index, cdac, mode in [(0, 0x400, 2), (8, 0x600, 2), (12, 0x80, 2)]:
        widget_cycles, duration5, timeout = estimate(
            shared,
            {
                "sensor_clock_divider": 16,
                "cdac_comp_divider": cdac,
                "mode_bits": mode,
                "route_factor": 1,
            },
        )
        print(
            f"widget={index:02d} widget_cycles={widget_cycles} "
            f"duration5={duration5} timeout_ticks={timeout}"
        )


if __name__ == "__main__":
    main()
