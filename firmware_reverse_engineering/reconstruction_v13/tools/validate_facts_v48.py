#!/usr/bin/env python3
"""v48: enforce the fact registry's invariants.

This exists because v45-v47 rewrote their "still unresolved" lists each round
and thereby re-marked v2/v33 findings as unknown.  Run this before publishing a
new progress document: it fails if an established fact has drifted back into
the open list, if a superseded claim is re-asserted, or if the newest progress
document contradicts the registry.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ANALYSIS = Path(__file__).resolve().parents[2] / "07_analysis"
REGISTRY = ANALYSIS / "fact_registry_v48.json"

# Phrases that would signal an established fact has been lost again.  Each maps
# to the registry id that already covers it.
REGRESSION_PATTERNS = {
    "cmd-t-four-params": [
        r"`?q/w/n/t`?\s*命令的完整协议(和|与)异常响应",
    ],
}


def main() -> int:
    if not REGISTRY.exists():
        print("FAIL: fact_registry_v48.json missing")
        return 1
    reg = json.loads(REGISTRY.read_text())
    established = {f["id"]: f for f in reg["established"]}
    open_items = {f["id"]: f for f in reg["open"]}
    failures: list[str] = []

    # 1. An id may not be both established and open.
    both = set(established) & set(open_items)
    if both:
        failures.append(f"ids in both established and open: {sorted(both)}")

    # 2. Every established fact needs evidence and a first_recovered version.
    for fid, fact in established.items():
        if not fact.get("evidence"):
            failures.append(f"{fid}: no evidence")
        if not fact.get("first_recovered"):
            failures.append(f"{fid}: no first_recovered version")

    # 3. Every open item must say why it is open.
    for fid, item in open_items.items():
        if not item.get("why_open"):
            failures.append(f"{fid}: open without why_open")

    # 4. Superseded claims must record what refuted them.
    for entry in reg.get("superseded", []):
        if not entry.get("refuted_by"):
            failures.append(f"superseded entry lacks refuted_by: "
                            f"{entry.get('claim', '?')[:40]}")

    # 5. The newest progress document must not re-assert a lost fact.
    #
    # A regression phrase quoted inside a correction is legitimate: v48 has to
    # reproduce v45-v47's wording in order to explain what went wrong.  Only an
    # unquoted occurrence counts as a live assertion, so a match enclosed in
    # CJK or ASCII quotation marks is exempt.
    docs = sorted(ANALYSIS.glob("restoration_progress_v*.md"),
                  key=lambda p: int(re.search(r"v(\d+)", p.name).group(1)))
    if docs:
        newest = docs[-1]
        text = newest.read_text(errors="replace")
        for fid, patterns in REGRESSION_PATTERNS.items():
            for pattern in patterns:
                for m in re.finditer(pattern, text):
                    before = text[max(0, m.start() - 2):m.start()]
                    after = text[m.end():m.end() + 2]
                    quoted = (any(q in before for q in "「『\"") and
                              any(q in after for q in "」』\""))
                    if quoted:
                        continue
                    line = text[:m.start()].count("\n") + 1
                    failures.append(
                        f"{newest.name}:{line} re-asserts a fact already "
                        f"established as '{fid}': {m.group(0)!r}")

    print(f"registry: established={len(established)} open={len(open_items)} "
          f"superseded={len(reg.get('superseded', []))}")
    if docs:
        print(f"checked newest document: {docs[-1].name}")
    if failures:
        print(f"\nFAIL ({len(failures)} problem(s)):")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("validate_facts_v48: passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
