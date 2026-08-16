#!/usr/bin/env python3
"""Change-impact analyzer: what can an edit to this function affect?

Given a function (address or name), joins every reachability source we
hold:
  - static callers (instruction index) and dispatch-contract membership;
  - which state machines contain it (derived State names);
  - which ROUTES actually executed it (build/profiles corpus from
    tools/build_profile_corpus.py) and in which game-mode contexts;
  - which regression CONTRACTS exercise those routes — the required
    pre-merge gates.

Coverage caveat: the corpus only proves where the function DID run;
absence from all profiles means "unproven", not "unreachable" — check
the coverage number before trusting a small blast radius.

usage: python tools/impact.py BFC745
       python tools/impact.py Player_HandleHitEvents --json out.json
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import atlas  # noqa: E402
from structure import resolve_function  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
PROFILES = REPO / "build" / "profiles"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("query")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    function, rows, code_names = resolve_function(args.query)
    entry_addr = min(atlas.row_address(r) for r in rows
                     if r["function"] == function)
    entry = code_names.get(entry_addr)
    display = entry["name"] if entry and entry.get("name") else function
    out: dict = {"function": function, "address": f"0x{entry_addr:06X}",
                 "name": display}
    print(f"=== impact of {display} (0x{entry_addr:06X}) ===")

    # static callers
    callers = sorted({r["function"] for r in rows
                      if function in r["pseudocode"]
                      and r["function"] != function})
    out["static_callers"] = callers
    print(f"\nstatic callers ({len(callers)}):")
    for label in callers[:15]:
        caller_addr = int(label[5:], 16) if label.startswith("CODE_") else None
        caller = code_names.get(caller_addr) if caller_addr else None
        print(f"  {label}" + (f"  ({caller['name']})" if caller and
                              caller.get("name") else ""))

    # dispatch membership + state machine
    dispatch_roles = []
    for dispatch in atlas.load_dispatches():
        if dispatch["site"] == entry_addr or any(
                t == entry_addr for t in dispatch["targets"]):
            role = "site" if dispatch["site"] == entry_addr else "target"
            dispatch_roles.append(
                {"role": role, "site": f"0x{dispatch['site']:06X}",
                 "kind": dispatch["kind"], "cfg": dispatch["cfg"]})
    out["dispatch"] = dispatch_roles
    for d in dispatch_roles:
        print(f"dispatch {d['role']} of {d['site']} ({d['kind']}) "
              f"[{d['cfg']}]")
    if entry and entry.get("provenance") == "table-derived":
        print(f"state machine: {entry['name'].rsplit('_State', 1)[0]} "
              f"(via {entry.get('via_site')})")

    # runtime corpus
    executed_in = []
    for profile_path in sorted(PROFILES.glob("*.profile.jsonl")):
        for line in profile_path.read_text(errors="replace").splitlines():
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue
            if int(row["pc24"], 16) == entry_addr:
                executed_in.append(
                    {"route": profile_path.stem.replace(".profile", ""),
                     "calls": row["calls"], "contexts": row["contexts"],
                     "frames": [row["first_frame"], row["last_frame"]]})
                break
    out["executed_in_routes"] = executed_in
    print(f"\nruntime evidence ({len(executed_in)} routes in corpus):")
    for e in executed_in:
        print(f"  {e['route']}: {e['calls']}x, contexts {e['contexts']}, "
              f"frames {e['frames'][0]}..{e['frames'][1]}")
    if not executed_in:
        print("  NONE — unproven reachability, not proof of dead code")

    # required regression gates: contracts whose script (or quickload leg)
    # matches an executing route
    routes = {e["route"] for e in executed_in}
    required = []
    for contract_path in sorted((REPO / "contracts").glob("*.json")):
        try:
            contract = json.loads(contract_path.read_text())
        except json.JSONDecodeError:
            continue
        if "checkpoints" not in contract:
            continue
        scripts = [contract.get("script", "")]
        if contract.get("quickload"):
            scripts.append(contract["quickload"].get("script", ""))
        if any(Path(s).stem in routes for s in scripts if s):
            required.append(contract.get("name", contract_path.stem))
    out["required_gates"] = required
    print(f"\nrequired regression gates: "
          f"{', '.join(required) if required else '(none cover it — '
          'consider recording a route/contract before editing)'}")
    print("full sweep advisable: python tools/level_sweep.py")

    if args.json:
        args.json.write_text(json.dumps(out, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
