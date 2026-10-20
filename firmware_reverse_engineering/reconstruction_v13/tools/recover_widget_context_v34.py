#!/usr/bin/env python3
"""Decode a modern fifth-generation candidate overlay for CoinFT's 0x38 bytes."""

from pathlib import Path
import struct


BASE = 0x184
STRIDE = 0x38
COUNT = 13


def u16(data: bytes, off: int) -> int:
    return struct.unpack_from("<H", data, off)[0]


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def decode(data: bytes, index: int) -> dict[str, int]:
    off = BASE + index * STRIDE
    return {
        "fingerCap": u16(data, off + 0x00),
        "sigPFC": u16(data, off + 0x02),
        "maxRawCount": u16(data, off + 0x04),
        "maxRawCountRow": u16(data, off + 0x06),
        "fingerTh": u16(data, off + 0x08),
        "proxTh": u16(data, off + 0x0A),
        "lowBslnRst": u16(data, off + 0x0C),
        "snsClk": u16(data, off + 0x0E),
        "rowSnsClk": u16(data, off + 0x10),
        "gestureDetected": u32(data, off + 0x14),
        "gestureDirection": u32(data, off + 0x18),
        "xDelta": u16(data, off + 0x1C),
        "yDelta": u16(data, off + 0x1E),
        "noiseTh": u16(data, off + 0x20),
        "nNoiseTh": u16(data, off + 0x22),
        "hysteresis": u16(data, off + 0x24),
        "onDebounce": data[off + 0x26],
        "snsClkSource": data[off + 0x27],
        "numSubConversions": u16(data, off + 0x28),
        "cdacRef": data[off + 0x2A],
        "rowCdacRef": data[off + 0x2B],
        "cicRate": data[off + 0x2C],
        "cdacDitherEn": data[off + 0x2D],
        "cdacDitherValue": data[off + 0x2E],
        "coarseInitBypassEn": data[off + 0x2F],
        "cdacCompDivider": u16(data, off + 0x30),
        "lfsrBits": data[off + 0x32],
        "reserved0": u32(data, off + 0x34),
    }


def main() -> None:
    path = Path(__file__).parents[2] / "07_analysis" / "runtime_snapshots_20260828" / "live_run_sram.bin"
    data = path.read_bytes()
    if len(data) < BASE + COUNT * STRIDE:
        raise SystemExit(f"snapshot too short: {len(data)}")
    print(f"snapshot={path}")
    print(f"base=0x{BASE:04x} stride=0x{STRIDE:02x} count={COUNT}")
    for index in range(COUNT):
        values = decode(data, index)
        print(
            f"widget={index:02d} maxRaw=0x{values['maxRawCount']:04x} "
            f"maxRow={values['maxRawCountRow']} fingerTh={values['fingerTh']} "
            f"proxTh={values['proxTh']} lowRst={values['lowBslnRst']} "
            f"snsClk={values['snsClk']} rowSnsClk={values['rowSnsClk']} "
            f"subConv={values['numSubConversions']} cdacRef={values['cdacRef']} "
            f"rowCdacRef={values['rowCdacRef']} cic={values['cicRate']} "
            f"dither={values['cdacDitherEn']} compDiv={values['cdacCompDivider']} "
            f"lfsrBits={values['lfsrBits']} reserved=0x{values['reserved0']:08x}"
        )


if __name__ == "__main__":
    main()
