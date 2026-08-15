# DKC1Recomp

Static recompilation of *Donkey Kong Country* (SNES, USA v1.0) into a native
desktop application, using the [`snesrecomp`](https://github.com/Nicktendonick/snesrecomp)
framework — following the working [`DKC2Recomp`](https://github.com/Nicktendonick/DKC2Recomp)
project (same Rare engine family) as the template.

**Status: bring-up.** Analysis configuration is generated and validated; the
recompiler emits AOT variants from it. There is no playable host application
yet — see `docs/BRINGUP.md` for the roadmap and current state.

## Why this game is a strong recomp candidate

Unlike most bring-ups, DKC1 does not need blind code discovery: the
[Yoshifanatic1 DKC1 disassembly](https://github.com/Yoshifanatic1/Donkey-Kong-Country-1-Disassembly)
(GPL-3) rebuilds the ROM byte-identically, and an IDA-based pipeline over it
(in the disassembly repo under `Tools/IDA/`) provides:

- the exact start and size of **every instruction** (53,230 instructions /
  126,790 bytes) derived from the asar symbol map — no heuristics;
- 1,276 function entries with proven entry M/X flag states;
- **complete target lists for the indirect-dispatch sites**, extracted from
  the disassembly's own `dw` jump tables (25 sites, 293 targets — including
  the 460-entry game-mode dispatcher and the animation-command table);
- ~90 semantically identified engine routines and RAM addresses.

`tools/ingest_dkc1_disasm.py` distills that into the per-bank CFG files in
`recomp/`. DKC1 executes all code from banks **80-BF** (low-RAM-mirror halves
of HiROM); the cfg banks follow that layout.

## Supported ROM

Headerless *Donkey Kong Country* USA v1.0:

| Property | Expected value |
| --- | --- |
| Size | 4,194,304 bytes |
| SHA-256 | `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15` |

The ROM must remain outside Git. No ROM bytes, extracted assets, or generated
game code are committed.

## Generate the recompiled sources

```powershell
git submodule update --init --recursive
python scripts\generate_snesrecomp.py --rom "C:\private\dkc1.sfc"
```

Output lands in ignored `generated/snesrecomp/`. Regenerate `recomp/*.cfg`
from the disassembly pipeline with:

```powershell
python tools\ingest_dkc1_disasm.py --work "<disassembly>\Tools\IDA\work" --out recomp
```

## Content boundary

- Never commit ROMs, saves, extracted assets, or generated game binaries.
- `recomp/*.cfg` contain only names, PCs, and structural metadata derived
  from the GPL-3 disassembly; provenance is recorded in each file header.
