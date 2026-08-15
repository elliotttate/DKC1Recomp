# DKC1 recomp widescreen architecture

The recomp displays a 342x224 source image: 43 extra SNES pixels on each side
of the native 256x224 frame. With the SNES 7:6 pixel aspect, this is 1.78125,
within one source pixel of 16:9.

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
The terrain shadow uses absolute world-X/world-Y keys. The shared
`RetainHistory` mode is deliberately disabled because it changes Y to a
viewport-relative key; combining that mode with DKC1's absolute ROM-prefill
rows caused complete margin misses and the original hard black side cutoff.

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

## Fail-closed scene policy

`Dkc1DrawPpuFrame` first verifies a supported PPU shape, reconstructs the
candidate margins, and calibrates the native viewport. Only a successful
calibration enables extra space, repeated bounded backdrop layers, generated
visibility adapters, and object prefetch. A failed calibration clears the
private shadow and centers the native frame over black. Calibration confidence
has a fixed two-frame grace for isolated dynamic-tile mismatches; it never
accumulates with play time, so a transition cannot retain stale level art for
seconds.

Jungle Hijinxs uses 64-column BG1/BG2 world layers and a bounded 32-column BG3
sky. Only the stream-selected terrain plane is keyed to the ROM level map.
Every other enabled BG—including the independently staged parallax plane and
BG3 sky—is repeated from its authentic native scanline after world
calibration. Treating the parallax plane as terrain produced all-miss shadow
lookups and transparent side cutoffs; repeating from register shape alone
would instead repeat logos and transition art.

## Validation record

- Override unit tests cover every category, exact-match failure, and applying
  the transformer twice without further changes.
- Runtime contract tests lock down the exact native-mode 16-bit stack push
  used by the type-$05 child retry and require the presentation camera to move
  BG scroll and OAM together without writing DKC1's logical camera or bounds.
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

## Level-edge presentation clamp and black-screen repair

Keeping DKC1's logical camera stock avoids the ROM hack's collision, exit, and
boss-boundary regressions, but a centered 342-pixel host viewport at the
authored left endpoint asks for 43 pixels before the beginning of the level.
Those nonexistent world pixels produced the hard left-side cutoff until the
player first moved right. The host now computes a presentation-only target:

`clamp(logicalCamera, lowerBound + 43, upperBound - 43)`

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

## Tradeoffs and next tests

Object prefetch is not purely visual: an actor may start simulating before it
would in a native-width run. This is necessary when the actor is visible, but
it can change allocation order or phase. The current child retry repairs the
proven permanent-loss case; it does not prove every actor family safe.

Future acceptance should compare narrow and wide deterministic routes at
object lifecycle boundaries, not require byte-identical WRAM after prefetch.
The important invariants are authored object identity, world position,
collision/interaction alignment, level progression, exits, and deterministic
replay. Save-state reports from the SuperZSNES project remain valuable as
route definitions and correctness oracles even though their binary states are
not directly loadable by this host.
