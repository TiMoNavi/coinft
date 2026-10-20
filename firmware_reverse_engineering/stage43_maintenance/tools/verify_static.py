#!/usr/bin/env python3
"""Check Stage 43 layout and factory-data invariants without target hardware."""

from __future__ import annotations

import hashlib
import pathlib
import struct
import subprocess
import sys


STAGE = pathlib.Path(__file__).resolve().parents[1]
ELF = STAGE / "build/out/43_maintenance_commands.elf"
BIN = STAGE / "build/out/43_maintenance_commands.bin"
FACTORY_BIN = STAGE.parents[1] / "01_firmware_原始hex/CoinFT_V2_firmware.bin"
FRAME_HASH = "2f0ba501362c4b81bd7cf6a9a9518651311b52f936b381f61373c13e8104758c"
EEPROM_START = 0x5000
EEPROM_END = 0x5200


def tool_output(toolchain: pathlib.Path, name: str, *args: str) -> str:
    command = toolchain / "bin" / name
    return subprocess.check_output((str(command), *args), text=True)


def symbols(toolchain: pathlib.Path) -> dict[str, tuple[int, int, str]]:
    output = tool_output(
        toolchain, "arm-none-eabi-nm", "-S", "--defined-only", str(ELF)
    )
    result: dict[str, tuple[int, int, str]] = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) == 4:
            address, size, kind, name = fields
            result[name] = (int(address, 16), int(size, 16), kind)
        elif len(fields) == 3:
            address, kind, name = fields
            result[name] = (int(address, 16), 0, kind)
    return result


def sections(toolchain: pathlib.Path) -> dict[str, tuple[int, int, int]]:
    output = tool_output(toolchain, "arm-none-eabi-objdump", "-h", str(ELF))
    result: dict[str, tuple[int, int, int]] = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 7 and fields[0].isdigit() and fields[1].startswith("."):
            result[fields[1]] = tuple(int(value, 16) for value in fields[2:5])
    return result


def flash_symbol(image: bytes, table: dict[str, tuple[int, int, str]], name: str) -> bytes:
    address, size, _ = table[name]
    return image[address : address + size]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)
    print(f"PASS {message}")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: verify_static.py TOOLCHAIN_ROOT", file=sys.stderr)
        return 2

    toolchain = pathlib.Path(sys.argv[1])
    image = BIN.read_bytes()
    factory = FACTORY_BIN.read_bytes()
    table = symbols(toolchain)
    section = sections(toolchain)

    require(section[".text"][1] + section[".text"][0] <= EEPROM_START,
            "application text ends below 0x5000")
    require(section[".data"][2] + section[".data"][0] <= EEPROM_START,
            ".data load image ends below 0x5000")
    require(section[".coinft_eeprom"] == (0x200, EEPROM_START, EEPROM_START),
            "EEPROM section is exactly 0x5000..0x51ff")
    require(table["__etext"][0] == section[".data"][2],
            "startup .data source equals its real load address")

    eeprom = image[EEPROM_START:EEPROM_END]
    require(len(eeprom) == 0x200, "binary contains the full EEPROM section")
    require(eeprom == bytes(0x200), "rebuilt EEPROM initial image is all zero")
    require(eeprom == factory[EEPROM_START:EEPROM_END],
            "rebuilt EEPROM initial bytes match factory BIN")

    frames = flash_symbol(image, table, "coinft_factory_frames")
    require(len(frames) == 14 * 6 * 4, "factory frame table is 14 x 6 words")
    require(hashlib.sha256(frames).hexdigest() == FRAME_HASH,
            "factory frame table hash matches SWD export")

    expected_config = struct.pack("<IBBBBI", 128, 0, 2, 0, 1, EEPROM_START)
    require(flash_symbol(image, table, "eeprom_config") == expected_config,
            "Em_EEPROM config is 128/extended/wear2/nonredundant/blocking/0x5000")

    for name in (
        "msc_0_interrupt_IRQHandler",
        "msc_1_interrupt_IRQHandler",
        "tcpwm_interrupts_0_IRQHandler",
        "Cy_Em_EEPROM_Init",
        "Cy_Em_EEPROM_Read",
        "Cy_Em_EEPROM_Write",
    ):
        require(name in table and table[name][2] in ("T", "t"),
                f"{name} is linked as code")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, KeyError) as error:
        print(f"FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
