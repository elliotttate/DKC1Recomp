#!/usr/bin/env python3
"""DKC1 code atlas: one query joining every knowledge source by address.

The project holds five views of the same byte-exact program — the labeled
disassembly, the mechanical pseudocode lift, the IDA database's curated
names/descriptions, the live recompiled C, and the runtime evidence
(dispatch contracts, known issues). They are all keyed by 24-bit address,
so this tool answers "what is $BFB27C?" or "what is $7E1595?" from ALL of
them at once. Debug output (invariant verdicts, click-to-provenance, trace
PCs, Dkc1ResumePc) feeds straight in.

usage:
  python tools/atlas.py BFB27C          # code address
  python tools/atlas.py 7E1595          # WRAM address (also: 1595, wram:1595)
  python tools/atlas.py name:collision  # search names, labels, descriptions
  python tools/atlas.py BFB27C --callers --json out.json
"""
from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
# Prefer the consolidated in-repo copy; fall back to the original layout.
_DISASM_CANDIDATES = [REPO / "reference" / "disassembly",
                      Path(r"D:\Downloads\DKLR\DKC1_Disassembly")]
DISASM_ROOT = next((p for p in _DISASM_CANDIDATES if p.exists()),
                   _DISASM_CANDIDATES[0])
PSEUDO = DISASM_ROOT / "DKC1" / "Pseudocode"
RENAME_MAP = DISASM_ROOT / "Tools" / "IDA" / "work" / "rename_map.json"
RAM_MAP = DISASM_ROOT / "DKC1" / "RAM_Map_DKC1.asm"


def load_rename_map():
    try:
        data = json.loads(RENAME_MAP.read_text(encoding="utf-8"))
    except OSError:
        return {}, {}
    code = {int(entry["ea"], 16): entry for entry in data.get("code", [])}
    ram = {int(entry["ea"], 16) & 0x1FFFF: entry
           for entry in data.get("ram", [])}
    return code, ram


def load_ram_map_labels():
    labels = {}
    try:
        text = RAM_MAP.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return labels
    for match in re.finditer(r"^!(\S+)\s*=\s*\$([0-9A-Fa-f]+)", text,
                             re.M):
        labels.setdefault(int(match.group(2), 16) & 0x1FFFF,
                          []).append(match.group(1))
    return labels


def iter_instruction_index():
    path = PSEUDO / "instruction_index.csv"
    with path.open(encoding="utf-8", errors="replace", newline="") as f:
        yield from csv.DictReader(f)


def row_address(row) -> int:
    bank, _, offset = row["address"].partition(":")
    return (int(bank, 16) << 16) | int(offset, 16)


def load_dispatches():
    dispatches = []
    pattern = re.compile(
        r"^indirect_dispatch\s+([0-9A-Fa-f]{4})\s+\d+\s+(\S+)(.*?)"
        r"targets:([0-9A-Fa-f,]+)", re.M)
    for cfg in sorted((REPO / "recomp").glob("bank*.cfg")):
        bank = int(cfg.stem.replace("bank", ""), 16)
        for match in pattern.finditer(cfg.read_text(errors="replace")):
            dispatches.append({
                "site": (bank << 16) | int(match.group(1), 16),
                "kind": match.group(2),
                "attrs": match.group(3).strip(),
                "targets": [int(t, 16) for t in match.group(4).split(",")],
                "cfg": cfg.name})
    return dispatches


def find_generated(address: int):
    """Locate the live recompiled variant(s) for a code address."""
    needle = f"cpu_trace_func_entry(cpu, 0x{address:06X}"
    bank = (address >> 16) & 0xFF
    hits = []
    candidates = sorted((REPO / "generated" / "snesrecomp").glob(
        f"bank{bank:02x}_*.c"))
    candidates += [REPO / "generated" / "snesrecomp" / "dispatch_v2.c"]
    for path in candidates:
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for match in re.finditer(re.escape(needle) + r', "([^"]+)"', text):
            hits.append({"file": path.name, "variant": match.group(1)})
    return hits


def known_issue_mentions(token: str):
    try:
        issues = json.loads((REPO / "docs" / "KNOWN_ISSUES.json").read_text())
    except OSError:
        return []
    return [issue["id"] for issue in issues.get("issues", [])
            if token.lower() in json.dumps(issue).lower()]


def show_code(address: int, want_callers: bool, out: dict):
    code_names, _ = load_rename_map()
    out["address"] = f"0x{address:06X}"
    out["kind"] = "code"

    # Containing function + disassembly/pseudocode from the index.
    rows = list(iter_instruction_index())
    by_addr = {row_address(r): r for r in rows}
    row = by_addr.get(address)
    containing = None
    if row is None:
        # mid-instruction or data: nearest preceding row in the same bank
        prior = [a for a in by_addr if a <= address and
                 (a >> 16) == (address >> 16)]
        if prior:
            row = by_addr[max(prior)]
    if row is not None:
        containing = row["function"]
    out["function_label"] = containing

    func_addr = None
    if containing and containing.startswith("CODE_"):
        func_addr = int(containing[5:], 16)
    entry = code_names.get(address) or (
        code_names.get(func_addr) if func_addr else None)
    if entry:
        out["name"] = entry.get("name")
        out["description"] = entry.get("desc")

    if containing:
        func_rows = [r for r in rows if r["function"] == containing]
        listing = []
        for r in func_rows:
            marker = ">>" if row_address(r) == address else "  "
            listing.append(f"{marker} {r['address']}  "
                           f"{r['assembly']:<28} {r['pseudocode']}")
        out["listing"] = listing
        out["source_line"] = row["source_line"] if row else None

    # Dispatch contract membership.
    out["dispatch"] = []
    for d in load_dispatches():
        if d["site"] == address:
            out["dispatch"].append(
                {"role": "site", "kind": d["kind"], "attrs": d["attrs"],
                 "targets": [f"0x{t:06X}" for t in d["targets"]],
                 "cfg": d["cfg"]})
        elif address in d["targets"]:
            out["dispatch"].append(
                {"role": "target", "site": f"0x{d['site']:06X}",
                 "kind": d["kind"], "cfg": d["cfg"]})

    out["generated"] = find_generated(
        func_addr if func_addr is not None else address)
    out["known_issues"] = known_issue_mentions(f"{address:06X}") + \
        known_issue_mentions(f"${address & 0xFFFF:04X}")

    if want_callers and containing:
        callers = sorted({r["function"] for r in rows
                          if containing in r["pseudocode"]
                          and r["function"] != containing})
        named = []
        for label in callers:
            caller_addr = int(label[5:], 16) if label.startswith("CODE_") \
                else None
            caller_entry = code_names.get(caller_addr) if caller_addr else None
            named.append(label + (f"  ({caller_entry['name']})"
                                  if caller_entry else ""))
        out["callers"] = named


def show_wram(address: int, out: dict):
    address &= 0x1FFFF
    _, ram_names = load_rename_map()
    out["address"] = f"$7E{address:04X}"
    out["kind"] = "wram"
    entry = ram_names.get(address)
    if entry:
        out["name"] = entry.get("name")
        out["description"] = entry.get("desc")
    out["ram_map_labels"] = load_ram_map_labels().get(address, [])

    # Actor-array membership: the 26 per-slot word arrays span base..base+0x32.
    for base, name in sorted(
            (int(e["ea"], 16) & 0x1FFFF, e.get("name", ""))
            for e in ram_names.values()):
        if base <= address <= base + 0x32 and name.startswith(
                ("Spr", "Player")):
            slot = address - base
            if slot % 2 == 0 and slot > 0:
                out.setdefault("array_note", []).append(
                    f"= {name}[slot index 0x{slot:02X}]")

    # Code that touches this address. Labeled operands appear as
    # !RAM_DKC1_* in the disassembly, raw ones as $xxxx — search both.
    tokens = [f"${address:04X}"]
    tokens += [f"!{label}" for label in out["ram_map_labels"]]
    touchers = {}
    code_names, _ = load_rename_map()
    for r in iter_instruction_index():
        assembly = r["assembly"]
        if any(token in assembly for token in tokens):
            label = r["function"]
            touchers.setdefault(label, 0)
            touchers[label] += 1
    ranked = sorted(touchers.items(), key=lambda kv: -kv[1])[:14]
    named = []
    for label, count in ranked:
        func_addr = int(label[5:], 16) if label.startswith("CODE_") else None
        entry = code_names.get(func_addr) if func_addr else None
        named.append(f"{label} x{count}" +
                     (f"  ({entry['name']})" if entry else ""))
    out["accessed_by"] = named
    out["known_issues"] = known_issue_mentions(f"{address:04X}")


def search_names(term: str, out: dict):
    code_names, ram_names = load_rename_map()
    term_lower = term.lower()
    out["kind"] = "search"
    out["code"] = [
        {"ea": f"0x{ea:06X}", "name": e.get("name"),
         "desc": (e.get("desc") or "")[:120]}
        for ea, e in sorted(code_names.items())
        if term_lower in json.dumps(e).lower()]
    out["wram"] = [
        {"ea": f"$7E{ea:04X}", "name": e.get("name"),
         "desc": (e.get("desc") or "")[:120]}
        for ea, e in sorted(ram_names.items())
        if term_lower in json.dumps(e).lower()]


def print_report(out: dict):
    def line(text=""):
        print(text)

    if out["kind"] == "search":
        for section in ("code", "wram"):
            if out[section]:
                line(f"-- {section} matches --")
                for e in out[section]:
                    line(f"  {e['ea']}  {e['name']}")
                    if e["desc"]:
                        line(f"      {e['desc']}")
        if not out["code"] and not out["wram"]:
            line("no matches")
        return

    line(f"=== {out['address']} ({out['kind']}) ===")
    if out.get("name"):
        line(f"name: {out['name']}")
    if out.get("description"):
        line(f"desc: {out['description']}")
    if out.get("ram_map_labels"):
        line(f"disasm labels: {', '.join(out['ram_map_labels'])}")
    for note in out.get("array_note", []):
        line(f"array: {note}")
    if out.get("function_label"):
        line(f"function: {out['function_label']}"
             + (f" (source line {out['source_line']})"
                if out.get("source_line") else ""))
    for d in out.get("dispatch", []):
        if d["role"] == "site":
            line(f"dispatch SITE ({d['kind']} {d['attrs']}) "
                 f"[{d['cfg']}] -> {len(d['targets'])} runtime-proven "
                 f"targets:")
            line("  " + " ".join(d["targets"]))
        else:
            line(f"dispatch TARGET of {d['site']} ({d['kind']}) "
                 f"[{d['cfg']}]")
    for g in out.get("generated", []):
        line(f"recomp: {g['variant']}  ({g['file']})")
    if out.get("callers") is not None:
        line("callers:")
        for c in out["callers"] or ["(none found in static index)"]:
            line(f"  {c}")
    if out.get("accessed_by"):
        line("accessed by:")
        for a in out["accessed_by"]:
            line(f"  {a}")
    if out.get("known_issues"):
        line(f"known issues mentioning this: "
             f"{', '.join(out['known_issues'])}")
    if out.get("listing"):
        line("")
        line("-- disassembly + pseudocode --")
        listing = out["listing"]
        if len(listing) > 60:
            marked = next((i for i, l in enumerate(listing)
                           if l.startswith(">>")), 0)
            lo = max(0, marked - 25)
            shown = listing[lo:lo + 55]
            if lo:
                line(f"  ... {lo} earlier instructions ...")
            listing = shown
        for l in listing:
            line(l)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("query")
    parser.add_argument("--callers", action="store_true")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    out: dict = {}
    query = args.query.strip()
    if query.lower().startswith("name:"):
        search_names(query[5:], out)
    else:
        text = query.lower().replace("wram:", "").replace("code:", "")
        text = text.replace("$", "").replace("0x", "")
        if text.startswith("7e"):
            text = text[2:]
            forced_wram = True
        else:
            forced_wram = query.lower().startswith("wram:")
        value = int(text, 16)
        if forced_wram or value < 0x020000:
            show_wram(value, out)
        else:
            show_code(value, args.callers, out)

    print_report(out)
    if args.json:
        args.json.write_text(json.dumps(out, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
