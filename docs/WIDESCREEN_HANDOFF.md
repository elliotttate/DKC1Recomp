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
record reaches that interval. A whole-pool seed on the first dispatch after a
reset treats actors already present in a loaded snapshot as left-censored and
started. Evidence is under `build/phaseguard-v3-first-divergence/`.

This removes source `$0D`'s early motion/speed difference, while retaining its
early allocation/bookmark. It is **not accepted as a complete object-prefetch
solution yet**: opaque slot work and graphics initialization remain
indeterminate for records `$0D/$0E`, and source `$0B` still demonstrates the
separate case where a wide actor persists while stock culls and reallocates it.
Do not claim early activation harmless, and do not ship this guard until a
visual oracle proves that prefetched actors remain correctly presented and a
semantic trace resolves the remaining work fields.

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
