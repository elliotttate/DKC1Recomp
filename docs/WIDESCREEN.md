# DKC1 recomp widescreen architecture

The recomp's standard widescreen mode displays a 342x224 source image: 43
extra SNES pixels on each side of the native 256x224 frame. With the SNES 7:6
pixel aspect, this is 1.78125, within one source pixel of 16:9. The native
macOS host also offers a symmetric 308x224 mode with 26 pixels per side; its
1.60417 display aspect is the closest even-width presentation to 16:10.

The visible desktop exposes presentation modes under `View -> Aspect Ratio`.
Windows offers `Native 4:3 (256x224)` and `Widescreen 16:9 (342x224)`; macOS
also offers `Widescreen 16:10 (308x224)`. Switching modes changes only the
host framebuffer, source pitch, presentation history, and window size; it does
not reload the ROM or alter cartridge WRAM, camera, collision, or level state.
The desktop defaults to 16:9. On macOS, `DKC1_ASPECT=16:10` selects 16:10 at
startup. For scripted/headless runs, `DKC1_WIDESCREEN=1` selects 16:9 and
`DKC1_WIDESCREEN=0` selects native 4:3.

The macOS texture always retains its native source width (256, 308, or 342
pixels). The Metal presentation width applies the SNES 7:6 pixel aspect,
giving approximately 299x224, 359x224, or 399x224 on screen. Windowed mode
keeps nearest-neighbor integer scaling. Fullscreen uses a fractional fit. The
default `Sharp Bilinear` sampler keeps most of each source texel flat and
blends only across an approximately one-output-pixel boundary; it avoids both
full-frame blur and the alternating 9/10-pixel columns produced by a nearest
fit on the tested Retina panel. The View menu also retains persistent `Smooth
(Linear)` and `Pixel Sharp (Nearest)` choices. Every choice consumes the same
maximum display area. This scaling is presentation-only and never changes PPU
pixels or cartridge state.

## Whole-game model: a virtual presentation tilemap

The scalable design follows DKC's existing rolling-map contract instead of
widening rooms one at a time. `Level_SetTilemapPointers` at `$81:8C66`
publishes the map, metatile-definition, and VRAM-ring sources. The horizontal
column DMA at `$81:883F` advances only when `CameraX & $FFF8` changes; the
vertical path at `$81:8A6F/$81:8DFA` follows the equivalent eight-pixel camera
phase. The host mirrors those cartridge-authentic writes into a world-keyed
presentation cache and decodes authored ROM metatiles only for cells the
native 256-pixel viewport has not populated yet.

Each rendered tile is resolved by provenance, not by level ID:

1. a live PPU/VRAM tile intersecting the native viewport;
2. a cartridge tilemap write observed at its exact world key;
3. an authored ROM metatile that calibrates against the live native image;
4. a separately proven periodic/parallax continuation;
5. transparent fallback or centered stock presentation.

This makes the native image the oracle and keeps collision, exits, bosses,
camera bounds, and object state independent from presentation. It also gives
the project one place to fix coverage math. A fine-scrolled 256-pixel viewport
intersects 33 8x8 tile columns, not 32; the shared shadow therefore retains the
partial 33rd live tile on every wide 64-column map. The serialized v2 shadow
format deliberately retains its historical 32-column array: the live overhang
is recaptured on the first frame after load, preserving old save-state
continuity without freezing transient edge data into the file format.

The model cannot manufacture art that does not exist in the ROM. Some fixed
rooms have intentionally transparent cells outside the stock camera. Those
scenes require a source-backed continuation capability or must fail closed;
silently repeating a wall or wrapping the tilemap is not a whole-game fix.

## What came from DKC2Recomp

DKC2Recomp supplied the useful architecture rather than game-specific
constants:

- extend the host PPU presentation instead of moving the game's logical
  camera;
- prefill a private world-keyed tilemap shadow for the extra margins;
- calibrate decoded world tiles against the live rolling tilemap;
- widen only when the scene passes exact structural and calibration gates;
- center unsupported screens over black rather than showing stale memory;
- isolate source adaptations in an idempotent, fail-closed post-generator.

DKC1's level maps are static ROM data rather than DKC2's WRAM maps, so its
margin decoder follows DKC1's horizontal and vertical metatile formats.
The terrain decoder retains absolute world-X/world-Y coordinates, while each
bounded shadow plane projects them through a stable scene-local origin. X
origins are 512-pixel aligned to preserve the rolling map's two-screen parity;
Y origins are 256-pixel aligned to preserve its 32-row wrap. This is required
for high-coordinate bonus and vertical stages whose absolute tile indices
exceed the shared cache dimensions. `RetainHistory` remains disabled because
its viewport-relative Y contract is incompatible with DKC1's ROM-prefill rows.

## What came from the SuperZSNES work

The emulator ROM-patch investigation supplied exact DKC1 routine families,
failure mechanisms, and save-state evidence. The generated-code override pass
ports the parts that remain necessary when the logical camera stays stock:

| Domain | Recomp adaptation | Why it remains necessary |
| --- | --- | --- |
| common sprites | widen the two shared display-cull windows | otherwise objects exist but pop at the old 256px edge |
| placed objects | widen 18 left, 14 span, and two right-prefetch paths | ropes, enemies, barrels, and logic objects must become eligible across the visible margins |
| bananas | widen its private coverage and preserve 9-bit OAM X | prevents late bananas and positive-X wraparound ghosts |
| vertical ropes | widen the private renderer cull and pack OAM X-high | prevents left/right margin pop-in and wrapped rope segments |
| type-$05 groups | retry only zero-bookmark children while the group remains visible | wider prefetch increases actor-pool pressure; the stock one-shot parent could otherwise permanently lose a target barrel or child actor |
| OBJ scanline budget | add one sprite/sliver slot per live 8-pixel margin column | prevents extra-margin objects from consuming the native 32-sprite/34-sliver budget and cutting 8x8 pieces out of Kong poses |
| diagnostics | BGSC, per-layer scroll, and terrain-ready telemetry | identifies whether a bad frame is unsupported, miscalibrated, or an object-domain failure |

All transformations match exact generated labels and constants, fail if the
expected source shape changes, and are tested for idempotence. They are gated
by `Dkc1VideoTerrainReady()`, so fixed screens and unsupported layouts retain
the stock visibility rules.

## What was intentionally not ported

The SuperZSNES ROM hack moved DKC1's logical camera bounds inward to reveal
more world. That required compensating patches for tilemap initialization,
row streaming, player endpoint clamps, cave exits, banana coordinates, and
K. Rool's arena logic. The recomp does not move those bounds, so copying those
patches would recreate the very gameplay bugs they repaired.

These remain cartridge-authentic in the recomp:

- camera and layer coordinates;
- collision and player movement limits;
- exit-door and level-transition tests;
- boss arena and progression bounds;
- tilemap stream coordinates and initialization.

This separation is the central rule: presentation may reveal more pixels;
gameplay coordinates are not translated merely to make the picture wider.

## Widescreen OBJ scanline capacity

The hardware renderer accepts at most 32 sprites and fetches at most 34
eight-pixel OBJ slivers on one 256-pixel scanline. Applying those same totals
to a 342-pixel world view lets sprites in the 86 added columns consume the
native viewport's budget. A dense two-Kong pose can then lose complete 8x8
pieces even though WRAM OAM, PPU OAM, and OBJ graphics are valid; changing pose
or moving can make the damage disappear again.

DKC1 opts into a host render policy that adds one sprite and one sliver of
capacity for every additional eight-pixel column actually visible. The policy
uses `extraLeftCur + extraRightCur`, not the allocated framebuffer border.
Consequently native-width play and centered logos, menus, and unsupported
scenes retain the exact 32/34 limits. `NoSpriteLimits` is deliberately not
used: removing the limits entirely could expose normally hidden overlap and
flicker behavior in future scenes.

The headless summary, per-frame OAM JSONL, checkpoints, and F9 flight-recorder
manifest now record the PPU `rangeOver` and `timeOver` latches. The original
reported bad frame predates those captures, so final acceptance still requires
one exact recurrence/replay proving that the latch fires before this policy and
does not fire after it. The focused synthetic PPU test already proves the
native, centered, and live-margin boundary cases.

## Fail-closed scene policy

`Dkc1DrawPpuFrame` first builds a hard scene identity from DKC mode, level,
entrance, source/map/metatile/VRAM signatures, PPU layout, active layers, and
terrain selection. A changed identity rejects retained pixels immediately.
The candidate world coordinates and calibration are then computed read-only.
They cannot mutate the retained shadow. Camera bounds must span the requested
wide extension before calibration is eligible. Only an accepted horizontal or
vertical layout enters phase two and commits shadow origins, capture, and
prefill.

A failed decision clears retained pixels and centers the native frame over
black. Within one unchanged hard identity, calibration has a true two-frame
remaining miss budget for isolated dynamic-tile mismatches; it never
accumulates with play time and cannot cross an identity change. This prevents
transition frames from seeding a wrong layout or retaining stale level art for
seconds.

Jungle Hijinxs uses 64-column BG1/BG2 world layers and a bounded 32-column BG3
sky. Only the stream-selected terrain plane is keyed to the ROM level map.
Every other enabled BG—including the independently staged parallax plane and
BG3 sky—is repeated from its authentic native scanline after world
calibration. Treating the parallax plane as terrain produced all-miss shadow
lookups and transparent side cutoffs; repeating from register shape alone
would instead repeat logos and transition art.

This policy is selected per scene from the physical BGxSC width, not from the
terrain classification. Jungle Hijinxs Bonus 1 uses a real 64-column BG3 cave
foreground (`BG3SC=$5B`), so that BG3 is neither repeated nor clamped to 256
pixels: it is added to the PPU render mask and uses the renderer's BG3-wide
path. Bounded 32-column BG3 scenes continue to use native-scanline repetition.
All BG3 presentation gates reset every frame to prevent scene leakage.

## Validation record

- Override unit tests cover every category, exact-match failure, and applying
  the transformer twice without further changes.
- The shared shadow unit test proves a nonzero fine-X phase captures the live
  33rd tile from a 64-column map while the on-disk v2 snapshot shape remains
  compatible with existing 32-column states.
- Runtime contract tests lock down the exact native-mode 16-bit stack push
  used by the type-$05 child retry and require the presentation camera to move
  BG scroll and OAM together without writing DKC1's logical camera or bounds.
- The PPU sprite-limit test proves native and centered screens still reject
  sprite 33/sliver 35, while a live 16-pixel extension admits exactly two
  additional slots without enabling unlimited sprites.
- The MSVC host builds successfully after regeneration.
- Deterministic frame 7,600 stock-width output retains the known frame, WRAM,
  VRAM, CGRAM, OAM, and audio hashes.
- Deterministic frame 7,600 wide output reports `terrain_ready=1`, extends the
  Jungle world and BG3 sky, and has stable presentation output.
- Frame 600 reports `terrain_ready=0`: both 43-pixel margins are entirely
  black and the centered 256x224 crop is pixel-identical to the native-width
  oracle. Its partial Rare graphic is therefore a native renderer result, not
  a widescreen leak.
- A 14,000-frame scripted wide run completes. The pre-existing unresolved
  `$BE8179` dispatch warning remains a separate recomp bring-up issue.
- The visible 7,644-frame boot/map/Jungle route records two PPU-frame epochs,
  one hard-identity transition, one horizontal cold start, zero grace accepts,
  zero raw fallbacks, and zero trace-policy violations. No shadow commit occurs
  before DKC publishes camera bounds wide enough for both margins.

## Level-edge presentation clamp and black-screen repair

Keeping DKC1's logical camera stock avoids the ROM hack's collision, exit, and
boss-boundary regressions, but a centered wide host viewport at the authored
left endpoint asks for margin pixels before the beginning of the level. Those
nonexistent world pixels produced the hard left-side cutoff until the player
first moved right. The host now computes a presentation-only target using the
active per-side extent:

`clamp(logicalCamera, lowerBound + extra, upperBound - extra)`

The resulting bias is applied to all BG scroll inputs and decoded OAM X for
the rendered frame, then removed before HDMA continues. Generated object cull
adapters account for the same bias. Logical camera, collision, exits, level
streaming, and WRAM bounds remain unchanged. The deterministic first Jungle
frame now reports `presentation_bias=43` and renders continuous scene data at
the left edge; frame 7,600 reports bias zero after ordinary scrolling.

The later all-black failure was unrelated to background culling. The type-$05
child-allocation retry had emulated a native 16-bit 65816 push by writing at
the old stack pointer and subtracting two. A later `PLA` therefore consumed
one stale byte and shifted the caller's return frame; the captured failure
returned from `$BD:FC19` to invalid `$BD:FE01` and eventually executed garbage.
The retry now follows the generated core's exact push sequence: decrement S,
write the little-endian word, then decrement S again. A complete 12,984-frame
widescreen route finishes without that invalid return or a black frame.

## Slip-Slide Ride moving-margin calibration

The supplied Slip-Slide Ride quicksave (mode `$0009`, level `$0051`, entrance
`$006D`) exposed a presentation-only flicker while traversing. The first bad
trace frame was absolute frame `271198` (zero-based capture index 6). The
cartridge logical camera normally leads or trails BG1's rendered PPU scroll by
one to four pixels in this room, while the vertical map has an authored
512-pixel phase. Calibration previously floor-divided each absolute coordinate
to a tile and then subtracted. Whenever only one coordinate crossed an
eight-pixel boundary, the decoder jumped to an adjacent ROM row or column for
one frame and rebuilt both margins from the wrong source.

Calibration now subtracts the signed pixel coordinates first and rounds that
delta to the nearest tile, resolving an exact four-pixel half-tile tie toward
zero. This is a coordinate-domain conversion rather than a room capability:
independently truncating two absolute pixel positions can select adjacent tiles
even when the positions differ only by the cartridge's normal camera smoothing.
The signed conversion keeps the X decode offset at zero across that smoothing,
still advances a real five-pixel crossing, and preserves authored vertical
phases such as 512 pixels. It therefore applies to every calibrated layout
without consulting mode, level, entrance, or ROM-bank IDs.

The exact 330-frame Right+Y route reduced world-aligned temporal changes in the
two margins from 2,064,024 to 582,396 pixels (71.8%). It improved 123 frames and
made none worse. The old trace used six decode-offset states; the corrected
trace uses `[0,64]` on 329 frames and `[0,-64]` only at the wrap. Three repeats
match in framebuffer, WRAM, VRAM, CGRAM, both OAM copies, audio, and trace.
The correction changes center pixels on 13 transient frames, and every one of
those corrected centers matches the native-width oracle exactly.

A later user state on the icy slope exposed the exact half-tile case while
moving uphill and downhill. With ties rounded away from zero, its 780-frame
Right+Y/Left+Y replay used X offsets `-1` on 120 frames and `+1` on seven
frames. Resolving the tie toward zero keeps `[0,-32]` for every frame. The A/B
changes 1,666,730 pixels across 129 frames, all in the two margins and none in
the native 256-pixel center; final WRAM and VRAM are byte-identical. Three
independent repeats match in framebuffer, WRAM, VRAM, CGRAM, both OAM copies,
audio, and trace. The second immutable user state is SHA-256
`1c72f2e5151f7603d255ff7e79ba458b9feeb6979a0634b48826f9a3d3f05af5`.

The supplied state was also validated through a fresh-entry branch rather than
treated as the only oracle. A deterministic controller route climbed the
vertical rope, traversed to the visible Zinger, took the normal death, settled
on the Slip-Slide Ride map node, and re-entered with B. The 900-frame re-entry
trace contains 135 fail-closed transition frames followed by 765 accepted
extended frames, with zero raw fallback and zero trace-policy violations. All
evidence is under `build/repros/ice-cave-flicker-20260830/`; the preserved user
state is SHA-256
`42176b43e8fc6ef90a10f651355218d68137216402ab4af34deb5f82b60d68d3`.

The global promotion was checked against the pre-promotion runner on the exact
underwater state and the 780-frame cave traversal: framebuffer, WRAM, and VRAM
remain byte-identical. A controller-only map sweep reached four authentic
entrances (`$003E`, `$00A7`, `$006D`, and `$0024`); all four passed three
repeats with zero terrain misses, raw margin fallback, policy violations, or
repeat instability. Twelve 420-frame neutral/Right+Y/Left+Y traversal branches
also match the pre-promotion runner exactly in final framebuffer, WRAM, VRAM,
CGRAM, OAM, scene state, and widescreen grade. The available map root does not
unlock the other 36 entrances in the committed capability floor, so this is
not recorded as a complete all-entrance promotion result.

## Structural vertical-wall continuation

Vertical rooms can contain wholly transparent lateral map cells that stock
hardware could never show even though an authored wall ends at the native
viewport. Margin continuation is now selected from the ROM topology instead
of a Croctopus Chase scene tuple. The target metatile must be wholly
transparent, the source back toward the stock edge must be fully populated,
and the same empty-target/full-source relationship must occur on an adjacent
metatile row. Any partial intervening metatile is treated as an authored
opening and fails closed. Horizontal layouts are ineligible.

This capability runs only while pre-filling presentation margins. It does not
write WRAM, VRAM, camera bounds, collision, object state, or native pixels. The
historic Croctopus wall maps satisfy the structural predicate on consecutive
rows; the currently available exact underwater and cave states remain
byte-identical because the predicate correctly stays inactive at their tested
positions.

## Tradeoffs and next tests

Object prefetch is not purely visual: an actor may start simulating before it
would in a native-width run. This is necessary when the actor is visible, but
it can change allocation order or phase. The current child retry repairs the
proven permanent-loss case; it does not prove every actor family safe.

An experimental, default-off dispatch guard can retain the early allocation
and bookmark while delaying placed-actor behavior until the source enters the
stock scanner interval. Its phase history is keyed to gameplay context and
survives soft presentation fallbacks; native state loads/imports reset it
explicitly. The three-repeat forced-fallback contract passes, but the guard is
not a release default until wide-persistent actors and opaque initialization
work have authored route oracles.

Future acceptance should compare narrow and wide deterministic routes at
object lifecycle boundaries, not require byte-identical WRAM after prefetch.
The important invariants are authored object identity, world position,
collision/interaction alignment, level progression, exits, and deterministic
replay. Save-state reports from the SuperZSNES project remain valuable as
route definitions and correctness oracles even though their binary states are
not directly loadable by this host.
