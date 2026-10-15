#!/usr/bin/env python3
"""Source-owned generated adapters for co-op stable extended actor handles.

All storage and loop changes are runtime gated. Generated code is private.
The original literal field is retained before effective-address arithmetic,
which prevents an extended index from aliasing the following cartridge table.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re

TABLES = tuple(int(value, 16) for value in re.findall(
    r"COOP_ACTOR_FIELD\((0x[0-9a-fA-F]+)\)",
    (Path(__file__).resolve().parents[1] / "runner/dkc1_coop_fields.def").read_text()))

FIELD_LITERALS = {base + delta for base in TABLES for delta in (-1, 0, 1)}
INCLUDE = '#include "dkc1_coop_entities.h"'

# ROM-proven actor iterations. Allocators retain their native loops and use
# a fallback hook instead. In particular, $B882C2's #$32 is a lives count.
FORWARD_LOOPS = (
    ("CODE_80A1E6_M0X0", "A1E9", "X", 0x34),
    ("CODE_80A1F4_M0X0", "A1F7", "X", 0x34),
    ("CODE_80CF2D_M0X0", "CF44", "X", 0x34),
    ("CODE_80E225_M0X0", "E465", "X", 0x34),
    ("CODE_80E37B_M0X0", "E465", "X", 0x34),
    ("CODE_80E695_M0X0", "E6A0", "Y", 0x34),
    ("CODE_B5833B_M0X0", "834A", "X", 0x34),
    ("CODE_B696F6_M0X0", "9736", "X", 0x1E),
    ("CODE_B6BF66_M0X0", "BF73", "X", 0x34),
    ("CODE_B6F521_M0X0", "F53E", "X", 0x34),
    ("CODE_B882D9_M0X0", "82E4", "X", 0x34),
    ("CODE_BCADDB_M0X0", "ADDE", "X", 0x34),
    ("CODE_BCC874_M0X0", "C879", "Y", 0x34),
    ("CODE_BDF44B_M0X0", "F458", "X", 0x34),
    ("CODE_BF8000_M0X0", "8035", "X", 0x34),
    ("CODE_BF804B_M0X0", "806F", "X", 0x34),
    ("CODE_BF8091_M0X0", "80B7", "X", 0x34),
    ("CODE_BF8091_M0X0", "80E6", "X", 0x34),
    ("CODE_BF80F0_M0X0", "8119", "X", 0x34),
    ("CODE_BF845A_M0X0", "848D", "X", 0x34),
    ("DKC1_NorSpr12_UnknownSprite_Main_M0X0", "D93B", "X", 0x34),
)

# Each body starts with LDA $0D45,X / BEQ before any side effect. $BF848F
# is deliberately absent: its body must clear the hidden-ID table $1665.
LIVE_FORWARD_LOOPS = frozenset({
    ("CODE_BF8000_M0X0", "8035"),
    ("CODE_BF804B_M0X0", "806F"),
    ("CODE_BF8091_M0X0", "80B7"),
    ("CODE_BF8091_M0X0", "80E6"),
    ("CODE_BF80F0_M0X0", "8119"),
})

COOP_CATALOG = json.loads((Path(__file__).resolve().parents[1] /
                           "contracts/coop-hooks.json").read_text())
ENTITY_HOOKS = COOP_CATALOG["entity_hooks"]
IMMEDIATE_HOOKS = COOP_CATALOG["immediates"]


def _function(sources: dict[Path, str], name: str) -> tuple[Path, int, int]:
    marker = f"RecompReturn {name}(CpuState *cpu) {{"
    found = [(path, text.index(marker)) for path, text in sources.items()
             if marker in text]
    if len(found) != 1:
        raise ValueError(f"expected exactly one {name}, found {len(found)}")
    path, start = found[0]
    text = sources[path]
    depth = 0
    for pos in range(text.index("{", start), len(text)):
        depth += (text[pos] == "{") - (text[pos] == "}")
        if not depth:
            return path, start, pos + 1
    raise ValueError(f"unclosed generated function {name}")


def _edit_function(sources, name, change):
    path, start, end = _function(sources, name)
    sources[path] = sources[path][:start] + change(sources[path][start:end]) + sources[path][end:]


def _block(text: str, label: str) -> tuple[int, int]:
    start = text.index(f"  L_{label}_M0X0:")
    next_label = re.search(r"\n  L_[0-9A-F]+_M[01]X[01]:", text[start + 1:])
    end = start + 1 + next_label.start() if next_label else len(text)
    return start, end


def _args(text: str, opening: int) -> tuple[list[str], int]:
    depth, start, args = 1, opening + 1, []
    for pos in range(start, len(text)):
        char = text[pos]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if not depth:
                args.append(text[start:pos].strip())
                return args, pos + 1
        elif char == "," and depth == 1:
            args.append(text[start:pos].strip())
            start = pos + 1
    raise ValueError("unclosed generated memory call")


def adapt_indexed_accesses(text: str) -> tuple[str, int]:
    edits = []
    for match in re.finditer(r"\bcpu_(read|write)(8|16)\(", text):
        args, end = _args(text, match.end() - 1)
        if len(args) not in (3, 4) or args[0] != "cpu":
            continue
        index = re.search(r"\(uint32\)0x([0-9a-f]+) \+ \(uint32\)cpu->([XY])", args[2])
        if not index or int(index[1], 16) not in FIELD_LITERALS:
            continue
        # An indirect outer read may contain an indexed inner pointer read.
        # Rewrite that inner call only; overlapping replacements corrupt code.
        if "cpu_read" in args[2] or "cpu_write" in args[2]:
            continue
        if "(uint32)cpu->DB << 16" not in args[2]:
            raise ValueError("unrecognized indexed actor bank expression: " + args[2])
        compact = re.sub(r"[()\s]", "", args[2])
        if not re.fullmatch(
                r"uint16uint32cpu->DB<<16\+uint320x[0-9a-f]+\+uint32cpu->[XY]",
                compact):
            raise ValueError("unrecognized indexed actor address expression: " + args[2])
        base, reg = int(index[1], 16), index[2]
        operation = match[1].capitalize() + match[2]
        params = ["cpu", "cpu->DB", f"0x{base:04x}u", f"cpu->{reg}"]
        if len(args) == 4:
            params.append(args[3])
        replacement = f"Dkc1CoopIndexed{operation}(" + ", ".join(params) + ")"
        edits.append((match.start(), end, replacement))
    for start, end, replacement in reversed(edits):
        text = text[:start] + replacement + text[end:]
    return text, len(edits)


def _adapt_forward(text, label, register, native_end, live=False):
    start, end = _block(text, label)
    block = text[start:end]
    helper = (f"Dkc1CoopEntityLiveLoopIndex(cpu->{register})" if live else
              f"Dkc1CoopEntityPoolLoopIndex(cpu->{register}, 0x{native_end:x}u)"
              if native_end == 0x1e else f"Dkc1CoopEntityLoopIndex(cpu->{register})")
    if "Dkc1CoopEntityLoopEnd(" in block:
        # Upgrade an already adapted private tree without regenerating ROM C.
        if live:
            block = block.replace(f"Dkc1CoopEntityLoopIndex(cpu->{register})", helper)
        return text[:start] + block + text[end:]
    pattern = rf"(    uint16 _v\d+ = )0x{native_end:x};(?=\n\s+uint16 _v\d+ = cpu_read_{register.lower()}16\(cpu\);)"
    matches = list(re.finditer(pattern, block))
    if len(matches) != 1:
        raise ValueError(f"expected one actor bound at {label}")
    match = matches[0]
    replacement = (f"    cpu->{register} = {helper};\n" + match[1] +
                   f"Dkc1CoopEntityLoopEnd(0x{native_end:x}u);")
    block = block[:match.start()] + replacement + block[match.end():]
    return text[:start] + block + text[end:]


def _indirect_write(text, script=False):
    helper = "Dkc1CoopEntityScriptWrite16" if script else "Dkc1CoopIndexedWrite16"
    if helper in text:
        return text
    pattern = r"cpu_write16\(cpu, 0x00, \(uint16\)\(cpu->D \+ 0x0000 \+ cpu->X\), (_v\d+)\)"
    def replacement(match):
        if script:
            return f"{helper}(cpu, {match[1]})"
        return f"{helper}(cpu, 0x00, cpu->D, cpu->X, {match[1]})"
    text, count = re.subn(pattern, replacement, text)
    if count != 1:
        raise ValueError("expected one indirect actor field write")
    return text


def _immediate(text, hook):
    start, end = _block(text, hook["aot_label"])
    block = text[start:end]
    if hook["call"] not in block:
        block, count = re.subn(r"(uint16 _v\d+ = )" + hook["native"] + ";",
            lambda m: m[1] + hook["call"] + ";", block)
        if count != 1:
            raise ValueError(f"expected actor search immediate at {hook['pc']}")
    return text[:start] + block + text[end:]


def _native_hook(text, hook):
    """Retain each existing AOT instruction phase; no CPU flags are added."""
    kind, call = hook["kind"], hook["call"]
    if kind == "collision-start":
        if call in text:
            return text
        text, count = re.subn(r"(uint16 _v\d+ = )0x1e;",
                             lambda m: m[1] + call + ";", text)
        if count != 1:
            raise ValueError("expected collision pool start $1E")
        return text
    start, end = _block(text, hook.get("aot_label", hook["pc"][-4:]))
    block = text[start:end]
    if kind == "clear-range":
        if "Dkc1CoopEntityClearRange(" in block:
            return text
        pattern = r"(    uint16 (_v\d+) = cpu_read_a16\(cpu\);\n)(?=    uint16 _v\d+ = \(uint16\)\(\(\2 & 0xFFFF\) >> 1\);)"
        block, count = re.subn(pattern, lambda m: m[1] +
            "    " + call.replace("cpu->A", m[2]) + ";\n", block)
        if count != 1:
            raise ValueError("expected bulk actor clear range count")
    elif kind == "label":
        code = (f"    if ({call})\n      goto L_{hook['redirect'][-4:]}_M0X0;"
                if "redirect" in hook else f"    {call};")
        if code in block:
            return text
        marker = hook.get("aot_after", hook.get("aot_before",
            "    cpu->coprocessor_master_cycles = cpu->master_cycles;"))
        if block.count(marker) != 1:
            raise ValueError(f"native actor boundary changed: {hook['pc']}")
        if "redirect" in hook and f"  L_{hook['redirect'][-4:]}_M0X0:" not in text:
            raise ValueError(f"native actor redirect changed: {hook['pc']}")
        insertion = code + "\n" + marker if "aot_before" in hook else marker + "\n" + code
        block = block.replace(marker, insertion, 1)
    else:
        raise ValueError(f"unknown native actor hook kind: {kind}")
    return text[:start] + block + text[end:]


def apply_overrides(sources: dict[Path, str]) -> None:
    for function, label, register, native_end in FORWARD_LOOPS:
        _edit_function(sources, function,
                       lambda text, l=label, r=register, n=native_end,
                       live=(function, label) in LIVE_FORWARD_LOOPS:
                       _adapt_forward(text, l, r, n, live))
    for function in ("CODE_B58064_M0X0", "CODE_B58283_M0X0"):
        _edit_function(sources, function, _indirect_write)
    _edit_function(sources, "CODE_B6E83C_M0X0", lambda t: _indirect_write(t, True))
    # Preserve transformation order within each function, locating its source
    # once even when it owns several native hook boundaries.
    groups = {}
    for transform, hooks in ((_immediate, IMMEDIATE_HOOKS),
                             (_native_hook, ENTITY_HOOKS)):
        for hook in hooks:
            groups.setdefault(hook["symbol"], []).append((transform, hook))
    for symbol, transforms in groups.items():
        def apply_group(text, transforms=transforms):
            for transform, hook in transforms:
                text = transform(text, hook)
            return text
        _edit_function(sources, symbol, apply_group)
    # Migrate the retired early sink adapter without editing generated files.
    for path, text in list(sources.items()):
        sources[path] = text.replace(
            "    if (Dkc1CoopIndependentWaterSink(cpu))\n      goto L_A40D_M0X0;\n", "")
    for path, text in list(sources.items()):
        text, _ = adapt_indexed_accesses(text)
        if "Dkc1CoopEntit" in text or "Dkc1CoopIndexed" in text:
            if INCLUDE not in text:
                marker = '#include "funcs.h"'
                if text.count(marker) != 1:
                    raise ValueError(f"missing generated include anchor: {path}")
                text = text.replace(marker, marker + "\n" + INCLUDE, 1)
        if (("Dkc1CoopIndependentDamage(cpu)" in text or
             "Dkc1CoopIndependentFall(cpu)" in text) and
            '#include "dkc1_coop_mod.h"' not in text):
            text = text.replace(INCLUDE, INCLUDE + '\n#include "dkc1_coop_mod.h"', 1)
        sources[path] = text


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated-dir", type=Path, required=True)
    args = parser.parse_args()
    sources = {p: p.read_text(encoding="utf-8") for p in args.generated_dir.glob("*.c")}
    original = dict(sources)
    apply_overrides(sources)
    changed = 0
    for path, text in sources.items():
        if text != original[path]:
            path.write_text(text, encoding="utf-8", newline="\n")
            changed += 1
    print(f"Applied co-op entity overrides to {changed} translation units")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
