# DKC1Recomp widescreen handoff

Date: 2026-08-15
Repository: `C:\Users\ellio\Documents\GitHub\DKC1Recomp`
Branch/base: `main` at `d9d55b9c8787f99ef012f7715e89ebf1ac754663`

This document describes the current uncommitted widescreen work, the behavior
that is already proven, the failures still under investigation, and the next
safe debugging steps. The worktree is intentionally dirty. Do not reset or
replace it before reviewing `git status` and the files listed below.

## Goal and coordinate policy

The recomp presents 342x224 source pixels: the native 256x224 frame plus 43
SNES pixels on each side. At the SNES 7:6 pixel aspect this is 1.78125, within
one source pixel of 16:9.

The central design rule is:

- host presentation may reveal additional world pixels;
- logical camera coordinates, collision, movement clamps, exits, boss arena
  limits, and tile-stream coordinates remain the cartridge program;
- object visibility/activation may be widened only when those objects are
  genuinely visible in a validated gameplay margin.

This differs from the SuperZSNES ROM patch, which moved DKC1's logical camera
bounds and then required compensating patches throughout gameplay code.

## Current implementation

### Host-side rendering

Relevant files:

- `runner/dkc1_game.c`
- `runner/dkc1_video.c`
- `runner/dkc1_video.h`
- `runner/headless_main.c`

The host currently:

1. Recognizes candidate Mode-1 64-column background layouts.
2. Decodes DKC1 horizontal or vertical level tiles from the verified ROM.
3. Compares decoded native-viewport samples with the live rolling tilemap.
4. Uses a world-keyed `WsShadow` tilemap for the added margins.
5. Prefills the terrain margins with a rounded-up seven-column side extent
   plus one fine-scroll guard tile.
6. Widens 64-column BG1/BG2 world planes.
7. Repeats bounded BG3 only after gameplay calibration. Jungle Hijinxs needs
   this for its 32-column sky; clamping BG3 produced black sky in the margin.
8. Centers unsupported/fixed screens over black with
   `PpuSetExtraSpaceCentered`.
9. Exposes BGSC, all four scroll pairs, and `terrain_ready` in headless output.

### Generated-code visibility adapters

Relevant files:

- `scripts/apply_dkc1_widescreen_overrides.py`
- `scripts/generate_snesrecomp.py`
- `tests/test_apply_dkc1_widescreen_overrides.py`

Generation automatically applies an exact-shape, fail-closed transformer to
the private generated C. The transformed game code remains ignored by Git;
the transformer is the source-owned artifact.

The current generated tree contains 48 helper calls in 21 C files:

- two shared sprite-cull windows;
- 18 left-side placed-object activation comparisons;
- 14 activation-span comparisons;
- two right-prefetch comparisons;
- banana formation traversal/clipping and both direct-OAM X-high writers;
- the private vertical-rope cull and upper-OAM X-high packing;
- a type-`$05` group retry that revisits only children whose `$192B` bookmark
  is still zero while the group remains inside the validated wide window.

Every helper is gated by `Dkc1VideoTerrainReady()`. The transformer is
idempotent and rejects changed or ambiguous generated source shapes.

## What the SuperZSNES work contributed

The emulator investigation remains highly useful even though its binary save
states cannot be loaded directly by this recomp. It supplied:

- the complete bank-`$BD` object-window family, including private type-`$09`
  section-controller comparisons missed by a global cull patch;
- banana-private traversal, clipping, collision, and OAM producer behavior;
- vertical-rope private culling and 9-bit OAM packing behavior;
- proof that type-`$05` parents can survive a one-shot child allocation
  failure and require a missing-child retry;
- exact level routes, source-record identities, expected actors, and gameplay
  outcomes for future recomp regression recipes;
- the distinction between presentation, activation/streaming, and gameplay
  coordinate domains.

The following ROM-patch changes should **not** be copied into the recomp while
logical camera bounds remain stock:

- camera-bound relocation and initializer backsteps;
- widened row-builder/DMA compensation;
- player endpoint compensation and cave-exit changes;
- camera-relative banana coordinate correction from the ROM patch;
- K. Rool logical-arena bound reconstruction;
- generic tilemap repair hooks for old save-state corruption.

Those were compensations for changed logical bounds, not general widescreen
requirements.

## Proven working behavior

### Build and transformation

- `build_host.bat` completes with `HOST_BUILD_OK`.
- The two override tests pass:
  - all adaptation categories are applied and a second pass is a no-op;
  - changing a known generated constant makes the transformer fail closed.
- Reapplying the transformer to the current generated tree makes no changes.

### Native-width regression

With `DKC1_WIDESCREEN=0` and `build\play_level.txt`, frame 7,600 retains the
known pre-adapter hashes:

| Surface/state | SHA-256 or value |
| --- | --- |
| frame | `beefc0ef4b6bfdef8077dd423fc7b9fd14cee219268f7340cfd96238628154b5` |
| WRAM | `c17698246e4da92140bfbccdd6e7ef4582fd2897082661034c8349ae1d0ca517` |
| VRAM | `acec99c9d9be70b3d57761ca82977adc9a28322eb53ba62c12ac2a4143e3e7fc` |
| CGRAM | `2f6ce319cc44c61b4b13722fffb643eb44be6f08d38139ee2be204f37f747826` |
| OAM | `c93c711ef08f251c1a23344da76e02c19c7e3ccb586606ba2b8512ab171a27d5` |
| audio FNV-1a | `c3409aaab79a3a5b` |

This proves the generated adapters are inert in native-width mode.

### Fixed-screen fail-closed behavior

At frame 600:

- widescreen reports `terrain_ready=0`;
- the central 256x224 crop is pixel-identical to the native-width frame;
- both 43-pixel side margins are entirely black;
- WRAM, VRAM, CGRAM, and OAM hashes match the native-width run.

The partial Rare graphic visible at the lower-right of this Nintendo frame is
also present in the native 256x224 oracle. It is not a widescreen regression.

Local evidence:

- `C:\Users\ellio\AppData\Local\Temp\dkc1-wide-final2-f600.png`
- `C:\Users\ellio\AppData\Local\Temp\dkc1-narrow-f600.png`

### Gameplay calibration

The deterministic Jungle route reaches frame 7,600 with:

- `BGMODE=$09`, main/sub masks `$17/$17`;
- BGSC `[$6D,$75,$24,$00]`;
- `terrain_ready=1`;
- camera `$0157,$00B1` in the wide run;
- coherent BG1/BG2 terrain and a continued BG3 sky rather than the earlier
  solid-black sky margin.

## Resolved scene-atomic shadow issues (2026-08-15)

### 1. Deterministic Jungle margin history

Two frame-7,600 wide captures have identical WRAM, VRAM, CGRAM, and OAM but
different final frame hashes:

| Capture | Frame SHA-256 | Local image |
| --- | --- | --- |
| accumulated calibration hold | `0d320a02ddcb2f4914de845f0d809f3c1eb707a8672eb884cbc82abfb5d70d67` | `dkc1-wide-final-f7600.png` |
| fixed two-frame calibration grace | `5c1ce7db340a3e5a3e40d9c3f39013f7133528f0931f089853dbf180627da689` | `dkc1-wide-final2-f7600.png` |

The pixel diff contains 2,509 changed pixels:

- 986 in the left 43-pixel margin;
- 1,471 in the right 43-pixel margin;
- only 52 in the native 256-pixel center.

This isolates the difference primarily to host shadow/presentation history,
not SNES VRAM, OAM, or gameplay state. The visual appearance currently
includes inconsistent dark/empty shapes near margin edges.

The current source uses hard scene identity plus a two-phase shadow commit and
a true two-frame remaining soft-miss budget. The visible boot/map/Jungle route
now produces one accepted horizontal cold start and no earlier provisional
layout. This resolves the known history-dependent cold-start path. It does not
replace the still-needed every-level transition matrix.

A 100-frame capture of frames 7,500-7,599 showed no full-frame pillarbox
event. Both margins retained nonblack pixels in every frame. Therefore the
observed issue is not simply the entire renderer entering the centered-black
fallback; it is more likely shadow history, individual plane coverage, edge
repeat, or a cold/reinitialized margin.

Local evidence:

- before: `C:\Users\ellio\AppData\Local\Temp\dkc1-wide-final-f7600.png`
- current: `C:\Users\ellio\AppData\Local\Temp\dkc1-wide-final2-f7600.png`
- sequence: `C:\Users\ellio\AppData\Local\Temp\dkc1-grace-audit\frame_007500.ppm`
  through `frame_007599.ppm`

### 2. Calibration and shadow mutation are separated

`Dkc1PrepareWidescreenShadow()` now uses a two-phase decision:

1. derive scene identity and calibrate from live PPU/VRAM without mutating the
   retained shadow;
2. only after acceptance, initialize/update/commit the shadow and prefill.

The per-frame trace logs identity reset, bounds readiness, accepted
calibration, grace, cold start, and shadow commit. The accepted 7,644-frame
visible route has zero commits on centered or bounds-not-ready frames and zero
policy violations.

### 3. Scene identity and per-frame tile agreement are separated

An animated or recently streamed tile can reduce calibration agreement even
though the level identity is unchanged. Conversely, retaining confidence
because many earlier frames matched can expose stale art after a transition.

The runtime now distinguishes:

- **hard identity invalidators:** game mode/entrance, source signature, map
  bank/base, metatile base, stream VRAM base, BGMODE, BGSC, terrain layer, and
  active wide-layer mask;
- **soft per-frame evidence:** decoded/live sample match count.

Hard identity changes immediately reject retained layout/pixels. Within one
unchanged identity, only two consecutive soft misses are tolerated. Camera
bounds readiness is an additional precondition, because PPU source shape can
become visible several frames before DKC publishes usable logical bounds.

Evidence: `build/bounds-candidate-20260815-170328/ws-trace.jsonl` and its
`ws-summary.json` (local, generated, not committed).

## Remaining issues

### 4. Wider object activation changes simulation timing

Wide and native frame-7,600 runs do not have identical WRAM:

- wide WRAM: `c8f22eadf984479812eb547e0c19bcc85d159c6df5d29af8d428f7fb03041d1e`
- native WRAM: `c17698246e4da92140bfbccdd6e7ef4582fd2897082661034c8349ae1d0ca517`
- wide camera X: `$0157`; native camera X: `$0163`.

Some divergence is expected because visible-margin objects activate earlier,
but earlier activation can also advance position, state, collision, or actor
allocation order. It must not be dismissed as presentation-only.

The recomp needs a first-divergence and object-lifecycle comparison equivalent
to the SuperZSNES `DKCFirstDivergenceLocator` and
`DKCObjectPrefetchPhaseAuditor`. Acceptance should compare an early wide actor
at the frame the native run next becomes eligible, including position,
motion, state, collision candidates, source/bookmark, and allocation episode.

### 5. Ported object fixes are not yet route-complete in the recomp

The override unit test proves exact generated-source rewriting, not gameplay.
The following still need deterministic recomp routes and outcome assertions:

- cave banana formation placement **and pickup**;
- banana right-edge coverage without wrapped ghosts;
- cave exit traversal;
- vertical rope visibility and motion on both margins;
- Slipslide Ride type-`$09` section progression and later ropes/enemies;
- grouped barrel/type-`$05` child retry and actual barrel transfer;
- high-world-X Barrel Cannon Canyon rendering near `$8000`;
- Croctopus and Poison Pond section/door completion;
- K. Rool fresh entry, left-side graphics, and phase progression.

SuperZSNES `.szst` files are forensic and route-definition inputs only. They
cannot be treated as native recomp save states.

### 6. Transition coverage is incomplete

Boot frame 600 proves fixed-screen centering when the host has not previously
trusted gameplay terrain. It does **not** prove the more important transition
case: a long calibrated gameplay session entering Nintendo/title/splash,
save-select, map, bonus, death, or another level.

Capture these transitions after enough gameplay to exercise the old
accumulated-hold failure. Unsupported frames must show the exact native center
and black sides; no prior world tiles may survive in either margin.

### 7. Pre-existing recomp dispatch gap

Long runs still report the unresolved `$BE8179` dispatch and explicitly skip
the handler's side effects. This predates the widescreen adapters, but it can
invalidate long-route conclusions. Prove and authorize that dispatch contract
separately rather than attributing every later anomaly to widescreen.

## Recommended next implementation step

Do not tune the calibration threshold or grace again without per-frame
evidence. Add a default-off headless diagnostic record containing:

- host frame and SNES frame;
- game mode, level, entrance, fade;
- source signature and each component field;
- BGMODE, BGSC, active main/sub masks, wide-layer mask, terrain layer;
- camera and all four PPU scroll pairs;
- horizontal and vertical `matches/decodable` scores;
- selected layout, calibrated result, grace/miss count;
- whether `WsShadowReset`, cold initialization, `WsShadowFrame`, prefill,
  BG3 repeat, world extension, or centered fallback occurred;
- world X/Y chosen for each layer and margin tile count;
- margin stats or hashes for each BG plane and final left/right margins.

Use this record with frames 7,500-7,599 and a gameplay-to-title transition.
The immediate question is: **which exact frame and reason changes the retained
margin pixels while SNES VRAM/OAM remain identical?**

After the trace identifies the event, prefer this structure:

1. Compute a strict scene-identity key.
2. Immediately invalidate on a key change.
3. Calibrate without mutating retained output.
4. Commit shadow updates only on accepted frames.
5. Preserve a bounded last-good shadow only within the same identity.
6. Fail closed to exact native-center/black margins after a small consecutive
   miss budget.
7. Add a deterministic test that alternates accepted and rejected frames and
   proves no old-scene tiles survive.

## Reproduction commands

Clean ROM contract:

- headerless DKC USA v1.0;
- size `4,194,304` bytes;
- SHA-256
  `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.

Build and unit tests:

```powershell
Set-Location 'C:\Users\ellio\Documents\GitHub\DKC1Recomp'
.\build_host.bat
$env:PYTHONDONTWRITEBYTECODE = '1'
python -m unittest discover -s tests -v
```

Wide Jungle frame 7,600:

```powershell
$env:DKC1_WIDESCREEN = '1'
$env:SNESRECOMP_INPUT_PLAY =
  'C:\Users\ellio\Documents\GitHub\DKC1Recomp\build\play_level.txt'
$env:DKC1_FRAME_PPM =
  'C:\Users\ellio\AppData\Local\Temp\dkc1-wide-check-f7600.ppm'
.\build\dkc1_snesrecomp_headless.exe `
  'D:\Downloads\Donkey Kong Country (USA)\Donkey Kong Country (USA).sfc' `
  7600
```

Native oracle: run the same command with `DKC1_WIDESCREEN=0`.

Fixed-screen frame 600: clear `SNESRECOMP_INPUT_PLAY`, run 600 frames in wide
and native modes, then assert that wide columns 43-298 equal the native image
and columns 0-42/299-341 are black.

## Release gates

Widescreen is not release-ready until all of the following are true:

- build and exact-shape override tests pass;
- native-width hashes remain unchanged;
- fixed/unknown screens are exact native-center plus black margins;
- gameplay margins are continuous in raw BG planes and the final frame;
- repaired offscreen rows/columns remain correct when scrolled into view;
- every widened private object family is interactable at its visible position;
- early activation is lifecycle-safe or explicitly corrected;
- the grouped-child retry is proven by source index, actor ID, persistence,
  capacity recovery, and gameplay transfer;
- affected routes replay deterministically at least three times;
- fresh-entry and historical-state conclusions are kept separate;
- the `$BE8179` dispatch gap is either resolved or excluded from the route;
- final-window captures, not only internal plane images, pass visual review;
- no ROM, generated game code, states, captures, or transient tier-2 logs are
  committed.

## Things not to do

- Do not copy the SuperZSNES logical-camera compensation patches into this
  host-camera design.
- Do not fix a sprite visually without proving pickup/collision/effect
  coordinates.
- Do not globally shift OAM; patch private producers that lose X-high.
- Do not infer initialization from the sign bit around world X `$8000`.
- Do not treat a partial debugger plane as the final-window oracle.
- Do not enlarge the actor pool before proving real exhaustion.
- Do not accept a one-frame PPU OAM mismatch without checking WRAM OAM and one
  complete VBlank later.
- Do not hide wide/native WRAM divergence under an ignore list before
  classifying the first actor/state difference.
