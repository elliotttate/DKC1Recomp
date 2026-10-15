#!/usr/bin/env python3
"""Apply source-owned DKC1 presentation-widescreen adaptations.

The generated C is private/ignored and is recreated from the user's ROM.
This fail-closed post-generation step adapts cull/visibility constants,
direct-OAM packing, and DKC's cartridge tile-stream selectors. Camera,
collision, exits, and boss bounds remain the exact cartridge program.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import re


COOP_CATALOG = json.loads((Path(__file__).resolve().parents[1] /
                           "contracts/coop-hooks.json").read_text())
COOP_EVENT_HOOKS = COOP_CATALOG["hooks"]
COOP_OPERAND_HOOKS = COOP_CATALOG["operands"]


def inject_coop_events(sources, group):
    headers = {"camera": "dkc1_coop_world.h", "mount": "dkc1_coop_mod.h",
               "hud": "dkc1_coop_hud.h", "pickup": "dkc1_coop_mod.h",
               "rope": "dkc1_coop_mod.h", "map": "dkc1_coop_mod.h", "wheel": "dkc1_coop_mod.h", "scanner": "dkc1_coop_world.h"}
    for hook in COOP_EVENT_HOOKS:
        if hook["group"] != group: continue
        code = hook.get("aot")
        if code is None:
            code = (f'    if ({hook["call"]}) goto L_{hook["redirect"][-4:]}_M0X0;\n'
                    if "redirect" in hook else f'    {hook["call"]};\n')
        inject_coop_label(sources, hook["symbol"], hook.get("aot_label", hook["pc"][-4:]), code,
                          after_deadline=hook.get("after_deadline", True), header_name=headers[group],
                          before_statement=hook.get("aot_before"))


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
    # Migrate generated trees adapted by the earlier global object-window
    # experiment. Proxy mode needs scanner-only helpers so normal sprite,
    # banana, and rope presentation remain wide.
    if helper.startswith("Dkc1VideoObjectScanner"):
        legacy_helper = ("Dkc1VideoExpandCullLeft" if helper.endswith("Left")
                         else "Dkc1VideoExpandCullSpan")
        legacy = f"{legacy_helper}({literal})"
        if legacy in block:
            if block.count(legacy) != 1:
                raise ValueError(
                    f"ambiguous legacy {legacy} in {symbol}/{start_label}")
            block = block.replace(legacy, expression, 1)
            sources[path] = add_include(text[:start] + block + text[end:])
            return
    pattern = rf"(uint16\s+\w+\s*=\s*){re.escape(literal)};"
    block, count = re.subn(pattern, rf"\1{expression};", block)
    if count != 1:
        raise ValueError(
            f"expected one {literal} constant in {symbol}/{start_label}; "
            f"found {count}")
    sources[path] = add_include(text[:start] + block + text[end:])


def adapt_function_cpu_constant(sources: dict[Path, str], symbol: str,
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
    expression = f"{helper}(cpu, {literal})"
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


def adapt_stream_selector(sources: dict[Path, str], symbol: str,
                          convergence_label: str) -> None:
    path, text = locate_function(sources, symbol)
    function_start, function_end = function_span(text, symbol)
    start = text.find(convergence_label, function_start, function_end)
    if start < 0:
        raise ValueError(f"{symbol} does not contain {convergence_label}")
    insert_at = start + len(convergence_label)
    call = "Dkc1VideoSelectStreamX(cpu, cpu_read_a16(cpu))"
    tail = text[insert_at:function_end]
    if call in tail:
        if tail.count(call) != 1:
            raise ValueError(f"ambiguous existing selector in {symbol}")
        return
    injection = (
        "\n    cpu_write_a_m(cpu, (uint16)("
        "Dkc1VideoSelectStreamX(cpu, cpu_read_a16(cpu))));")
    sources[path] = add_include(text[:insert_at] + injection + text[insert_at:])


def adapt_vertical_row_builds(sources: dict[Path, str]) -> None:
    """Run DKC's 36-entry vertical row builder twice in wide gameplay.

    The row body tail-calls CODE_818A18, which copies the staging buffer into
    the 64-entry WRAM row ring and consumes the original JSL return with RTL.
    Rather than duplicating generated instruction semantics, begin the first
    biased pass at the two authentic body labels.  Immediately before either
    CODE_818A18 RTL, Advance requests one generated tail-call back through the
    correct outer row entry.  The second pass then restores Layer1 X and the
    original hardware return frame is consumed exactly once.
    """
    for symbol, label, alternate in (
            ("CODE_81890E_M0X0", "L_891A_M0X0:", "false"),
            ("CODE_818CEF_M0X0", "L_8CFB_M0X0:", "true")):
        path, text = locate_function(sources, symbol)
        function_start, function_end = function_span(text, symbol)
        start = text.find(label, function_start, function_end)
        if start < 0:
            raise ValueError(f"{symbol} does not contain {label}")
        insert_at = start + len(label)
        call = f"Dkc1VideoBeginWideRowBuild(cpu, {alternate});"
        block = text[insert_at:function_end]
        if call not in block:
            injection = (
                "\n    /* Wide-only second row-window transaction. */\n"
                f"    {call}")
            text = text[:insert_at] + injection + text[insert_at:]
            sources[path] = add_include(text)
        elif block.count(call) != 1:
            raise ValueError(f"ambiguous wide row begin in {symbol}")

    symbol = "CODE_818A18_M0X0"
    path, text = locate_function(sources, symbol)
    function_start, function_end = function_span(text, symbol)
    block = text[function_start:function_end]
    marker = "{ uint16 _ret_s = cpu->S;  /* RTL pop hardware return frame */"
    dispatch = "Dkc1VideoAdvanceWideRowBuild(cpu)"
    if dispatch not in block:
        if block.count(marker) != 2:
            raise ValueError(
                "expected exactly two CODE_818A18 RTL sites; "
                f"found {block.count(marker)}")
        injection = (
            "{\n"
            "      const uint8_t _ws_row_next = "
            "Dkc1VideoAdvanceWideRowBuild(cpu);\n"
            "      if (_ws_row_next != 0) {\n"
            "        cpu->PB = 0x81;\n"
            "        cpu->host_return_valid = _hrv;\n"
            "        cpu_tailcall_inherit_return_context(_entry_s, _hrv);\n"
            "        RecompReturn _tc;\n"
            "        if (_ws_row_next == 1) {\n"
            "          extern RecompReturn CODE_81890E_M0X0(CpuState *cpu);\n"
            "          _tc = CODE_81890E_M0X0(cpu);\n"
            "        } else {\n"
            "          extern RecompReturn CODE_818CEF_M0X0(CpuState *cpu);\n"
            "          _tc = CODE_818CEF_M0X0(cpu);\n"
            "        }\n"
            "        RecompStackPop();\n"
            "        return _tc;\n"
            "      }\n"
            "    }\n"
            "    ")
        block = block.replace(marker, injection + marker)
        sources[path] = add_include(
            text[:function_start] + block + text[function_end:])
    elif block.count(dispatch) != 2:
        raise ValueError("ambiguous wide row continuation in CODE_818A18")


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
    uint16 _ws_rope_x = cpu_read_a16(cpu);
    uint16 _ws_cull_x = Dkc1VideoBiasCullX(_ws_rope_x);
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
    elif "uint16 _ws_rope_x" not in block:
        old = (
            "uint16 _ws_cull_x = "
            "Dkc1VideoBiasCullX(cpu_read_a16(cpu));")
        new = (
            "uint16 _ws_rope_x = cpu_read_a16(cpu);\n"
            "    uint16 _ws_cull_x = Dkc1VideoBiasCullX(_ws_rope_x);")
        if block.count(old) != 1:
            raise ValueError("unexpected legacy vertical-rope cull adapter")
        block = block.replace(old, new, 1)
        sources[path] = add_include(text[:start] + block + text[end:])

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

    # The stock writer ORs the large-sprite bit into the current upper-OAM
    # word.  That is safe only while its native cull guarantees X=0..255:
    # after another writer used the same slot for a negative/right-margin
    # object, the adjacent X-high bit can remain set and wrap a rope at X=252
    # to signed X=-4.  In wide mode, replace that additive merge with an
    # authoritative size/X-high merge for every emitted rope segment.
    path, text, start, end = locate_function_block(
        sources, "CODE_80A7ED_M0X0", "L_A878_M0X0:", "L_A877_M0X0:")
    block = text[start:end]
    merge_helper = "Dkc1VideoMergeOamSizeAndXHigh"
    if merge_helper not in block:
        mask_assignment = re.search(
            r"uint16\s+(\w+)\s*=\s*[^;]*0x80a545[^;]*;", block)
        if not mask_assignment:
            raise ValueError("missing vertical-rope upper-OAM size mask")
        existing_assignment = re.search(
            r"uint16\s+(\w+)\s*=\s*cpu_read16\([^;]*cpu->Y[^;]*;",
            block[mask_assignment.end():])
        if not existing_assignment:
            raise ValueError("missing vertical-rope upper-OAM existing word")
        existing_variable = existing_assignment.group(1)
        or_pattern = re.compile(
            r"(uint16\s+(\w+)\s*=\s*\(uint16\)\()"
            r"(\w+)\s*\|\s*(\w+)(\);)")
        matches = []
        for match in or_pattern.finditer(block, mask_assignment.end()):
            if existing_variable in (match.group(3), match.group(4)):
                matches.append(match)
        if len(matches) != 1:
            raise ValueError(
                "expected one vertical-rope upper-OAM OR merge; "
                f"found {len(matches)}")
        match = matches[0]
        mask_variable = (match.group(4)
                         if match.group(3) == existing_variable
                         else match.group(3))
        replacement = (
            f"uint16 {match.group(2)} = {merge_helper}("
            f"{existing_variable}, {mask_variable}, "
            "_ws_rope_x);")
        block = block[:match.start()] + replacement + block[match.end():]
        sources[path] = add_include(text[:start] + block + text[end:])
    elif block.count(merge_helper) != 1:
        raise ValueError("ambiguous vertical-rope upper-OAM merge adaptation")
    else:
        legacy_argument = re.compile(
            r"(Dkc1VideoMergeOamSizeAndXHigh\(\w+,\s*\w+,\s*)"
            r"cpu_read16\(cpu, 0x00, \(uint16\)\(cpu->D \+ 0x0076\)\)"
            r"(\))")
        block, count = legacy_argument.subn(r"\1_ws_rope_x\2", block)
        if count == 1:
            sources[path] = add_include(text[:start] + block + text[end:])
        elif "_ws_rope_x" not in block:
            raise ValueError("unexpected vertical-rope X source in OAM merge")


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


def adapt_placed_actor_phase_guard(sources: dict[Path, str]) -> None:
    # Remove the first delay-only experiment if this generated tree was
    # already adapted. It hid prefetched actors because ID zero dispatches no
    # presentation path.
    symbol = "CODE_BF8087_M0X0"
    path, text = locate_function(sources, symbol)
    start, end = function_span(text, symbol)
    block = text[start:end]
    legacy = (
        "    /* Keep widened prefetch presentational until stock eligibility. */\n"
        "    if (!Dkc1VideoShouldRunPlacedActor(cpu))\n"
        "      cpu_write_a_m(cpu, 0);\n"
    )
    if legacy in block:
        if block.count(legacy) != 1:
            raise ValueError("ambiguous legacy placed-actor phase guard")
        block = block.replace(legacy, "", 1)
        text = text[:start] + block + text[end:]
        sources[path] = text

    # Reuse the actor JSL catalog; its overlay entries own these Begin/End
    # boundaries, including the already existing scheduled fast path.
    spec = importlib.util.spec_from_file_location(
        "coop_dispatch_schedule", Path(__file__).with_name("apply_dkc1_coop_schedule.py"))
    schedule = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(schedule)
    for owner, pc, _target, ret, overlay in schedule.CALLS:
        if not overlay: continue
        start_label = f"L_{(pc - 2) & 0xffff:04X}_M0X0:"
        end_label = f"L_{ret & 0xffff:04X}_M0X0:"
        path, text, start, end = locate_function_block(
            sources, owner, start_label, end_label)
        block = text[start:end]
        begin_call = "Dkc1VideoBeginPlacedActorDispatch(cpu);"
        end_call = "Dkc1VideoEndPlacedActorDispatch(cpu);"
        if begin_call in block or end_call in block:
            # The host continuation fast path has its own matching cleanup
            # before it skips the native JSL. Both paths end exactly once.
            endings = 1 + block.count("Dkc1CoopScheduleActor(cpu,")
            if block.count(begin_call) != 1 or block.count(end_call) != endings:
                raise ValueError(
                    f"ambiguous placed-actor transaction in {start_label}")
            continue
        store_pattern = re.compile(
            r"(    cpu_write16\(cpu, 0x00, \(uint16\)\(cpu->D \+ "
            r"0x0082\), \w+\);\n)")
        block, count = store_pattern.subn(
            lambda m: m[1] + "    " + begin_call + "\n", block, 1)
        if count != 1:
            raise ValueError(
                f"expected current-index store in {start_label}")
        goto_pattern = re.compile(r"(    goto L_[0-9A-F]+_M0X0;)")
        matches = list(goto_pattern.finditer(block))
        if len(matches) != 1:
            raise ValueError(
                f"expected one post-dispatch goto in {start_label}; "
                f"found {len(matches)}")
        offset = matches[0].start()
        block = (block[:offset] +
                 "    " + end_call + "\n" +
                 block[offset:])
        sources[path] = add_include(text[:start] + block + text[end:])


def adapt_margin_proxy_render_calls(sources: dict[Path, str]) -> None:
    """Borrow host proxies only for the exact CODE_BBA849 call duration.

    Every abnormal return in the call block receives the matching cleanup, so
    an interpreter yield or propagated non-local return cannot leave borrowed
    actor slots in cartridge WRAM.  The hook belongs immediately before the
    JSL call, not merely at the containing block's first label: CODE_80C96F's
    block resets the OAM pointer and clears the upper table before that JSL,
    which would otherwise erase every proxy sprite just injected.
    """
    for owner, start_label, end_label in (
            ("OAM_BeginFrameAndDrawHUD_M0X0", "L_A1B9_M0X0:",
             "L_A1BD_M0X0:"),
            ("CODE_80C96F_M0X0", "L_C973_M0X0:", "L_C9B1_M0X0:")):
        path, text, start, end = locate_function_block(
            sources, owner, start_label, end_label)
        block = text[start:end]
        begin = "Dkc1MarginProxyBeginRender(cpu);"
        finish = "Dkc1MarginProxyEndRender(cpu);"
        call_anchor = (
            "    {\n"
            "      /* JSL return frame -> cpu->S (Option-1) */\n"
        )
        if block.count(call_anchor) != 1:
            raise ValueError(
                f"expected one renderer JSL call in {owner}; "
                f"found {block.count(call_anchor)}")
        if begin in block or finish in block:
            if block.count(begin) != 1 or block.count(finish) < 1:
                raise ValueError(
                    f"ambiguous margin-proxy renderer hook in {owner}")
            # Migrate trees generated by the earlier label-entry hook.  In
            # CODE_80C96F that placement preceded the OAM reset/clear block,
            # so the renderer did real work that was immediately discarded.
            begin_line = "    Dkc1MarginProxyBeginRender(cpu);\n"
            if block.count(begin_line) != 1:
                raise ValueError(
                    f"unexpected margin-proxy begin indentation in {owner}")
            block = block.replace(begin_line, "", 1)
            block = block.replace(call_anchor, begin_line + call_anchor, 1)
            sources[path] = add_include(text[:start] + block + text[end:])
            continue

        block = block.replace(
            call_anchor,
            "    Dkc1MarginProxyBeginRender(cpu);\n" + call_anchor,
            1)

        # Cleanup before every return reachable after injection.
        return_pattern = re.compile(r"(?m)^(\s*)(return\s+[^;]+;)$")
        block, return_count = return_pattern.subn(
            lambda match: (match.group(1) +
                           "Dkc1MarginProxyEndRender(cpu);\n" +
                           match.group(1) + match.group(2)), block)
        if return_count < 1:
            raise ValueError(f"expected abnormal return path in {owner}")

        gotos = list(re.finditer(r"(?m)^(\s*)(goto\s+L_[0-9A-F]+_M0X0;)",
                                 block))
        if len(gotos) != 1:
            raise ValueError(
                f"expected one normal post-render goto in {owner}; "
                f"found {len(gotos)}")
        match = gotos[0]
        replacement = (match.group(1) +
                       "Dkc1MarginProxyEndRender(cpu);\n" +
                       match.group(1) + match.group(2))
        block = block[:match.start()] + replacement + block[match.end():]
        sources[path] = add_include(text[:start] + block + text[end:])


COOP_CULL_FAILURES = tuple((h["symbol"],h["failure"],h["success"],h["routine"])
                          for h in COOP_CATALOG["culls"])


def inject_coop_label(sources: dict[Path, str], symbol: str,
                      label: str, injection: str, *, after_deadline: bool = False,
                      header_name: str = "dkc1_coop_world.h",
                      before_statement: str | None = None) -> None:
    """Add a source-owned hook at one exact native basic-block boundary."""
    path, text = locate_function(sources, symbol)
    start, end = function_span(text, symbol)
    block = text[start:end]
    marker = f"  L_{label}_M0X0:\n"
    if block.count(marker) != 1:
        raise ValueError(f"expected one {symbol}/{label} co-op boundary")
    insert_at = block.index(marker) + len(marker)
    if after_deadline:
        label_start = insert_at
        following = re.search(r"\n  L_[0-9A-F]+_M[01]X[01]:", block[label_start:])
        label_end = label_start + following.start() if following else len(block)
        cycle_marker = "    cpu->coprocessor_master_cycles = cpu->master_cycles;\n"
        if block[label_start:label_end].count(cycle_marker) != 1:
            raise ValueError(f"expected one {symbol}/{label} cycle boundary")
        insert_at = block.index(cycle_marker, label_start, label_end) + len(cycle_marker)
        if before_statement is not None:
            # The shared catalog may name an instruction inside an AOT block.
            # Insert after its preceding native operands, before the JML itself.
            if block[insert_at:label_end].count(before_statement) != 1:
                raise ValueError(f"expected one {symbol}/{label} instruction boundary")
            insert_at = block.index(before_statement, insert_at, label_end)
    if injection in block:
        existing_at = insert_at - len(injection) if before_statement else insert_at
        if block.count(injection) != 1 or block[existing_at:existing_at + len(injection)] != injection:
            raise ValueError(f"ambiguous existing co-op hook {symbol}/{label}")
        return
    block = block[:insert_at] + injection + block[insert_at:]
    text = add_include(text[:start] + block + text[end:])
    header = f'#include "{header_name}"'
    if header not in text:
        text = text.replace(INCLUDE, INCLUDE + "\n" + header, 1)
    sources[path] = text


def adapt_coop_world(sources: dict[Path, str]) -> None:
    # Retire source-owned hooks from the former independent-cart policy.
    retired = {
        "CODE_B69F36_M0X0": "    if (Dkc1CoopMinecartIndependent(cpu)) goto L_9F6E_M0X0;\n",
    }
    for path, text in list(sources.items()):
        for symbol, hook in retired.items():
            if f"RecompReturn {symbol}(CpuState *cpu) {{" in text:
                start, end = function_span(text, symbol)
                block = text[start:end]
                if hook in block:
                    text = text[:start] + block.replace(hook, "") + text[end:]
        sources[path] = text
    # The water controller has completed, even if the next host deadline
    # interrupts the rest of this cartridge update. Never step it on redraw.
    inject_coop_events(sources, "camera")
    # Complete the activation pass, initialization and source-window culls
    # at their original phases; the gameplay timer is never repeated.
    inject_coop_events(sources, "scanner")
    for symbol, failure, success, routine in COOP_CULL_FAILURES:
        inject_coop_label(sources, symbol, failure,
                          f"    if (Dkc1CoopWorldKeepActor(cpu, {routine}))\n"
                          f"      goto L_{success}_M0X0;\n")


def adapt_coop_operands(sources: dict[Path, str]) -> None:
    # Level input consumes per-Kong buffers, not the map's effective pad.
    # Remove an earlier global-publication hook from already adapted units.
    symbol = "CODE_80A257_M0X0"
    path, text = locate_function(sources, symbol)
    start, end = function_span(text, symbol)
    sources[path] = text[:start] + text[start:end].replace(
        "    Dkc1CoopPublishMapPads(cpu);\n", "") + text[end:]
    for hook in COOP_OPERAND_HOOKS:
        path, text = locate_function(sources, hook["symbol"])
        start, end = function_span(text, hook["symbol"])
        if "aot_label" in hook:
            label = "\n  L_" + hook["aot_label"] + "_M0X0:"
            start = text.index(label, start, end)
            following = re.search(r"\n  L_\w+:", text[start + len(label):end])
            if following:
                end = start + len(label) + following.start()
        block = text[start:end]
        old = hook["read"]
        new = hook["call"].format(native=old)
        if block.count(new) == 1 and block.count(old) == 1:
            continue
        if block.count(old) != 1 or new in block:
            raise ValueError(f"expected one {hook['name']}")
        block = block.replace(old,new)
        text = text[:start] + block + text[end:]
        header = f'#include "{hook["header"]}"'
        if header not in text:
            text = text.replace(INCLUDE,INCLUDE+"\n"+header,1)
        sources[path] = text


def adapt_coop_mounted_partner(sources: dict[Path, str]) -> None:
    # Upgrade the old mount-only gate without leaving a second generated hook.
    for path, text in sources.items():
        sources[path] = text.replace(
            "    if (Dkc1CoopBlockMountedSwap(cpu)) goto L_BFA1_M0X0;\n", "")
    inject_coop_events(sources, "mount")


def adapt_coop_ropes(sources: dict[Path, str]) -> None:
    inject_coop_events(sources, "rope")


def adapt_coop_hud(sources: dict[Path, str]) -> None:
    # Capture only the completed native HUD pass. Effects and world actors
    # surround these boundaries and are rendered independently per view.
    inject_coop_events(sources, "hud")
    path, text = locate_function(sources, "CODE_BBA772_M0X0")
    start, end = function_span(text, "CODE_BBA772_M0X0")
    block = text[start:end]
    marker = "    { uint16 _ret_s = cpu->S;  /* RTL pop hardware return frame */"
    exits = [h for h in COOP_EVENT_HOOKS if h["group"] == "hud-return"]
    if len(exits) != 3 or len({h["call"] for h in exits}) != 1:
        raise ValueError("HUD return contracts disagree")
    call = f'    {exits[0]["call"]};\n'
    if block.count(marker) != 3 or block.count(call) not in (0, 3):
        raise ValueError("expected three BBA772 HUD sprite returns")
    if call not in block:
        block = block.replace(marker, call + marker)
        sources[path] = text[:start] + block + text[end:]


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


def apply_adjacent_adapter(sources: dict[Path, str], name: str) -> None:
    # Resolve beside this script, including when a caller imports it by path
    # from a different working directory.
    spec = importlib.util.spec_from_file_location(
        name, Path(__file__).with_name(name + ".py"))
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    module.apply_overrides(sources)


def adapt_coop_entities(sources: dict[Path, str]) -> None:
    apply_adjacent_adapter(sources, "apply_dkc1_coop_entities")


def adapt_coop_graphics(sources: dict[Path, str]) -> None:
    apply_adjacent_adapter(sources, "apply_dkc1_coop_graphics")


def adapt_coop_schedule(sources: dict[Path, str]) -> None:
    apply_adjacent_adapter(sources, "apply_dkc1_coop_schedule")


def apply_overrides(generated_dir: Path) -> list[Path]:
    sources = load_sources(generated_dir)
    original = dict(sources)

    # Widen DKC's own rolling-tilemap initializers and horizontal stream
    # selectors. This is required for private ice/underwater builders that the
    # host's generic ROM decoder cannot reproduce. The helpers fail closed in
    # title/map/narrow scenes and retain stock logical camera bounds.
    adapt_function_cpu_constant(
        sources, "CODE_809E32_M0X0", "L_9E9C_M0X0:",
        "0x100", "Dkc1VideoInitialBackstep")
    adapt_function_cpu_constant(
        sources, "CODE_809E32_M0X0", "L_9E9C_M0X0:",
        "0x20", "Dkc1VideoInitialColumnCount")
    adapt_function_cpu_constant(
        sources, "CODE_80C501_M0X0", "L_C53A_M0X0:",
        "0x108", "Dkc1VideoInitialBackstep")
    adapt_function_cpu_constant(
        sources, "CODE_80C501_M0X0", "L_C53A_M0X0:",
        "0x21", "Dkc1VideoInitialColumnCount")
    for symbol, label in (
        ("Level_BuildTilemapColumn_TypeA_M0X0", "L_8722_M0X0:"),
        ("Level_DMATilemapColumnToVRAM_M0X0", "L_8868_M0X0:"),
        ("CODE_8188A8_M0X0", "L_88CE_M0X0:"),
        ("Level_BuildTilemapColumn_TypeB_M0X0", "L_8E17_M0X0:"),
    ):
        adapt_stream_selector(sources, symbol, label)
    adapt_vertical_row_builds(sources)

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
            "0x20", "Dkc1VideoObjectScannerCullLeft")
    for symbol, block in SPAN_BLOCKS:
        adapt_function_constant(
            sources, symbol, f"L_{block}_M0X0:",
            "0x140", "Dkc1VideoObjectScannerCullSpan")
    for symbol, block in PREFETCH_BLOCKS:
        adapt_function_constant(
            sources, symbol, f"L_{block}_M0X0:",
            "0x120", "Dkc1VideoObjectScannerCullLeft")
    adapt_placed_actor_phase_guard(sources)
    adapt_type5_child_retry(sources)
    adapt_margin_proxy_render_calls(sources)
    adapt_coop_world(sources)
    adapt_coop_operands(sources)
    adapt_coop_hud(sources)
    adapt_coop_mounted_partner(sources)
    adapt_coop_ropes(sources)
    inject_coop_events(sources, "map")
    inject_coop_events(sources, "wheel")
    inject_coop_events(sources, "pickup")

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

    # Keep literal actor-table bases available to the co-op virtual-handle
    # adapter before generated units are written. The standalone module owns
    # the field inventory and rejects unmatched allocator/loop contracts.
    adapt_coop_entities(sources)
    adapt_coop_graphics(sources)
    adapt_coop_schedule(sources)

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
