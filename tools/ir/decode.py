"""Stage 1: instruction_index.csv rows -> IR ops (lossless, validated).

Each IROp keeps the verbatim operand expression and source row; nothing
is normalized away. Addressing mode is derived from the operand's
syntactic shape plus the Asar width suffix, then proven two ways by
ir_validate.py: computed length == the listing's size column, and
computed opcode byte == the actual ROM byte at that address (HiROM
mirror fold). Asar struct syntax (`FOO[$02].Bar`) is plain absolute
addressing, NOT indirect-long — the classifier keys on a leading
bracket, never on brackets mid-expression.
"""
from __future__ import annotations

import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import atlas  # noqa: E402
from ir import isa  # noqa: E402

CONFIG_DIR = atlas.PSEUDO / "config"

SUFFIX_BYTES = {"b": 1, "w": 2, "l": 3}


@dataclass
class IROp:
    addr: int                 # 24-bit pc
    size: int                 # listing's byte length (validated)
    mnemonic: str
    suffix: str | None        # 'b'/'w'/'l' as written, None if bare
    mode: str                 # isa mode name
    expr: str                 # verbatim operand expression (no index part)
    index: str | None         # 'x'/'y'/'s' top-level index register
    expr2: str = ""           # MVN/MVP second operand
    function: str = ""        # containing function label
    label: str = ""           # label at this address (branch target etc.)
    source_line: str = ""
    comment: str = ""         # trailing ; comment, kept verbatim
    # stage-2+ annotations
    mw: int | None = None     # M width bit at this op (0=16,1=8), if proven
    xw: int | None = None     # X width bit
    width_assumed: bool = False   # True if mw/xw rest on call-preservation
    target: int | None = None     # resolved branch/jump/call target
    ea: int | None = None         # resolved effective/base address (stage 3)
    region: str = ""              # wram/mmio/rom/db-dependent (stage 3)
    sym: str = ""                 # typed rendering (stage 3)

    @property
    def opcode(self) -> int:
        return isa.OPCODES[(self.mnemonic, self.mode)]


class DecodeError(ValueError):
    pass


def _split_mnemonic(token: str) -> tuple[str, str | None]:
    if "." in token:
        mnemonic, suffix = token.split(".", 1)
        if suffix not in SUFFIX_BYTES:
            raise DecodeError(f"bad suffix {token!r}")
        return mnemonic.upper(), suffix
    return token.upper(), None


def _split_index(text: str) -> tuple[str, str | None]:
    """Split a trailing top-level ,x / ,y / ,s (commas inside () []
    never count)."""
    depth = 0
    comma = None
    for i, ch in enumerate(text):
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        elif ch == "," and depth == 0:
            comma = i
    if comma is None:
        return text, None
    reg = text[comma + 1:].strip().lower()
    if reg in ("x", "y", "s"):
        return text[:comma].strip(), reg
    return text, None


def _wrapped(text: str, open_ch: str, close_ch: str) -> bool:
    """True if text is fully enclosed by ONE outer pair of open/close."""
    if not text.startswith(open_ch) or not text.endswith(close_ch):
        return False
    depth = 0
    for i, ch in enumerate(text):
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
            if depth == 0:
                return i == len(text) - 1
    return False


def classify(mnemonic: str, suffix: str | None,
             operand: str) -> tuple[str, str, str | None, str]:
    """-> (mode, expr, index, expr2)."""
    if not operand:
        if (mnemonic, "acc") in isa.OPCODES:
            return "acc", "", None, ""
        return "imp", "", None, ""
    if mnemonic in ("MVN", "MVP"):
        src, dst = operand.split(",", 1)
        return "block", src.strip(), None, dst.strip()
    if operand.startswith("#"):
        return "imm", operand[1:].strip(), None, ""

    base, reg = _split_index(operand)
    if _wrapped(base, "(", ")"):
        inner, ireg = _split_index(base[1:-1].strip())
        if ireg == "x":
            if mnemonic in ("JMP", "JML", "JSR"):
                return "absindx", inner, "x", ""
            return "indx", inner, "x", ""
        if ireg == "s":
            return "sriy" if reg == "y" else "sr", inner, reg, ""
        if mnemonic in ("JMP", "JML", "JSR"):
            return "absind", inner, None, ""
        if mnemonic == "PEI":
            return "ind", inner, None, ""
        return ("indy", inner, "y", "") if reg == "y" \
            else ("ind", inner, None, "")
    if _wrapped(base, "[", "]"):
        inner = base[1:-1].strip()
        if mnemonic in ("JMP", "JML"):
            return "absindl", inner, None, ""
        return ("indly", inner, "y", "") if reg == "y" \
            else ("indl", inner, None, "")

    if mnemonic in isa.BRANCHES or mnemonic == "BRA":
        return "rel", base, None, ""
    if mnemonic in ("BRL", "PER"):
        return "rell", base, None, ""
    if mnemonic == "PEA":
        return "abs", base, None, ""
    if reg == "s":
        return "sr", base, "s", ""
    if suffix is None:
        # lone bare form in the corpus: `JMP CODE_xxxxxx` (absolute)
        if mnemonic in ("JMP", "JSR"):
            return "abs", base, None, ""
        if mnemonic == "JML":
            return "long", base, None, ""
        raise DecodeError(f"memory operand without width suffix: "
                          f"{mnemonic} {operand!r}")
    fam = {"b": "dp", "w": "abs", "l": "long"}[suffix]
    if reg == "x":
        fam = {"dp": "dpx", "abs": "absx", "long": "longx"}[fam]
    elif reg == "y":
        if fam == "long":
            raise DecodeError(f"long,y does not exist: {operand!r}")
        fam = {"dp": "dpy", "abs": "absy"}[fam]
    return fam, base, reg, ""


def decode_row(row: dict) -> IROp:
    asm = row["assembly"]
    comment = ""
    if "\t" in asm:
        asm, _, tail = asm.partition("\t")
        comment = tail.strip().lstrip("\t; ")
    asm = asm.strip()
    parts = asm.split(None, 1)
    mnemonic, suffix = _split_mnemonic(parts[0])
    operand = parts[1].strip() if len(parts) > 1 else ""
    mode, expr, index, expr2 = classify(mnemonic, suffix, operand)

    if mode == "imm":
        if mnemonic in ("REP", "SEP", "WDM", "BRK", "COP"):
            operand_bytes = 1
        elif suffix is not None:
            operand_bytes = SUFFIX_BYTES[suffix]
        else:
            raise DecodeError(f"unsized immediate: {asm!r}")
    else:
        operand_bytes = isa.MODE_OPERAND_BYTES[mode]
        if suffix is not None and SUFFIX_BYTES[suffix] != operand_bytes:
            raise DecodeError(
                f"suffix .{suffix} disagrees with mode {mode}: {asm!r}")

    op = IROp(addr=atlas.row_address(row), size=1 + operand_bytes,
              mnemonic=mnemonic, suffix=suffix, mode=mode, expr=expr,
              index=index, expr2=expr2, function=row["function"],
              label=row["labels"], source_line=row["source_line"],
              comment=comment)
    if (op.mnemonic, op.mode) not in isa.OPCODES:
        raise DecodeError(f"no opcode for {op.mnemonic}/{op.mode}: {asm!r}")

    # Direct branch/jump/call targets are CODE_XXXXXX names.
    if op.mode in ("rel", "rell", "abs", "long") and (
            mnemonic in isa.BRANCHES or mnemonic in isa.UNCONDITIONAL or
            mnemonic in isa.CALLS or mnemonic in isa.JUMPS):
        match = re.fullmatch(r"CODE_([0-9A-Fa-f]{6})", op.expr)
        if match:
            op.target = int(match.group(1), 16)
    return op


def load_functions() -> dict[str, list[IROp]]:
    """function label -> ops in address order (whole corpus)."""
    functions: dict[str, list[IROp]] = {}
    for row in atlas.iter_instruction_index():
        op = decode_row(row)
        functions.setdefault(row["function"], []).append(op)
    for ops in functions.values():
        ops.sort(key=lambda o: o.addr)
    return functions


def load_func_facts() -> dict[int, dict]:
    """entry addr24 -> {label, end, entry_mx, exit_mx} from the decoder's
    proven static facts in Pseudocode/config/*.cfg."""
    facts: dict[int, dict] = {}
    for cfg in sorted(CONFIG_DIR.glob("bank*.cfg")):
        bank = None
        for line in cfg.read_text(errors="replace").splitlines():
            line = line.strip()
            if line.startswith("bank"):
                match = re.match(r"bank\s*=\s*0x([0-9A-Fa-f]+)", line)
                if match:
                    bank = int(match.group(1), 16)
                continue
            if not line.startswith("func ") or bank is None:
                continue
            match = re.match(
                r"func\s+(\S+)\s+([0-9A-Fa-f]{4})\s+end:([0-9A-Fa-f]{4})"
                r"\s+entry_mx:(\d),(\d)(?:\s+exit_mx:(\d),(\d))?", line)
            if not match:
                continue
            addr = (bank << 16) | int(match.group(2), 16)
            facts[addr] = {
                "label": match.group(1),
                "end": (bank << 16) | int(match.group(3), 16),
                "entry_mx": (int(match.group(4)), int(match.group(5))),
                "exit_mx": (int(match.group(6)), int(match.group(7)))
                if match.group(6) is not None else None,
            }
    return facts


def rom_offset(pc24: int) -> int | None:
    """HiROM mirror fold (the pipeline's pc24 convention)."""
    bank = (pc24 >> 16) & 0xFF
    addr = pc24 & 0xFFFF
    if 0xC0 <= bank <= 0xFF:
        return ((bank - 0xC0) << 16) | addr
    if addr >= 0x8000:
        return ((bank & 0x3F) << 16) | addr
    return None


def load_rom(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) % 0x8000 == 0x200:   # headered copy: strip
        data = data[0x200:]
    return data
