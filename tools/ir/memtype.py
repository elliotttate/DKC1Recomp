"""Stage 3: memory typing — resolve operand expressions, classify regions.

Every memory operand gets, when provable:
  op.ea     — resolved base address (24-bit for long modes, 16-bit else)
  op.region — wram / mmio / rom / db-dependent / indirect / stack / block
  op.sym    — typed rendering: curated or RAM-map name, NorSpr SoA
              accesses lifted to NorSpr[X].Field
Direct-page operands assume D=0 (DKC1 gameplay convention; TCD sites are
counted and surfaced, never silently folded). Absolute operands with
0x2000 <= ea < 0x8000 depend on DB and are honestly tagged db-dependent
unless they are the universally mirrored MMIO windows.

Symbol values come from the disassembly's own define files (DKC1/*.asm +
Global/**/*.asm) and struct declarations; expressions evaluate through a
whitelisted-AST evaluator, never Python eval.
"""
from __future__ import annotations

import ast
import re
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import atlas  # noqa: E402
from ir import isa  # noqa: E402
from ir.decode import IROp  # noqa: E402

DEFINE_RE = re.compile(r"^!(\S+)\s*=\s*([^;\r\n]+)", re.M)
STRUCT_RE = re.compile(r"^struct\s+(\S+)\s+(\S+)", re.M)

MEM_MODES = {"dp", "dpx", "dpy", "abs", "absx", "absy", "long", "longx"}
IND_MODES = {"ind", "indy", "indx", "indl", "indly"}
SR_MODES = {"sr", "sriy"}

_ALLOWED_NODES = (ast.Expression, ast.BinOp, ast.UnaryOp, ast.Constant,
                  ast.Add, ast.Sub, ast.Mult, ast.FloorDiv, ast.BitAnd,
                  ast.BitOr, ast.BitXor, ast.LShift, ast.RShift,
                  ast.USub, ast.Invert)


def _parse_literal(text: str) -> int:
    if text.startswith("$"):
        return int(text[1:], 16)
    if text.startswith("%"):
        return int(text[1:], 2)
    return int(text)


class Resolver:
    def __init__(self) -> None:
        self.defines: dict[str, int] = {}
        self.structs: dict[str, int] = {}
        self._load_defines()
        self._ram_labels = atlas.load_ram_map_labels()  # addr -> [names]
        _, self._ram_names = atlas.load_rename_map()    # addr -> entry
        self._label_addrs = sorted(self._ram_labels)
        # MMIO addr -> REGISTER_* name
        self.mmio_names: dict[int, str] = {}
        for name, value in self.defines.items():
            if name.startswith("REGISTER_") and 0x2000 <= value <= 0x43FF:
                self.mmio_names.setdefault(value, name)
        self.tcd_sites = 0

    def _load_defines(self) -> None:
        roots = [atlas.DISASM_ROOT / "DKC1",
                 atlas.DISASM_ROOT / "Global"]
        raw: dict[str, str] = {}
        raw_structs: dict[str, str] = {}
        seen: set[Path] = set()
        for root in roots:
            if not root.is_dir():
                continue
            for path in sorted(root.rglob("*.asm")):
                if path in seen:
                    continue
                seen.add(path)
                try:
                    text = path.read_text(encoding="utf-8",
                                          errors="replace")
                except OSError:
                    continue
                for match in DEFINE_RE.finditer(text):
                    raw.setdefault(match.group(1),
                                   match.group(2).strip())
                for match in STRUCT_RE.finditer(text):
                    raw_structs.setdefault(match.group(1), match.group(2))
        # defines chain to other defines: resolve to a fixpoint
        changed = True
        while changed:
            changed = False
            for name, text in raw.items():
                if name in self.defines:
                    continue
                value = self.eval_expr(text)
                if value is not None:
                    self.defines[name] = value
                    changed = True
        for name, text in raw_structs.items():
            base = self.eval_expr(text)
            if base is not None:
                self.structs[name] = base

    def eval_expr(self, expr: str) -> int | None:
        """Whitelisted-AST evaluation of an Asar operand expression."""
        text = expr.strip()
        if not text:
            return None
        # struct member: resolve base, drop the field (offset unknown)
        struct = re.fullmatch(
            r"([A-Za-z_][A-Za-z0-9_]*)\[(\$?[0-9A-Fa-f]+)\]\.\w+"
            r"(.*)", text)
        if struct:
            base = self.structs.get(struct.group(1))
            if base is None:
                return None
            # keep the array index; the field offset stays symbolic
            return base
        # token substitution: $hex, %bin, !defines, CODE_/DATA_, symbols
        def sub(match: re.Match) -> str:
            token = match.group(0)
            if token.startswith("$"):
                return f" 0x{token[1:]} "
            if token.startswith("%"):
                return f" 0b{token[1:]} "
            if token.startswith("!"):
                value = self.defines.get(token[1:])
                return f" {value} " if value is not None else " None "
            named = re.fullmatch(r"(CODE|DATA)_([0-9A-Fa-f]{6})", token)
            if named:
                return f" 0x{named.group(2)} "
            value = self.defines.get(token, self.structs.get(token))
            return f" {value} " if value is not None else " None "

        substituted = re.sub(
            r"\$[0-9A-Fa-f]+|%[01]+|![A-Za-z_][A-Za-z0-9_]*"
            r"|[A-Za-z_][A-Za-z0-9_]*", sub, text)
        if "None" in substituted:
            return None
        try:
            tree = ast.parse(substituted.strip(), mode="eval")
        except SyntaxError:
            return None
        for node in ast.walk(tree):
            if not isinstance(node, _ALLOWED_NODES):
                return None
        try:
            return int(eval(compile(tree, "<expr>", "eval"), {}, {}))
        except Exception:  # noqa: BLE001
            return None

    def _wram_sym(self, addr: int, op: IROp) -> str:
        """Name a WRAM address; lift NorSpr SoA accesses."""
        offset = addr & 0x1FFFF
        labels = self._ram_labels.get(offset)
        suffix = ""
        if not labels:
            # nearest preceding label within 0x40 bytes -> label+off
            import bisect
            i = bisect.bisect_right(self._label_addrs, offset) - 1
            if i >= 0 and offset - self._label_addrs[i] <= 0x40:
                base = self._label_addrs[i]
                labels = self._ram_labels[base]
                suffix = f"+${offset - base:02X}"
        if not labels:
            return ""
        label = labels[0]
        curated = self._ram_names.get(offset)
        friendly = curated["name"] if curated and curated.get("name") \
            else None
        soa = re.fullmatch(r"RAM_DKC1_NorSpr_(\w+)", label)
        if soa and op.index in ("x", "y"):
            field = friendly or soa.group(1)
            name = f"NorSpr[{op.index.upper()}].{field}{suffix}"
        elif op.index in ("x", "y") and not suffix:
            name = f"{friendly or label}[{op.index.upper()}]"
        else:
            name = (friendly or label) + suffix
        if friendly and friendly != name and not soa:
            return name
        return name

    def _classify(self, addr: int, is_long: bool,
                  op: IROp) -> tuple[str, str]:
        if is_long:
            bank = (addr >> 16) & 0xFF
            offset = addr & 0xFFFF
            if bank in (0x7E, 0x7F):
                return "wram", self._wram_sym(addr, op)
            if (bank < 0x40 or 0x80 <= bank < 0xC0) and offset < 0x2000:
                return "wram", self._wram_sym(offset, op)
            if (bank < 0x40 or 0x80 <= bank < 0xC0) and \
                    0x2000 <= offset < 0x8000:
                name = self.mmio_names.get(offset, "")
                return "mmio", name
            return "rom", ""
        if addr < 0x2000:
            return "wram", self._wram_sym(addr, op)
        if 0x2100 <= addr <= 0x21FF or 0x4200 <= addr <= 0x43FF or \
                addr in (0x4016, 0x4017):
            return "mmio", self.mmio_names.get(addr, "")
        if addr >= 0x8000:
            return "rom", ""
        return "db-dependent", ""

    def annotate(self, op: IROp) -> str:
        """Resolve + classify one op; returns the stats kind."""
        if op.mnemonic == "TCD":
            self.tcd_sites += 1
        if op.mode in ("imp", "acc", "imm", "rel", "rell"):
            return "nonmem"
        if op.mnemonic in ("JMP", "JML", "JSR", "JSL", "PEA", "PER"):
            return "nonmem"
        if op.mode == "block":
            op.region = "block"
            return "block"
        if op.mode in SR_MODES:
            op.region = "stack"
            return "stack"
        if op.mode in IND_MODES:
            # resolve the POINTER's dp location; data address is dynamic
            pointer = self.eval_expr(op.expr)
            if pointer is not None:
                op.ea = pointer
                op.sym = self._wram_sym(pointer, op) or op.sym
            op.region = "indirect"
            return "indirect"
        if op.mode not in MEM_MODES:
            return "nonmem"
        value = self.eval_expr(op.expr)
        if value is None:
            return "unresolved"
        op.ea = value
        is_long = op.mode in ("long", "longx")
        op.region, sym = self._classify(value, is_long, op)
        if sym:
            op.sym = sym
        return op.region
