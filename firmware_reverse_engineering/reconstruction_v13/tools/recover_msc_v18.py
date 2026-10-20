#!/usr/bin/env python3
"""Host-only shadow model for CoinFT FUN_000012e8/01404.

The model consumes the existing read-only Flash/SRAM snapshots.  It mirrors
the recovered scheduler, sensor-frame construction, and three-word CSW mask
updates without touching SWD, MMIO, Flash, or the target board.
"""

from pathlib import Path
import struct


ROOT = Path(__file__).resolve().parents[2]
FLASH_PATH = ROOT / "07_analysis" / "CoinFT_V2_firmware.bin"
SRAM_PATH = ROOT / "07_analysis" / "runtime_snapshots_20260828" / "live_run_sram.bin"

COMMON = 0x4550
WIDGET_CONFIG = 0x46A0
SENSOR_TABLES = 0x45A0
PIN_TABLE = 0x4608
SHIELD_TABLE = 0x4698
WIDGET_CONTEXT = 0x184
SCHEDULE = 0x130
FRAME_BASE = 0x99C

SENTINEL = 0xFFFF
ROW_MIRROR_THRESHOLD = 0xFFFC


def u8(blob, off):
    return blob[off]


def u16(blob, off):
    return struct.unpack_from("<H", blob, off)[0]


def u32(blob, off):
    return struct.unpack_from("<I", blob, off)[0]


def s8(blob, off):
    value = u8(blob, off)
    return value - 0x100 if value & 0x80 else value


def addr_to_sram_offset(addr):
    if not 0x20000000 <= addr < 0x20000000 + 0x8000:
        raise ValueError(f"address is outside snapshot SRAM: 0x{addr:08x}")
    return addr - 0x20000000


def schedule_entry(sram, index):
    off = SCHEDULE + index * 4
    return u16(sram, off), u16(sram, off + 2)


def widget_record(flash, widget):
    return WIDGET_CONFIG + widget * 0x8C


def widget_context(sram, widget):
    return WIDGET_CONTEXT + widget * 0x38


def sensor_record_ptr(flash, widget, sensor):
    record = widget_record(flash, widget)
    table = u32(flash, record + 8)
    # All pointers in this recovered table target the Flash image itself.
    return table + sensor * 8


def pin_mask_for_sensor(flash, widget, sensor, channel):
    """Translate the sensor's pin-list record exactly as FUN_00001404."""
    record = sensor_record_ptr(flash, widget, sensor)
    if u8(flash, record + 6) != channel:
        return 0, [], record, u32(flash, record)
    count = u8(flash, record + 5)
    pin_list = u32(flash, record)
    mask = 0
    bits = []
    for item in range(count):
        bit = s8(flash, pin_list + item * 8 + 5)
        if 0 <= bit < 32:
            mask |= 1 << bit
            bits.append(bit)
    return mask, bits, record, pin_list


def shield_mask(flash, channel):
    count = u8(flash, COMMON + 0x3C)
    mask = 0
    bits = []
    for item in range(count):
        record = SHIELD_TABLE + item * 8
        if u8(flash, record + 6) == channel:
            bit = s8(flash, record + 5)
            if 0 <= bit < 32:
                mask |= 1 << bit
                bits.append(bit)
    return mask, bits


def update_csw(mask_words, mask, selector):
    """Exact FUN_00001204 semantics; selector values are 1/5/6/7."""
    old = tuple(mask_words)
    mask_words[0] = old[0] & ~mask
    mask_words[1] = old[1] & ~mask
    mask_words[2] = old[2] & ~mask
    if selector & 1:
        mask_words[2] = old[2] | mask
    if selector & 2:
        mask_words[1] = old[1] | mask
    if selector & 4:
        mask_words[0] = old[0] | mask


def adjusted_sensor_value(method, lfsr_mode, value):
    if (lfsr_mode & 3) == 2:
        return (value >> ((method == 1) + 1)) & 0xFFFF
    return value


def build_sensor_frame(flash, sram, schedule_index, trace=None):
    widget, sensor = schedule_entry(sram, schedule_index)
    if widget >= 13:
        raise ValueError(f"invalid widget {widget} at schedule {schedule_index}")
    wr = widget_record(flash, widget)
    wc = widget_context(sram, widget)
    method = u8(flash, wr + 0x84)

    value = u16(sram, wc + 0x0E)
    if method == 1 and u8(flash, wr + 0x3A) <= sensor:
        value = u16(sram, wc + 0x10)
    lfsr_mode = u8(sram, wc + 0x21)
    sense_div = adjusted_sensor_value(method, lfsr_mode, value)
    frame = [0, 0, 0, 0, 0, 0]
    frame[5] = ((sense_div - 1) << 16) | (u8(sram, wc + 0x36) << 30) | ((lfsr_mode & 3) << 28)

    # FUN_00001238: scan divider and CDAC selection.
    comp_div = u16(sram, wc + 0x34)
    frame[3] = ((comp_div + ((comp_div == 0) - 1)) << 16) & 0x0FFF0000
    # The runtime common context takes the normal CSD path in this snapshot.
    frame[4] = 0x00400000
    if sensor < 0x10000:
        sensor_ptr = u32(flash, wr + 4)
        sensor_off = addr_to_sram_offset(sensor_ptr) + sensor * 10 + 9
        frame[4] |= u8(sram, sensor_off) << 8
    frame[4] |= u8(sram, wc + 0x2E) & 0xFF

    # FUN_000012e8's final overwrite of scan/control fields.
    frame[3] = (((u8(flash, wr + 0x86) - 1) << 28) |
                (frame[3] & 0xFFFF0000) |
                ((u32(sram, wc + 0x2C) & 0xFFFF) - 1)) & 0x7FFFFFFF
    common_flag = u8(flash, COMMON + 0x4B)
    frame[5] |= (common_flag << 4) | 3
    frame[5] &= 0xFFFFFFFF
    frame[3] &= 0xFFFFFFFF
    frame[4] &= 0xFFFFFFFF
    if trace is not None:
        trace.update(widget=widget, sensor=sensor, method=method,
                     sense_div=sense_div, lfsr_mode=lfsr_mode,
                     sensor_byte9=u8(sram, sensor_off),
                     scan=frame[3], cdac=frame[4], ctl=frame[5])
    return frame


def build_instance(flash, sram, instance):
    base_csw = u32(sram, 0x59C + instance * 0x18C + 0x34)
    channel = u8(flash, COMMON + 0x4E) + instance
    common_mode_a = u8(sram, 0x8C4 + 0x52)
    common_mode_b = u8(sram, 0x8C4 + 0x51)
    selector_a = 1
    if common_mode_a == 2:
        selector_a = 1
    elif common_mode_a == 5:
        selector_a = 7
    selector_b = 1
    if common_mode_b == 2:
        selector_b = 1
    elif common_mode_b == 4:
        selector_b = 6

    results = []
    for slot in range(7):
        raw_index = instance * 7 + slot
        first_word, _ = schedule_entry(sram, raw_index)
        schedule_index = raw_index
        if first_word > ROW_MIRROR_THRESHOLD:
            schedule_index = raw_index + 7 if instance == 0 else raw_index - 7
        trace = {}
        frame = build_sensor_frame(flash, sram, schedule_index, trace)
        widget, sensor = schedule_entry(sram, schedule_index)
        masks = [0, 0, 0]
        operations = []

        if first_word == SENTINEL:
            update_csw(masks, base_csw, selector_b)
            operations.append(("base/sentinel", base_csw, selector_b))
        else:
            wr = widget_record(flash, widget)
            method = u8(flash, wr + 0x84)
            update_csw(masks, base_csw, selector_b if method == 1 else selector_a)
            operations.append(("base", base_csw, selector_b if method == 1 else selector_a))
            if first_word <= ROW_MIRROR_THRESHOLD and method == 1:
                sensor_info = pin_mask_for_sensor(flash, widget, sensor, channel)
                sensor_mask = sensor_info[0]
                if sensor_mask:
                    update_csw(masks, sensor_mask, 5)
                    operations.append(("sensor", sensor_mask, 5, sensor_info[1]))
            count = u8(flash, COMMON + 0x3C)
            if count:
                smask, bits = shield_mask(flash, channel)
                update_csw(masks, smask, 6 if method == 1 else selector_a)
                operations.append(("shield", smask, 6 if method == 1 else selector_a, bits))
        frame[0:3] = masks
        results.append({"slot": slot, "raw_index": raw_index,
                        "schedule_index": schedule_index, "widget": widget,
                        "sensor": sensor, "frame": frame,
                        "operations": operations, "trace": trace})
    return results


def compare(instance_results, sram):
    mismatches = []
    for item in instance_results:
        slot = item["slot"]
        expected = [u32(sram, FRAME_BASE + (item["raw_index"] if item["raw_index"] < 14 else 0) * 0x18 + i * 4)
                    for i in range(6)]
        # Runtime frame storage is physically row-major: instance 0 uses 0..6,
        # instance 1 uses 7..13, even when the scheduler entry is mirrored.
        actual = item["frame"]
        if actual != expected:
            mismatches.append((slot, expected, actual))
    return mismatches


def main():
    flash = FLASH_PATH.read_bytes()
    sram = SRAM_PATH.read_bytes()
    all_results = []
    for instance in range(2):
        print(f"## Shadow model instance {instance}")
        results = build_instance(flash, sram, instance)
        all_results.extend(results)
        for item in results:
            words = " ".join(f"0x{x:08x}" for x in item["frame"])
            ops = "; ".join(f"{op[0]} mask=0x{op[1]:08x} sel={op[2]}" for op in item["operations"])
            print(f"- slot {item['slot']}: raw={item['raw_index']} effective={item['schedule_index']} "
                  f"widget={item['widget']} sensor={item['sensor']} {words}")
            print(f"  - {ops}")
    mismatches = compare(all_results, sram)
    print(f"## Comparison: {14 - len(mismatches)}/14 frames match all six words")
    for slot, expected, actual in mismatches:
        print(f"- mismatch row-slot {slot}: expected {[f'0x{x:08x}' for x in expected]} "
              f"actual {[f'0x{x:08x}' for x in actual]}")


if __name__ == "__main__":
    main()
