#!/usr/bin/env python3
"""v46 same-board synchronized capture: KitProg3 SWD reads + KitProg3 UART bridge.

Both the SWD probe and the UART bridge belong to the *same* KitProg3, which is
attached to the *same* CoinFT board.  That is what v45 stage A asked for and
what previous rounds could not do (Teensy sat on a second board).

Strictly read-only.  The only bytes ever written to the target are the
documented UART console commands (`i` idle / `s` stream), which the stock
firmware already exposes.  No halt, reset, acquire, erase, unlock, program,
SFlash write, peripheral-register write or CPU-register write is issued.
"""

from __future__ import annotations

import argparse
import array
import fcntl
import json
import os
import select
import socket
import struct
import subprocess
import sys
import termios
import threading
import time
from pathlib import Path

OPENOCD = "/Applications/ModusToolboxProgtools-1.7/openocd/bin/openocd"
OPENOCD_SCRIPTS = "/Applications/ModusToolboxProgtools-1.7/openocd/scripts"
TCL_PORT = 6666
IOSSIOSPEED = 0x80045402

FRAME_LEN = 26
FRAME_SOF = 0x02
FRAME_EOF = 0x03
UART_BAUD = 1000000

# Read-only address plan.  Keep every region separate so that a failed read is
# recorded as a failure instead of silently corrupting a neighbouring region.
READ_PLAN = [
    # 13 x 10-byte sensor records; 0x2000045c is exactly where the 13 widget
    # contexts (0x184 + 13*0x38) end, so read one record past the 12 UART
    # channels to test whether a 13th record exists.
    ("sensor_records", 0x2000045C, 33),
    # History/baseline array.  v46 decoding puts its base at 0x2000091a with the
    # same 10-byte stride and 13 entries; read from 0x20000918 so the preceding
    # halfword stays visible instead of being assumed away.
    ("history", 0x20000918, 36),
    ("sar_result", 0x20000B30, 1),
    ("msc0_frame", 0x2000099C, 6),
    ("msc0_170", 0x40290170, 1),
    ("msc1_170", 0x402A0170, 1),
    ("msc0_190", 0x40290190, 1),
    ("msc1_190", 0x402A0190, 1),
    ("msc0_20c", 0x4029020C, 1),
]


class RawSerial:
    """stdlib-only serial port; macOS needs IOSSIOSPEED for 1 Mbaud."""

    def __init__(self, path: str, baud: int) -> None:
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(self.fd)
        ispeed, ospeed, cc = attrs[4], attrs[5], list(attrs[6])
        cc[termios.VMIN] = 0
        cc[termios.VTIME] = 0
        cflag = termios.CS8 | termios.CREAD | termios.CLOCAL
        termios.tcsetattr(self.fd, termios.TCSANOW,
                          [0, 0, cflag, 0, ispeed, ospeed, cc])
        fcntl.ioctl(self.fd, IOSSIOSPEED, array.array("i", [baud]), 1)
        termios.tcflush(self.fd, termios.TCIOFLUSH)

    def write(self, data: bytes) -> None:
        os.write(self.fd, data)

    def read_nowait(self) -> bytes:
        r, _, _ = select.select([self.fd], [], [], 0.02)
        if not r:
            return b""
        try:
            return os.read(self.fd, 65536)
        except (BlockingIOError, OSError):
            return b""

    def close(self) -> None:
        os.close(self.fd)


class UartCollector(threading.Thread):
    """Timestamp every read burst so frames can be placed on the host clock."""

    def __init__(self, port: RawSerial) -> None:
        super().__init__(daemon=True)
        self.port = port
        self.chunks: list[tuple[float, bytes]] = []
        self._stop = threading.Event()

    def run(self) -> None:
        while not self._stop.is_set():
            data = self.port.read_nowait()
            if data:
                self.chunks.append((time.monotonic(), data))

    def stop(self) -> None:
        self._stop.set()
        self.join(timeout=2.0)

    def raw(self) -> bytes:
        return b"".join(c for _, c in self.chunks)

    def timed_frames(self) -> list[tuple[float, list[int]]]:
        """Return (host_time, 12 halfwords) using per-chunk arrival times.

        A chunk's bytes are attributed to the chunk's arrival timestamp; the
        UART runs at 1 Mbaud so a 26-byte frame occupies ~260 us and a chunk
        rarely spans more than a few frames.  This is an upper bound on the
        frame's true arrival time, never an interpolation across a gap.
        """
        out: list[tuple[float, list[int]]] = []
        buf = bytearray()
        buf_ts: list[float] = []
        for ts, data in self.chunks:
            buf += data
            buf_ts.extend([ts] * len(data))
            while True:
                start = buf.find(FRAME_SOF)
                if start < 0 or len(buf) - start < FRAME_LEN:
                    break
                if buf[start + FRAME_LEN - 1] != FRAME_EOF:
                    del buf[:start + 1]
                    del buf_ts[:start + 1]
                    continue
                body = bytes(buf[start + 1:start + FRAME_LEN - 1])
                vals = list(struct.unpack("<12H", body))
                out.append((buf_ts[start + FRAME_LEN - 1], vals))
                del buf[:start + FRAME_LEN]
                del buf_ts[:start + FRAME_LEN]
        return out


class OpenOcdTcl:
    """Persistent read-only OpenOCD session driven over its TCL RPC port."""

    TERM = b"\x1a"

    def __init__(self) -> None:
        self.proc = subprocess.Popen(
            [OPENOCD, "-s", OPENOCD_SCRIPTS,
             "-c", f"tcl_port {TCL_PORT}",
             "-c", "gdb_port disabled",
             "-c", "telnet_port disabled",
             "-f", "interface/kitprog3.cfg",
             "-c", "set PSOC4_USE_ACQUIRE 0",
             "-f", "target/psoc4.cfg",
             "-c", "init"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.banner: list[str] = []
        # Drain openocd's output continuously; a full pipe would otherwise stall
        # its startup and make the tcl port look like it never appeared.
        self._drain = threading.Thread(target=self._pump, daemon=True)
        self._drain.start()
        self.sock = self._connect()

    def _pump(self) -> None:
        if self.proc.stdout is None:
            return
        for line in self.proc.stdout:
            self.banner.append(line.rstrip())

    def _connect(self) -> socket.socket:
        deadline = time.monotonic() + 60.0
        last: OSError | None = None
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError(
                    "openocd exited during startup:\n" + "\n".join(self.banner[-25:]))
            try:
                s = socket.create_connection(("127.0.0.1", TCL_PORT), timeout=5.0)
                s.settimeout(20.0)
                return s
            except OSError as exc:
                last = exc
                time.sleep(0.2)
        self.proc.kill()
        raise RuntimeError(
            f"openocd tcl port never came up ({last}):\n" + "\n".join(self.banner[-25:]))

    def cmd(self, text: str) -> str:
        self.sock.sendall(text.encode() + self.TERM)
        buf = bytearray()
        while self.TERM not in buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise RuntimeError("openocd closed the tcl connection")
            buf += chunk
        return buf.split(self.TERM)[0].decode(errors="replace")

    def read_words(self, addr: int, count: int) -> list[int] | None:
        """mdw via TCL; returns None on any read error instead of faking data."""
        try:
            text = self.cmd(f"mdw {addr:#010x} {count}")
        except OSError:
            return None
        if "Error" in text or "Failed" in text:
            return None
        words: list[int] = []
        for line in text.splitlines():
            if ":" not in line:
                continue
            _, _, tail = line.partition(":")
            for tok in tail.split():
                try:
                    words.append(int(tok, 16))
                except ValueError:
                    return None
        return words if len(words) == count else None

    def close(self) -> None:
        try:
            self.sock.sendall(b"shutdown" + self.TERM)
        except OSError:
            pass
        try:
            self.sock.close()
        except OSError:
            pass
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        self._drain.join(timeout=2.0)


def capture(outdir: Path, rounds: int, gap: float, port_path: str, state: str) -> dict:
    outdir.mkdir(parents=True, exist_ok=True)
    port = RawSerial(port_path, UART_BAUD)
    collector = UartCollector(port)
    collector.start()

    # Always start from a known idle state.  OpenOCD's target examination reads
    # ROM/CPUID space and is the one step that reliably loses SWD arbitration
    # against a streaming CPU, so bring the debug session up while the firmware
    # is idle and only then ask for the stream.
    port.write(b"i")
    time.sleep(0.4)
    collector.chunks.clear()

    ocd = OpenOcdTcl()

    if state == "stream":
        port.write(b"s")
        time.sleep(0.4)

    samples = []
    try:
        for i in range(rounds):
            entry: dict = {"round": i, "regions": {}}
            for name, addr, count in READ_PLAN:
                t_before = time.monotonic()
                words = ocd.read_words(addr, count)
                t_after = time.monotonic()
                entry["regions"][name] = {
                    "addr": addr,
                    "count": count,
                    "t_before": t_before,
                    "t_after": t_after,
                    "words": words,
                    "ok": words is not None,
                }
            samples.append(entry)
            time.sleep(gap)
    finally:
        ocd.close()
        time.sleep(0.3)
        collector.stop()
        if state == "stream":
            port.write(b"i")
            time.sleep(0.2)
        port.close()

    raw = collector.raw()
    frames = collector.timed_frames()
    (outdir / f"uart_{state}_raw.bin").write_bytes(raw)

    result = {
        "state": state,
        "uart_bytes": len(raw),
        "uart_frames": len(frames),
        "rounds": rounds,
        "samples": samples,
        "frames": [{"t": t, "values": v} for t, v in frames],
        "openocd_banner": ocd.banner[-40:],
    }
    (outdir / f"sync_{state}.json").write_text(json.dumps(result, indent=1))
    return result


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--port", default="/dev/cu.usbmodem14403")
    ap.add_argument("--rounds", type=int, default=8)
    ap.add_argument("--gap", type=float, default=0.25)
    ap.add_argument("--state", choices=["stream", "idle"], default="stream")
    args = ap.parse_args()

    res = capture(Path(args.outdir), args.rounds, args.gap, args.port, args.state)
    ok = sum(1 for s in res["samples"]
             for r in s["regions"].values() if r["ok"])
    total = sum(len(s["regions"]) for s in res["samples"])
    print(f"state={res['state']} uart_bytes={res['uart_bytes']} "
          f"uart_frames={res['uart_frames']} swd_reads_ok={ok}/{total}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
