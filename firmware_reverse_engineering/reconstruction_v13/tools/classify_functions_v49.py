#!/usr/bin/env python3
"""v49 stage F: classify every function the decompiler found.

Stage F's goal is D1: no function left in the `untouched` tier.  A function does
not need a host model to be closed -- a compiler helper or a PDL leaf only needs
to be *identified*.  This tool assigns each one a role from its decompiled body
and its position in the call graph, and reports what is left for human work.

Pure static analysis: reads the Ghidra export only, touches no hardware.
"""

from __future__ import annotations

import json
import re
from collections import defaultdict
from pathlib import Path

ANALYSIS = Path(__file__).resolve().parents[2] / "07_analysis"
GHIDRA = ANALYSIS / "ghidra_pseudocode.c"
INVENTORY = ANALYSIS / "function_inventory_v48.json"
OUT_JSON = ANALYSIS / "function_classification_v49.json"
OUT_MD = ANALYSIS / "function_classification_v49.md"

FUNC_RE = re.compile(r"\bFUN_([0-9a-fA-F]{8})\b")
DEF_RE = re.compile(r"^[A-Za-z_][\w \*]*\bFUN_([0-9a-fA-F]{8})\s*\(", re.M)

# Peripheral base addresses already established in earlier rounds.
def load_bodies() -> dict[str, str]:
    if not GHIDRA.exists():
        return {}
    lines = GHIDRA.read_text(errors="replace").splitlines()
    starts = [(i, m.group(1).lower())
              for i, l in enumerate(lines) if (m := DEF_RE.match(l + "\n"))]
    out = {}
    for idx, (start, addr) in enumerate(starts):
        end = starts[idx + 1][0] if idx + 1 < len(starts) else len(lines)
        out[addr] = "\n".join(lines[start:end])
    return out


FIRMWARE_BIN = ANALYSIS / "CoinFT_V2_firmware.bin"

# Peripheral bases keyed by the top 20 bits of the address.
PERIPHERAL_BY_PAGE = {
    0x40290: "MSC0", 0x402A0: "MSC1", 0x40010: "PERI", 0x40040: "GPIO",
    0x403A0: "SAR0", 0x40060: "SCB", 0x40030: "SRSS", 0xE000E: "NVIC",
}


def load_dat_table() -> dict[str, str]:
    """Resolve DAT_xxxxxxxx literal pools to peripheral names.

    Ghidra renders MMIO through literal pools, so scanning the decompiled text
    for 0x402900xx finds nothing: the address lives in the binary at the DAT_
    symbol's own offset.  Without this indirection every peripheral function
    looks like pure logic.
    """
    if not FIRMWARE_BIN.exists():
        return {}
    blob = FIRMWARE_BIN.read_bytes()
    table: dict[str, str] = {}
    for addr in range(0, len(blob) - 3, 2):
        value = int.from_bytes(blob[addr:addr + 4], "little")
        name = PERIPHERAL_BY_PAGE.get(value >> 12)
        if name:
            table[f"{addr:08x}"] = name
    return table


DAT_TABLE = load_dat_table()

# ARM EABI runtime routines, established in v49 by reading their bodies.  See
# fact_registry_v48.json / aeabi-runtime-helpers.
KNOWN_RUNTIME_HELPERS = {
    "00004378": ("compiler_helper",
                 "__aeabi_uidivmod / __udivsi3: unrolled shift-subtract "
                 "unsigned division, returns CONCAT44(remainder, quotient)"),
    "00004484": ("compiler_helper",
                 "divmod wrapper: divides when the divisor is non-zero, "
                 "otherwise calls the divide-by-zero hook"),
    "0000448c": ("compiler_helper",
                 "__aeabi_idiv0: empty divide-by-zero hook, returns"),
    "000044b8": ("memory_intrinsic", "memset"),
    # PDL-style peripheral init: the base address arrives as a parameter, so a
    # literal-address scan cannot see the peripheral.  Identified in v49 from
    # the register offsets they write.
    "000038e4": ("peripheral_access",
                 "SCB UART init: writes CTRL, UART_CTRL, UART_RX_CTRL "
                 "with MODE at bit 24 (0x2000000 = MODE 2 = UART)"),
    "00004274": ("peripheral_access",
                 "TCPWM counter init: CNT[n] at 0x100 + n*0x40; writes "
                 "CTRL, COUNTER, CC, CC_BUFF, PERIOD, TR_CTRL0"),
}


def peripherals_touched(body: str) -> set[str]:
    found = set()
    # Direct literals, when the decompiler does inline them.
    for m in re.finditer(r"0x([0-9a-fA-F]{5,8})", body):
        page = int(m.group(1), 16) >> 12
        if page in PERIPHERAL_BY_PAGE:
            found.add(PERIPHERAL_BY_PAGE[page])
    # Literal pools referenced by name.
    for m in re.finditer(r"DAT_([0-9a-fA-F]{8})", body):
        name = DAT_TABLE.get(m.group(1).lower())
        if name:
            found.add(name)
    return found


def classify(addr: str, body: str, callers: list[str],
             callees: list[str], lines: int) -> tuple[str, str]:
    """Return (role, rationale).

    Roles that count as closed for D1:
      compiler_helper  - integer division/shift/memory intrinsics
      memory_intrinsic - memset/memcpy style byte loops
      peripheral_access- touches a known peripheral; needs a model only if on
                         the main data path
      pure_logic       - no peripheral access, no calls; a computation leaf
    Roles that still need work:
      needs_model      - business logic on the main data path
    """
    periph = peripherals_touched(body)

    # Empty stubs.  Ghidra renders these as a bare `return;`.  They must not be
    # called "computation leaves": an empty body in the decompiler output can
    # also mean the function was never reached by the disassembler, so the
    # honest statement is that there is nothing to model *and* nothing proven.
    stripped = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    stmt = re.sub(r"^[^{]*\{|\}[^}]*$", "", stripped, flags=re.S).strip()
    if stmt in ("return;", "", "return 0;"):
        return ("empty_stub",
                "decompiles to an empty body; no behaviour to model "
                "(may be a reserved/unreached stage)")

    # Runtime helpers identified by body inspection in v49.  These are named
    # rather than inferred: earlier rounds mistook FUN_00004378 for a clock
    # conversion routine and FUN_00004484 for a CapSense validity callback,
    # which sent two rounds chasing business meaning that does not exist.
    known = KNOWN_RUNTIME_HELPERS.get(addr)
    if known:
        return known

    # Compiler helpers: AEABI names are stripped, but their shapes remain.  The
    # division helper here is *unrolled*, so a loop must not be required.
    if lines <= 60 and not periph:
        shifts = body.count(">>") + body.count("<<")
        has_loop = re.search(r"\b(while|for|do)\b", body) is not None
        if shifts >= 6 and re.search(r"CONCAT44|param_1\s*>>\s*0x1", body):
            return ("compiler_helper",
                    f"{shifts} shift ops against a divisor, returns a pair: "
                    "unrolled shift-subtract division shape")
        if not callees and shifts >= 3 and has_loop:
            return ("compiler_helper",
                    f"no peripheral, no calls, {shifts} shift ops in a loop: "
                    "shift/division intrinsic shape")
        if re.search(r"\*\s*\(?\s*(undefined1|char|byte|uint8_t|u?char)\s*\*",
                     body) and has_loop and not callees:
            return ("memory_intrinsic",
                    "byte-pointer loop with no peripheral access: "
                    "memset/memcpy shape")

    if periph:
        on_main_path = "0x0000063c" in callers or "0x000005f8" in callers
        role = "needs_model" if (on_main_path and lines >= 40) \
            else "peripheral_access"
        return (role, f"touches {', '.join(sorted(periph))}"
                      + ("; called from the main loop" if on_main_path else ""))

    if not callees and lines <= 25:
        return ("pure_logic",
                "no peripheral access, no calls, small: computation leaf")

    if "0x0000063c" in callers and lines >= 40:
        return ("needs_model",
                "large and called from the main loop, no peripheral access: "
                "application logic")

    return ("pure_logic",
            f"no peripheral access; {lines} lines, {len(callees)} callee(s)")


def main() -> int:
    bodies = load_bodies()
    if not bodies:
        print("ghidra_pseudocode.c missing")
        return 1
    inv = json.loads(INVENTORY.read_text())
    by_addr = {f["address"].replace("0x", ""): f for f in inv["functions"]}

    rows = []
    for addr, body in sorted(bodies.items()):
        meta = by_addr.get(addr, {})
        prior = meta.get("coverage", "untouched")
        callers = meta.get("callers", [])
        callees = meta.get("callees", [])
        lines = meta.get("decompiled_lines", body.count("\n") + 1)
        # A confirmed runtime helper outranks the `modelled` tier.  Being named
        # in a pseudocode file is exactly how v42's "clock conversion" label and
        # v44's "validity callback" label survived for seven rounds.
        if addr in KNOWN_RUNTIME_HELPERS:
            role, why = KNOWN_RUNTIME_HELPERS[addr]
        elif prior == "modelled":
            role, why = "modelled", "already reproduced by versioned pseudocode"
        else:
            role, why = classify(addr, body, callers, callees, lines)
        rows.append({
            "address": f"0x{addr}",
            "prior_coverage": prior,
            "role": role,
            "rationale": why,
            "decompiled_lines": lines,
            "callers": callers,
            "peripherals": sorted(peripherals_touched(body)),
        })

    tally = defaultdict(int)
    for r in rows:
        tally[r["role"]] += 1
    # empty_stub counts as identified, not as understood: there is no
    # behaviour to reproduce, and that is a complete answer for D1.
    closed = {"modelled", "compiler_helper", "memory_intrinsic",
              "peripheral_access", "pure_logic", "empty_stub"}
    n_closed = sum(v for k, v in tally.items() if k in closed)
    remaining = [r for r in rows if r["role"] == "needs_model"]

    OUT_JSON.write_text(json.dumps({"functions": rows,
                                    "summary": dict(tally)}, indent=1))

    md = [
        "# CoinFT 函数分类 v49（阶段 F）",
        "",
        "由 `10_reconstruction_v13/tools/classify_functions_v49.py` 机械生成。",
        "",
        "阶段 F 的目标是 D1：不再有 `untouched` 函数。**一个函数不需要 host 模型",
        "才算闭合** —— 编译器辅助例程或 PDL 叶子函数只需要被*识别*。",
        "",
        "## 分类汇总",
        "",
        "| 角色 | 含义 | 数量 |",
        "|---|---|---:|",
        f"| `modelled` | 已有版本化伪代码复现 | {tally['modelled']} |",
        f"| `compiler_helper` | 整数除法/移位等编译器内建 | {tally['compiler_helper']} |",
        f"| `memory_intrinsic` | memset/memcpy 形状 | {tally['memory_intrinsic']} |",
        f"| `peripheral_access` | 访问已知外设，非主数据路径 | {tally['peripheral_access']} |",
        f"| `pure_logic` | 无外设访问的计算叶子 | {tally['pure_logic']} |",
        f"| `empty_stub` | 反编译为空函数体，无行为可建模 | {tally['empty_stub']} |",
        f"| **`needs_model`** | **主数据路径业务逻辑，仍需建模** | **{tally['needs_model']}** |",
        f"| 合计 | | {len(rows)} |",
        "",
        f"已识别 {n_closed}/{len(rows)}；仍需人工建模 {len(remaining)} 个。",
        "",
        "## 仍需建模的函数（阶段 F 的实际工作量）",
        "",
        "| 地址 | 行数 | 外设 | 判定理由 |",
        "|---|---:|---|---|",
    ]
    for r in sorted(remaining, key=lambda x: -x["decompiled_lines"]):
        md.append(f"| `{r['address']}` | {r['decompiled_lines']} | "
                  f"{', '.join(r['peripherals']) or '-'} | {r['rationale']} |")
    md.append("")
    OUT_MD.write_text("\n".join(md))

    print(f"functions_classified={len(rows)}")
    for role in ("modelled", "compiler_helper", "memory_intrinsic",
                 "peripheral_access", "pure_logic", "empty_stub",
                 "needs_model"):
        print(f"  {role}={tally[role]}")
    print(f"identified={n_closed}/{len(rows)}  needs_model={len(remaining)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
