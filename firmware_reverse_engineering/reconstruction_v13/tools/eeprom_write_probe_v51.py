#!/usr/bin/env python3
"""v51 stage H: does the Em_EEPROM write actually persist?

v50 could not answer this.  The EEPROM region reads as all zeros in both the
shipped HEX and the 2026-08-28 flash dump, and all-zero is ambiguous: it is
both the erased state of PSoC4 flash and what a failed write would leave.

The experiment is read -> command -> read, all over SWD, so the answer comes
from the flash contents rather than from inference:

  `n` alone re-runs the device-ID bootstrap.  Because the row is currently
  uninitialised, the bootstrap should copy the default template and write it
  back.  If the region changes, writes persist; if not, they do not.

This is the first operation in the project that intentionally causes a flash
write, and it is only run with explicit authorisation.  It writes at most the
12-byte device ID that the firmware itself would write on its own.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from sync_capture_v46 import OpenOcdTcl, RawSerial, UART_BAUD  # noqa: E402

EEPROM_FLASH_BASE = 0x00005000
EEPROM_PROBE_WORDS = 64          # 256 bytes: covers both wear-levelling units
ID_BUFFER = 0x200004FD


def read_region(ocd: OpenOcdTcl, addr: int, words: int) -> list[int] | None:
    return ocd.read_words(addr, words)


def summarise(words: list[int] | None) -> str:
    if words is None:
        return "READ FAILED"
    blob = b"".join(struct.pack("<I", w) for w in words)
    nz = sum(1 for b in blob if b)
    text = bytes(c if 32 <= c < 127 else 0x2E for c in blob[:32])
    return f"{nz}/{len(blob)} non-zero bytes; first 32: {text!r}"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--port", default="/dev/cu.usbmodem14403")
    ap.add_argument("--command", default="n", choices=["n", "w"])
    ap.add_argument("--write-id", default="NFT\r",
                    help="payload for 'w'; must contain CR or be 12 bytes")
    ap.add_argument("--yes", action="store_true",
                    help="required: acknowledges that this writes flash")
    args = ap.parse_args()

    if not args.yes:
        print("Refusing to run without --yes: this command writes flash.")
        return 2

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    port = RawSerial(args.port, UART_BAUD)
    port.write(b"i")
    time.sleep(0.6)
    ocd = OpenOcdTcl()
    result: dict = {"command": args.command}

    try:
        before = read_region(ocd, EEPROM_FLASH_BASE, EEPROM_PROBE_WORDS)
        id_before = read_region(ocd, ID_BUFFER & ~3, 4)
        print(f"flash 0x{EEPROM_FLASH_BASE:08x} before: {summarise(before)}")
        print(f"id buffer before: {summarise(id_before)}")

        if args.command == "n":
            payload = b"n"
        else:
            body = args.write_id.encode("latin-1")
            if b"\r" not in body and len(body) < 12:
                print("Refusing: 'w' payload needs a CR or 12 bytes, "
                      "otherwise the firmware blocks.")
                return 3
            payload = b"w" + body
        print(f"sending {payload!r}")
        port.write(payload)
        time.sleep(2.0)

        # The firmware must still be alive: a stalled main loop means the
        # command consumed more bytes than we supplied.
        port.write(b"s")
        time.sleep(1.0)
        intr = ocd.read_words(0x40290170, 1)
        alive = intr is not None and intr[0] != 0
        print(f"after command: MSC INTR={intr}, scanning={alive}")
        port.write(b"i")
        time.sleep(0.5)

        after = read_region(ocd, EEPROM_FLASH_BASE, EEPROM_PROBE_WORDS)
        id_after = read_region(ocd, ID_BUFFER & ~3, 4)
        print(f"flash 0x{EEPROM_FLASH_BASE:08x} after:  {summarise(after)}")
        print(f"id buffer after:  {summarise(id_after)}")

        changed = (before is not None and after is not None
                   and before != after)
        result.update({
            "flash_before": before, "flash_after": after,
            "id_before": id_before, "id_after": id_after,
            "flash_changed": changed, "still_scanning": alive,
        })
        print(f"\nflash_changed={changed}")
        if changed and before and after:
            for i, (b, a) in enumerate(zip(before, after)):
                if b != a:
                    print(f"  word {i} @0x{EEPROM_FLASH_BASE + i * 4:08x}: "
                          f"0x{b:08x} -> 0x{a:08x}")
    finally:
        ocd.close()
        time.sleep(0.2)
        port.write(b"i")
        time.sleep(0.2)
        port.close()

    (outdir / f"eeprom_write_probe_{args.command}.json").write_text(
        json.dumps(result, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
