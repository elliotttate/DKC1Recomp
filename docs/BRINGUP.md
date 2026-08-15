# DKC1 bring-up log

## 2026-08-15 â€” repo created, first generation

- Scaffolded from the DKC2Recomp layout; `snesrecomp` pinned as a submodule at
  `cd941fc` (the same revision DKC2Recomp validates against).
- `tools/ingest_dkc1_disasm.py` generated `recomp/*.cfg` from the disassembly
  pipeline (`Tools/IDA/work` in the Yoshifanatic1 disassembly repo):
  13 banks, 1,276 function entries with proven `entry_mx`, 350 interior
  data regions, 25 indirect-dispatch contracts (293 targets) resolved from the
  disassembly's own `dw` tables.
- `v2_sync_funcs_h.py` accepted all cfgs (1,276 declarations).

### First generation result (native analyzer, 19.6s)

```
analysis: 1285 roots -> 1398 exact variants, 3433 edges
analysis: 922 AOT-eligible, 476 LLE-only
```

LLE-only reason classes (from `program_manifest.json`; address-specific
suffixes folded): 606 `unproven_call`, 462 `truncated_call_continuation`,
456 `unproven_callee_exit`, 18 `brk`, 15 `structural_poison`. Most of the
unproven-call volume is cascade from a smaller set of roots â€” raising AOT
coverage means fixing bounds/dispatch at the leaves (the DKC2 pattern).

### Known open items

- **6 unresolved dispatch sites** read pointer tables in WRAM
  (`$0508`, `$0002`, `$0000` via B5/B6 script engines) â€” marked as
  `# TODO(dispatch)` comments in the cfgs. Deriving their target sets needs
  the writers of those tables enumerated (the sprite spawn-script system).
- **9 dynamic RAM-pointer jumps** (e.g. the animation per-frame callback
  `JML [$7A]` at `BE:813B`) listed in the disassembly pipeline's
  `recomp_seed/dynamic_jumps.txt`; each needs a curated contract
  (candidate values are whatever the game stores to `$1341/$130D` etc.).
- No host application yet: next milestones are (1) clean generation,
  (2) build the generated static library, (3) port DKC2Recomp's runner
  integration (`runner/`, `src/`, `app/`, CMake) and boot-probe workflow,
  (4) LLE-vs-AOT differential validation on the attract loop.

### Provenance

Structural metadata derives from the GPL-3 Yoshifanatic1 DKC1 disassembly.
Semantic names come from the 2026-08-15 IDA analysis pass documented in the
disassembly repo (`Docs/RE_Findings_DKC1.md`); several were corroborated by
the disassembly's own `RAM_Map_DKC1.asm`.

## 2026-08-15 — merged Pseudocode cfg set; the game runs

- Discovered `DKC1_Disassembly/DKC1/Pseudocode/` — a prior SNESRecomp ingest of
  the same disassembly. Its cfg set (2,557 bounded funcs, 118 dispatch tables)
  replaced the first-pass cfgs, with our semantic names merged onto matching
  entries (45 renames). Result: **2,591 exact AOT variants, 0 LLE-only (100%
  static coverage)**.
- Ported DKC2Recomp's headless host adapters (`runner/`), plus a single-file
  Win32 GDI/waveOut interactive host (`runner/win32_host.c`) — no SDL needed.
- `build_host.bat` (direct MSVC, no CMake required) produces:
  - `build/dkc1_snesrecomp_headless.exe <rom> [frames]` — validation harness
    (state trace, frame PPM dumps, WRAM/VRAM dumps, audio PCM, input playback).
  - `build/dkc1_desktop.exe <rom>` — playable window (Z=B, X=Y, S=A, A=X,
    Q/W=L/R, Enter=Start, RShift=Select, arrows=D-pad, Esc=quit).
- **Validation:** 3,600-frame headless run completes cleanly. The boot shows
  "Nintendo PRESENTS" + Rare logo (frame 600), the full DKC intro cutscene
  (DK + boombox on the treetops, Cranky's gramophone) renders correctly with
  continuous audio (3,276/3,600 audio-active frames). Mode transitions and
  camera movement look sane.

## 2026-08-15 -- presentation-camera widescreen

Same architecture as DKC2Recomp, and deliberately aligned with the v2 design
notes from the SNES-side widescreen effort: the recompiled game logic stays
byte-for-byte stock (logical camera, spawn scanner, actor pool, clamps,
exits); widescreen is host-only presentation.

- `Dkc1VideoDecodeLevelTile` decodes DKC1's level maps straight from ROM
  (metatile formats from `$81:8705` horizontal / `$81:8DFA` vertical; flips
  in cell bits 14/15). Unlike DKC2 (WRAM maps), the source is static ROM, so
  prefill cannot race a decompressor.
- Per-frame runtime calibration decodes the native viewport and requires
  >=70% agreement with the live rolling tilemap before margins are prefilled;
  otherwise margins fall back to WsShadow capture/history + blank tiles.
- Terrain layer = the wide layer whose tilemap base matches the streamer
  base at `$7E1B13`; world keys unwrap PPU scroll against camera
  `$088B/$0895`. Non-terrain wide layer folds periodically (parallax).
  Fixed screens (logos, title, map transitions) pillarbox via
  `PpuSetExtraSpaceCentered`.
- `DKC1_WIDESCREEN=1` opts in the headless harness; the desktop host
  defaults ON (`DKC1_WIDESCREEN=0` reverts to 4:3).
- **Validated in real gameplay**: scripted-input run entered Jungle Hijinxs
  and ran right — margins carry correct level terrain, and sprites the game
  emits with 9-bit OAM X (the DK barrel) already render inside the margins.

Known issues / next:
- Thin black strip at the far-left margin edge in-level (westmost columns
  not filling; likely fine-scroll/west-extent off-by-one in the prefill or
  the calibration world key) — investigate with WsShadowGetMarginStats.
- Sprites still pop at the native cull edge (game-side display cull);
  DKC2-style generated-code cull-widening overrides are the next layer —
  safe here because activation/simulation timing stays stock.
- Map screen widens nicely; audit vertical levels and non-jungle tilesets.
