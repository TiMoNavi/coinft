#!/usr/bin/env python3
"""v48: build a canonical function inventory from the Ghidra export.

The v45-v47 rounds lost established facts because every round rewrote its own
"still unresolved" list from memory, with no durable per-function record.  This
tool derives the inventory mechanically from the decompiler output plus the
documentation, so a fact can only leave the inventory if the evidence does.
"""

from __future__ import annotations

import json
import re
from collections import defaultdict
from pathlib import Path

ANALYSIS = Path(__file__).resolve().parents[2] / "07_analysis"
GHIDRA = ANALYSIS / "ghidra_pseudocode.c"
OUT_JSON = ANALYSIS / "function_inventory_v48.json"
OUT_MD = ANALYSIS / "function_inventory_v48.md"

FUNC_RE = re.compile(r"\bFUN_([0-9a-fA-F]{8})\b")
# A definition looks like `type FUN_00001234(args)` at column 0 in the export.
DEF_RE = re.compile(r"^[A-Za-z_][\w \*]*\bFUN_([0-9a-fA-F]{8})\s*\(", re.M)


def load_ghidra() -> tuple[dict[str, int], dict[str, set[str]]]:
    """Return each function's decompiled line count and its callees."""
    if not GHIDRA.exists():
        return {}, {}
    text = GHIDRA.read_text(errors="replace")
    lines = text.splitlines()

    starts: list[tuple[int, str]] = []
    for i, line in enumerate(lines):
        m = DEF_RE.match(line + "\n")
        if m:
            starts.append((i, m.group(1).lower()))

    sizes: dict[str, int] = {}
    calls: dict[str, set[str]] = defaultdict(set)
    for idx, (start, addr) in enumerate(starts):
        end = starts[idx + 1][0] if idx + 1 < len(starts) else len(lines)
        body = "\n".join(lines[start:end])
        sizes[addr] = end - start
        for callee in FUNC_RE.findall(body):
            callee = callee.lower()
            if callee != addr:
                calls[addr].add(callee)
    return sizes, calls


def load_documentation() -> dict[str, set[str]]:
    """Which documents mention each function."""
    mentions: dict[str, set[str]] = defaultdict(set)
    for path in sorted(ANALYSIS.glob("*.md")):
        text = path.read_text(errors="replace")
        for addr in {a.lower() for a in FUNC_RE.findall(text)}:
            mentions[addr].add(path.name)
    for path in sorted(ANALYSIS.glob("coinft_firmware_pseudocode_v*.c")):
        text = path.read_text(errors="replace")
        for addr in {a.lower() for a in FUNC_RE.findall(text)}:
            mentions[addr].add(path.name)
    return mentions


def classify(addr: str, sizes: dict[str, int],
             mentions: dict[str, set[str]]) -> str:
    """Coverage tier for one function.

    `modelled` means a versioned pseudocode file reproduces its behaviour;
    `documented` means prose describes it but no host model exists;
    `named_only` means it is merely referenced; `untouched` means the
    decompiler found it and no document mentions it at all.
    """
    docs = mentions.get(addr, set())
    has_model = any(d.startswith("coinft_firmware_pseudocode_v") for d in docs)
    prose = {d for d in docs if d.endswith(".md")}
    if has_model:
        return "modelled"
    if len(prose) >= 2:
        return "documented"
    if docs:
        return "named_only"
    return "untouched"


def main() -> int:
    sizes, calls = load_ghidra()
    mentions = load_documentation()
    if not sizes:
        print("ghidra_pseudocode.c missing; inventory not built")
        return 1

    all_addrs = set(sizes) | set(mentions)
    callers: dict[str, set[str]] = defaultdict(set)
    for caller, callees in calls.items():
        for callee in callees:
            callers[callee].add(caller)

    rows = []
    for addr in sorted(all_addrs):
        tier = classify(addr, sizes, mentions)
        rows.append({
            "address": f"0x{addr}",
            "decompiled_lines": sizes.get(addr, 0),
            "in_ghidra_export": addr in sizes,
            "callees": sorted(f"0x{c}" for c in calls.get(addr, ())),
            "callers": sorted(f"0x{c}" for c in callers.get(addr, ())),
            "documents": sorted(mentions.get(addr, ())),
            "coverage": tier,
        })

    tiers = defaultdict(int)
    for row in rows:
        tiers[row["coverage"]] += 1
    total = len(rows)

    OUT_JSON.write_text(json.dumps({"functions": rows,
                                    "summary": dict(tiers)}, indent=1))

    lines = [
        "# CoinFT 固件函数清单 v48",
        "",
        "本文件由 `10_reconstruction_v13/tools/build_function_inventory_v48.py`",
        "从 `ghidra_pseudocode.c` 与全部分析文档机械生成，不手工编辑。",
        "它的存在目的是：让已确立的事实有持久载体，避免像 v45-v47 那样在",
        "重写「未闭合清单」时把早期已恢复的语义丢掉。",
        "",
        "## 覆盖度汇总",
        "",
        "| 层级 | 含义 | 数量 |",
        "|---|---|---:|",
        f"| `modelled` | 有版本化伪代码复现其行为 | {tiers['modelled']} |",
        f"| `documented` | 至少两份文档描述，但无 host 模型 | {tiers['documented']} |",
        f"| `named_only` | 仅被引用 | {tiers['named_only']} |",
        f"| `untouched` | 反编译器发现但无任何文档 | {tiers['untouched']} |",
        f"| **合计** | | **{total}** |",
        "",
        "## 按反编译行数排序的未建模函数",
        "",
        "这些是下一轮最值得投入的目标：体量大、且尚无 host 模型。",
        "",
        "| 地址 | 反编译行数 | 覆盖 | 调用者 |",
        "|---|---:|---|---|",
    ]
    pending = [r for r in rows
               if r["coverage"] in ("untouched", "named_only", "documented")
               and r["in_ghidra_export"]]
    pending.sort(key=lambda r: -r["decompiled_lines"])
    for row in pending[:40]:
        callers_txt = ", ".join(row["callers"][:4]) or "-"
        lines.append(f"| `{row['address']}` | {row['decompiled_lines']} | "
                     f"{row['coverage']} | {callers_txt} |")
    lines.append("")
    OUT_MD.write_text("\n".join(lines))

    print(f"functions_total={total}")
    for tier in ("modelled", "documented", "named_only", "untouched"):
        print(f"  {tier}={tiers[tier]}")
    print(f"wrote {OUT_JSON.name} and {OUT_MD.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
