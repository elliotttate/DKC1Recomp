#!/usr/bin/env python3
"""Coverage explorer: the ranked what-to-prove-next worklist.

Joins the full entrance universe (the disassembly's 256 EntranceID
defines) against every piece of evidence we hold — the capability
manifest's per-scene verdicts and the sweep report's route reach — into
one honest coverage picture:

  proven          widescreen proven in this scene
  degraded        widened but with raw fallbacks/blanks
  centered-only   a route reaches it, only pillarboxed evidence
  never-observed  NO route reaches this entrance (absence of evidence,
                  not proof of anything)

The worklist ranks by cheapness of the next unit of evidence:
centered-only scenes first (a route already exists — record a
widescreen leg), then unobserved *_Main levels (real gameplay scenes),
then the tail (bonus rooms, transitions, unused ids).

usage: python tools/coverage_explorer.py [--md docs/COVERAGE.md]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from ir import memtype  # noqa: E402

REPO = TOOLS.parent


def aggregate_status(statuses: list[str]) -> str:
    """Conservatively summarize every observed scene for an entrance."""
    if statuses and all(status == "proven" for status in statuses):
        return "proven"
    if "degraded" in statuses:
        return "degraded"
    if "centered" in statuses or "centered-only" in statuses:
        return "centered-only"
    return "reached-unmeasured"


def load_entrances() -> dict[int, list[str]]:
    resolver = memtype.Resolver()
    universe: dict[int, list[str]] = {}
    for name, value in resolver.defines.items():
        match = re.fullmatch(r"Define_DKC1_EntranceID_(\w+)", name)
        if match and 0 <= value <= 0xFF:
            universe.setdefault(value, []).append(match.group(1))
    return universe


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capabilities", type=Path,
                        default=REPO / "docs/CAPABILITIES.json")
    parser.add_argument("--sweep", type=Path,
                        default=REPO / "build/sweep/report.json")
    parser.add_argument("--json", type=Path,
                        default=REPO / "build/coverage.json")
    parser.add_argument("--md", type=Path,
                        default=REPO / "docs/COVERAGE.md")
    args = parser.parse_args()

    universe = load_entrances()
    try:
        capabilities = json.loads(args.capabilities.read_text())["scenes"]
    except OSError:
        capabilities = []
    try:
        sweep_routes = json.loads(args.sweep.read_text())["routes"]
    except OSError:
        sweep_routes = {}

    # entrance -> evidence
    by_entrance: dict[int, dict] = {}
    for scene in capabilities:
        match = re.fullmatch(r"\((\d+), (\d+), (\d+), (\d+)\)",
                             scene["scene"])
        if not match:
            continue
        entrance = int(match.group(2))
        slot = by_entrance.setdefault(entrance, {
            "statuses": [], "scenes": [], "routes": set()})
        slot["statuses"].append(scene["host_widescreen"])
        slot["scenes"].append(scene["scene"])
        slot["routes"].update(scene.get("evidence_routes", []))
    for route, entry in sweep_routes.items():
        for scene_key in entry.get("scenes", {}):
            match = re.fullmatch(r"\((\d+), (\d+), (\d+), (\d+)\)",
                                 scene_key)
            if match:
                slot = by_entrance.setdefault(int(match.group(2)), {
                    "statuses": [], "scenes": [], "routes": set()})
                slot["routes"].add(route)

    rows = []
    for value in range(0x100):
        names = universe.get(value, [])
        evidence = by_entrance.get(value)
        if evidence:
            statuses = evidence["statuses"]
            status = aggregate_status(statuses)
            routes = sorted(evidence["routes"])
        else:
            status = "never-observed"
            routes = []
        rows.append({"entrance": value,
                     "names": names or ["(no define)"],
                     "status": status, "routes": routes})

    counts: dict[str, int] = {}
    for row in rows:
        counts[row["status"]] = counts.get(row["status"], 0) + 1

    def is_main(row) -> bool:
        return any(re.search(r"_Main\d*$", n) for n in row["names"])

    def actionable(row) -> bool:
        # Invalid* are sentinel ids, not scenes anyone can prove
        return row["names"] != ["(no define)"] and not any(
            n.startswith("Invalid") for n in row["names"])

    worklist = (
        [r for r in rows if r["status"] in ("centered-only",
                                            "reached-unmeasured",
                                            "degraded")
         and actionable(r)]
        + [r for r in rows if r["status"] == "never-observed"
           and is_main(r) and actionable(r)]
        + [r for r in rows if r["status"] == "never-observed"
           and not is_main(r) and actionable(r)]
    )

    payload = {
        "schema": "dkc1.coverage.v1",
        "note": ("never-observed means no route reaches the entrance — "
                 "absence of evidence. Regenerate after level_sweep + "
                 "capability_manifest."),
        "counts": counts,
        "entrances": rows,
        "worklist": [r["entrance"] for r in worklist[:40]],
    }
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(payload, indent=1))

    lines = ["# Widescreen evidence coverage", "",
             "Generated by tools/coverage_explorer.py — do not edit.", ""]
    lines.append("| status | entrances |")
    lines.append("|---|---|")
    for status in ("proven", "degraded", "centered-only",
                   "reached-unmeasured", "never-observed"):
        if counts.get(status):
            lines.append(f"| {status} | {counts[status]} |")
    lines += ["", "## Next-evidence worklist (cheapest first)", ""]
    for row in worklist[:25]:
        names = ", ".join(row["names"][:2])
        via = f" via {', '.join(row['routes'][:2])}" if row["routes"] \
            else ""
        action = {
            "centered-only": "record a widescreen leg of the existing "
                             "route",
            "degraded": "fix fallbacks/blanks, then re-sweep",
            "reached-unmeasured": "add ws-trace to the reaching route",
            "never-observed": "record a route (fresh-entry sweep can "
                              "seed a snapshot)",
        }[row["status"]]
        lines.append(f"- `${row['entrance']:02X}` **{names}** — "
                     f"{row['status']}{via}: {action}")
    lines += ["", f"Full table: build/coverage.json "
                  f"({len(rows)} entrances)."]
    args.md.write_text("\n".join(lines) + "\n")

    print(f"coverage: {counts} -> {args.md}, {args.json}")
    print("top of worklist:")
    for row in worklist[:8]:
        print(f"  ${row['entrance']:02X} {row['names'][0]:40} "
              f"{row['status']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
