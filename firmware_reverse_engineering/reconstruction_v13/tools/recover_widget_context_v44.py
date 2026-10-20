#!/usr/bin/env python3
"""Audit CoinFT's hybrid widget context for the v44 dynamic-state baseline."""

from pathlib import Path
import struct


BASE = 0x184
STRIDE = 0x38
COUNT = 13
FLASH_WIDGET_CONFIG = 0x46A0
FLASH_COMMON_CONFIG = 0x4550
SRAM_SCHEDULE = 0x130
SRAM_FRAME_BASE = 0x99C
SRAM_INTERNAL_CONTEXT_PTR = 0x0C8
SRAM_BASE_ADDR = 0x20000000
SHARED_SNS_MODE_BASE = 0x49
COMMON_SNS_MODE_OFFSET = 0x4B
COMMON_DIVIDER_TYPE_OFFSET = 0x35
COMMON_DIVIDER_INDEX_OFFSET = 0x36
COMMON_DIVIDER_VALUE_OFFSET = 0x3F


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


def coinft_frame_fields(data: bytes, index: int) -> dict[str, int]:
    """Fields consumed by the recovered CoinFT frame builder."""
    off = BASE + index * STRIDE
    return {
        "raw_limit": u16(data, off + 0x04),
        "sense_divider": u16(data, off + 0x0E),
        "alternate_sense_divider": u16(data, off + 0x10),
        "frame_lfsr_mode": data[off + 0x21],
        "frame_sub_conversions": u16(data, off + 0x2C),
        "frame_cdac_sel_re": data[off + 0x2E],
        "frame_comp_divider": u16(data, off + 0x34),
        "frame_lfsr_bits_encoded": data[off + 0x36],
        "frame_lfsr_bits": data[off + 0x36] & 0x03,
    }


def shared_mode_fields(flash: bytes, sram: bytes, index: int) -> dict[str, int]:
    """Recover the two SNS_CTL mode fragments outside widget context."""
    record = FLASH_WIDGET_CONFIG + index * 0x8C
    context = BASE + index * STRIDE
    method = flash[record + 0x84]
    selector = sram[context + 0x31]
    mode_index = method + 1 if selector == 1 else (method - 1 if method else 0)
    shared_ptr = u32(sram, SRAM_INTERNAL_CONTEXT_PTR)
    shared_off = shared_ptr - SRAM_BASE_ADDR
    if shared_ptr < SRAM_BASE_ADDR or shared_off + SHARED_SNS_MODE_BASE + mode_index >= len(sram):
        shared_mode = 0
    else:
        shared_mode = sram[shared_off + SHARED_SNS_MODE_BASE + mode_index]
    common_mode = flash[FLASH_COMMON_CONFIG + COMMON_SNS_MODE_OFFSET]
    return {
        "method": method,
        "context_method_selector": selector,
        "shared_mode_index": mode_index,
        "shared_mode": shared_mode,
        "common_mode": common_mode,
        "sns_ctl_mode_bits": (shared_mode << 6) | (common_mode << 4),
    }


def common_divider_fields(flash: bytes) -> dict[str, int]:
    """Decode FUN_00001AF4's common-config divider inputs."""
    base = FLASH_COMMON_CONFIG
    raw_type = flash[base + COMMON_DIVIDER_TYPE_OFFSET]
    effective_type = 3 if raw_type > 2 else raw_type
    requested = flash[base + COMMON_DIVIDER_VALUE_OFFSET]
    encoded = (requested if requested else 1) - 1
    return {
        "raw_type": raw_type,
        "effective_type": effective_type,
        "divider_index": flash[base + COMMON_DIVIDER_INDEX_OFFSET],
        "requested_divider": requested,
        "encoded_divider": encoded,
        "disable_cmd": 0x40000000 | (effective_type << 6) |
                        flash[base + COMMON_DIVIDER_INDEX_OFFSET],
        "enable_cmd": 0x8000ff00 | (effective_type << 6) |
                       flash[base + COMMON_DIVIDER_INDEX_OFFSET],
    }


def snapshot_paths(root: Path) -> list[Path]:
    names = (
        "reset_acquire_sram.bin",
        "after_run_halt_sram.bin",
        "after_resume_400ms_halt_sram.bin",
        "live_run_sram.bin",
    )
    return [root / name for name in names]


def frame_tail_from_context(flash: bytes, sram: bytes, schedule_index: int) -> tuple[int, int, int]:
    """Rebuild SNS_SCAN_CTL/CDAC_CTL/SNS_CTL from CoinFT offsets only."""
    schedule_off = SRAM_SCHEDULE + schedule_index * 4
    widget = u16(sram, schedule_off)
    sensor = u16(sram, schedule_off + 2)
    if widget >= COUNT:
        raise ValueError(f"invalid widget {widget} at schedule {schedule_index}")
    record = FLASH_WIDGET_CONFIG + widget * 0x8C
    context = BASE + widget * STRIDE
    method = flash[record + 0x84]
    sense = u16(sram, context + 0x0E)
    if method == 1 and flash[record + 0x3A] <= sensor:
        sense = u16(sram, context + 0x10)
    frame_lfsr_mode = sram[context + 0x21]
    if (frame_lfsr_mode & 3) == 2:
        sense >>= 2 if method == 1 else 1
    comp_divider = u16(sram, context + 0x34)
    sub_conversions = u16(sram, context + 0x2C)
    num_conversions = flash[record + 0x86]
    sensor_context = u32(flash, record + 4)
    sensor_offset = sensor_context - 0x20000000 + sensor * 10
    sensor_byte9 = sram[sensor_offset + 9]
    scan_ctl = (((num_conversions - 1) << 28)
                 | (((comp_divider - 1) << 16) & 0x0FFF0000)
                 | ((sub_conversions - 1) & 0xFFFF)) & 0x7FFFFFFF
    cdac_sel_re_offset = 0x2F if method == 1 and flash[record + 0x3A] <= sensor else 0x2E
    cdac_ctl = 0x00400000 | (sensor_byte9 << 8) | sram[context + cdac_sel_re_offset]
    sns_ctl = (((sense - 1) << 16)
               | ((sram[context + 0x36] & 3) << 30)
               | ((frame_lfsr_mode & 3) << 28)
               | (flash[0x4550 + 0x4B] << 4)
               | 3) & 0xFFFFFFFF
    return scan_ctl, cdac_ctl, sns_ctl


def main() -> None:
    snapshot_root = Path(__file__).parents[2] / "07_analysis" / "runtime_snapshots_20260828"
    paths = snapshot_paths(snapshot_root)
    loaded = []
    for path in paths:
        data = path.read_bytes()
        if len(data) < BASE + COUNT * STRIDE:
            raise SystemExit(f"snapshot too short: {len(data)}")
        loaded.append((path, data))
    flash_path = Path(__file__).parents[2] / "07_analysis" / "CoinFT_V2_firmware.bin"
    flash = flash_path.read_bytes()
    path, data = loaded[-1]
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
    signatures = []
    for _, snapshot in loaded:
        signature = tuple(
            tuple(sorted(coinft_frame_fields(snapshot, index).items()))
            for index in range(COUNT)
        )
        signatures.append(signature)
    stable = all(signature == signatures[0] for signature in signatures[1:])
    print(f"cross_snapshot_key_fields={'4/4 stable' if stable else 'mismatch'}")
    print("coinft_frame_fields_widget00=" + str(coinft_frame_fields(data, 0)))
    print("coinft_frame_fields_widget12=" + str(coinft_frame_fields(data, 12)))
    shared_ptr = u32(data, SRAM_INTERNAL_CONTEXT_PTR)
    shared_off = shared_ptr - SRAM_BASE_ADDR
    if SRAM_BASE_ADDR <= shared_ptr < SRAM_BASE_ADDR + len(data):
        shared_table = list(data[shared_off + SHARED_SNS_MODE_BASE:shared_off + SHARED_SNS_MODE_BASE + 5])
    else:
        shared_table = []
    print(f"shared_context_ptr=0x{shared_ptr:08x}")
    print(f"shared_sns_mode_table={shared_table}")
    print(f"common_sns_mode=0x{flash[FLASH_COMMON_CONFIG + COMMON_SNS_MODE_OFFSET]:02x}")
    print("common_divider=" + str(common_divider_fields(flash)))
    if SRAM_BASE_ADDR <= shared_ptr < SRAM_BASE_ADDR + len(data):
        special0 = u32(data, shared_off + 0x20) | 5
        special1 = u32(data, shared_off + 0x24) | 5
        print(f"special_pair_sns_ctl_sources=0x{special0:08x},0x{special1:08x}")
    for index in range(COUNT):
        print(f"widget={index:02d} sns_mode_sources={shared_mode_fields(flash, data, index)}")
    shared_signatures = [
        tuple(tuple(sorted(shared_mode_fields(flash, snapshot, index).items()))
              for index in range(COUNT))
        for _, snapshot in loaded
    ]
    shared_stable = all(signature == shared_signatures[0]
                        for signature in shared_signatures[1:])
    print(f"shared_mode_sources={'4/4 stable' if shared_stable else 'mismatch'}")
    tail_matches = 0
    for index in range(14):
        effective = index
        widget = u16(data, SRAM_SCHEDULE + index * 4)
        if widget > 0xFFFC:
            effective = index + 7 if index < 7 else index - 7
        actual = frame_tail_from_context(flash, data, effective)
        expected = tuple(u32(data, SRAM_FRAME_BASE + index * 0x18 + 12 + word * 4)
                         for word in range(3))
        if actual == expected:
            tail_matches += 1
        else:
            print(f"frame_tail_mismatch index={index} expected={expected} actual={actual}")
    print(f"frame_tail_context_offsets={tail_matches}/14 match")
    normal_decim = [((u32(data, SRAM_FRAME_BASE + index * 0x18 + 20) >> 8) & 0xff)
                    for index in range(14)]
    print(f"normal_frame_decim_rates={normal_decim}")
    print(f"normal_frame_decim_zero={sum(rate == 0 for rate in normal_decim)}/14")


if __name__ == "__main__":
    main()
