#!/usr/bin/env python3
"""Apply source-owned DKC1 presentation-widescreen adaptations.

The generated C is private/ignored and is recreated from the user's ROM.
This fail-closed post-generation step changes only cull/visibility constants
and direct-OAM packing.  Camera, collision, exits, boss bounds, and tilemap
streaming remain the exact cartridge program.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re


INCLUDE = '#include "dkc1_video.h"'


def load_sources(generated_dir: Path) -> dict[Path, str]:
    sources = {
        path: path.read_text(encoding="utf-8")
        for path in generated_dir.glob("*.c")
    }
    if not sources:
        raise ValueError("generated directory contains no C translation units")
    return sources


def add_include(text: str) -> str:
    if INCLUDE in text:
        return text
    marker = '#include "funcs.h"'
    if text.count(marker) != 1:
        raise ValueError("generated unit has an unexpected funcs.h include")
    return text.replace(marker, marker + "\n" + INCLUDE, 1)


def locate_block(sources: dict[Path, str], start_label: str,
                 end_label: str) -> tuple[Path, str, int, int]:
    matches: list[tuple[Path, str, int, int]] = []
    for path, text in sources.items():
        start = text.find(start_label)
        if start < 0:
            continue
        end = text.find(end_label, start + len(start_label))
        if end < 0:
            raise ValueError(f"missing end label {end_label} after {start_label}")
        matches.append((path, text, start, end))
    if len(matches) != 1:
        raise ValueError(
            f"expected exactly one generated block {start_label}; "
            f"found {len(matches)}")
    return matches[0]


def locate_function(sources: dict[Path, str], symbol: str) -> tuple[Path, str]:
    marker = f"RecompReturn {symbol}(CpuState *cpu) {{"
    matches = [(path, text) for path, text in sources.items()
               if marker in text]
    if len(matches) != 1:
        raise ValueError(
            f"expected exactly one generated unit defining {symbol}; "
            f"found {len(matches)}")
    return matches[0]


def function_span(text: str, symbol: str) -> tuple[int, int]:
    """Return the exact generated function definition span.

    Some translated 65816 blocks are the last basic block in their function,
    so a following trace label is not available as an end marker.  Generated
    functions have balanced C braces; use that structural boundary instead of
    letting a terminal block silently escape fail-closed matching.
    """
    marker = f"RecompReturn {symbol}(CpuState *cpu) {{"
    start = text.find(marker)
    if start < 0:
        raise ValueError(f"missing generated function definition {symbol}")
    brace = text.find("{", start + len(marker) - 1)
    if brace < 0:
        raise ValueError(f"missing opening brace for {symbol}")
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return start, index + 1
    raise ValueError(f"unterminated generated function definition {symbol}")


def locate_function_block(sources: dict[Path, str], symbol: str,
                          start_label: str,
                          end_label: str) -> tuple[Path, str, int, int]:
    path, text = locate_function(sources, symbol)
    function_start, function_end = function_span(text, symbol)
    start = text.find(start_label, function_start, function_end)
    if start < 0:
        raise ValueError(f"{symbol} does not contain {start_label}")
    end = text.find(end_label, start + len(start_label), function_end)
    if end < 0:
        raise ValueError(
            f"{symbol} does not contain {end_label} after {start_label}")
    return path, text, start, end


def adapt_constant_block(sources: dict[Path, str], start_label: str,
                         end_label: str, literal: str, helper: str) -> None:
    path, text, start, end = locate_block(sources, start_label, end_label)
    block = text[start:end]
    expression = f"{helper}({literal})"
    if expression in block:
        if block.count(expression) != 1:
            raise ValueError(f"ambiguous existing {expression} in {start_label}")
        return
    pattern = rf"(uint16\s+\w+\s*=\s*){re.escape(literal)};"
    block, count = re.subn(pattern, rf"\1{expression};", block)
    if count != 1:
        raise ValueError(
            f"expected one {literal} constant in {start_label}; found {count}")
    sources[path] = add_include(text[:start] + block + text[end:])


def adapt_two_constants(sources: dict[Path, str], start_label: str,
                        end_label: str, adaptations: tuple[tuple[str, str], ...]) -> None:
    for literal, helper in adaptations:
        adapt_constant_block(sources, start_label, end_label, literal, helper)


def adapt_function_constant(sources: dict[Path, str], symbol: str,
                            start_label: str, literal: str,
                            helper: str) -> None:
    path, text = locate_function(sources, symbol)
    function_start, function_end = function_span(text, symbol)
    start = text.find(start_label, function_start, function_end)
    if start < 0:
        raise ValueError(f"{symbol} does not contain {start_label}")
    match = re.search(r"\n\s*(L_[0-9A-F]+_M[01]X[01]:)",
                      text[start + len(start_label):function_end])
    end = (start + len(start_label) + match.start(1)
           if match else function_end)
    block = text[start:end]
    expression = f"{helper}({literal})"
    if expression in block:
        if block.count(expression) != 1:
            raise ValueError(f"ambiguous existing {expression} in {start_label}")
        return
    pattern = rf"(uint16\s+\w+\s*=\s*){re.escape(literal)};"
    block, count = re.subn(pattern, rf"\1{expression};", block)
    if count != 1:
        raise ValueError(
            f"expected one {literal} constant in {symbol}/{start_label}; "
            f"found {count}")
    sources[path] = add_include(text[:start] + block + text[end:])


def adapt_nth_accumulator_write(sources: dict[Path, str], start_label: str,
                                end_label: str, write_index: int,
                                helper: str) -> None:
    path, text, start, end = locate_block(sources, start_label, end_label)
    block = text[start:end]
    if helper in block:
        if block.count(helper) != 1:
            raise ValueError(f"ambiguous existing {helper} in {start_label}")
        return
    pattern = re.compile(r"cpu_write_a_m\(cpu, \(uint16\)\((\w+)\)\);")
    matches = list(pattern.finditer(block))
    if write_index < 0 or write_index >= len(matches):
        raise ValueError(
            f"expected accumulator write {write_index} in {start_label}; "
            f"found {len(matches)} writes")
    match = matches[write_index]
    variable = match.group(1)
    replacement = (
        f"cpu_write_a_m(cpu, (uint16)({helper}({variable})));"
    )
    block = block[:match.start()] + replacement + block[match.end():]
    sources[path] = add_include(text[:start] + block + text[end:])


def adapt_function_accumulator_write(
        sources: dict[Path, str], symbol: str, start_label: str,
        write_index: int, helper: str) -> None:
    path, text = locate_function(sources, symbol)
    function_start, function_end = function_span(text, symbol)
    start = text.find(start_label, function_start, function_end)
    if start < 0:
        raise ValueError(f"{symbol} does not contain {start_label}")
    next_label = re.search(
        r"\n\s*L_[0-9A-F]+_M[01]X[01]:",
        text[start + len(start_label):function_end])
    end = (start + len(start_label) + next_label.start()
           if next_label else function_end)
    block = text[start:end]
    if helper in block:
        if block.count(helper) != 1:
            raise ValueError(f"ambiguous existing {helper} in {start_label}")
        return
    pattern = re.compile(r"cpu_write_a_m\(cpu, \(uint16\)\((\w+)\)\);")
    matches = list(pattern.finditer(block))
    if write_index < 0 or write_index >= len(matches):
        raise ValueError(
            f"expected accumulator write {write_index} in {start_label}; "
            f"found {len(matches)} writes")
    match = matches[write_index]
    variable = match.group(1)
    replacement = (
        f"cpu_write_a_m(cpu, (uint16)({helper}({variable})));"
    )
    block = block[:match.start()] + replacement + block[match.end():]
    sources[path] = add_include(text[:start] + block + text[end:])


def adapt_vertical_rope(sources: dict[Path, str]) -> None:
    # Preserve the original coordinate in DP $76, then bias only A/N/Z for
    # the stock BMI/CMP/BPL visibility sequence.
    path, text, start, end = locate_function_block(
        sources, "CODE_80A7ED_M0X0", "L_A7ED_M0X0:", "L_A809_M0X0:")
    block = text[start:end]
    marker = "Dkc1VideoBiasCullX"
    if marker not in block:
        stores = list(re.finditer(
            r"cpu_write16\(cpu, 0x00, \(uint16\)\(cpu->D \+ 0x0076\), "
            r"\w+\);", block))
        if len(stores) != 1:
            raise ValueError(
                f"expected one vertical-rope DP $76 store; found {len(stores)}")
        insertion = """
    /* Host-only cull key; DP $76 keeps the authentic OAM coordinate. */
    uint16 _ws_cull_x = Dkc1VideoBiasCullX(cpu_read_a16(cpu));
    cpu_write_a_m(cpu, (uint16)(_ws_cull_x));
    cpu->_flag_Z = (_ws_cull_x == 0) ? 1 : 0;
    cpu->_flag_N = ((_ws_cull_x & 0x8000) != 0) ? 1 : 0;
    cpu->P = (uint8)((cpu->P & ~0x82) |
                     (cpu->_flag_Z ? 0x02 : 0) |
                     (cpu->_flag_N ? 0x80 : 0));"""
        pos = stores[0].end()
        block = block[:pos] + insertion + block[pos:]
        text = text[:start] + block + text[end:]
        sources[path] = add_include(text)
    elif block.count(marker) != 1:
        raise ValueError("ambiguous vertical-rope cull adaptation")

    adapt_function_constant(
        sources, "CODE_80A7ED_M0X0", "L_A809_M0X0:",
        "0x100", "Dkc1VideoExpandCullSpan")

    path, text, start, end = locate_function_block(
        sources, "CODE_80A7ED_M0X0", "L_A878_M0X0:", "L_A877_M0X0:")
    block = text[start:end]
    helper = "Dkc1VideoPromoteOamSizeMask"
    if helper not in block:
        pattern = re.compile(
            r"(uint16\s+\w+\s*=\s*)"
            r"(cpu_read16\(cpu,\s*\(uint8\)\(\(\(\(uint32\)0x80a545\s*\+"
            r"\s*\(uint32\)cpu->X\)\)\s*>>\s*16\),\s*"
            r"\(uint16\)\(\(\(uint32\)0x80a545\s*\+\s*\(uint32\)cpu->X\)\)\))"
            r"(;)")
        replacement = (
            r"\1Dkc1VideoPromoteOamSizeMask(\2, "
            r"cpu_read16(cpu, 0x00, (uint16)(cpu->D + 0x0076)))\3")
        block, count = pattern.subn(replacement, block)
        if count != 1:
            raise ValueError(
                f"expected one vertical-rope upper-OAM mask load; found {count}")
        sources[path] = add_include(text[:start] + block + text[end:])
    elif block.count(helper) != 1:
        raise ValueError("ambiguous vertical-rope upper-OAM adaptation")


def adapt_type5_child_retry(sources: dict[Path, str]) -> None:
    path, text, start, end = locate_function_block(
        sources, "CODE_BDFB76_M0X0", "L_FB76_M0X0:", "L_FB80_M0X0:")
    block = text[start:end]
    call = "Dkc1VideoPrepareType5ChildRetry(cpu)"
    if call in block:
        if block.count(call) != 1:
            raise ValueError("ambiguous type-5 child-retry adaptation")
        return
    marker = "    cpu->coprocessor_master_cycles = cpu->master_cycles;"
    if block.count(marker) != 1:
        raise ValueError(
            "expected one type-5 entry cycle marker before active test")
    insertion = (
        marker + "\n"
        "    /* Retry missing children of an already-active wide group. */\n"
        "    if (Dkc1VideoPrepareType5ChildRetry(cpu))\n"
        "      goto L_FBF5_M0X0;")
    block = block.replace(marker, insertion, 1)
    sources[path] = add_include(text[:start] + block + text[end:])


LEFT_BLOCKS = (
    ("CODE_BDF502_M0X0", "F5AA"), ("CODE_BDF502_M0X0", "F5F8"),
    ("CODE_BDF6FF_M0X0", "F6FF"), ("CODE_BDF751_M0X0", "F751"),
    ("CODE_BDF78C_M0X0", "F78C"), ("CODE_BDF88A_M0X0", "F88A"),
    ("CODE_BDF8D5_M0X0", "F8D5"), ("CODE_BDF9A2_M0X0", "F9A2"),
    ("CODE_BDF9E8_M0X0", "F9E8"), ("CODE_BDFA31_M0X0", "FA31"),
    ("CODE_BDFADB_M0X0", "FADB"), ("CODE_BDFB0D_M0X0", "FB0D"),
    ("CODE_BDFB76_M0X0", "FBA5"), ("CODE_BDFCCC_M0X0", "FCCC"),
    ("CODE_BDFD00_M0X0", "FD00"), ("CODE_BDFE7F_M0X0", "FE7F"),
    ("CODE_BDFEE6_M0X0", "FF24"), ("CODE_BDFF9B_M0X0", "FFAD"),
)
SPAN_BLOCKS = (
    ("CODE_BDF502_M0X0", "F604"), ("CODE_BDF6FF_M0X0", "F70F"),
    ("CODE_BDF78C_M0X0", "F79C"), ("CODE_BDF88A_M0X0", "F899"),
    ("CODE_BDF8D5_M0X0", "F8E1"), ("CODE_BDF9A2_M0X0", "F9B6"),
    ("CODE_BDF9E8_M0X0", "F9F4"), ("CODE_BDFA31_M0X0", "FA3D"),
    ("CODE_BDFADB_M0X0", "FAE7"), ("CODE_BDFB0D_M0X0", "FB1D"),
    ("CODE_BDFCCC_M0X0", "FCD8"), ("CODE_BDFD00_M0X0", "FD0C"),
    ("CODE_BDFEE6_M0X0", "FF37"), ("CODE_BDFF9B_M0X0", "FFBE"),
)
PREFETCH_BLOCKS = (
    ("CODE_BDF502_M0X0", "F585"),
    ("CODE_BDFB76_M0X0", "FB80"),
)


def apply_overrides(generated_dir: Path) -> list[Path]:
    sources = load_sources(generated_dir)
    original = dict(sources)

    # Shared world-sprite renderer: the two authentic windows are
    # [-48,303] and [-88,343].
    adapt_two_constants(
        sources, "L_A8D4_M0X0:", "L_A8E5_M0X0:",
        (("0x30", "Dkc1VideoExpandCullLeft"),
         ("0x160", "Dkc1VideoExpandCullSpan")))
    adapt_two_constants(
        sources, "L_A904_M0X0:", "L_A915_M0X0:",
        (("0x58", "Dkc1VideoExpandCullLeft"),
         ("0x1b0", "Dkc1VideoExpandCullSpan")))

    # Placed-object activation windows, including the two type-$09 vertical
    # section-controller spans that caused Slipslide Ride softlocks.
    for symbol, block in LEFT_BLOCKS:
        adapt_function_constant(
            sources, symbol, f"L_{block}_M0X0:",
            "0x20", "Dkc1VideoExpandCullLeft")
    for symbol, block in SPAN_BLOCKS:
        adapt_function_constant(
            sources, symbol, f"L_{block}_M0X0:",
            "0x140", "Dkc1VideoExpandCullSpan")
    for symbol, block in PREFETCH_BLOCKS:
        adapt_function_constant(
            sources, symbol, f"L_{block}_M0X0:",
            "0x120", "Dkc1VideoExpandCullLeft")
    adapt_type5_child_retry(sources)

    # Banana formations have a private candidate window and direct OAM
    # writer.  No camera correction is required: the host leaves the logical
    # camera bounds stock.
    adapt_function_constant(
        sources, "CODE_B8B918_M0X0", "L_B918_M0X0:",
        "0x100", "Dkc1VideoExpandCullLeft")
    adapt_function_constant(
        sources, "CODE_B8B918_M0X0", "L_B93E_M0X0:",
        "0x10f", "Dkc1VideoExpandCullSpan")
    adapt_function_constant(
        sources, "CODE_B8B9B5_M0X0", "L_B9EA_M0X0:",
        "0xf", "Dkc1VideoExpandCullLeft")
    adapt_function_constant(
        sources, "CODE_B8B9B5_M0X0", "L_BA11_M0X0:",
        "0x107", "Dkc1VideoExpandCullLeft")
    adapt_function_accumulator_write(
        sources, "CODE_B8B9B5_M0X0", "L_BA67_M0X1:", 1,
        "Dkc1VideoPromoteOamXHigh")
    adapt_function_accumulator_write(
        sources, "CODE_B8B9B5_M0X0", "L_BACA_M0X1:", 1,
        "Dkc1VideoPromoteOamXHigh")

    adapt_vertical_rope(sources)

    changed = []
    for path, text in sources.items():
        if text != original[path]:
            path.write_text(text, encoding="utf-8", newline="\n")
            changed.append(path)
    return sorted(changed)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated-dir", required=True, type=Path)
    args = parser.parse_args()
    generated_dir = args.generated_dir.expanduser().resolve(strict=True)
    changed = apply_overrides(generated_dir)
    for path in changed:
        print(f"Applied DKC1 widescreen overrides: {path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"error: {error}")
        raise SystemExit(1)
