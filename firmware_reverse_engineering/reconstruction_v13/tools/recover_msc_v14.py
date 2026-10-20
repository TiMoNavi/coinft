#!/usr/bin/env python3
"""Read-only decoder for the confirmed fifth-generation MSC tables.

This tool deliberately reports raw values and ABI mismatches. It does not
write firmware, access SWD, or claim that a candidate register value is safe
to program into a target.
"""

from pathlib import Path
import struct


ROOT = Path(__file__).resolve().parents[2]
FLASH = ROOT / "07_analysis" / "CoinFT_V2_firmware.bin"
SRAM = ROOT / "07_analysis" / "runtime_snapshots_20260828" / "live_run_sram.bin"


def u8(data, off):
    return data[off]


def u16(data, off):
    return struct.unpack_from("<H", data, off)[0]


def u32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def p32(value):
    return f"0x{value:08x}"


MSC_BASE_FIELDS = (
    "ctl", "spare", "scanCtl1", "scanCtl2", "initCtl1", "initCtl2",
    "initCtl3", "initCtl4", "senseDutyCtl", "sensePeriodCtl", "filterCtl",
    "ccompCdacCtl", "ditherCdacCtl", "cswCtl", "swSelGpio", "swSelCdacRe",
    "swSelCdacCo", "swSelCdacCf", "swSelCmod1", "swSelCmod2", "swSelCmod3",
    "swSelCmod4", "obsCtl", "status1", "status2", "status3",
    "resultFifoStatus", "resultFifoRd", "intr", "intrSet", "intrMask",
    "intrMasked", "frameCmd",
)


def decode_msc_base(data, base, label):
    """Decode one 0x18c runtime image using the CAT2 MSCv3 ABI."""
    words = [u32(data, base + off) for off in range(0, 0x18c, 4)]
    print(f"## MSC base config {label} at file offset 0x{base:04x}")
    print("- ABI: cy_stc_msc_base_config_t, size 0x18c (exact)")
    for index, name in enumerate(MSC_BASE_FIELDS):
        value = words[index]
        if value:
            print(f"- +0x{index * 4:03x} {name} = {p32(value)}")

    sensor = words[0x21:0x27]
    print("- sensorConfig = [" + ", ".join(p32(x) for x in sensor) + "]")
    csw = words[0x27:0x47]
    print("- swSelCsw nonzero = " + str([(i, p32(x)) for i, x in enumerate(csw) if x]))
    funcs = words[0x47:0x4f]
    print("- swSelCswFunc nonzero = " + str([(i, p32(x)) for i, x in enumerate(funcs) if x]))
    for mode in range(4):
        off = 0x4f + mode * 5
        values = words[off:off + 5]
        if any(values):
            print(f"- mode[{mode}] = [" + ", ".join(p32(x) for x in values) + "]")

    ctl = words[0]
    print("- ctl bits: senseEn={} mscCmpEn={} clkSyncEn={} extFrameStartEn={} "
          "operatingMode={} bufMode={} clkMscRatio={} enabled={}".format(
              ctl & 1, (ctl >> 4) & 1, (ctl >> 8) & 1, (ctl >> 12) & 1,
              (ctl >> 16) & 3, (ctl >> 20) & 1, (ctl >> 24) & 1,
              (ctl >> 31) & 1))
    scan2 = words[3]
    init1 = words[5]
    init3 = words[6]
    init4 = words[7]
    print(f"- scanCtl2: numEpiCycles={scan2 & 0x3ff} chopPol={(scan2 >> 16) & 1} "
          f"chopEvenHold={(scan2 >> 24) & 1}")
    print(f"- initCtl2: cmod12Rail={init1 & 0xfff} cmod12Short={(init1 >> 16) & 0xfff}")
    print(f"- initCtl3: proOffset={init3 & 0xff} proTrips={(init3 >> 8) & 3} "
          f"cmodSel={(init3 >> 10) & 3} initMode={(init3 >> 15) & 1}")
    print(f"- initCtl4: dummySubConvs={init4 & 0xff} waitCycles={(init4 >> 16) & 0x3ff}")
    intr = words[0x1c]
    mask = words[0x1e]
    print(f"- interrupt image: intr={p32(intr)} intrMask={p32(mask)} "
          f"(sample={(intr >> 4) & 1}, init={(intr >> 12) & 1}, mask=0x{mask:08x})")
    return words


def compare_msc_bases(first, second):
    print("## MSC base config instance differences")
    differences = []
    for index, (left, right) in enumerate(zip(first, second)):
        if left != right:
            name = MSC_BASE_FIELDS[index] if index < len(MSC_BASE_FIELDS) else "tail"
            differences.append((index * 4, name, left, right))
    if not differences:
        print("- none")
    else:
        for off, name, left, right in differences:
            print(f"- +0x{off:03x} {name}: {p32(left)} -> {p32(right)}")


def decode_common(data):
    base = 0x4550
    fields32 = [
        (0x00, "cpuClkHz"),
        (0x04, "periClkHz"),
        (0x08, "ptrChConfig"),
        (0x0c, "ptrDmacBase"),
        (0x10, "ptrDmaWrChSnsCfgAddr"),
        (0x14, "ptrDmaRdChSnsCfgAddr"),
        (0x18, "ptrDmaWrChSnsCfgAddrLocal"),
        (0x1c, "ptrDmaRdChSnsCfgAddrLocal"),
        (0x20, "ptrEmptyRawCount"),
    ]
    fields16 = [
        (0x24, "numPin"),
        (0x26, "numSns"),
        (0x28, "proxTouchCoeff"),
        (0x2a, "csdRConst"),
        (0x2c, "numEpiCycles"),
        (0x2e, "numCoarseInitChargeCycles"),
        (0x30, "numCoarseInitSettleCycles"),
        (0x32, "numSlots"),
    ]
    fields8 = [
        (0x34, "numWd"),
        (0x35, "periDividerType"),
        (0x36, "periDividerIndex"),
        (0x37, "analogWakeupDelay"),
        (0x38, "swSensorAutoResetEn"),
        (0x39, "csdInactiveSnsConnection"),
        (0x3a, "csxInactiveSnsConnection"),
        (0x3b, "isxInactiveSnsConnection"),
        (0x3c, "csdShieldNumPin"),
        (0x3d, "csxRawTarget"),
        (0x3e, "csxCalibrationError"),
        (0x3f, "csdRawTarget"),
        (0x40, "csdCalibrationError"),
        (0x41, "isxRawTarget"),
        (0x42, "isxCalibrationError"),
        (0x43, "wbxRawTarget"),
        (0x44, "csdShieldMode"),
        (0x45, "numProOffsetCycles"),
        (0x46, "proOffsetCdacComp"),
        (0x47, "chopPolarity"),
        (0x48, "numBadScans"),
        (0x49, "counterMode"),
        (0x4a, "sensorConnection"),
        (0x4b, "syncClockEn"),
        (0x4c, "syncMode"),
        (0x4d, "masterChannelId"),
        (0x4e, "numChannels"),
        (0x4f, "channelOffset"),
        (0x50, "syncFrameStartEn"),
    ]
    print("## Common config at Flash 0x4550")
    for off, name in fields32:
        print(f"- +0x{off:02x} {name} = {p32(u32(data, base + off))}")
    for off, name in fields16:
        print(f"- +0x{off:02x} {name} = {u16(data, base + off)} (0x{u16(data, base + off):04x})")
    for off, name in fields8:
        print(f"- +0x{off:02x} {name} = {u8(data, base + off)} (0x{u8(data, base + off):02x})")
    print("- Current CAT2 fifth-gen C struct size: 0x54 after tail padding")
    print("- Observed next table pointer begins at +0x50: legacy ABI boundary candidate")


def decode_channel(data, base, label):
    print(f"## Channel descriptors: {label} at 0x{base:04x}")
    for index in range(2):
        off = base + index * 0x20
        values = [u32(data, off + i * 4) for i in range(8)]
        print(f"- channel {index}: hw={p32(values[0])} ctx={p32(values[1])} "
              f"port1={p32(values[2])} pin1={data[off + 0x0c]} "
              f"port2={p32(values[4])} pin2={data[off + 0x14]} "
              f"dma={data[off + 0x15]:02x}/{data[off + 0x16]:02x}/"
              f"{data[off + 0x17]:02x}/{data[off + 0x18]:02x} "
              f"shield={p32(values[7])}")


def decode_slots(data):
    base = 0x99c
    print("## Runtime sensor frames at SRAM 0x2000099c")
    names = ("SNS_SW_SEL_CSW_MASK2", "SNS_SW_SEL_CSW_MASK1",
             "SNS_SW_SEL_CSW_MASK0", "SNS_SCAN_CTL", "SNS_CDAC_CTL",
             "SNS_CTL")
    for slot in range(14):
        off = base + slot * 0x18
        words = [u32(data, off + i * 4) for i in range(6)]
        pairs = ", ".join(f"{name}={p32(value)}" for name, value in zip(names, words))
        print(f"- slot {slot:02d}: {pairs}")
        scan_ctl = words[3]
        cdac_ctl = words[4]
        sns_ctl = words[5]
        print("  - decoded: subConvs={} compDiv={} numConv={} "
              "cdacRe={} cdacCo={} clockRefRate={} "
              "start={} valid={} last={} multiCh={} senseDiv={} lfsrMode={}"
              .format(scan_ctl & 0xffff,
                      (scan_ctl >> 16) & 0xfff,
                      (scan_ctl >> 28) & 0x7,
                      cdac_ctl & 0xff,
                      (cdac_ctl >> 8) & 0xff,
                      (cdac_ctl >> 22) & 0x1,
                      sns_ctl & 0x1,
                      (sns_ctl >> 1) & 0x1,
                      (sns_ctl >> 2) & 0x1,
                      (sns_ctl >> 4) & 0x3,
                      (sns_ctl >> 16) & 0xfff,
                      (sns_ctl >> 28) & 0x3))


def decode_internal_context(data):
    base = 0x8c4
    callback = u32(data, base + 0x10)
    print("## Internal context at SRAM 0x200008c4")
    print(f"- +0x10 ptrISRCallback candidate = {p32(callback)}")
    print(f"- Thumb code address = {p32(callback & ~1)}")
    print("- Known static target: 0x00001989 -> FUN_00001988")
    print("- Remaining bytes retain legacy/opaque ABI status")


def main():
    flash = FLASH.read_bytes()
    sram = SRAM.read_bytes()
    decode_common(flash)
    print()
    decode_channel(flash, 0x548c, "Flash")
    print()
    decode_channel(sram, 0xf0, "SRAM snapshot")
    print()
    decode_internal_context(sram)
    print()
    template = decode_msc_base(flash, 0x5260, "Flash template")
    print()
    base0 = decode_msc_base(sram, 0x59c, "MSC0")
    print()
    base1 = decode_msc_base(sram, 0x728, "MSC1")
    print()
    print("## Runtime generation deltas: Flash template -> MSC0")
    compare_msc_bases(template, base0)
    print()
    print("## Runtime generation deltas: Flash template -> MSC1")
    compare_msc_bases(template, base1)
    print()
    compare_msc_bases(base0, base1)
    print()
    decode_slots(sram)


if __name__ == "__main__":
    main()
