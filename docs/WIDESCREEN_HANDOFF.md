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

SuperZSNES v0.230 `.szst` files are converted to a fail-closed portable bundle
with `tools/SuperZSNESStateExporter` and can be replayed by either host via
`DKC1_SUPERZSNES_STATE`. They are not byte-compatible with native recomp
snapshots. Raw memories and mapped execution state are restored; DSP
interpolation history is reconstructed, so the first imported audio buffer is
not a cross-runtime oracle. Promote an imported repro only after deterministic
native replay.

### 6. Transition coverage is partially closed

Boot frame 600 proves fixed-screen centering when the host has not previously
trusted gameplay terrain. The Jungle damage/death route now also proves the
more important calibrated-gameplay-to-nongameplay case: the two-checkpoint
`contracts/jungle-death-transition.json` contract passes three byte-identical
repeats, and its trace has exact black side hashes on all 11,470 centered
frames, zero raw fallbacks, and zero policy violations.

The inverse boot/fixed-screen/map-to-Jungle route is also integrated into
`contracts/jungle-entry.json`: 7,645 presentation frames, including 7,330
centered and 315 extended frames, replay with byte-identical complete traces
three times and the same zero-artifact gates.

Nintendo/title/splash, save-select, map, bonus, and cross-level transitions
remain open and must be captured after enough gameplay to exercise the old
accumulated-hold failure. Unsupported frames must show the exact native center
and black sides; no prior world tiles may survive in either margin.

### 7. Animation callback dispatch closure

The former `$BE8179` gap is now statically closed rather than merely harvested
from exercised routes. The disassembly contains 503 animation-opcode callback
uses naming 197 unique targets; all resolve through the USA 1.0 symbol file and
the cfg contains that exact canonical set. `tools/audit_animation_dispatch.py`
fails CI-style validation on a missing, extra, duplicate, unresolved, or
misordered target. This fixed a concrete non-widescreen gameplay defect where
missing `$BEA778` made one jump input repeat every 40 frames. Continue treating
other indirect dispatches as independent correctness surfaces, but do not
exclude `$BE8179` side effects from route conclusions anymore.

The independent surfaces are now audited too. `tools/audit_indirect_tables.py`
derives 118 of the 119 cfg contracts from explicit source tables, pointer-table
writers, or animation-record callback fields, with zero mismatches. The one
remaining contract is `$BE8179`, covered by the 503-use/197-target audit above.
This proves every current indirect allowlist from source rather than route
harvesting; future cfg additions must retain the same property.

## Recommended next implementation step

Do not tune the calibration threshold or grace without per-frame evidence.
The default-off `DKC1_WS_TRACE` record now contains scene/source identity, PPU
registers and scrolls, calibration decisions, shadow operations, world keys,
margin stats, native-center/side hashes, and VRAM/CGRAM/WRAM-OAM/PPU-OAM
hashes. `tools/analyze_ws_trace.py` calls a margin change stable-input only when
all of those relevant inputs and the native center are equal. The fresh
932-frame snapshot-scroll evidence reports no stable-input margin mutation;
the prior alerts were caused by omitting palette, scroll, and camera state.

Use this trace next on map/title, bonus, save-select, and cross-level routes,
and prioritize deterministic gameplay closure for the listed object/section
families instead of changing calibration from an incomplete hash comparison.

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

### Ropey Rampage vertical-rope wrap/flicker regression (2026-08-15)

The F9 bundle
`build/visible-flight/capture-f00003647-20260815-201028-p62236`
reproduces a private vertical-rope OAM defect. At replay frame 3,346 the
four rope segments in OAM entries 27-30 had low X `$FC`, but their upper-OAM
X-high bits remained set. The PPU therefore decoded X as `$1FC` (signed -4)
and drew the rope in the left margin instead of at X=252 near the right edge.
The upper bit could vary with same-frame OAM slot reuse, producing flicker.

`CODE_80A7ED` at `$80:A7ED` owns this rope path. Its stock upper-OAM writer
ORs a large-size mask into the existing packed word and never clears the
adjacent X-high bit. Stock culling to X=0..255 made that safe on cartridge,
but the widened private cull exposes reused slots and 9-bit coordinates. A
first host adaptation also incorrectly sampled DP `$76` at the upper-OAM
write: by then the routine had repacked `$76` as `Y:X-low`, so Y bit 0 was
being interpreted as X bit 8.

The source-owned generated-C override now:

1. retains the original 16-bit rope X in `_ws_rope_x` before `$76` is packed;
2. uses that value for the widened visibility test;
3. clears and then explicitly sets each segment's upper-OAM X-high bit from
   `_ws_rope_x`, while retaining the stock large-size bit;
4. uses the exact stock OR merge when terrain widescreen is inactive.

Exact replay evidence is under `build/rope-current/candidate-replay-v3/`.
Across frames 3,320-3,346, the old PPU rope X advanced 503..508 (signed
-9..-4); the corrected X advanced 247..252 with no left copy. At frame 3,346
WRAM-shadow and PPU OAM match, the rope entries are all X=252/large=1, the
OAM inspector verdict is `clean`, and range/time overflow remain zero.

The reusable two-sided contract is
`tools/verify_vertical_rope_margins.py`. Its immutable Ropey Rampage anchor is
the rolling-repro snapshot under
`build/visible-flight-rope-fixed/capture-f00102424-20260815-205454-p6496/`.
Three exact 240-frame runs per side pass. Right-margin OAM is byte-identical at
SHA-256 `338208A6B4E707BC16AAD2E4055A0A0912A16410785FFCC6146374EBEAD3B30A`
and reaches X=257..297; left-margin OAM is byte-identical at SHA-256
`1DE73321638E9F889045D32B482FE2ED6BF79795EA66602040D36F08504B5073`
and reaches signed X=-2..-41. There are zero low-byte alias copies and the
maximum WRAM-shadow/PPU disagreement is the expected one VBlank. The machine
report is `build/rope-margin-contract-v3/verification.json`.

The native safety replay is byte-identical before and after:

- final WRAM SHA-256:
  `EFFC2D0AE60C06941D1B0C7F023D811460F7B2E1FB813560ED84BDE609C6D184`;
- final frame SHA-256:
  `377AE129F1A3E499571330BE36801C3FB7200CA8D7CC864E9435C674401A4235`.

Do not derive rope X-high from the packed `$76` word and do not globally
clear upper OAM: other wide objects legitimately require X-high=1. The fix
must remain scoped to the private rope writer and own both bits of each OAM
pair it changes.

### Placed-object prefetch phase guard (experimental, 2026-08-15)

The 932-frame Jungle stock/wide differential proves that widened scanner
windows are not always presentational. Source record `$0D` (Gnawty, actor ID
`$4D`) allocates at relative frame 50 in wide mode and frame 64 in stock mode.
Without a guard it has already moved and accelerated by the stock allocation
frame. The first generated-C prototype accidentally read direct-page `$84`;
the opcode-grounded normal actor loop stores its current even actor index at
`!RAM_DKC1_NorSpr_CurrentIndexLo=$82` before calling `$BF:8087`, so that
prototype failed open.

The corrected experiment reads `$82`, reconstructs the stock scanner interval
from `$EF/$F1`, and suppresses the first behavior dispatch until the source
record reaches that interval. The normal actor pool is sampled at the frame
boundary **before** the cartridge scanner. Actors already present there (for
example in a loaded snapshot) are left-censored and trusted; actors allocated
by the widened scanner later in the same frame remain new and must pass the
stock interval. The earlier deferred whole-pool seed at first dispatch was
wrong: on a fresh entry it accidentally trusted the very margin-only actors
the guard was meant to hold. Phase history is reset only by a real
gameplay-context change
(`mode`, `level`, or `entrance`) or an explicit state import/load. A rejected
presentation frame is not a gameplay transition and no longer clears the
history. Evidence is under `build/phaseguard-v3-first-divergence/`.

The default-off `DKC1_WS_FORCE_FALLBACK_FRAME=<absolute-frame>` diagnostic
forces exactly one centered presentation frame without modifying cartridge
state. `dkc1.prefetch-phase.v1` records `prefetch_candidate`,
`prefetch_suppressed`, `soft_fallback_held`, and `prefetch_released`
transitions. Three independent 932-frame Jungle runs forced frame 7635 after
source `$0D` was prefetched. In all three, it was first suppressed at 7634,
remained held during the fallback at 7635, and released at the original stock
eligibility frame 7648. The phase and widescreen trace hashes were identical:

- phase: `BA36B9AAC7655908AE6B66D91854533CB99EAF25A4986C35B27207B4C21E2483`;
- widescreen: `33D30B1B8BAC33787C76E2631C9BE003A524D0590E398570F804C8B92908B244`.

The machine-verifiable result is
`build/phaseguard-v5-soft-fallback/verification.json`; rerun it with
`tools/verify_prefetch_soft_fallback.py`. The diagnostic and trace are inert
unless their environment variables are set.

This removes source `$0D`'s early motion/speed difference, while retaining its
early allocation/bookmark. It is **not accepted as a complete object-prefetch
solution yet**: opaque slot work and graphics initialization remain
indeterminate for records `$0D/$0E`, and source `$0B` still demonstrates the
separate case where a wide actor persists while stock culls and reallocates it.
Do not claim early activation harmless, and do not ship this guard until a
visual oracle proves that prefetched actors remain correctly presented and a
semantic trace resolves the remaining work fields.

The pre-scanner seed correction has a deterministic gameplay proof at
`build/first-divergence-treetop-righty-phaseguard-v2/`. Tree Top Town source
record `$02` (Gnawty `$4D`, authored X `$0180`) is allocated by the widened
scanner at relative frame 92, logged as `prefetch_candidate`, and held at its
initial state until the reconstructed stock interval reaches X `$0180` at
relative frame 190. Without the correction it ran about 98 extra AI frames,
hit the Kong, and the wide route exited gameplay at frame 545. With the
correction the 721-frame Right+Y route remains in entrance `$00A4`; Kong's
final position and state equal stock. Additional candidates `$03..$08` are
released at their exact stock-window crossings rather than by a fixed delay.

`tools/fresh_entry_stress_sweep.py --prefetch-phase-guard` is the only
accepted matrix invocation for this experiment. The harness removes ambient
`DKC1_PREFETCH_PHASE_GUARD` from child environments, then writes an explicit
0/1 value and records `config.prefetch_phase_guard`; this prevents a shell
setting from silently contaminating a baseline or, conversely, from being
silently discarded during a purported guarded run.

The rejected presentation-OAM companion is not part of the current design.
An instrumented suppressed Gnawty dispatch changed 50 WRAM bytes before the
transaction rollback, but it did not advance the OAM cursor or emit an OAM
entry. Source/IDA grounding explains why: placed-actor AI runs first and the
shared world-sprite renderer `CODE_BBA849` builds OAM later from the resulting
pose. Capturing OAM inside the suppressed AI transaction was therefore
state-safe but image-inert. The prototype and its environment switch were
removed. `DKC1_PREFETCH_TRANSACTION_DEBUG=1` remains as a default-off
write-set diagnostic. With `DKC1_LIFECYCLE_TRACE`, the first suppressed
dispatch for each pool ordinal emits `dkc1.prefetch-transaction.v1`: every
changed byte plus counts for the actor's own indexed state, other actor slots,
OAM, object bookmarks, verified scratch, and other global WRAM. The trace is
captured before the full 128 KiB rollback and never changes presentation.
`tools/analyze_prefetch_write_sets.py` groups those events by sprite ID and
authored source and fails closed on cross-actor, bookmark, persistent-global,
or truncated evidence. The three-scene evidence bundle is
`build/prefetch-write-set-highrisk-v3/`; all eight observed authored records
write only their own indexed actor fields plus verified scratch. No sampled
dispatch wrote another actor, OAM, `$192B` bookkeeping, or persistent global
WRAM. This is enough to prototype host-owned state projection for those exact
sprite/source pairs, not to declare every DKC actor class proxy-safe.

### BG2 parallax-loop regression (2026-08-15)

The visible Jungle report at entrance `$0016`, camera `$054E/$0071`, isolated
the repeated right-side tree to BG2.  The old host policy set
`repeat_mask=$06`: it rendered both non-terrain layers into temporary native
buffers and copied their opposite native edges into the side margins.  BG2 is
actually a 64-column plane (`wide_mask=$03`), so that policy discarded its
valid second tilemap screen.  It also changed 609 pixels in the authentic
256-pixel center compared with a native-width render.

The corrected policy is domain-specific:

- repeat only enabled 32-column layers (`repeat_mask=$04`, BG3 sky here);
- serve BG2 misses from the exact 64-column PPU map entry;
- label those reads `raw continuation`, with separate west/east counters and
  provenance color;
- retain `raw fallback` as a distinct release-blocking condition for an
  unresolved circular terrain map.

Exact-state validation used the F9 bundle
`build/visible-flight/capture-f00123656-20260815-185711-p72384`.  Three fresh
replays produced the same full-frame SHA-256
`F18A26733380BF0FCC3932DAD0AC2EEEC051FD28552B17447A1D72D5236B58E7`,
the same left/center/right hashes, `repeat_mask=4`, nonzero BG2 continuation,
and zero unsafe raw fallback.  Cropping wide columns 43-298 and comparing
them with the 256-pixel native run produced **zero changed pixels**; the old
edge-repeat build produced 609.  The candidate evidence is under
`build/bg-loop-diagnosis-candidate/` and is intentionally not release data.

### BG3 physical-width repeat regression (2026-08-15)

The Jungle Hijinxs Bonus 1 cave state at level `$0009`, entrance `$0006`
showed a faint foreground effect ending at the old left 4:3 boundary and a
matching fragment copied into the right margin.  Plane isolation proved the
reported line is BG3, not BG1 terrain, BG2 foreground art, OAM, or final-window
cropping.  Its exact PPU shape is Mode 1, `BGSC=[$69,$7C,$5B,$00]`: BG3's
`$5B` size bits are `3`, so it owns a real 64x64 tilemap and can address its
authored second horizontal screen.

There were two independent host gates.  First, the host incorrectly derived
`repeat_mask` from the inverse of the BG1/BG2 terrain-shadow mask.  That mask
intentionally excludes BG3, so even a physically wide BG3 was copied from the
native scanline.  Second, the shared PPU renderer normally clamps BG3 to the
native 256-pixel HUD width unless its dedicated BG3-wide gate is active.  The
first repeat-only candidate changed the cave mask from `$06` to `$02`, but it
left that second clamp in place and was rejected after visible testing.

The accepted policy now derives each enabled plane's physical width from its
own BGxSC horizontal-size bit.  It repeats only 32-column planes, ORs every
physical 64-column plane into the PPU render mask, and enables the existing
BG3-wide render path only while BG3 itself is physically wide.  All three PPU
presentation controls are reset at the start of every frame so a cave policy
cannot leak into a later bounded BG3 HUD, logo, or transition.  The cave trace
therefore reads `wide_mask=1`, `render_mask=5`, `repeat_mask=2`: BG1 and BG3
render wide, while only bounded BG2 repeats.

The immutable reproduction is
`build/cave-front-repeat-20260815-2104/cave-front-repeat.state`, SHA-256
`9A190D528803C07482E2D025C5490498C7C5FE632CF5C6007213A7BC485B6CF6`.
Three isolated accepted BG3 replays are byte-identical at SHA-256
`DC4001CD3BCAB522DE12CE5E4F8FF9D9FD5AB041C8D2D5107B1E7BCF45DCAD2B`.
The native-width BG3 oracle is
`093FAF5BCE37EA94F7EFE926397D46528F8526EF40E9F4E676764B3312C07123`;
comparing its 256 pixels with wide columns 43-298 produces **zero changed RGB
bytes**.  The live isolated-plane capture spans the full 342-pixel output and
records the same three masks.  Final evidence is under
`build/bg3-physical-v4-live/` and `build/bg3-physical-v4-regression/`.

The earlier repeat-only SHA-256
`AFFC248C6CB1EBFF9D9FE7BBFB09AA5FA1348B5F355E97A9BD8FD19CF2446885`
is retained only as rejected evidence: it removed the copied fragment but
still stopped BG3 at the old 4:3 boundary.

### High-world shadow-address regression (2026-08-15)

The later Jungle bonus quicksave at level `$0050`, entrance `$006C` exposed a
different hard 4:3 cutoff. Plane isolation showed that bounded BG3 correctly
repeated across 16:9; BG1 terrain was the plane stopping at the native edges.
Its absolute world X was `$9AF9` (tile 4,959), beyond WsShadow's 4,096-tile X
capacity. The trace recorded exactly 1,568 west and 1,344 east BG1 misses per
frame, all resolved as transparent blank. This also explains how high-Y
vertical stages could fail despite a valid PPU/map shape.

The fix keeps ROM decoding and trace world coordinates absolute but projects
each BG independently into a stable local cache window. Terrain origin X is
chosen to cover the full published camera range, native viewport, margins,
and guard tiles. Parallax gets its own origin so low-Y sky and high-Y terrain
can coexist. X origins are multiples of 512 pixels, preserving the rolling
map's left/right screen parity; Y origins are multiples of 256, preserving
its 32-row map wrap and VRAM-write attribution.

On the exact quicksave, BG1 origin `$9400` maps world `$9AF9` to local `$06F9`.
The three-frame regression has 8,736 terrain-margin hits and zero misses, and
the visible output is continuous on both sides. Evidence is under
`build/bonus-bg3-quicksave-20260815-2156-step/`; the durable gate is
`tools/verify_shadow_localization.py` and its report is
`shadow-localization-verification.json`.

### Transition contamination clean-history oracle (2026-08-16)

`tools/bisect_transition_contamination.py` now distinguishes bad cartridge
state from retained host presentation history. For each sampled route frame it
saves a native snapshot and the live-history frame, then loads that snapshot
in a new process and performs a diagnostic zero-frame render. The latter runs
`Dkc1DrawPpuFrame` without advancing CPU, APU, or PPU time, so WRAM, VRAM,
CGRAM, WRAM OAM, and PPU OAM remain exact while the host-only widescreen
shadow starts clean. A margin-only difference is contamination; a raw-state
or native-center difference fails closed.

The current Expresso Bonus quicksave (level `$0050`, entrance `$006C`, source
SHA-256 `200B3D34526A489F2E411543B8B1D0183AAC2414D7857C7478D4648E1A299F0C`)
is clean at relative frames 1 and 30. The path-history and fresh-history
renders have zero changed pixels in left, center, and right regions, with
exact WRAM/VRAM/CGRAM and both OAM copies. Evidence is
`build/bonus-transition-clean-history-v2/report.json`.

The same tools build then replayed imported states 0, 1, 2, and 5 for 180
frames in native and wide modes, three repeats each. All eight mode/state
cells are deterministic in WRAM, VRAM, CGRAM, OAM, and final pixels, with zero
OBJ range/time overflow. This is a stability gate, not route-completion proof;
the authored exit/barrel/section outcomes listed above remain required.
Manifest: `build/imported-state-suite-transition-tool-v18/manifest.json`.

### Live streamed terrain capture and bonus-stage culling (2026-08-16)

The Snow Barrel Blast fresh-entry route exposed a stable right-side cutoff/
repeat that the earlier sweep grader missed. The cartridge adapters had
proved a complete widened rolling-map fill, but `WsShadowFrame` still copied
only the native viewport. The host then either retained an old ROM-prefilled
margin or served blanks outside those 32 columns. The ROM decoder was not a
safe fallback here: it calibrated perfectly during forced blank, then matched
only about 30 of 224 sampled visible tiles after the real terrain appeared.

The accepted implementation keeps `extend_world` dependent on a successfully
prepared shadow and adds an authoritative live-tile capture path. When the
checksum-locked cartridge stream coverage is complete, DKC1 projects the
entire widened terrain range plus an eight-pixel guard into world tile
coordinates, reads those exact cells from the 64-column rolling PPU map, and
records them as captured provenance with `WsShadowCaptureTile`. This handles
west and east columns symmetrically; simply increasing the generic capture
column count was rejected because that API grows only east of the native
origin.

A later Right+Y stress branch exposed a second host-only artifact at a fixed
Jungle Hijinxs camera: a 13-pixel strip at the extreme left changed downward
in eight-pixel steps for twelve frames after cartridge inputs had stopped
changing. The live range was already authoritative, but the ROM bootstrap
refill continued to overwrite it after stream readiness as per-cell
game-write cooldowns expired. The accepted policy is now one-way: the ROM
decoder bootstraps margins only before complete stream coverage; after
coverage is proven, the symmetric live capture is published and left intact.
`DKC1_WS_TRACE` now hashes the full 128 KiB WRAM image and gates stability on
identity/reset boundaries, so changing decoded source data cannot masquerade
as host nondeterminism.

Snow Barrel evidence is
`build/snowbarrel-live-stream-capture-v6/`: BG1 records 351,904 west hits and
342,720 east hits, zero misses, zero blank/raw fallback, and no stable-input
margin mutation. Its WRAM, VRAM, CGRAM, and both OAM hashes remain identical
to the pre-fix run. The final frame is continuous and is retained as
`final.png`.

The current full fresh-entry sweep is
`build/world-map-fresh-entry-sweep-v8/`. It explores 65 map nodes and 325
edges, reaches 40 distinct fresh entrances, and replays each three times. The
strict grade is 40/40: zero terrain misses, zero raw fallbacks, zero
stable-input margin changes, and deterministic native state. Thirty-seven
entrances widen. Necky's Nuts, Boss Dumb Drum, and Necky's Revenge remain
intentionally centered over black because their fixed arenas publish
`lower==upper`; a tested forced-wide candidate produced visibly corrupt
right-edge tiles despite nominal stream coverage. That rejected evidence is
under `build/fixed-arena-stream-capture-v8/`.

Imported playtester states 0, 1, 2, and 5 were then replayed in
native and wide modes, three times each. All 24 runs pass and every WRAM,
VRAM, CGRAM, and OAM stream is deterministic. Manifest:
`build/imported-state-suite-live-stream-v20/manifest.json`. The verified
runner SHA-256 is
`81313a07606aa57a211c68065e0e84b88e62c48371e4e7b992d8490d8343c22d`.

`tools/fresh_entry_stress_sweep.py` extends the entrance gate with neutral,
Right+Y, and Left+Y motion from every immutable pre-entry snapshot. The clean
matrix at `build/fresh-entry-stress-v6/report.json` contains 120 branches and
240 isolated native/wide processes. It reports zero hard presentation
failures: no shadow-cache OOB, no terrain miss/raw fallback, no true OAM
X-high loss, no persistent OAM pipeline mismatch, no OAM-budget regression,
and no stable-input margin mutation. Every branch does report native/wide
machine-state divergence and is therefore honestly queued as a lifecycle
investigation; this matrix is not route-completion proof.

The original stress matrix started input after one fixed host-frame delay.
That is not a valid native/wide comparison: the widened tilemap initializer
can take a different number of host frames, and Tree Top Town's entrance walk
was exactly one frame behind in wide mode. `--align-gameplay-ready` now runs
each side to an independent post-entry snapshot, then requires matching level,
mode, resolved entrance, fade phase, active Kong identity, position, state,
and animation before applying the common input. Camera bounds are deliberately
excluded because their difference is the feature under test. The default 64
neutral frames after the coarse predicates clears the measured entrance-walk
skew. A mismatch rejects the branch without producing lifecycle evidence.

This changes the interpretation materially. The aligned Tree Top Town gate at
`build/fresh-entry-stress-v7-aligned-tree64/` reaches the same `$00F9` outcome
in native and wide mode in both repeats; the earlier critical exit difference
was phase contamination. The aligned Temple Tempest gate also removes its old
stock-only records. Winky's Walkway remains a real lifecycle result: without
the prefetch guard, source `$02` advances before stock eligibility and sources
`$03/$04` never allocate; with the guard they allocate with stock-identical
identity/pose (`build/fresh-entry-stress-v7-aligned-winky-guard/`).

The phase guard must nevertheless remain opt-in. A 40-entry, 120-branch
aligned guarded matrix is retained at
`build/fresh-entry-stress-v8-aligned-guard-full/`. It removes all `stock_only`
findings and reduces critical findings from 16 to one, but the remaining
Misty Mine Right+Y case is decisive: native dies and returns to the map while
the guarded wide run remains alive. The corresponding aligned unguarded
control at `build/fresh-entry-stress-v10-aligned-misty-baseline/` has both
sides die and reach the same map state. Therefore transactionally freezing
every early type-1 actor is not semantics-preserving. The next architecture
should keep the cartridge gameplay scanner native-width and render added
margin objects through presentation-only proxies, rather than retaining wide
actors in the real pool and rolling back all their WRAM effects.

Readiness now uses the resolved `level_state` entrance/fade oracle instead of
assuming the map entrance persists or `fade==0`: Reptile Rumble redirects
`$00EA->$0001`, while Ropey Rampage and Ice Age Alley retain nonzero gameplay
fade values. Equal nonzero bounds are valid fixed arenas. The corrected
affected-entry gate is
`build/fresh-entry-stress-v9-aligned-guard-readiness/`. Lifecycle triage also
preserves alignment-rejected branches as skipped evidence instead of indexing
an empty run list.

### Expresso Bonus quicksave culling audit (2026-08-16)

The user quicksave at level `$0050`, entrance `$006C`, SHA-256
`200B3D34526A489F2E411543B8B1D0183AAC2414D7857C7478D4648E1A299F0C`,
was rechecked after the live-stream policy change.  Same-frame isolated BG1,
BG2, BG3, OBJ, composite, and provenance captures show no hard edge at either
old 4:3 boundary.  BG1 terrain and BG2 foliage continue across the full
342-pixel presentation; BG3's authored periodic field also spans the output.
The 360-frame neutral/Right+Y/Left+Y branches report zero raw fallback, zero
terrain-margin miss, zero policy violation, and zero stable-input margin
mutation.  Evidence is under
`build/bonus-quicksave-culling-20260816/`.

The corrected pre-scanner phase guard was also replayed for 240 Right+Y frames
from this exact quicksave. Every sampled frame (0 through 210 in 30-frame
steps) is byte-identical to the unguarded build, including the timed fade and
exit, and the trace still contains zero raw fallback or policy violation.
This save therefore does not reproduce the Tree Top early-AI defect and the
guard does not hide or change its reported image.

The capture did expose a recorder defect rather than a renderer defect.  Its
F9 bundle retained a frame-300 anchor even though F12 later replaced the
machine with the bonus quicksave.  Replaying 3,325 recorded neutral inputs
therefore ended at a different WRAM hash.  The recorder now treats F12,
file-picker loads, and scripted `state_load` as hard timeline boundaries:
old inputs/anchors are invalidated and the loaded machine is captured as the
new anchor before any further emulated frame.  This is required evidence
discipline for future reports where the visual corruption occurs before the
quicksave and disappears on reload.

Interactive F11 now closes the other half of this evidence gap.  A successful
quicksave writes the native machine snapshot and—when the rolling recorder is
armed—also exports the causal anchor, resolved input history, final raw machine
planes, and same-frame BG1/BG2/BG3/OBJ/composite captures.  Format-v8 snapshots
preserve the host-only widescreen shadow directly; the companion bundle adds
the causal history needed to explain how it became invalid.  This path was
exercised in a visible build: the F11
bundle under `build/bonus-quicksave-repro-candidate-v4/` contains the snapshot,
inputs, raw planes, manifest, and all five requested layer captures, while the
user's original quicksave was restored byte-for-byte afterward.  The visible
candidate `dkc1_widescreen_desktop_quicksave_repro_v2.exe` was then launched
paused at this exact bonus-room state so the next occurrence can be preserved
without a separate F9 step.

Native state format v8 now preserves that host state directly as well.  The
extra DKC1 chunk contains a sparse, bounds-checked WsShadow snapshot plus the
calibration identity, cache origins, presentation bias, cartridge stream
coverage, and placed-actor phase state.  Existing v4-v7 states still load and
take the established cold-rebuild path.  On the Expresso Bonus state, the
authoritative 60-frame split test saved at frame 30 and resumed the remaining
30 frames in a fresh process.  It matched the uninterrupted run exactly for
the composite framebuffer, WRAM, VRAM, CGRAM, WRAM OAM, PPU OAM, renderer
state, and cumulative shadow counters.  The generated state is 324,858 bytes
rather than a raw multi-megabyte cache dump.  Re-run the contract with
`tools/verify_widescreen_savestate.py`; the recorded result is
`build/widescreen-savestate-roundtrip-bonus/report.json`.

The automatic blank-margin detector also had a blind spot relevant to this
report: it discarded a frame when *both* added margins were fully flat, on the
assumption that this was normal pillarboxing.  It now receives the proven
extended-gameplay latch from the renderer.  Fully flat sides are still ignored
for centered menus/logos/fades, but they are classified as
`full_flat_gameplay` and increment the automatic-export trigger during a
supported level.  `DKC1_AUTO_EXPORT=1` activates this detector even when no
`DKC1_BLANK_SCAN` JSONL path was requested.

The detector now also checks 16-line bands. The original whole-column profile
could dilute a BG/window/foreground cutoff occupying only part of the 224-line
frame. A band is promoted only when its adjacent native edge has structure and
at least eight margin columns collapse to a flat value; JSONL schema v3 records
`partial_height_flat` with `y0`/`y1`. The compiled model proves a 16-line
two-sided cull is detected while a centered non-gameplay pillarbox is ignored.
On the Expresso quicksave, a 240-frame Right+Y gate produced zero events under
the stronger detector (`build/bonus-bandscan-live-gate/blank-scan.jsonl`).

An offline arbitrary-snapshot matrix now removes dependence on one guessed
input macro. `tools/snapshot_widescreen_stress.py` ran the exact Expresso Bonus
quicksave through twelve fixed, diagonal, alternating, and box routes twice
each (`build/bonus-snapshot-stress-v3/report.json`). All repeat machine and
detector signatures were exact. The 24 runs contained 7,992 extended gameplay
frames, 22,871,744 terrain hits, no terrain miss/raw fallback/strict failure,
and no rendered-blank event. Therefore the supplied old state and these
420-frame branches do not reproduce the player's cull. The visible rolling
recorder remains necessary for the longer/manual route; it is armed at the
same state and will export automatically when the failure occurs.

A visible Right+Y pass from the same state reached the normal bonus-exit fade
at host frame 224 and completed 517 frames without a rendered-blank event.
The first auto-export at that fade was rejected: its composite and all four
isolated planes were intentionally black, while `blank-scan.jsonl` remained
empty.  The exporter had polled cumulative shadow diagnostics that changed
during scene teardown. `MaybeAutoExport` now consumes those counters whenever
extended terrain is unavailable and can only promote their later increments
while `Dkc1VideoTerrainReady()` is true. The corrected visible build is
`build/dkc1_widescreen_desktop_bonus_autocapture_v5.exe`, with live evidence
under `build/bonus-quicksave-autocapture-v8/`.

The later manual report at host frame 18,758 was preserved again from the
long-running desktop on 2026-08-16. F11 rewrote `quicksave.state`, but its
SHA-256 remained exactly
`200B3D34526A489F2E411543B8B1D0183AAC2414D7857C7478D4648E1A299F0C`;
WRAM, VRAM, CGRAM, WRAM OAM, and PPU OAM in the new F9 bundle also match the
earlier frame-18,758 bundle byte-for-byte. A fresh current-build same-frame
capture shows BG1, BG2, BG3, OBJ, and composite across all 342 pixels. The
wide center (`x=43..298`) is pixel-identical to a native 256x224 replay for
all five surfaces (zero changed pixels). Two deterministic 1,200-frame
Right+Y replays reach the normal bonus exit with 3,117,408 terrain hits, zero
terrain misses, zero raw-margin pixels, zero policy violations, and no blank
event. Evidence is under `build/bonus-current-same-frame-layers/` and
`build/bonus-snapshot-long-right-v4/`.

The F9 causal anchor exported by the old PID 39728 is not admissible: that
executable predates the state-load timeline reset and its anchor replays
Jungle Hijinxs instead of the loaded bonus snapshot. The current snapshot and
raw final planes remain valid. A current `dkc1_desktop_tools.exe` was launched
paused at the same immutable state for visual confirmation; any distinct
pop-in/cull must be captured there at the visible failing frame rather than
in the stale long-running process.

A boundary-specific isolated-plane audit was added after that recheck. On the
same frame, BG1 and BG2 are clean at both centered 256-pixel boundaries: their
side margins neither empty nor copy the opposite native edge. BG3 is an exact
opposite-edge repeat on both sides, matching the explicit `repeat_mask=$04`
policy for this bounded 32-column periodic horizon. Its boundary transition is
not an outlier (difference 0.0134, nearby median 0.0156), so this is not the
reported hard cull by itself. The machine-readable result is
`build/bonus-current-masked-layers/legacy-width-audit.json`. Layer-capture v2
also emits a backdrop-only surface and per-plane P5 occupancy masks, preventing
the shared gradient/backdrop from being mistaken for OBJ repetition. The
auditor checks 16-line bands as well as the whole plane; this exact state has
no hard partial-height cull. Rerun it with
`tools/detect_legacy_width_cull.py` against same-frame isolated captures.

A subsequent 420-frame, 12-action replay initially appeared to contradict
that result: the first detector revision reported a 27-pixel flat band at
relative frame 307, after the bonus exit returned to Jungle Hijinxs. Raw VRAM
comparison resolved it. The BG2 `$7400` tilemap is byte-identical to independent
fresh Jungle captures, and the flat interval begins four pixels *outside* the
old 256-pixel boundary before authored foliage resumes. It is a transparent
opening in the 64-column parallax map, not a legacy-width cutoff. Repeating a
native 256-pixel BG2 scanline in the margins (the DKC2Recomp strategy for
proven cyclic parallax) produced obvious duplicated palms and hard seams, so
that experiment was rejected and removed.

The runtime band detector now requires a flat cutoff to be connected directly
to the native/margin seam and walks outward only until structured pixels
resume. Its compiled model retains the genuine full- and partial-height cull
oracles and adds the exact four-pixel-offset/27-pixel authored-gap regression.
With that correction, two exact repeats of all twelve 420-frame action routes
are clean: zero process failures, deterministic cull events, strict margin
failures, terrain misses, raw fallback pixels, or policy violations. Evidence
is under `build/bonus-bandscan-v2-full-matrix/report.json`. The saved bonus
frame is therefore not a reproducible cull; a distinct visible failure must be
saved or exported on its exact frame rather than inferred from this state.

The visible diagnostic panel was corrected during the same recheck.  Its old
`Scanner` row displayed the type-9 section words at `$1E07-$1E0D`, not the
object scanner itself.  It now reads the real scanner record at `$00A4`, the
range at `$00EF/$00F1`, and reports the section controller separately.  At the
saved Expresso Bonus frame the actual scanner is record `$02`, range
`$994F..$9BF5` (406 pixels), with 43 presentation pixels on each side and the
widescreen world marked ready.  This rules out a native-width gameplay scanner
at the exact saved frame; the remaining reported appearance is a plane or
presentation-path question, not evidence that placed objects were culled.

### Presentation-only margin proxy renderer gate (2026-08-16)

The first approved margin proxy now reaches DKC's authentic sprite renderer.
The initial host transaction was correct but the sprite never appeared because
word `$0AB1,x` was modeled as actor state.  It is actually the global
`NorSpr_DrawOrderIndexLo` list.  Restoring the scratch actor's zero there
removed the borrowed slot from `CODE_BBA849` before it could draw.  The proxy
now preserves the authentic draw-order entry while borrowing a free actor
slot, retains only its host-owned displayed-pose state, and restores every
normal actor word after the draw.

`tools/verify_margin_proxy_ab.py` is the release gate for this path.  It
requires audio equality, no gameplay-owned WRAM difference, both WRAM-shadow
and PPU-OAM changes, an actual OAM-cursor advance, and pixel changes confined
to the side bands.  Renderer scratch, OAM shadow, and the sprite graphics DMA
queue are classified separately instead of being mistaken for gameplay state.
The two cartridge tails following the renderer (`CODE_80A203` and
`CODE_80A49D`) consume only the advanced OAM cursor from those differing
direct-page values; the other renderer temporaries are not gameplay inputs.

The Winky's Walkway source-`$02` Kritter proof uses the aligned ready snapshot
`00d9-WinkysWalkway_Main`.  At relative frame 3 the proxy adds exactly 37
pixels in bounding box `[298,90,306,98]`, where the sprite begins entering the
right extension.  The protected center is unchanged, audio PCM is exact, and
all 66 changed WRAM bytes belong to named presentation domains (48 OAM, 11
renderer scratch, 7 sprite-upload queue).  Three independent proxy-on and
proxy-off replays are byte-identical for framebuffer, WRAM, VRAM, audio, and
proxy event stream; all three A/B reports pass under
`build/winky-proxy-repeat-gate-20260816/`.

The apparent one-frame absence immediately after injection is normal SNES
pipeline behavior: DKC writes the WRAM OAM shadow first and the PPU consumes it
at the following VBlank.  Never reject or relocate a proxy from one PPU-OAM
sample without checking the WRAM shadow and the next complete frame.

### Runtime 4:3 / 16:9 desktop option (2026-08-15)

The visible Win32 host exposes `View -> Aspect Ratio` with native 4:3
(`256x224`) and pixel-aspect-correct 16:9 (`342x224`) radio options. Switching
updates only host presentation width, row pitch, DIB metadata, window size,
and widescreen presentation history. It does not run an emulated frame,
reload a state, or write cartridge WRAM.

Paused behavior is explicit. Wide-to-native center-crops the rendered frame.
Native-to-wide pillarboxes it until the next ordinary frame can rebuild real
margins. An immediate paused wide/native/wide comparison restores a cached
copy of the exact wide frame. The live cave test at host frame 1137 resized
the full window `1080 -> 908 -> 1080` pixels and found zero changed pixels in
the restored 684x448 (2x) game region. Evidence is under
`build/aspect-menu-live-v3-20260815/`.

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

## Post-bonus BG1 transition contamination (2026-08-16)

The broken Jungle frame after leaving a bonus room was not cartridge tilemap
damage. A retained-history render and a forced-cold render of the same v8
snapshot had byte-identical WRAM, VRAM, CGRAM, and both OAM copies, while the
retained BG1 margins contained the repeated outer strips. BG2 and BG3 were
not the source.

The exact 464-frame flight bundle is
`build/bonus-safe-control-repros-20260816/capture-f00000464-20260816-075932-p28476`.
Binary search located the first visible difference at relative frame 307.
The widened column initializer had completed one frame earlier while the live
VRAM ring still mixed outgoing-bonus and returning-Jungle data. The renderer
accepted column-count coverage as sufficient to bootstrap an unknown layout,
captured those transitional cells, and skipped the clean ROM prefill.

The fix separates presentation proof from gameplay readiness:

- completed stream coverage may revalidate only an already-established
  layout;
- completed coverage with an unknown layout is invalidated and its pixels are
  rejected for that frame;
- the next calibrated frame performs the normal cold ROM prefill;
- the previous next-frame terrain-ready value is restored after the rejected
  presentation frame, so widened gameplay culls retain byte-exact timing.

`DKC1_WS_COLD_STATE_LOAD=1` is the default-off diagnostic oracle used to
discard only serialized host presentation history while preserving the loaded
SNES state exactly. `tools/bisect_transition_contamination.py` compares raw
WRAM/VRAM first, then the left margin, native center, and right margin.

The repaired route is pixel-identical to its cold reconstruction in all three
regions. Three independent replays agree on:

- frame SHA-256 `13b87dc1137cf737135ba7e9e572c88442b174a961c29d1450a150379395d43b`;
- WRAM SHA-256 `f1bfb99712cd06d85c194dc26d46c15276eb1600a2aa9087c9ee9c550b4ac7dd`;
- VRAM SHA-256 `99353eadfafd748d734a42971beaffa3d1324ed6bccb6d28b052dc32a3d66fea`;
- CGRAM SHA-256 `10db7dad300104d04691ab92d0a8294fe2ed9c7c8c8119781441c2896ad4c95e`;
- PPU and WRAM OAM SHA-256
  `b546233c7c9ab27b4d7c4396d664b5997af4f5941c07c2674d2efeeda343aa6a`;
- audio FNV-1a `d4f10376e4bf6dce`.

Candidate evidence is under
`build/post-bonus-fix-candidate-v3-20260816/` and the two repeat directories.
The complete host build and all 152 Python tests pass. The exact 361-frame
Snow Barrel Blast route also retains its prior framebuffer, WRAM, VRAM,
CGRAM, both OAM, and audio hashes; it still records 257 intentional
stream-only revalidations with zero policy violations, raw fallbacks,
stable-input margin changes, or nonblack centered margins. That regression is
`build/post-bonus-fix-snowbarrel-route-20260816/`.

## Wide vertical-row staging corruption (2026-08-16)

The black and mixed-color rectangular terrain blocks seen after the Jungle
bonus return were genuine cartridge BG1 corruption, not retained host history
or a composite artifact. They were present in isolated BG1, survived a cold
render of the exact snapshot, and appeared in native presentation after the
wide run had already written the bad VRAM. Replaying the same flight-recorder
anchor and 809 inputs in native mode remained clean while wide mode produced
zero tilemap entries in three diagonal groups:

- row 19, ring columns 45..50;
- row 20, ring columns 46..51;
- row 21, ring columns 47..52.

The root cause is the stock `$81890E/$818CEF` vertical row builder. One call
stages only 36 tile entries in WRAM, but `$818A18` publishes the result into a
64-entry ring row that is subsequently transferred as two full 32-entry
halves. That is sufficient for the native viewport and insufficient for the
widened viewport during simultaneous horizontal/vertical movement.

`Dkc1VideoBeginWideRowBuild` and `Dkc1VideoAdvanceWideRowBuild` now wrap both
authentic row builders. In eligible wide gameplay they execute the unchanged
stock body twice: first with Layer1X biased left by 56 pixels, then 144 pixels
to the right of that first pass. The union covers 54 tile entries. The exact
original `$088B` is restored before the caller resumes and the ordinary full
row DMA still runs once. Native/ineligible execution remains a direct stock
path. The generated-code override preserves the inherited hardware return
context while tail-calling the second pass, so the original return is consumed
only after both stock bodies finish.

Evidence:

- the original 809-frame repro is visually clean and the formerly zero cells
  now equal the native raw-BG1 oracle;
- the later 3,559-frame route that produced mixed-color rectangles is clean in
  three independent replays;
- all three longer replays have identical frame SHA-256
  `7aaeed6679d9d09dbba9934bfc9733fcf2d87cffb15abb21855f395ad9d1080f`,
  WRAM SHA-256
  `c1acb48f01660565db725e0175717cfa93f002fbe554780a0fb014147cd87714`,
  and VRAM SHA-256
  `63c807972d4b59c36f59fd0c73118e93f7688df02b76f259302eef345f849778`;
- the automatic transition sentinel sampled the detected boundary at relative
  frames 1, 2, 3, 5, 9, 17, and 33. Retained/cold WRAM, VRAM, CGRAM, both OAM
  copies, and all five isolated/final surfaces were identical at every sample;
- all 156 Python tests pass.

The accepted evidence is under `build/black-box-rowfix-long-r1/` through
`-r3/`, `build/black-box-rowfix-layers/`, and
`build/transition-sentinel-rowfix-long-20260816/`. A window that was already
running before the build continued to show the defect because Windows kept
the old executable image mapped; visual QA must verify the build identity in
the title or executable hash after a restart.

## Seven-tile cartridge stream guard gap (2026-08-16)

The later one-tile-wide vertical strip near the middle of the screen is a
separate BG1 defect. The saved state contains the bad data in VRAM, so retained
and cold host rendering, isolated BG1, composite, and even native-width
presentation all reproduce it. At Layer1X `$075D`, the strip maps to physical
BG1 tilemap ring column 59. Compared with the clean oracle, 29 of its 32 entries
are wrong while every neighboring column is complete. The column survives
neutral and vertical-only branches, but ordinary rightward streaming rewrites
all 32 entries and repairs it.

The renderer consumes seven complete margin tiles to cover the 43-pixel 16:9
extension at every sub-tile phase. The cartridge hooks had been scaled to a
48-pixel/six-tile margin, leaving exactly one physical ring column outside the
initializer and moving-row coverage. The fix restores the cartridge contract
used by the proven ROM patch: a 56-pixel margin, `$0170/$0178` initializer
backsteps, and `$002E/$002F` initial column counts. The host still crops the
unused guard pixels to 342x224.

Evidence:

- exact affected snapshot and isolated planes:
  `build/visible-rowfix-flight-20260816/capture-f00005014-20260816-084158-p69336`;
- retained versus cold and wide versus native renders are byte-identical,
  proving the defect is guest BG1 state rather than shadow provenance;
- three 3,559-frame margin-7 replays are byte-identical and retain the accepted
  framebuffer SHA-256
  `5786377682795be94746fc42dcb5c358a17b7d6e9fd9353bf7da4ecaef39686e`;
- their WRAM and VRAM hashes are respectively
  `b52f79bae703e5470daaa90663212a3e18d4d349dd2a426f277d13e78059389b`
  and
  `1394609a461d0081423bd59e46908a73c5583ede24059e2c70a5de439afd90eb`;
- `build/transition-sentinel-margin7-20260816/report.html` passes all six
  discovered transitions and 39 retained/cold samples without a raw-state,
  layer, center, or margin divergence.

Loading the original post-corruption snapshot is intentionally not accepted as
proof of the initializer fix: save states serialize the already-bad VRAM. Use a
fresh-entry route or a repaired later checkpoint for visual QA.

## Fixed-layout bonus cave initializer containment (2026-08-16)

Jungle Hijinxs Bonus 1 (`$0032=$0001`, level `$0030=$0009`, entrance
`$003E=$0006`) exposed a separate guest-VRAM regression: the entire purple
floor under Donkey/Rambi disappeared and the right cave wall became a
checkerboard of unrelated columns.  Isolated BG1/BG2 and native-width renders
of the same snapshot reproduced it, proving cartridge state corruption rather
than host margin history or composition.

The shared initial backstep/count hooks introduced by `6bf8981` treated every
call to the two rolling-map initializer bodies as proof that cartridge
widening was safe.  This fixed cave calls the same machinery, but its authored
layout is not a rolling terrain capability boundary.  The widened pass wrote
unrelated cave columns into the native VRAM ring; a later identity reset could
reject the coverage claim but could not undo those writes.  The later
seven-tile and double-row changes made ordinary scrolling coverage complete,
but were not the root cause: archived 05:44, 07:44, 07:56, and pre-rowfix 08:11
binaries all reproduce the missing floor from the same fresh-entry route.

`Dkc1VideoCartridgeWideningSceneEligible` now fails closed for this exact
scene.  It keeps the stock backstep/count and bypasses widened stream selection
and both row-builder passes; only host-side side presentation remains eligible.
The already-corrupt bonus snapshot remains historical evidence and is not a
valid fix test.

The subsequent Ropey Rampage fresh-entry A/B showed that the same broad
assumption also corrupted an ordinary scrolling level.  Default policy is
therefore now stock cartridge initialization/row streaming for every scene,
with host-side ROM prefill supplying the 16:9 margins.  The old widened
initializer/row experiment is available only with
`DKC1_ENABLE_EXPERIMENTAL_CARTRIDGE_WIDENING=1`; it is not a release mode.
This is a global containment of the bad 8:21 policy, not a growing room
denylist.  The exact Bonus 1 rejection remains as defense in depth even under
the experimental switch.

Evidence:

- fresh anchor/input bundle:
  `build/visible-margin7-flight-20260816/capture-f00158989-20260816-094934-p70536`;
- three independent 3,589-frame guarded replays are byte-identical: frame
  `2007b0bfe5bf8e367ca94d647882922a6621aff07b331017d8754ef6849e9e2b`,
  WRAM `905023a7e93db616e875d20d52fcbee0a35949f87be813231419cd4eab8f65c1`,
  and VRAM `cd4582c0015bf3f77af452eacb23bfaff8b7a1dd76442fd80aac6dccc32145b2`;
- the guarded isolated composite restores the complete floor at
  `build/bonus-replay-scene-guard-r1/layers-wide/composite.png`;
- the ordinary 809-frame and 3,559-frame terrain routes retain their accepted
  framebuffer hashes.  Their offscreen WRAM/VRAM hashes intentionally differ
  because the experimental cartridge writes no longer execute.

Ropey Rampage supplied the cross-level decision gate.  From the preserved
world-map node, the same 601 controller frames were replayed with only the
cartridge initializer policy changed.  Experimental widening deterministically
produced sliced/missing terrain; stock cartridge initialization produced the
complete floor and background.  The stock-default route then repeated three
times with exact frame `b894ba8bc5f8217cf86b4603649c464e307409c9cef89932a6d330921d80492f`,
WRAM `5748eb983e4cb2b41df1089806c27479ea9c2c660c3cee97336efbf2c79dd259`,
and VRAM `80476dd3f2661027fd19d37c2119e05b72904733a3f299ea88f3d24d30ec5793`.
The equivalent Jungle entry is also clean and exact across three runs, and the
six-transition/37-sample retained-versus-cold sentinel passes.

## Split map and metatile-definition banks

The host ROM decoder must treat the level-map bank and the metatile-definition
bank as independent cartridge state. `Level_SetTilemapPointers` at `$81:8C66`
publishes them in `$D5` and `$D6`, respectively. Most early test scenes happened
to make a one-bank assumption look valid; underwater level `$0061`, entrance
`$80BF`, does not: its map is `$E9:0000` while its metatile definitions are
`$D0:0000`.

Using `$D5` for both reads decoded plausible but wrong tiles. The safety gate
then measured only 50 of 224 native viewport entries matching the live rolling
tilemap and correctly selected centered fallback, making the whole scene appear
4:3. Passing `$D5` to the map-cell read and `$D6` to the definition read raises
the exact same state's vertical score to 212/224 and enables real two-sided
extension. Both banks are now part of the hard source identity and per-frame
trace; a change in either invalidates retained margins.

Acceptance evidence is external under `build/current-level-live/` and is not
committed. Three independent 120-frame replays from the exact state produced
identical frame, WRAM, and VRAM hashes. This is a decoder-contract fix, not a
level allowlist or relaxed calibration threshold.

## Authored transparent cells beyond the native underwater boundary

Successful ROM decoding does not guarantee that the cartridge contains useful
art outside the stock viewport. In Croctopus Chase level `$0061`, entrance
`$00BF`, the exact world position X `$053F`, Y `$2900` calibrates the vertical
layout at 224/224 entries. BG1 nevertheless stopped at world X `$0640` in the
lower-right margin. Offline ROM inspection proved that the adjacent native-edge
metatile at X `$0620-$063F` contains wall art in all sixteen 8x8 characters,
while the following map cells are wholly transparent. BG2 correctly continued
the water behind them, so blank-frame and shadow-hit counters could not detect
the terrain hole.

The repair is intentionally not a global repeat mode. During ROM margin prefill
only, and only for this exact gameplay level/source signature (mode `$0003`,
level `$0061`, vertical layout, map bank `$E9`, definition bank `$D0`), a right-
margin metatile may reuse the nearest native-edge metatile when the target is
wholly transparent and the source has pixels in all sixteen characters. Any
partial target, partial source, left margin, native pixel, or other scene is
unchanged. `boundary_continuation_tiles` in the widescreen trace records this
separately from ordinary authored ROM hits.

The exact save-state frame continued 96 transparent margin tiles and changed
only right-margin pixels. WRAM, VRAM, CGRAM, PPU OAM, and WRAM OAM remained
byte-identical. All 57,344 native-center pixels match the native oracle. Three
zero-frame captures are byte-identical (SHA-256
`AAB0EF0B8351CA15FB278450AA4ED369797A1BE92D07CDD8F98201D1106AC0D4`),
and three replays of the preserved 3,551-frame visible input history end at the
manifested WRAM SHA-256
`F5BCA064C4E89CB7BE63E194927D95A6487A8CD32109B9BEFF76C0F923CBAEFC`.

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
- the static `$BE8179` contract audit passes and routes report no unexpected
  indirect targets;
- final-window captures, not only internal plane images, pass visual review;
- widened gameplay does not let margin sprites consume the native viewport's
  32-sprite/34-sliver scanline budget; native and centered screens retain the
  exact limits, and the captured bad pose passes a bounded-budget replay;
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
