# DKC1Recomp

Static recompilation of *Donkey Kong Country* (SNES, USA v1.0) into a native
desktop application, using the project's pinned
[`snesrecomp` fork](https://github.com/elliotttate/snesrecomp)
framework — following the working [`DKC2Recomp`](https://github.com/Nicktendonick/DKC2Recomp)
project (same Rare engine family) as the template.

**Status: playable bring-up.** The project has 100% statically generated game
code, a headless validation host, a playable Win32 host, continuous audio, and
an opt-in 342x224 widescreen presentation path. See `docs/BRINGUP.md` for the
chronological bring-up record and `docs/WIDESCREEN.md` for the widescreen
architecture, ported SuperZSNES findings, validation, and limitations.

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

## Debugging and validation

The widescreen port is developed from byte-exact, deterministic evidence—not
screenshots alone. [`docs/WIDESCREEN_DEBUG_TOOLS.md`](docs/WIDESCREEN_DEBUG_TOOLS.md)
defines the native debug-tool roadmap, ordered from per-frame decision traces
and replayable snapshots through object-lifecycle analysis and whole-game
level sweeps. [`docs/WIDESCREEN_HANDOFF.md`](docs/WIDESCREEN_HANDOFF.md)
records the current evidence, hashes, open issues, and release gates.

## Licensing and third-party code

Project-authored host and tooling code is MIT licensed. The pinned
`snesrecomp` framework has its own PolyForm Noncommercial license and
third-party notices. DKC structural metadata retains its documented GPL-3
disassembly provenance. See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

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

Generation automatically runs the fail-closed widescreen override pass. It
adapts only source-visible culling, object activation, and direct-OAM packing;
logical camera, collision, exits, boss arenas, and tile streaming remain the
cartridge program.

## Build and run

With a Visual Studio developer environment available:

```powershell
.\build_host.bat
.\build\dkc1_desktop.exe "C:\private\dkc1.sfc"
```

The desktop host enables widescreen by default. Set `DKC1_WIDESCREEN=0` for
the exact 256x224 presentation path. The headless validator accepts a frame
count and supports deterministic input playback and private frame/state
captures; its environment variables are documented in `docs/BRINGUP.md`.

For visible widescreen debugging, `F7` pauses or resumes, `F8` advances one
frame while paused, and `F9` exports the rolling repro history when the flight
recorder is armed:

```powershell
$env:DKC1_FLIGHT_RECORDER = '1'
$env:DKC1_FLIGHT_RECORDER_DIR = "$env:TEMP\dkc1-repros"
.\build\dkc1_desktop.exe "C:\private\dkc1.sfc"
```

The recorder keeps roughly one minute of input and periodic native snapshot
anchors in memory; it writes nothing until F9. Validate and deterministically
replay an exported bundle with:

```powershell
python tools\verify_flight_bundle.py "<bundle>" `
  --runner build\dkc1_snesrecomp_headless.exe --rom "C:\private\dkc1.sfc"
```

## Content boundary

- Never commit ROMs, saves, extracted assets, or generated game binaries.
- `recomp/*.cfg` contain only names, PCs, and structural metadata derived
  from the GPL-3 disassembly; provenance is recorded in each file header.
