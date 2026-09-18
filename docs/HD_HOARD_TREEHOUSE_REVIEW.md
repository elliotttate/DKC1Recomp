# Banana Hoard and Treehouse HD plates

September 17, 2026. Review record. This is an HD host-
presentation change only; cartridge streaming, activation, collision, bounds,
save data, and the original low-resolution frame are unchanged.

## Reported symptoms

- Kong's Banana Hoard entrance `$0047` showed horizontally smeared rock on
  the right and repeated 32-pixel material blocks on the left.
- The fixed-room art obscured the two top-right HUD OAM components, leaving
  only a narrow yellow strip.
- The 53x60 `KONG'S / BANANA / HOARD` OAM sign was painted back over the HD
  wall as its chunky source raster.
- The treehouse interior entrance `$005C` still used low-resolution room art.
- Diddy's Hoard-room hat-stomp gag fell back to original-resolution composites
  and impact pieces.
- The saved Grounded finish slider changed the setting but not the fixed-room
  image because those rooms bypass the GPU scene compositor.

The first bad frame is the first visible frame after either supplied-state
load. These are presentation/material failures, not cartridge-state
divergence.

## Preserved reproductions

```text
build/hd-slice/hoard-treehouse-20260917/original-states/
  tester-quicksave-20260917-1005.state
    SHA256 50a120d73091de3d95e0f0ddeff34ceef89c79fa4b961f934e1cfaeca2d12b1a
  fresh-treehouse.state
    SHA256 5dd646c00531280b34bef3243dd753fe1a066a89033d21fd4846b78501f2b858
```

Hoard exact tuple:

```text
mode $0001, level $002D, entrance $0047
camera $B000,$0108, bounds $B000..$B000
BGSC [$69,$7C,$5B,$00], CGWSEL $10, CGADSUB $93
```

Treehouse exact tuple:

```text
mode $000D, level $0040, entrance $005C
$D5=$E3, $1B11=$0200, bounds $0000..$0000
BGSC [$70,$74,$6D,$00], CGWSEL $02, CGADSUB $C2
```

`routes/hoard-to-treehouse.inputs` enters the treehouse with controller input;
`fresh-treehouse.state` was captured from that route, not constructed by RAM
patching. The earlier clean left-walk Hoard entry remains recorded in
`docs/HD_HOARD_REVIEW.md`.

## Change

- Added exact, fail-closed fixed-scene selection for the treehouse tuple.
- Added optional preloaded `fixed-hoard-wide.bgra` and
  `fixed-treehouse-wide.bgra` plates. Missing files still fail closed.
- The treehouse plate replaces its fixed background while live OAM remains on
  top. The Hoard plate overlays only authored opaque regions; DK, Diddy, and
  HUD OAM remain live.
- Fixed scenes use the CPU scene compositor even when Metal presentation is
  enabled, avoiding a second fixed-plate ABI and shader path.
- Hoard HUD components above y=64 are explicitly composited above its
  presentation plate in HD only. The low-resolution priority result is not
  altered.
- The Hoard sign is recognized narrowly by its room-local 53x60 OAM geometry.
  In HD only, that original raster is omitted so the authored sign in the
  fixed plate remains visible. Other OAM objects are unchanged.

The final Hoard plate contains three independently reviewable regions:

1. x=0..639: newly authored continuous left cave with varied stalactites;
   the former repeated material-grid blocks are gone.
2. the sign region: a smooth, source-faithful three-line wooden sign with the
   exact text `KONG'S / BANANA / HOARD`.
3. x=1080..1367: the expanded right repair, blended from x=1080 and fully
   replacing the former banded wall and boulder.

The generated left-cave source is preserved as:

```text
build/hd-slice/hoard-treehouse-20260917/generated/hoard-left-generated-v2.png
build/hd-slice/hoard-treehouse-20260917/generated/hoard-left-fitted-v2.png
build/hd-slice/hoard-treehouse-20260917/generated/fixed-hoard-wide-v5.png
```

Its generation prompt preserved the room silhouette, floor, openings, purple
palette, lighting, and sign placement while replacing repeated blocks with
unique mineral ridges and stalactites. No characters, HUD, paths, or new room
geometry were requested. The right-wall generation similarly preserved the
original geometry while removing repeated rows and horizontal bands.

### Diddy hat-stomp and source ordering

The supplied Hoard state reaches the complete prank during a 180-frame idle
replay. Twenty-nine standalone Diddy/hat/effect rasters were first assembled or
source-reconstructed at 4x. The remaining failure was OAM ordering: neutral
impact tiles `$31C0..$31C6` are interleaved among Diddy's palette-2,
priority-3 pieces. Treating each palette as one flattened object changed the
native overlap; joining every touching palette merged unrelated actors.

The runtime exception is limited to the exact Hoard tuple, the exact seven
impact tile identities, Diddy's palette/priority, and overlapping OAM boxes.
Priority rotation is honored. This restores the native reconstruction oracle
without changing guest OAM or object behavior.

Of 25 combined HD poses, 19 assemble directly from approved primitives, one
uses exact per-cell source ownership, and five reuse approved art plus
continuous Lanczos source reconstruction for only 18 otherwise unowned native
cells. The offline assembler's reconstruction path is explicit opt-in, capped
at 25 percent, and recorded in `object-composite-provenance.json`. The final
180-frame contact sheet is:

```text
build/hd-slice/hoard-treehouse-20260917/diddy-hat-stomp-final-sequence/contact.png
```

The follow-up pass traced the remaining rough late frames to six small impact
materials rather than Diddy's body or the hat. Three white puff pairs and three
late streak/debris overlays were edited against their exact source silhouettes.
Ten dependent late composites were rebuilt. Source-reconstructed cells now use
one continuous Lanczos image and ownership mask, eliminating the visible 4x
cell grid without changing native ownership. The final late-frame replay is:

```text
build/hd-slice/hoard-treehouse-20260917/qa-late-diddy-banana-20260917/
  diddy-stage-sequence-final/contact.png
```

### Banana silhouettes and HUD propagation

All eight ROM-decoded `BananaStatic` rotations were repaired as one registered
sheet. The new art preserves each source key and exact 4x material dimensions,
adds transparent inset around the silhouette, restores clipped tips and sides,
and removes the original stair-step contour. The change was propagated through
all 800 normal count/phase HUD materials and the 210 captured object composites
that embed a static banana. No material identities were added or removed.

The accepted generated sheets, final registration paths, and exact edit prompts
are recorded in:

```text
build/hd-slice/hoard-treehouse-20260917/qa-late-diddy-banana-20260917/
  generation-prompts.json
  banana-registered-v1-contact.png
  diddy-late-effects-registered-v1-contact.png
```

The frame-0 live route image showing all three repaired bananas is
`banana-route-wide-metal/frame_000000.png`, SHA256
`b2f17adf8ed8bebd08b968a3ce4dc81acd573015212f956129869e7c68a49ead`.
The final Diddy late-frame contact is SHA256
`24832efedaa0db5e86a15f299d9e0fbb73bb35372728c545416ea1cfc4d296cb`.

### Grounded finish on fixed rooms

Hoard and Treehouse are intentionally CPU-composited 4x plates. Grounded
finish previously existed only in the GPU HD scene shader, so its slider had
no visible effect here. The macOS CPU upload path now runs the same finish
formula on 4x HD frames before display filtering. Native 224-line inputs are
unchanged, and already-finished direct GPU textures are not processed twice.
The offscreen Metal test verifies strength 0 is exact, 33 is weaker than 100,
more than 90 percent of the synthetic HD frame changes at 100, repeated output
is deterministic, and direct texture presentation stays raw.

## Exact-state evidence

Final Hoard output is in:

```text
build/hd-slice/hoard-treehouse-20260917/candidate-hoard-v4/run{1,2,3}/
```

All three independent three-frame runs are byte-identical:

```text
native PPM  38143f029a92cbd87be4552b7352335fef5cefb5bc92bc19eda999dd436e3185
HD PPM      6e3be52a9cd619757e9f0b30af24ab468e54cb6650e133fa3ceae352a9dff1a9
WRAM        08153b1f65d3f52388dec6cbac1b3a7b04c807e92737b249f3dfaf70f63b85b8
VRAM        379f25232dfaa52590add63b19d6c7693e8f0f141a2da6d15a02e7b36ae97b69
```

Every run reports `mismatch_pixels=0`, `native_mismatch_pixels=0`, supported
entrance 71, `fixed_plate="hoard"`, and zero visible material misses.

Treehouse runs 2 through 5 remain identical after the Hoard HUD/sign change:

```text
native PPM  7cbfbd289f5124dd28a99f80d46d0c0d6878297fe7636a2e97a57a457060f557
HD PPM      bd2ee352fff96a93c78dfe5a6fd4e6a9f07a40e68f07f805d939b70d11aeec6b
WRAM        43d33624ce8cfac7d0c2ef63092251c06ea8b4258e5dcc27c396e92194185f9b
VRAM        9fdaa5f1a604724da37c28a601dd9055106d7f4db993cdcba42011cd8928dc02
```

Treehouse reports `mismatch_pixels=0`, `native_mismatch_pixels=0`, supported
entrance 92, and `fixed_plate="treehouse"`. Its 20-22 small visible object
miss samples are existing live-object fallback pixels, not background holes.

Final extended replay evidence:

```text
build/hd-slice/hoard-treehouse-20260917/verify-diddy-final-exact-cpu/results.json
  12 deterministic native/wide runs, 180/180 supported frames per HD run
  zero fallback pixels and zero visible OBJ misses

build/hd-slice/hoard-treehouse-20260917/verify-hoard-final-fresh-cpu/results.json
  clean build/hd-slice/entry.state plus 300-frame Left route
  12 deterministic native/wide runs, zero fallback and zero wide material misses

build/hd-slice/hoard-treehouse-20260917/verify-treehouse-final-exact-cpu/results.json
  controller-derived fresh-treehouse.state
  12 deterministic native/wide runs, zero fallback and zero background misses

build/hd-slice/hoard-treehouse-20260917/qa-late-diddy-banana-20260917/
  verify-diddy-final/results.json
  12 deterministic native/wide runs, 180/180 supported HD frames,
  zero fallback pixels and zero visible object misses

build/hd-slice/hoard-treehouse-20260917/qa-late-diddy-banana-20260917/
  verify-banana-final/results.json
  12 deterministic native/wide pickup runs, banana count 3,
  125/125 Metal-validated HD frames with zero fallback or GPU mismatch
```

The final private pack contains 24,263 materials and 1,741,595,200 resident
pixel bytes. Its raster digest is
`b47a6b39bcf96782bcb7dd719eeaa21520de4914114633ae46960da9ed5d0252`.
Diddy's exact-state guest hashes remain unchanged from the
pre-art probe: frame `07d9155e...63ed8`, WRAM `77c003d4...8e8e5`, VRAM
`602b843f...16b`, CGRAM `3551261b...8e6`, OAM `f0cf1c33...d385`, and audio
FNV-1a `5273b8935e1247fb`.

### Candy portrait follow-up

The first HD treehouse plate gave the framed Candy portrait the correct bow
and costume colors but an invented, masculine face and head silhouette. The
September 17 follow-up used the exact plate as its edit target and the supplied
DKC-era Candy references for identity, then composited only the accepted
portrait opening back into the canonical plate. Candy now retains her pale
pink muzzle, green eyes and heavy lashes, pink eyelids and lips, blonde curls,
polka-dot bow, brown fur, pink outfit, and hand-on-hip pose.

The replacement is bounded to x=987..1044 and y=264..428 of the 1368x896
plate. Exactly 8,904 RGB pixels changed, and an independent image difference
found no changed pixel outside that rectangle. The wood frame, wall, room
lighting, tire overlap, furniture, geometry, and all other room pixels are
unchanged. The original and replacement fixed-plate hashes are:

```text
original fixed-treehouse-wide.bgra
  715975fedddaef4724ad08f4a9b0c8ce8608466a61850f9876d3c80b519249aa
replacement fixed-treehouse-wide.bgra
  33ceb4a39a711449004cf6111f547380606b970fafedf1206679624cdfcb787d
accepted plate PNG
  4e23e778705a8172ee5be3d34c555c7e219669e238af458f378488b310c57f6
```

Generation input, the unmodified plate, accepted edit, deterministic mask,
raw round-trip, and replay evidence are preserved under:

```text
build/hd-slice/treehouse-candy-20260917/
```

The accepted 58x168 portrait patch is the deliberate exception to the normal
private-art rule and is tracked in Git as:

```text
assets/hd-preview/treehouse-candy-v1.png
assets/hd-preview/treehouse-candy-v1.rgba
assets/hd-preview/treehouse-candy-v1.json
```

`scripts/apply_hd_fixed_patch.py` verifies the unmodified plate and patch
hashes, performs the same rounded source-over blend, and requires the corrected
plate hash before replacing the file atomically. The macOS packaging script
runs it after copying a private HD pack; already-corrected plates are accepted
idempotently and any unknown plate fails closed.

`verify-treehouse-candy-v1/results.json` passes 12 deterministic native/wide
idle replays from the controller-derived `fresh-treehouse.state`. Every audited
frame reports `fixed_plate="treehouse"`, zero reconstruction mismatches, and
zero fallback pixels. The repeated wide HD result is
`ba45bbc5b254908f5d4e013a067e27eecbf9db1423eebbcb3661d7f9b905e451`;
WRAM, VRAM, CGRAM, OAM, and audio remain identical across all repeats. The
rebuilt 0.0.15 app contains the replacement raw hash, passes strict deep
codesign verification, and was inspected paused in the real 16:9 application
window. This is a fixed-plate art correction; the 40-entrance and transition
matrices were not rerun.

## Visible QA and review build

The signed preview app is:

```text
build/macos/DKC1 Hoard and Tree House HD.app
bundle id com.flat2vr.dkc1recomp.hd.hoard-treehouse-20260917
```

It opens the preserved Hoard save at 16:9 and pauses on the first rendered
frame. Both Hoard and Treehouse were inspected in the real application window
after the final package was signed. Hoard left-wall repetition, right-wall
banding, covered HUD, and the chunky sign raster are absent; Treehouse fills the
wide frame with its HD plate and keeps its live actors/HUD above it. A live
Grounded finish change from 100 to 0 and back to 100 visibly changed the fixed
Hoard frame. The follow-up live pass also played the complete Diddy prank into
the outdoor scene and visibly checked the repaired rotating bananas and HUD
counter. The bundle was restored to the Hoard state and is left open,
paused, with the 3x finish setting selected for tester review.

## Validation and scope

```text
cmake --build build/macos --target dkc1_macos dkc1_snesrecomp_headless test_macos_graphics --parallel
./build/macos/test_macos_graphics runner/macos_graphics.metal
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.test_hd_object_composites tests.test_hd_preload \
  tests.test_hd_polish tests.test_widescreen_runtime_contracts -v
# 52 tests: OK
```

The exact Hoard state, controller-derived treehouse entry, Jungle banana route,
and final visible macOS window are validated. The complete 40-entrance
capability floor and transition-contamination matrix were not rerun. The fixed
plates remain restricted to their two exact room tuples; the shared banana art
is validated on the Jungle pickup route and through its generated HUD/composite
set, not every scene in the game. No commit was made; tester visual acceptance
is still required.
