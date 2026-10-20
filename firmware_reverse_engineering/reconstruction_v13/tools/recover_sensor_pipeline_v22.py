#!/usr/bin/env python3
"""Verify the recovered D40/D28 history and baseline initialization shape."""

from pathlib import Path
import struct


ROOT = Path(__file__).resolve().parents[2]
FLASH = (ROOT / "07_analysis" / "CoinFT_V2_firmware.bin").read_bytes()
SRAM = (ROOT / "07_analysis" / "runtime_snapshots_20260828" / "live_run_sram.bin").read_bytes()


def u16(blob, off):
    return struct.unpack_from("<H", blob, off)[0]


def u32(blob, off):
    return struct.unpack_from("<I", blob, off)[0]


def main():
    mismatches = []
    expected_history = bytearray(13 * 10 + 2)
    # FUN_00000D9A calls FUN_00000D40 in descending widget order.  With
    # flags=0x1415, D40 writes at offsets 0,2,6,8 and 10; offset 10 is the
    # first word of the next contiguous history record and is intentional.
    for widget in range(12, -1, -1):
        record = 0x46A0 + widget * 0x8C
        sensor_ptr = u32(FLASH, record + 4) - 0x20000000
        raw = u16(SRAM, sensor_ptr)
        history_ptr = u32(FLASH, record + 0x1C) - 0x20000000
        history_offset = history_ptr - 0x918
        flags = u16(FLASH, record + 0x78)
        if flags & 0x10:
            struct.pack_into("<H", expected_history, history_offset + 0, raw)
            struct.pack_into("<H", expected_history, history_offset + 2, raw)
            history_offset += 4
        if flags & 0x400:
            struct.pack_into("<H", expected_history, history_offset + 2, raw)
            struct.pack_into("<H", expected_history, history_offset + 4, raw)
            struct.pack_into("<H", expected_history, history_offset + 6, raw)

    for widget in range(13):
        record = 0x46A0 + widget * 0x8C
        sensor_ptr = u32(FLASH, record + 4) - 0x20000000
        raw = u16(SRAM, sensor_ptr)
        flags = u16(FLASH, record + 0x78)
        history_ptr = u32(FLASH, record + 0x1C) - 0x20000000
        history_offset = history_ptr - 0x918
        expected = bytes(expected_history[history_offset:history_offset + 10])
        observed = SRAM[history_ptr:history_ptr + 10]
        if expected != observed:
            mismatches.append((widget, expected, observed))
        baseline = u16(SRAM, sensor_ptr + 2)
        status = SRAM[sensor_ptr + 6]
        neg = SRAM[sensor_ptr + 7]
        print(f"widget {widget:02d}: raw=0x{raw:04x} baseline=0x{baseline:04x} "
              f"status=0x{status:02x} neg=0x{neg:02x} flags=0x{flags:04x} "
              f"history={'match' if expected == observed else 'mismatch'}")
    print(f"## History comparison: {13 - len(mismatches)}/13 records match")
    for widget, expected, observed in mismatches:
        print(f"- widget {widget}: expected {expected.hex()} observed {observed.hex()}")
    if mismatches:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
