"""Prepare DKC1 interpreter hooks without editing the pinned engine.

The inserted callback runs before opcode classification, preserving the
bridge's call/return ownership. Rule tables share the AOT adapter contracts.
"""
from pathlib import Path
import argparse
import hashlib
import sys

import apply_dkc1_coop_entities as entities
import apply_dkc1_coop_graphics as graphics
import apply_dkc1_coop_schedule as schedule
import apply_dkc1_widescreen_overrides as world

ROOT = Path(__file__).resolve().parents[1]


def adapt(source: str) -> str:
    marker = "        if (s_pre_opcode_hook_count > 0) {"
    if source.count(marker) != 1:
        raise ValueError("pinned interpreter hook boundary changed")
    insertion = """        /* DKC1 co-op: act before the bridge classifies calls/returns. */
        if (Dkc1CoopEntitiesActive() || Dkc1CoopActorPending()) {
            sync_interp_to_cpu(&in, cpu);
            uint32_t coop_redirect = Dkc1CoopInterpBefore(cpu, pc_before);
            sync_cpu_to_interp(cpu, &in);
            if (coop_redirect) {
                in.k = (uint8_t)(coop_redirect >> 16);
                in.pc = (uint16_t)coop_redirect;
                continue;
            }
        }
"""
    return source.replace(marker, insertion + marker, 1)


def loop_rules(rom: bytes, loops=None) -> list[tuple[int, str, int]]:
    sys.path.insert(0, str(ROOT / "snesrecomp/recompiler"))
    import snes65816 as decoder
    decoder.set_rom_mapping(decoder.ROM_MAP_HIROM)
    result = set()
    for function, label, register, bound in (
            entities.FORWARD_LOOPS if loops is None else loops):
        bank = int(function[5:7], 16) if function.startswith("CODE_") else 0xBF
        pc = int(label, 16)
        for _ in range(35):
            insn = decoder.decode_insn(rom, decoder.rom_offset(bank, pc), pc, bank, 0, 0)
            if insn.mnem == "CP" + register and insn.operand == bound:
                result.add(((bank << 16) | pc, register, bound))
                break
            pc += insn.length
        else:
            raise ValueError(f"missing actor loop comparison: {function}/{label}")
    return sorted(result)


def rules(rom: bytes) -> str:
    for hook in world.COOP_CATALOG["culls"]:
        pc=int(hook["pc"],16)
        at=pc & 0x3fffff
        expected=bytes.fromhex(hook["bytes"])
        if rom[at:at+len(expected)]!=expected:
            raise ValueError(f"native cull contract changed: {pc:06X}")
    # The two horizontal culls return carry-clear for an in-window actor.
    for pc in (0xbcbc73, 0xbcbc94):
        offset = pc & 0x3fffff
        if rom[offset:offset + 4] != bytes.fromhex('18 60 38 60'):
            raise ValueError(f"native horizontal cull return changed: {pc:06x}")
    # Pin the complete native instruction at each residual boundary, including
    # preceding cursor/pop operands when the hook is inside an AOT block.
    seen = {int(h["pc"], 16) for h in world.COOP_CATALOG["culls"]}
    seen.update(entry for _symbol, entry, _ret, _call in graphics.RETURN_HOOKS)
    for section in ("hooks", "entity_hooks", "resource_hooks", "immediates"):
        for hook in world.COOP_CATALOG[section]:
            pc = int(hook["pc"], 16)
            if pc in seen:
                raise ValueError(f"duplicate co-op boundary ${pc:06X}")
            seen.add(pc)
            at = int(hook.get("source_pc", hook["pc"]), 16) & 0x3fffff
            expected = bytes.fromhex(hook["bytes"])
            if rom[at:at + len(expected)] != expected:
                raise ValueError(f"co-op {'event' if section == 'hooks' else section} contract changed at ${pc:06X}")
    live_pcs = {pc for pc, _, _ in loop_rules(rom, (
        rule for rule in entities.FORWARD_LOOPS
        if rule[:2] in entities.LIVE_FORWARD_LOOPS))}
    lines = ["/* Generated from the source-owned AOT adapter contracts. */",
             "static const CoopLoopRule kCoopLoopRules[] = {"]
    for pc, reg, bound in loop_rules(rom):
        lines.append(f"  {{0x{pc:06x}u, 0x{bound:x}u, '{reg}', {int(pc in live_pcs)}}},")
    lines += ["};", "", "static uint32_t CoopResourceRedirect(CpuState *cpu, uint32_t pc) {",
              "  switch (pc) {"]
    for _symbol, entry, ret, call in graphics.RETURN_HOOKS:
        call = call.replace("_host_return_pc24", "UINT32_MAX")
        lines += [f"    case 0x{entry:06x}u:", f"      return {call} ? 0x{ret:06x}u : 0;"]
    for symbol, failure, success, routine in world.COOP_CULL_FAILURES:
        bank = symbol.split('_')[1][:2].lower()
        lines += [f"    case 0x{bank}{failure.lower()}u:",
                  f"      return Dkc1CoopWorldKeepActor(cpu, {routine}u) ? 0x{bank}{success.lower()}u : 0;"]
    lines += ["    default: return 0;", "  }", "}", "",
              "static uint32_t CoopScheduledRedirect(CpuState *cpu, uint32_t pc) {",
              "  switch (pc) {"]
    for _owner, pc, target, ret, _overlay in schedule.CALLS:
        opcode = rom[pc & 0x3fffff:(pc & 0x3fffff) + 4]
        if opcode != bytes((0x22, target & 255, (target >> 8) & 255, target >> 16)) or ret != pc + 4:
            raise ValueError(f"actor JSL contract changed at ${pc:06X}")
        if _overlay and rom[(pc & 0x3fffff) - 2:pc & 0x3fffff] != bytes.fromhex("86 82"):
            raise ValueError(f"actor dispatch index store changed at ${pc:06X}")
        lines += [f"    case 0x{pc:06x}u:",
                  f"      return Dkc1CoopScheduleActor(cpu, 0x{target:06x}u, 0x{ret:06x}u) ? 0x{ret:06x}u : 0;"]
    lines += ["    default: return 0;", "  }", "}", "",
              "static int CoopIsDispatchBoundary(uint32_t pc) {",
              "  switch (pc) {"]
    for _owner, pc, _target, ret, _overlay in schedule.CALLS:
        lines += [f"    case 0x{pc:06x}u: case 0x{ret:06x}u: return 1;"]
    lines += ["    default: return 0;", "  }", "}", ""]
    lines += ["static uint32_t CoopEventRedirect(CpuState *cpu, uint32_t pc) {", "  switch (pc) {"]
    for hook in world.COOP_EVENT_HOOKS:
        pc = int(hook["pc"], 16)
        lines.append(f"    case 0x{pc:06x}u:")
        if "redirect" in hook:
            lines.append(f'      return {hook["call"]} ? 0x{hook["redirect"].lower()}u : 0;')
        else:
            lines.append(f'      {hook["call"]}; return 0;')
    lines += ["    default: return 0;", "  }", "}", ""]
    lines += ["static uint32_t CoopNativeRedirect(CpuState *cpu, uint32_t pc) {",
              "  switch (pc) {"]
    for hook in entities.ENTITY_HOOKS:
        if hook["kind"] not in ("label", "collision-start", "clear-range"):
            raise ValueError(f"unknown native actor hook kind: {hook['kind']}")
        lines.append(f"    case 0x{hook['pc'].lower()}u:")
        call = (f"cpu->A = {hook['call']}" if hook["kind"] == "collision-start"
                else hook["call"])
        if "redirect" in hook:
            lines.append(f"      return {call} ? 0x{hook['redirect'].lower()}u : 0;")
        else:
            lines.append(f"      {call}; return 0;")
    for hook in graphics.RESOURCE_HOOKS:
        lines.append(f"    case 0x{hook['pc'].lower()}u:")
        if hook["kind"] == "group-release":
            lines += ["    {", "      uint16_t _coop_graphics_handle = cpu->Y;",
                      f"      if ({hook['call']}) {{",
                      "        cpu->Y = _coop_graphics_handle;",
                      f"        return 0x{hook['redirect'].lower()}u;",
                      "      }", "      return 0;", "    }"]
        elif hook["kind"] == "palette":
            # These interpreter callbacks use raw WRAM, unlike AOT's native
            # bus reads. Preserve their original open-bus behavior.
            call = hook["call"].format(read86="CoopRaw16(cpu, 0x86)",
                                       read82="CoopRaw16(cpu, 0x82)")
            lines.append(f"      {call}; return 0;")
        else:
            raise ValueError(f"unknown graphics hook kind: {hook['kind']}")
    for _owner, pc, _target, ret, overlay in schedule.CALLS:
        if overlay:
            lines += [f"    case 0x{pc:06x}u:",
                      "      Dkc1VideoBeginPlacedActorDispatch(cpu); return 0;",
                      f"    case 0x{ret:06x}u:",
                      "      Dkc1VideoEndPlacedActorDispatch(cpu); return 0;"]
    lines += ["    default: return 0;", "  }", "}", "",
              "static int CoopSearchImmediate(uint32_t pc, uint16_t *value) {",
              "  switch (pc) {"]
    for hook in entities.IMMEDIATE_HOOKS:
        lines += [f"    case 0x{hook['pc'].lower()}u:",
                  f"      *value = {hook['call']}; return 1;"]
    lines += ["    default: return 0;", "  }", "}", "",
              "static int CoopPrivateEventBoundary(uint32_t pc) {", "  switch (pc) {"]
    for hook in world.COOP_EVENT_HOOKS + entities.ENTITY_HOOKS:
        if hook.get("private_boundary"):
            lines.append(f"    case 0x{hook['pc'].lower()}u: return 1;")
    lines += ["    default: return 0;", "  }", "}", ""]
    lines += ["static int CoopOperandRead(CpuState *cpu, uint32_t pc, uint8_t bank,",
              "                           uint16_t address, uint8_t *value) {",
              "  if ((bank & 0x40u) && bank != 0x7eu) return 0;",
              "  pc |= 0x800000u; /* Same HiROM aliases as native opcode policies. */",
              "  switch (pc) {"]
    seen = set()
    for hook in world.COOP_OPERAND_HOOKS:
        pc = int(hook["pc"],16)
        if pc in seen or hook["width"] != 2:
            raise ValueError("unsupported or duplicate operand contract")
        seen.add(pc)
        expected = bytes.fromhex(hook["bytes"])
        at = pc & 0x3fffff
        if rom[at:at+len(expected)] != expected:
            raise ValueError(f"{hook['name']} changed: {pc:06X}")
        address = int(hook["address"],16)
        call = hook["call"].format(native="native")
        lines += [f"    case 0x{pc:06x}u:",
                  f"      if (address >= 0x{address:04x}u && address < 0x{address+2:04x}u) {{",
                  f"        unsigned shift=8u*(address-0x{address:04x}u);",
                  "        uint16_t native=(uint16_t)((uint16_t)cpu_read8(cpu,bank,address)<<shift);",
                  f"        *value=(uint8_t)({call}>>shift);",
                  "        return 1;", "      }", "      break;"]
    lines += ["    default: break;", "  }", "  return 0;", "}", ""]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=ROOT / "dkc1.sfc")
    args = parser.parse_args()
    rom = args.rom.read_bytes()
    if hashlib.sha256(rom).hexdigest() != "fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15":
        raise ValueError("co-op interpreter contracts require the supported stock ROM")
    source = (ROOT / "snesrecomp/runner/src/snes/interp_bridge.c").read_text(encoding="utf-8")
    for name, result in (("dkc1_coop_interp_bridge.inc", adapt(source)),
                         ("dkc1_coop_interp_rules.inc", rules(rom))):
        target = ROOT / "generated/host" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_text(encoding="utf-8") != result:
            target.write_text(result, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
