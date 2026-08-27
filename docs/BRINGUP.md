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
- **9 dynamic RAM-pointer jumps** were initially listed in the disassembly
  pipeline's `recomp_seed/dynamic_jumps.txt`. The animation opcode dispatcher
  at `$BE8179` is now closed by a source-backed 197-target contract and an
  exact audit; the remaining sites still need equivalent writer/table proofs.
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

Same presentation architecture as DKC2Recomp, informed by the tested
SuperZSNES ROM patch but deliberately avoiding its camera-bound strategy.
Logical camera coordinates, collision, movement clamps, exits, boss arenas,
and tile streaming stay stock. Generated adapters widen only visibility and
placed-object activation after the host proves a supported gameplay layout.

- `Dkc1VideoDecodeLevelTile` decodes DKC1's level maps straight from ROM
  (metatile formats from `$81:8705` horizontal / `$81:8DFA` vertical; flips
  in cell bits 14/15). Unlike DKC2 (WRAM maps), the source is static ROM, so
  prefill cannot race a decompressor.
- Per-frame runtime calibration decodes the native viewport and requires
  >=70% agreement with the live rolling tilemap before margins are prefilled.
  A Mode-1/64-column register shape alone is not accepted. Unknown layouts,
  logos, title screens, and transitions are centered with black sides. The
  calibration latch has only a two-frame grace; it does not accumulate a
  multi-second stale-layout hold during long gameplay sessions.
- Terrain layer = the wide layer whose tilemap base matches the streamer
  base at `$7E1B13`; world keys unwrap PPU scroll against camera
  `$088B/$0895`. Non-terrain wide layer folds periodically (parallax).
  Fixed screens (logos, title, map transitions) pillarbox via
  `PpuSetExtraSpaceCentered`.
- `DKC1_WIDESCREEN=1` opts in the headless harness; the desktop host
  defaults to 16:9 (`DKC1_WIDESCREEN=0` starts in 4:3). The visible host also
  exposes `View -> Aspect Ratio` so either presentation can be selected while
  paused or playing without reloading the ROM or save state.
- The post-generation override pass ports the proven SuperZSNES visibility
  fixes: 18 left activation sites, 14 span sites, two right-prefetch sites,
  common sprite culls, banana-private culls/OAM X-high, vertical-rope
  culls/OAM X-high, and retry of missing type-$05 group children.
- BG1/BG2 world layers widen independently; bounded BG3 may edge-repeat only
  after gameplay calibration. This fixes Jungle Hijinxs' black sky margin.
- **Validated in real gameplay:** deterministic scripted input enters Jungle
  Hijinxs and runs right. At frame 7,600 the wide frame is calibrated and
  coherent; a stock-width run retains its pre-adapter frame, WRAM, VRAM, OAM,
  and audio hashes. A 14,000-frame wide run completes.

Known issues / next:
- Wider placed-object activation can advance actor simulation relative to a
  stock-width run. The type-$05 retry fixes the proven one-shot child loss,
  but every level and object family still needs deterministic route coverage.
- Fixed/unknown screens intentionally pillarbox until given a scene-specific,
  oracle-tested extension. The Nintendo frame-600 partial Rare graphic also
  appears in the 256x224 run and is not a widescreen regression.
- Audit vertical levels, underwater stages, bosses, object-pool pressure, and
  save-state restoration with the deterministic tooling used by the ROM hack.
- `$BE8179` is resolved by an exact 197-target source-backed contract. The
  former incomplete route harvest caused real gameplay breakage (a repeating
  jump) and is guarded by `tools/audit_animation_dispatch.py` plus a
  deterministic one-tap regression. Other indirect dispatch sites remain
  independent bring-up risks unless source-audited. The new
  `tools/audit_indirect_tables.py` proves the other 118 cfg contracts exactly
  from disassembly tables/records (zero missing or extra targets), so no
  current indirect allowlist depends only on route harvesting.
- Contact damage and the Jungle death/non-gameplay transition now pass a
  two-checkpoint, three-repeat byte-identical closure contract. Every centered
  transition frame has exact black margins with no raw fallback; map/title,
  bonus, save-select, and cross-level routes remain to be covered.
- The full boot/fixed-screen/map-to-Jungle entry contract applies the same
  trace gate in the other direction. Its three 7,645-frame runs have identical
  traces and zero raw fallback, nonblack centered sides, policy violations, or
  stable-input margin mutations.

### Native widescreen decision trace

`DKC1_WS_TRACE` enables a default-off JSONL record at the exact presentation
boundary. Each frame records scene/source identity, PPU registers, both layout
scores, the selected calibration/grace state, shadow reset/prefill/fallback
decisions, world keys, margin-stat deltas, region hashes, and raw VRAM, CGRAM,
plus both OAM-copy hashes. The analyzer also requires equal PPU/scroll,
camera/world, and center-pixel state before reporting a stable-input margin
mutation. This is the causal substrate for the planned provenance
overlay, first-divergence locator, and lifecycle tools. See
`docs/WIDESCREEN_DEBUG_TOOLS.md` for usage and schema details.

## 2026-08-27 — native macOS host and deadline frame pacing

- **Symptom:** the first native SDL host presented each completed frame and
  then used relative `SDL_Delay` calls. Variable emulation, AppKit, and
  renderer cost therefore appeared as alternating short/long submissions.
  Its eight-frame late guard could also burst several frames after a short
  pause.
- **Root cause/domain:** host presentation timing only. Cartridge state,
  streaming, activation, gameplay coordinates, and generated code were not
  involved.
- **Change:** the arm64 macOS host now schedules the native NTSC cadence
  (60.098811862 Hz) on absolute Mach deadlines. It coarse-waits until an
  adaptive measured-work window, samples input, runs one cartridge frame,
  fine-waits, and presents. A miss over 2 ms re-anchors instead of catching
  up. Pause/resume, single-step, save/load, focus, resizing, fullscreen, and
  aspect changes explicitly re-anchor. `DKC1_FPS_STATS=1` reports submission
  intervals, measured host work, stalls, and re-anchors. This is deliberately
  host-only and does not skip or synthesize cartridge frames.
- **Source/build:** clean USA ROM SHA-256
  `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`;
  source commit `b404cb7` plus the working-tree macOS changes; final measured
  arm64 executable SHA-256
  `6b0c7ebf7e321a199e7e873208475cc42057cccfe44f8bcfc336ae84c76f6dbf`.
- **Determinism:** three independent 600-frame headless runs were
  byte-identical, including full logs. Frame SHA-256 was
  `a7366146a96b47e75d48d3814bfe0859c71f68bcc3fac3d7429befb9b50430dd`;
  WRAM `405229e90a31728901261cf0c9804b1cad668ba78ec99a94ea096010ed8ae792`;
  VRAM `628205f68fcfb17ebff713087d104ca0d896205b38855678dc0578b5386b6aec`;
  CGRAM `ab7733ad35514f04d2ddaacc0298a3d9dfe983857bf465c2b96ce1300eb8c4a1`;
  OAM `44ddd2f478477ebd1c1cd5b99400af48cd46033c59173195f48870e608cec810`.
- **Visible QA:** a real 1,200-frame application-window run on an Apple M3
  Max/Liquid Retina XDR display completed normally. Including three host
  stalls, submission telemetry reported 59.706 Hz average, 16.749 ms average
  interval, 15.330-44.712 ms range, three intervals over 1.25 native frames,
  2.336 ms average measured work, and 26.788 ms maximum work. The pacer
  re-anchored rather than issuing catch-up bursts. These are host-submission
  timestamps, not WindowServer scanout timestamps. Raw output is retained at
  `build/macos/pacing-validation/visible_1200.log`.
- **Validation:** `./build_macos.sh`, 205 Python tests (one unavailable local
  imported-state fixture skipped), `git diff --check`, deep ad-hoc signature
  verification, visible native menu/aspect QA, and the three-repeat headless
  gate passed.
- **Residual scope:** this validates boot/title presentation and host timing,
  not the complete gameplay/cross-layout matrix. Extended gameplay pacing,
  explicit fixed-60-Hz versus adaptive-refresh display comparison, and direct
  compositor/scanout measurement remain untested. No widescreen capability or
  gameplay behavior was promoted by this change.
