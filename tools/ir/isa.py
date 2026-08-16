"""65816 ISA tables: addressing modes, opcode bytes, def/use, flags.

Single source of truth for the IR stages. Mode names:
  imp acc imm                        (no memory operand)
  dp dpx dpy abs absx absy long longx (direct memory)
  ind indy indx indl indly sr sriy    (dp-based indirection)
  absind absindx absindl              (JMP/JSR indirection)
  rel rell block                      (branches, MVN/MVP)
Validated by tools/ir_validate.py stage1: computed opcode byte and length
must match the ROM byte and the listing's size column on every row.
"""
from __future__ import annotations

# operand byte count implied by each mode ('imm' comes from the width
# suffix the listing carries; REP/SEP immediates are always 1 byte).
MODE_OPERAND_BYTES = {
    "imp": 0, "acc": 0,
    "dp": 1, "dpx": 1, "dpy": 1,
    "abs": 2, "absx": 2, "absy": 2,
    "long": 3, "longx": 3,
    "ind": 1, "indy": 1, "indx": 1, "indl": 1, "indly": 1,
    "sr": 1, "sriy": 1,
    "absind": 2, "absindx": 2, "absindl": 2,
    "rel": 1, "rell": 2, "block": 2,
}

# (mnemonic, mode) -> opcode byte. Complete 65816 matrix, keyed the way
# the Asar listing spells it (JMP.l for JML, JMP.w [..] for the DC form).
OPCODES: dict[tuple[str, str], int] = {}


def _op(mnemonic: str, **modes: int) -> None:
    for mode, byte in modes.items():
        OPCODES[(mnemonic, mode)] = byte


_op("LDA", imm=0xA9, dp=0xA5, dpx=0xB5, abs=0xAD, absx=0xBD, absy=0xB9,
    long=0xAF, longx=0xBF, ind=0xB2, indy=0xB1, indx=0xA1, indl=0xA7,
    indly=0xB7, sr=0xA3, sriy=0xB3)
_op("STA", dp=0x85, dpx=0x95, abs=0x8D, absx=0x9D, absy=0x99,
    long=0x8F, longx=0x9F, ind=0x92, indy=0x91, indx=0x81, indl=0x87,
    indly=0x97, sr=0x83, sriy=0x93)
_op("ADC", imm=0x69, dp=0x65, dpx=0x75, abs=0x6D, absx=0x7D, absy=0x79,
    long=0x6F, longx=0x7F, ind=0x72, indy=0x71, indx=0x61, indl=0x67,
    indly=0x77, sr=0x63, sriy=0x73)
_op("SBC", imm=0xE9, dp=0xE5, dpx=0xF5, abs=0xED, absx=0xFD, absy=0xF9,
    long=0xEF, longx=0xFF, ind=0xF2, indy=0xF1, indx=0xE1, indl=0xE7,
    indly=0xF7, sr=0xE3, sriy=0xF3)
_op("CMP", imm=0xC9, dp=0xC5, dpx=0xD5, abs=0xCD, absx=0xDD, absy=0xD9,
    long=0xCF, longx=0xDF, ind=0xD2, indy=0xD1, indx=0xC1, indl=0xC7,
    indly=0xD7, sr=0xC3, sriy=0xD3)
_op("AND", imm=0x29, dp=0x25, dpx=0x35, abs=0x2D, absx=0x3D, absy=0x39,
    long=0x2F, longx=0x3F, ind=0x32, indy=0x31, indx=0x21, indl=0x27,
    indly=0x37, sr=0x23, sriy=0x33)
_op("ORA", imm=0x09, dp=0x05, dpx=0x15, abs=0x0D, absx=0x1D, absy=0x19,
    long=0x0F, longx=0x1F, ind=0x12, indy=0x11, indx=0x01, indl=0x07,
    indly=0x17, sr=0x03, sriy=0x13)
_op("EOR", imm=0x49, dp=0x45, dpx=0x55, abs=0x4D, absx=0x5D, absy=0x59,
    long=0x4F, longx=0x5F, ind=0x52, indy=0x51, indx=0x41, indl=0x47,
    indly=0x57, sr=0x43, sriy=0x53)
_op("LDX", imm=0xA2, dp=0xA6, dpy=0xB6, abs=0xAE, absy=0xBE)
_op("LDY", imm=0xA0, dp=0xA4, dpx=0xB4, abs=0xAC, absx=0xBC)
_op("STX", dp=0x86, dpy=0x96, abs=0x8E)
_op("STY", dp=0x84, dpx=0x94, abs=0x8C)
_op("STZ", dp=0x64, dpx=0x74, abs=0x9C, absx=0x9E)
_op("CPX", imm=0xE0, dp=0xE4, abs=0xEC)
_op("CPY", imm=0xC0, dp=0xC4, abs=0xCC)
_op("BIT", imm=0x89, dp=0x24, dpx=0x34, abs=0x2C, absx=0x3C)
_op("TRB", dp=0x14, abs=0x1C)
_op("TSB", dp=0x04, abs=0x0C)
_op("ASL", acc=0x0A, dp=0x06, dpx=0x16, abs=0x0E, absx=0x1E)
_op("LSR", acc=0x4A, dp=0x46, dpx=0x56, abs=0x4E, absx=0x5E)
_op("ROL", acc=0x2A, dp=0x26, dpx=0x36, abs=0x2E, absx=0x3E)
_op("ROR", acc=0x6A, dp=0x66, dpx=0x76, abs=0x6E, absx=0x7E)
_op("INC", acc=0x1A, dp=0xE6, dpx=0xF6, abs=0xEE, absx=0xFE)
_op("DEC", acc=0x3A, dp=0xC6, dpx=0xD6, abs=0xCE, absx=0xDE)
_op("INX", imp=0xE8)
_op("INY", imp=0xC8)
_op("DEX", imp=0xCA)
_op("DEY", imp=0x88)
_op("JMP", abs=0x4C, long=0x5C, absind=0x6C, absindx=0x7C, absindl=0xDC)
_op("JML", long=0x5C, absindl=0xDC)
_op("JSR", abs=0x20, absindx=0xFC)
_op("JSL", long=0x22)
_op("RTS", imp=0x60)
_op("RTL", imp=0x6B)
_op("RTI", imp=0x40)
_op("BCC", rel=0x90)
_op("BCS", rel=0xB0)
_op("BEQ", rel=0xF0)
_op("BNE", rel=0xD0)
_op("BMI", rel=0x30)
_op("BPL", rel=0x10)
_op("BVC", rel=0x50)
_op("BVS", rel=0x70)
_op("BRA", rel=0x80)
_op("BRL", rell=0x82)
_op("PHA", imp=0x48)
_op("PLA", imp=0x68)
_op("PHX", imp=0xDA)
_op("PLX", imp=0xFA)
_op("PHY", imp=0x5A)
_op("PLY", imp=0x7A)
_op("PHP", imp=0x08)
_op("PLP", imp=0x28)
_op("PHB", imp=0x8B)
_op("PLB", imp=0xAB)
_op("PHD", imp=0x0B)
_op("PLD", imp=0x2B)
_op("PHK", imp=0x4B)
_op("PEA", abs=0xF4)
_op("PEI", ind=0xD4)
_op("PER", rell=0x62)
_op("REP", imm=0xC2)
_op("SEP", imm=0xE2)
_op("CLC", imp=0x18)
_op("SEC", imp=0x38)
_op("CLI", imp=0x58)
_op("SEI", imp=0x78)
_op("CLD", imp=0xD8)
_op("SED", imp=0xF8)
_op("CLV", imp=0xB8)
_op("TAX", imp=0xAA)
_op("TAY", imp=0xA8)
_op("TXA", imp=0x8A)
_op("TYA", imp=0x98)
_op("TSX", imp=0xBA)
_op("TXS", imp=0x9A)
_op("TXY", imp=0x9B)
_op("TYX", imp=0xBB)
_op("TCD", imp=0x5B)
_op("TDC", imp=0x7B)
_op("TCS", imp=0x1B)
_op("TSC", imp=0x3B)
_op("XBA", imp=0xEB)
_op("XCE", imp=0xFB)
_op("MVN", block=0x54)
_op("MVP", block=0x44)
_op("NOP", imp=0xEA)
_op("WDM", imm=0x42)
_op("STP", imp=0xDB)
_op("WAI", imp=0xCB)
_op("BRK", imm=0x00)
_op("COP", imm=0x02)

# Branch mnemonic -> (flag, taken-when-set)
BRANCHES = {
    "BEQ": ("Z", True), "BNE": ("Z", False),
    "BCS": ("C", True), "BCC": ("C", False),
    "BMI": ("N", True), "BPL": ("N", False),
    "BVS": ("V", True), "BVC": ("V", False),
}
UNCONDITIONAL = {"BRA", "BRL"}
CALLS = {"JSR", "JSL"}
RETURNS = {"RTS", "RTL", "RTI"}
JUMPS = {"JMP", "JML"}

# Immediate width class: which width bit sizes the immediate operand.
IMM_WIDTH_CLASS = {
    "LDA": "m", "ADC": "m", "SBC": "m", "CMP": "m", "AND": "m",
    "ORA": "m", "EOR": "m", "BIT": "m",
    "LDX": "x", "LDY": "x", "CPX": "x", "CPY": "x",
}

# Memory-op width class (how many bytes a load/store/RMW moves).
MEM_WIDTH_CLASS = {
    "LDA": "m", "STA": "m", "STZ": "m", "ADC": "m", "SBC": "m",
    "CMP": "m", "AND": "m", "ORA": "m", "EOR": "m", "BIT": "m",
    "ASL": "m", "LSR": "m", "ROL": "m", "ROR": "m", "INC": "m",
    "DEC": "m", "TRB": "m", "TSB": "m",
    "LDX": "x", "STX": "x", "CPX": "x",
    "LDY": "x", "STY": "x", "CPY": "x",
}

# Register/flag def-use per mnemonic (addressing modes add index-register
# reads on top of these; see decode.py). Registers: A X Y S D DB PB.
# Flags: C Z N V I Dflag M XF E.  "mem" (read) / "store" (write) /
# "rmw" (both) describe the memory effect of the memory-operand forms.
_RW = {
    "LDA": dict(w=["A"], f=["N", "Z"], mem=True),
    "LDX": dict(w=["X"], f=["N", "Z"], mem=True),
    "LDY": dict(w=["Y"], f=["N", "Z"], mem=True),
    "STA": dict(r=["A"], store=True),
    "STX": dict(r=["X"], store=True),
    "STY": dict(r=["Y"], store=True),
    "STZ": dict(store=True),
    "ADC": dict(r=["A"], w=["A"], fr=["C"], f=["N", "V", "Z", "C"],
                mem=True),
    "SBC": dict(r=["A"], w=["A"], fr=["C"], f=["N", "V", "Z", "C"],
                mem=True),
    "CMP": dict(r=["A"], f=["N", "Z", "C"], mem=True),
    "CPX": dict(r=["X"], f=["N", "Z", "C"], mem=True),
    "CPY": dict(r=["Y"], f=["N", "Z", "C"], mem=True),
    "AND": dict(r=["A"], w=["A"], f=["N", "Z"], mem=True),
    "ORA": dict(r=["A"], w=["A"], f=["N", "Z"], mem=True),
    "EOR": dict(r=["A"], w=["A"], f=["N", "Z"], mem=True),
    "BIT": dict(r=["A"], f=["N", "V", "Z"], mem=True),  # imm: Z only
    "TRB": dict(r=["A"], f=["Z"], rmw=True),
    "TSB": dict(r=["A"], f=["Z"], rmw=True),
    "ASL": dict(f=["N", "Z", "C"], rmw=True),   # acc form: A r/w
    "LSR": dict(f=["N", "Z", "C"], rmw=True),
    "ROL": dict(fr=["C"], f=["N", "Z", "C"], rmw=True),
    "ROR": dict(fr=["C"], f=["N", "Z", "C"], rmw=True),
    "INC": dict(f=["N", "Z"], rmw=True),
    "DEC": dict(f=["N", "Z"], rmw=True),
    "INX": dict(r=["X"], w=["X"], f=["N", "Z"]),
    "INY": dict(r=["Y"], w=["Y"], f=["N", "Z"]),
    "DEX": dict(r=["X"], w=["X"], f=["N", "Z"]),
    "DEY": dict(r=["Y"], w=["Y"], f=["N", "Z"]),
    "TAX": dict(r=["A"], w=["X"], f=["N", "Z"]),
    "TAY": dict(r=["A"], w=["Y"], f=["N", "Z"]),
    "TXA": dict(r=["X"], w=["A"], f=["N", "Z"]),
    "TYA": dict(r=["Y"], w=["A"], f=["N", "Z"]),
    "TSX": dict(r=["S"], w=["X"], f=["N", "Z"]),
    "TXS": dict(r=["X"], w=["S"]),
    "TXY": dict(r=["X"], w=["Y"], f=["N", "Z"]),
    "TYX": dict(r=["Y"], w=["X"], f=["N", "Z"]),
    "TCD": dict(r=["A"], w=["D"], f=["N", "Z"]),
    "TDC": dict(r=["D"], w=["A"], f=["N", "Z"]),
    "TCS": dict(r=["A"], w=["S"]),
    "TSC": dict(r=["S"], w=["A"], f=["N", "Z"]),
    "XBA": dict(r=["A"], w=["A"], f=["N", "Z"]),
    "XCE": dict(fr=["C"], f=["C", "E", "M", "XF"]),
    "PHA": dict(r=["A", "S"], w=["S"]),
    "PHX": dict(r=["X", "S"], w=["S"]),
    "PHY": dict(r=["Y", "S"], w=["S"]),
    "PHB": dict(r=["DB", "S"], w=["S"]),
    "PHD": dict(r=["D", "S"], w=["S"]),
    "PHK": dict(r=["S"], w=["S"]),
    "PHP": dict(r=["S"], w=["S"],
                fr=["C", "Z", "N", "V", "I", "Dflag", "M", "XF"]),
    "PLA": dict(r=["S"], w=["A", "S"], f=["N", "Z"]),
    "PLX": dict(r=["S"], w=["X", "S"], f=["N", "Z"]),
    "PLY": dict(r=["S"], w=["Y", "S"], f=["N", "Z"]),
    "PLB": dict(r=["S"], w=["DB", "S"], f=["N", "Z"]),
    "PLD": dict(r=["S"], w=["D", "S"], f=["N", "Z"]),
    "PLP": dict(r=["S"], w=["S"],
                f=["C", "Z", "N", "V", "I", "Dflag", "M", "XF"]),
    "PEA": dict(r=["S"], w=["S"]),
    "PEI": dict(r=["S", "D"], w=["S"]),
    "PER": dict(r=["S"], w=["S"]),
    "REP": dict(f=[]),   # mask-dependent; decode.py fills from operand
    "SEP": dict(f=[]),
    "JSR": dict(r=["S"], w=["S"]),
    "JSL": dict(r=["S"], w=["S"]),
    "RTS": dict(r=["S"], w=["S"]),
    "RTL": dict(r=["S"], w=["S"]),
    "RTI": dict(r=["S"], w=["S"],
                f=["C", "Z", "N", "V", "I", "Dflag", "M", "XF"]),
    "CLC": dict(f=["C"]), "SEC": dict(f=["C"]),
    "CLI": dict(f=["I"]), "SEI": dict(f=["I"]),
    "CLD": dict(f=["Dflag"]), "SED": dict(f=["Dflag"]),
    "CLV": dict(f=["V"]),
    "MVN": dict(r=["A", "X", "Y", "DB"], w=["A", "X", "Y", "DB"],
                mem=True, store=True),
    "MVP": dict(r=["A", "X", "Y", "DB"], w=["A", "X", "Y", "DB"],
                mem=True, store=True),
    "JMP": dict(), "JML": dict(), "NOP": dict(), "WDM": dict(),
    "STP": dict(), "WAI": dict(), "BRK": dict(), "COP": dict(),
}
for _b in list(BRANCHES) + ["BRA", "BRL"]:
    _RW[_b] = dict(fr=[BRANCHES[_b][0]] if _b in BRANCHES else [])


def rw(mnemonic: str) -> dict:
    return _RW[mnemonic]
