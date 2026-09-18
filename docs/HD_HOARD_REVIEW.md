# Kong's Banana Hoard: 16:9 and HD

> September 17 follow-up: the presentation-only fixed-plate candidate that
> removes the remaining left repetition, right smear, covered HUD, and chunky
> sign is documented in `docs/HD_HOARD_TREEHOUSE_REVIEW.md`. The historical
> clamp evidence below remains useful, but is not the current visual candidate.

September 17, 2026. Private HD / host-presentation only. No cartridge
streaming, activation, collision, save format, or default-release changes.

Walking left from the Jungle Hijinxs treehouse, or loading the tester's
hoard quicksave, dropped the picture to a centered 4:3 pillarbox of original
pixels. The outdoor treehouse at camera 0 stayed 16:9 HD. The failure is the
locked Banana Hoard interior.

## Scene identity

Exact-state replay of
`build/hd-slice/nano-level-20260916/polish/signs-20260916/user/quicksave.state`
(copied as evidence; not rewritten):

```text
mode $0001, level $002D, entrance $0047
map DA:0000 / metatiles BF00
camera / bounds $B000,$0108 / $B000..$B000
BGMODE $09, CGWSEL $10, CGADSUB $93
INIDISP $09 on the supplied save (night/fade brightness)
```

The same DA:0000/BF00 tileset as Jungle Bonus 1, but a different room with
a zero-span camera. BG1 `$69` and BG3 `$5B` are physical 64-column maps.
VRAM columns 32..63 are distinct authored cave, not a wrap of the native
256.

Fresh left-walk from `build/hd-slice/entry.state` (280 frames of Left)
enters this exact tuple after the treehouse door. Outdoor frames through
camera 0 remain Jungle entrance `$0016`, 16:9, HD supported, zero visible
misses.

## Causes

1. HD `HdSceneEligible` accepted Jungle `$0016`, the D9 jungle-return map,
   and Bonus 1 (`$6900..$6C00`) only. Entrance `$0047` failed closed.
2. The hoard uses the same inverted empty color window as Bonus 1
   (`CGWSEL=$10`, `CGADSUB=$93`). Without that math exception, the room
   would still reject after eligibility.
3. Widescreen `bounds_ready` requires a camera span of at least two
   margins. `$B000..$B000` correctly refuses rolling-map calibration, then
   the host pillarboxed the locked interior.

Connected-cave art stays on origin `$6900`. The hoard does not borrow that
world pack.

## Change

- `Dkc1HdHoardSceneEligible` is the explicit tuple above.
- HD scene and cave color-math accept it. Bonus-cave connected world does
  not.
- Host widescreen may `extend_world` for this locked tuple. `terrain_ready`
  stays false, so object windows and cartridge streaming remain stock 256.
- The 64-column VRAM ring is a different cave stretch, not the neighbor of
  this 256. The left wall is a Jungle-style reflection of the authored 4:3
  tiles about one tile inside that wall. The right opening continues the
  last authored column (`Dkc1LockedInteriorSourceX`); flipping that mouth
  put a fold through the cave. HD samples those same 4:3 tiles.
- The rolling-map shadow, `bounds_ready` gate, and other rooms are
  unchanged.

Rejected alternatives: showing the raw 64-column ring (disconnected cave);
mirroring the right cave mouth (visible fold); widening Bonus 1's cave
world to `$B000` without source registration; dropping `bounds_ready`
globally; inventing new side art.

## Evidence

```text
P=build/hd-slice/nano-level-20260916/polish/signs-20260916
P/hoard-inspect/          # pre-change 4:3 + scene_guard
P/hoard-fix/              # first 16:9 pass (raw 64-column ring)
P/hoard-mirror/           # both-margin reflection (right fold rejected)
P/hoard-right-clamp/      # left wall reflect, right opening continues
P/hoard-right-clamp/repeat-{1,2,3}/
P/hoard-fix-walk/         # fresh left-walk into the hoard
P/treehouse-outdoor/      # camera 0 Jungle, still 16:9 HD
```

Three independent 3-frame exact-state replays of the left-reflect /
right-continue presentation are byte-identical:

```text
frame  38143f029a92cbd87be4552b7352335fef5cefb5bc92bc19eda999dd436e3185
wram   08153b1f65d3f52388dec6cbac1b3a7b04c807e92737b249f3dfaf70f63b85b8
vram   379f25232dfaa52590add63b19d6c7693e8f0f141a2da6d15a02e7b36ae97b69
hd ppm ace58f3556df64bd974ef089a89c5431b8d2a935e087a6d96d625c6d252d3a7e
```

WRAM/VRAM match the pre-change hoard inspect. `terrain_ready=0`. HD
reconstruction mismatch is zero. The left margin still reflects the
authored wall about the one-tile inset. Every right-margin column matches
native column 255 (the last 4:3 tile), so the cave mouth continues instead
of folding. Sprites stay in the 256. Some BG1 samples still fall back to
original pixels where this room is not in the pack.

The 40-entrance capability floor was not rerun. This is one locked
interior and does not change shared calibration.

## Residual

- The hoard sign uses a scale2x 4x of the authored 53x60 raster so the
  three-line text stays sharp. Twelve previously missing BG1 chunks are
  now context-keyed Fatality materials (`hoard-sharp/`). This room's
  exact-state replay has zero visible misses.
- Door-transition frames with unpublished bounds still pillarbox, then
  recover when the `$B000` tuple is live.
- Cave `entry.state` / `bonus-cave-20260916/user` were not opened or
  overwritten.
