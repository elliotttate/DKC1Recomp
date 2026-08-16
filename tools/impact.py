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
import re
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import atlas  # noqa: E402
from structure import resolve_function  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
PROFILES = REPO / "build" / "profiles"
CALL_GRAPH = atlas.RENAME_MAP.parent / "functions.json"
HEX_ADDRESS = re.compile(r"^(?:0x)?([0-9A-Fa-f]{6})$")


def _parse_address(value: object) -> int | None:
    match = HEX_ADDRESS.fullmatch(str(value))
    return int(match.group(1), 16) if match else None


def _function_label_at(rows: list[dict], address: int) -> str | None:
    row = next((item for item in rows
                if atlas.row_address(item) == address), None)
    return row["function"] if row else None


def structured_static_callers(
        entry_addr: int, rows: list[dict], path: Path = CALL_GRAPH,
) -> tuple[list[str], list[dict], str] | None:
    """Resolve callers from the exported IDA call graph.

    ``functions.json`` carries actual code xrefs. Its caller values may be a
    function name or an instruction address inside the caller, so map either
    form back through the instruction atlas without searching pseudocode text.
    ``None`` means no structured entry was available and lets the caller use
    the exact-assembly fallback below.
    """
    try:
        graph = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    if not isinstance(graph, list):
        return None
    entries = [item for item in graph if isinstance(item, dict)]
    target = next((item for item in entries
                   if _parse_address(item.get("ea")) == entry_addr), None)
    if target is None or not isinstance(target.get("callers"), list):
        return None
    by_name = {str(item.get("name")): item for item in entries
               if item.get("name")}

    evidence = []
    for raw in target["callers"]:
        token = str(raw)
        callsite = _parse_address(token)
        owner = None
        if callsite is None:
            owner = by_name.get(token)
        else:
            # IDA exports the containing function name when it knows it, and
            # the exact xref address otherwise. Recover the containing range.
            owner = next((item for item in entries
                          if (_parse_address(item.get("ea")) is not None and
                              _parse_address(item.get("ea")) <= callsite <
                              _parse_address(item.get("ea")) +
                              int(item.get("size", 0)))), None)
        owner_addr = (_parse_address(owner.get("ea"))
                      if owner is not None else None)
        label = (_function_label_at(rows, owner_addr)
                 if owner_addr is not None else None)
        if label is None and callsite is not None:
            label = _function_label_at(rows, callsite)
        if label is None:
            label = str(owner.get("name")) if owner is not None else token
        evidence.append({
            "function": label,
            "name": owner.get("name") if owner is not None else None,
            "address": (f"0x{owner_addr:06X}"
                        if owner_addr is not None else None),
            "xref": token,
        })
    labels = sorted({item["function"] for item in evidence})
    evidence.sort(key=lambda item: (item["function"], item["xref"]))
    return labels, evidence, str(path)


def assembly_static_callers(
        function: str, entry_addr: int, rows: list[dict],
) -> tuple[list[str], list[dict], str]:
    """Fallback using exact control-flow operands, never text substrings."""
    target_labels = {function, f"CODE_{entry_addr:06X}"}
    callers = []
    evidence = []
    direct = re.compile(
        r"^\s*(?:JSR|JSL|JMP|JML)(?:\.[bwl])?\s+"
        r"(?P<target>[A-Za-z_]\w*|\$[0-9A-Fa-f]{6})\s*$",
        re.I,
    )
    for row in rows:
        match = direct.fullmatch(row.get("assembly", ""))
        if not match:
            continue
        target = match.group("target")
        target_address = (int(target[1:], 16)
                          if target.startswith("$") else None)
        if target not in target_labels and target_address != entry_addr:
            continue
        if row["function"] == function:
            continue
        callers.append(row["function"])
        evidence.append({
            "function": row["function"],
            "address": f"0x{atlas.row_address(row):06X}",
            "instruction": row["assembly"],
        })
    return (sorted(set(callers)), evidence,
            "instruction_index.csv exact control-flow operands")


def static_callers(
        function: str, entry_addr: int, rows: list[dict],
        path: Path = CALL_GRAPH,
) -> tuple[list[str], list[dict], str]:
    structured = structured_static_callers(entry_addr, rows, path)
    if structured is not None:
        return structured
    return assembly_static_callers(function, entry_addr, rows)


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

    # Static callers: prefer the exported IDA xref/call graph. A missing
    # graph falls back to exact control-flow operands from the instruction
    # atlas; loose pseudocode substring matches are never caller evidence.
    callers, caller_evidence, caller_source = static_callers(
        function, entry_addr, rows)
    out["static_callers"] = callers
    out["static_caller_evidence"] = caller_evidence
    out["static_callers_source"] = caller_source
    print(f"\nstatic callers ({len(callers)}):")
    print(f"  source: {caller_source}")
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

    # IR data-level reachability (stage 4 summaries): what this function
    # writes, and which functions read those addresses — the paths an
    # edit can affect WITHOUT any call relationship.
    summaries_path = REPO / "build" / "ir" / "summaries.json"
    try:
        summaries = json.loads(summaries_path.read_text())["functions"]
    except OSError:
        summaries = None
    if summaries and function in summaries:
        mine = summaries[function]
        writes = [a for a in mine["writes"] if a["region"] == "wram"]
        out["ir_writes"] = writes
        shown = {a.get("sym") or a["ea"] for a in writes}
        print(f"\nIR write set ({len(writes)} wram stores"
              f"{', +indirect' if mine['indirect_writes'] else ''}): "
              f"{', '.join(sorted(shown)[:10])}"
              + (" ..." if len(shown) > 10 else ""))
        coupled: dict[str, int] = {}
        spans = []
        for a in writes:
            ea = int(a["ea"], 16)
            spans.append((ea, ea + (0x33 if a["indexed"] else
                                    2 if a["width"] == 16 else 1)))
        for label, s in summaries.items():
            if label == function:
                continue
            count = 0
            for access in s["reads"]:
                if access["region"] != "wram":
                    continue
                ea = int(access["ea"], 16)
                hi = ea + (0x33 if access["indexed"] else
                           2 if access["width"] == 16 else 1)
                if any(lo < hi and ea < shi for lo, shi in spans):
                    count += 1
            if count:
                coupled[label] = count
        out["data_coupled_readers"] = len(coupled)
        top = sorted(coupled.items(), key=lambda kv: -kv[1])[:8]
        print(f"data-coupled readers (share written addresses): "
              f"{len(coupled)} functions"
              + (f"; top: " + ", ".join(f"{k} x{v}" for k, v in top)
                 if top else ""))

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
