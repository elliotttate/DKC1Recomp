#!/usr/bin/env python3
"""Source-owned co-op OBJ resource adapters for the locked DKC1 ROM.

Only the host-owned metadata path bypasses physical graphics/palette slots.
The generated function prologue and hardware RTS/RTL return logic remain
intact. Global palette requests retain the native allocator.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re


INCLUDE = '#include "dkc1_coop_graphics.h"'
RESOURCE_HOOKS = json.loads((Path(__file__).resolve().parents[1] /
                            "contracts/coop-hooks.json").read_text())["resource_hooks"]


def _function(sources: dict[Path, str], symbol: str) -> tuple[Path, int, int]:
    marker = f"RecompReturn {symbol}(CpuState *cpu) {{"
    matches = [(path, text.index(marker)) for path, text in sources.items()
               if marker in text]
    if len(matches) != 1:
        raise ValueError(f"expected one graphics function {symbol}, got {len(matches)}")
    path, start = matches[0]
    text = sources[path]
    depth = 0
    for i in range(start + len(marker) - 1, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if not depth:
                return path, start, i + 1
    raise ValueError(f"unclosed graphics function {symbol}")


def _save(sources: dict[Path, str], path: Path, start: int, end: int,
          function: str) -> None:
    text = sources[path]
    text = text[:start] + function + text[end:]
    if INCLUDE not in text:
        marker = '#include "funcs.h"'
        if text.count(marker) != 1:
            raise ValueError(f"unexpected generated includes in {path}")
        text = text.replace(marker, marker + "\n" + INCLUDE, 1)
    sources[path] = text


def _entry_hook(sources: dict[Path, str], symbol: str,
                entry: str, code: str, marker: str) -> int:
    path, start, end = _function(sources, symbol)
    function = sources[path][start:end]
    if marker in function:
        if code not in function:
            raise ValueError(f"stale graphics hook {marker}")
        return 0
    if function.count(entry) != 1:
        raise ValueError(f"unexpected entry {entry} in {symbol}")
    function = function.replace(entry, code + "\n" + entry, 1)
    _save(sources, path, start, end, function)
    return 1


def _return_hook(sources: dict[Path, str], symbol: str, entry_pc: int,
                 return_pc: int, expression: str) -> int:
    path, start, end = _function(sources, symbol)
    function = sources[path][start:end]
    label = f"L_COOP_GRAPHICS_RETURN_{entry_pc:06X}"
    code = f"  if ({expression}) goto {label};"
    entry = f"  L_{entry_pc & 0xffff:04X}_M0X0:"
    if function.count(entry) != 1:
        raise ValueError(f"missing allocator entry {entry}")
    trace = re.search(r"dbg_rt[sl]_trace\(cpu, 0x" +
                      f"{return_pc:x}" + r"u,", function)
    if not trace:
        raise ValueError(f"missing hardware return ${return_pc:06X}")
    if label + ":" in function:
        if code not in function or function.count(label + ":") != 1:
            raise ValueError(f"stale graphics return hook {symbol}")
        return 0
    block = function.rfind("    { uint16 _ret_s = cpu->S;", 0, trace.start())
    if block < 0:
        raise ValueError(f"missing hardware return frame in {symbol}")
    function = function[:block] + f"  {label}:\n" + function[block:]
    function = function.replace(entry, code + "\n" + entry, 1)
    _save(sources, path, start, end, function)
    return 1


RETURN_HOOKS = (
    ("CODE_808521_M0X0", 0x808521, 0x80852B,
     "Dkc1CoopGraphicsCancelKongPaletteUpload(cpu)"),
    ("CODE_BDF346_M0X0", 0xBDF346, 0xBDF36D,
     "Dkc1CoopGraphicsAllocate(cpu, 0)"),
    ("CODE_BDF2EB_M0X0", 0xBDF2EB, 0xBDF341,
     "Dkc1CoopGraphicsAllocate(cpu, 1)"),
    ("CODE_BDF38C_M0X0", 0xBDF38C, 0xBDF39D,
     "Dkc1CoopGraphicsRelease(cpu, cpu->X)"),
    ("CODE_BDF36E_M0X0", 0xBDF36E, 0xBDF385,
     "Dkc1CoopGraphicsCapacity(cpu)"),
    ("CODE_BDF1F0_M0X0", 0xBDF1F0, 0xBDF1F3,
     "Dkc1CoopGraphicsAllocatePalette(cpu, _host_return_pc24)"),
    ("CODE_BDF23A_M0X0", 0xBDF23A, 0xBDF253,
     "Dkc1CoopGraphicsReplacePalette(cpu, 0)"),
    ("CODE_BDF254_M0X0", 0xBDF254, 0xBDF265,
     "Dkc1CoopGraphicsReplacePalette(cpu, 0)"),
    ("CODE_B6D1FA_M0X0", 0xB6D1FA, 0xB6D22C,
     "Dkc1CoopGraphicsReplacePalette(cpu, 1)"),
    ("CODE_BDF2DA_M0X0", 0xBDF2DA, 0xBDF2E6,
     "Dkc1CoopGraphicsReleasePalette(cpu, _host_return_pc24)"),
    ("CODE_BDF2D6_M0X0", 0xBDF2D6, 0xBDF2D9,
     "Dkc1CoopGraphicsReleasePalette(cpu, _host_return_pc24)"),
)


def apply_overrides(sources: dict[Path, str]) -> dict[str, int]:
    """Mutate an in-memory source batch; the parent adapter owns writes."""
    count = 0
    for symbol, entry, ret, call in RETURN_HOOKS:
        count += _return_hook(sources, symbol, entry, ret, call)

    for hook in RESOURCE_HOOKS:
        if hook["kind"] == "group-release":
            # Preserve Y across the callback; absent physical owners must not
            # enter the native adjacent-owner zero-equality scan.
            code = (f"  /* {hook['marker']} */\n  {{\n"
                    "    uint16 _coop_graphics_handle = cpu->Y;\n"
                    f"    if ({hook['call']}) {{\n"
                    "      cpu->Y = _coop_graphics_handle;\n"
                    f"      goto L_{hook['redirect'][-4:]}_M0X0;\n    }}\n  }}")
        elif hook["kind"] == "palette":
            call = hook["call"].format(read86="cpu_read16(cpu, 0, 0x0086u)",
                                       read82="cpu_read16(cpu, 0, 0x0082u)")
            code = f"  /* {hook['marker']} */\n  {call};"
        else:
            raise ValueError(f"unknown graphics hook kind: {hook['kind']}")
        count += _entry_hook(sources, hook["symbol"],
                             f"  L_{hook['pc'][-4:]}_M0X0:", code, hook["marker"])
    return {"coop_graphics": count}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("generated_dir", type=Path)
    args = parser.parse_args()
    originals = {p: p.read_text(encoding="utf-8")
                 for p in args.generated_dir.glob("*.c")}
    if not originals:
        parser.error("generated directory has no C translation units")
    sources = originals.copy()
    result = apply_overrides(sources)
    for path, text in sources.items():
        if text != originals[path]:
            path.write_text(text, encoding="utf-8", newline="\n")
    print(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
