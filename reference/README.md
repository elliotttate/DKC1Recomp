# reference/ — consolidated source material

Everything needed to understand or reconstruct any layer of this project,
physically in one place. **Content here (except this README) is untracked
and gitignored** — it includes another project's git repository, large
generated knowledge bases, and extracted-asset build inputs that this
repo's policy never commits. Treat it as read-only reference; fixes never
land here (see `.claude/skills/dkc1-tools/SKILL.md`).

## Layout

| folder | provenance (original path) | contents |
|---|---|---|
| `disassembly/` | `D:\Downloads\DKLR\DKC1_Disassembly` | The labeled byte-identical DKC1 disassembly **including its own `.git` history** ("all versions"): DKC1 sources + `RAM_Map_DKC1.asm`, `Misc_Defines_DKC1.asm`, the `Pseudocode/` mechanical lift + lossless listing + `instruction_index.csv`, `Tools/IDA/` headless pipeline + seeded `DKC1_U1.i64` + curated `work/rename_map.json`, `Docs/RE_Findings_DKC1.md`, asset/level data needed for byte-identical reassembly, and the LEGACY widescreen hack sources (`DKC1/Custom/Patches/Widescreen_*.asm`, `RomMap/ROM_Map_HACK_*` — reference only, never for recomp work). |
| `legacy-widescreen/` | `D:\Downloads\DKLR\DKC-Widescreen-358x224` | The retired asar/emulator widescreen project: docs/worklogs, mods, scripts, packaging, tools. Its `artifacts/` (verification ROMs, state analyses, releases) and `rom/` were NOT copied — regenerate via its own scripts, or the originals remain at the source path. |
| `dkc-recomp-seed/` | `D:\Downloads\DKLR\DKC-Recomp` | The earlier recomp bring-up seed (disassembler/emitter sources, disasm outputs) that preceded this repo. Build outputs and the ROM excluded. |

Excluded everywhere: `*.sfc *.smc *.srm *.state` and emulator save dirs
(never stored in this repo, tracked or not). The supported ROM is
headerless DKC1 USA v1.0,
sha256 `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.

## External repositories (clean public clones; not duplicated here)

- engine upstream: https://github.com/mstan/snesrecomp (our fork with all
  DKC1 work: https://github.com/elliotttate/snesrecomp — this repo's
  `snesrecomp/` submodule)
- https://github.com/... `snes_ida`, `ida-65816-module`,
  `Donkey-Kong-Country-1-Disassembly` (upstream of `disassembly/`) —
  local clones at `C:\Users\ellio\Documents\GitHub\` were verified clean
  with no unpushed commits (2026-08-16).

## Tooling integration

`tools/atlas.py` and `tools/export_ida_dispatch.py` prefer these in-repo
paths and fall back to the original `D:\Downloads\DKLR\` locations, so the
repo works standalone. Rebuild the IDA database headlessly with
`disassembly/Tools/IDA/build-database.ps1` if `DKC1_U1.i64` goes stale.
