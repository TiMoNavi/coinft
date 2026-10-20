#!/usr/bin/env python3
"""v47 stage B: force experiment with spoken cues, on one CoinFT board.

The operator's hands are on the sensor, so the cue channel is macOS `say`
(Chinese voice) and the terminal only keeps the audit log.

Positions are labelled in the *operator's* frame (right-up, left-up, ... as
seen looking at the board).  Nothing here assumes that frame matches the
schematic's RU/LU/LD/RD nets -- recovering that correspondence is the point of
the experiment, so the mapping is derived afterwards from which channels
respond.

Strictly read-only with respect to the target: the only bytes written are the
stock firmware's `i` and `s` console characters.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from sync_capture_v46 import OpenOcdTcl, RawSerial, UartCollector, UART_BAUD  # noqa: E402

# Only the sensor record array is polled: fewer words per round means a shorter
# tear window and a higher effective sample rate, which matters more here than
# breadth of coverage.
RECORD_ADDR = 0x2000045C
RECORD_WORDS = 33
RECORD_COUNT = 13
UART_CHANNELS = 12

VOICE = "Tingting"

# Consecutive failed reads that mean the SWD link is down rather than merely
# contended; a healthy link contends but recovers within a few reads.
MAX_FAIL_STREAK = 40

# Operator-frame positions, in the order the operator said they would press.
POSITIONS = ["右上角", "左上角", "左下角", "右下角", "中心"]

# (key, spoken cue, cue budget seconds, capture seconds)
#
# The cue budget is a fixed allowance for speech plus operator reaction, and it
# is honoured whether or not `say` finishes early.  Fixed budgets are what make
# the printed timetable exact, so the operator can be told the wall-clock time
# of every press in advance instead of having to watch for a prompt.
PHASES = [
    ("unloaded", "请不要接触传感器", 4.0, 5.0),
    ("press", "现在按压 {pos}，一直按住不要松", 4.0, 30.0),
    ("release", "现在松开", 4.0, 8.0),
]

POSITION_INTRO_BUDGET = 4.0


def configure_phases(cue: float, unloaded: float, hold: float,
                     release: float) -> None:
    """Let the operator trade capture length against total session time."""
    global PHASES, POSITION_INTRO_BUDGET
    POSITION_INTRO_BUDGET = cue
    PHASES = [
        ("unloaded", "请不要接触传感器", cue, unloaded),
        ("press", "现在按压 {pos}，一直按住不要松", cue, hold),
        ("release", "现在松开", cue, release),
    ]


def phase_offsets() -> tuple[list[tuple[str, float, float]], float]:
    """Offsets of each phase within one position, and the position's length."""
    out = []
    t = POSITION_INTRO_BUDGET
    for key, _cue, budget, seconds in PHASES:
        out.append((key, t + budget, seconds))
        t += budget + seconds
    return out, t


def build_timetable(start_wall: float,
                    positions: list[str]) -> list[dict]:
    """Absolute wall-clock start of every capture window."""
    offsets, per_position = phase_offsets()
    rows = []
    for i, pos in enumerate(positions):
        base = start_wall + i * per_position
        for key, offset, seconds in offsets:
            rows.append({
                "position": pos,
                "phase": key,
                "start": base + offset,
                "seconds": seconds,
            })
    return rows


def speak(text: str, enabled: bool) -> None:
    """Fire the cue without blocking, so fixed budgets stay exact."""
    print(f"  [cue] {time.strftime('%H:%M:%S')}  {text}", flush=True)
    if not enabled:
        return
    try:
        subprocess.Popen(["say", "-v", VOICE, text],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except OSError:
        pass


def sleep_until(wall: float) -> None:
    remaining = wall - time.time()
    if remaining > 0:
        time.sleep(remaining)


def run_phase(ocd: OpenOcdTcl, seconds: float, interval: float) -> list[dict]:
    """Poll the record array for `seconds`, tagging each read with its time.

    `interval` mainly keeps the UART collector thread scheduled: polling flat
    out (~250 reads/s) starves it of the GIL and the frame log comes back
    nearly empty.  Force varies far more slowly than the resulting rate, so
    there is nothing to gain from polling harder.
    """
    samples = []
    streak = 0
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        t_before = time.monotonic()
        words = ocd.read_words(RECORD_ADDR, RECORD_WORDS)
        t_after = time.monotonic()
        samples.append({
            "t_before": t_before,
            "t_after": t_after,
            "words": words,
            "ok": words is not None,
        })
        # A long failure streak means the SWD link itself is down (a loose
        # probe wire does this).  Ending the phase surfaces that immediately
        # instead of burning the operator's hold time on dead reads.
        if words is None:
            streak += 1
            if streak >= MAX_FAIL_STREAK:
                print(f"    ! {streak} consecutive read failures - check the "
                      f"SWD wiring; ending phase early", flush=True)
                break
            time.sleep(interval * 4.0)
        else:
            streak = 0
            time.sleep(interval)
    return samples


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--port", default="/dev/cu.usbmodem14403")
    ap.add_argument("--positions", nargs="*", default=POSITIONS)
    ap.add_argument("--start-at",
                    help="local wall-clock HH:MM or HH:MM:SS for the first cue")
    ap.add_argument("--lead-in", type=float, default=10.0,
                    help="seconds before the first cue when --start-at is unset")
    ap.add_argument("--no-voice", action="store_true")
    ap.add_argument("--poll-interval", type=float, default=0.02,
                    help="gap between SWD reads; keeps the UART thread alive")
    ap.add_argument("--dry-run", action="store_true",
                    help="print the timetable and exit; touches no hardware")
    ap.add_argument("--cue-budget", type=float, default=4.0)
    ap.add_argument("--unloaded-seconds", type=float, default=5.0)
    ap.add_argument("--hold-seconds", type=float, default=30.0)
    ap.add_argument("--release-seconds", type=float, default=8.0)
    args = ap.parse_args()

    configure_phases(args.cue_budget, args.unloaded_seconds,
                     args.hold_seconds, args.release_seconds)

    voice = not args.no_voice

    # Resolve the first cue's wall-clock instant before opening any hardware,
    # so the timetable can be printed (and read out) up front.
    if args.start_at:
        parts = [int(x) for x in args.start_at.split(":")]
        while len(parts) < 3:
            parts.append(0)
        now = time.localtime()
        target = time.struct_time((
            now.tm_year, now.tm_mon, now.tm_mday,
            parts[0], parts[1], parts[2],
            now.tm_wday, now.tm_yday, -1))
        start_wall = time.mktime(target)
        if start_wall < time.time():
            start_wall += 24 * 3600.0
    else:
        start_wall = time.time() + args.lead_in

    timetable = build_timetable(start_wall, args.positions)
    _, per_position = phase_offsets()
    print(f"起始时刻 (本机/北京时间): "
          f"{time.strftime('%H:%M:%S', time.localtime(start_wall))}")
    print(f"每个位置耗时 {per_position:.0f}s，"
          f"共 {len(args.positions)} 个位置，"
          f"总计约 {per_position * len(args.positions) / 60.0:.1f} 分钟")
    print("按压时刻表：")
    for row in timetable:
        if row["phase"] != "press":
            continue
        print(f"    {time.strftime('%H:%M:%S', time.localtime(row['start']))}"
              f"  按压 {row['position']}  保持 {row['seconds']:.0f} 秒")
    if args.dry_run:
        return 0

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    port = RawSerial(args.port, UART_BAUD)
    collector = UartCollector(port)
    collector.start()

    # Idle first so OpenOCD's target examination does not race a scanning CPU.
    port.write(b"i")
    time.sleep(0.4)
    collector.chunks.clear()
    ocd = OpenOcdTcl()
    port.write(b"s")
    time.sleep(0.5)

    # Refuse to burn the operator's time if the firmware is not actually
    # scanning: idle means INTR == 0 and raw == baseline, so a press would
    # leave no trace at all.
    intr = ocd.read_words(0x40290170, 1)
    if intr is not None and intr[0] == 0:
        print("  ! MSC INTR == 0：固件未进入扫描态（'s' 未生效）。"
              "请检查 KitProg3 与 CoinFT 之间的 UART 接线后重试。", flush=True)
        ocd.close()
        collector.stop()
        port.close()
        return 2

    result = {"positions": [], "voice": voice, "start_wall": start_wall}
    t0 = time.monotonic()
    try:
        lead = start_wall - time.time()
        if lead > 12.0:
            speak(f"实验将在 {int(lead)} 秒后开始，请把手放在传感器附近，"
                  f"先不要接触", voice)
        sleep_until(start_wall - 10.0)
        speak("十秒后开始", voice)
        sleep_until(start_wall)

        offsets, per_position = phase_offsets()
        for pos_index, pos in enumerate(args.positions):
            base = start_wall + pos_index * per_position
            speak(f"第 {pos_index + 1} 个位置，{pos}", voice)
            entry = {"position": pos, "index": pos_index, "phases": {}}
            for (key, cue, budget, seconds), (okey, offset, _s) in zip(
                    PHASES, offsets):
                assert key == okey
                # Cue fires at the start of its budget; capture begins when the
                # budget expires, exactly as advertised in the timetable.
                sleep_until(base + offset - budget)
                speak(cue.format(pos=pos), voice)
                sleep_until(base + offset)
                t_start = time.monotonic()
                samples = run_phase(ocd, seconds, args.poll_interval)
                entry["phases"][key] = {
                    "t_start": t_start,
                    "t_end": time.monotonic(),
                    "wall_start": base + offset,
                    "samples": samples,
                }
                ok = sum(1 for s in samples if s["ok"])
                print(f"    {pos} / {key}: {ok}/{len(samples)} reads ok",
                      flush=True)
            result["positions"].append(entry)
        speak("实验结束，可以松手了", voice)
    finally:
        ocd.close()
        time.sleep(0.3)
        collector.stop()
        port.write(b"i")
        time.sleep(0.3)
        port.close()

    frames = collector.timed_frames()
    result["frames"] = [{"t": t, "values": v} for t, v in frames]
    result["t0"] = t0
    result["uart_frames"] = len(frames)
    (outdir / "force_experiment.json").write_text(json.dumps(result))
    (outdir / "uart_force_raw.bin").write_bytes(collector.raw())

    total = sum(len(p["phases"][k]["samples"])
                for p in result["positions"] for k in p["phases"])
    ok = sum(1 for p in result["positions"] for k in p["phases"]
             for s in p["phases"][k]["samples"] if s["ok"])
    print(f"positions={len(result['positions'])} uart_frames={len(frames)} "
          f"swd_reads_ok={ok}/{total}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
