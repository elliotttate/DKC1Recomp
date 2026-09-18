# Jungle Bonus 1: Nano scenery and native HD support

September 16, 2026. Private opt-in HD experiment. No cartridge streaming,
activation, collision, object lifecycle, save format or global widescreen
capability changes. The preceding doorway fix remains in place; see
[HD_NANO_LEFT_EDGE_REVIEW.md](HD_NANO_LEFT_EDGE_REVIEW.md).

## Reproduction and root causes

The latest tester quicksave from `left-tile-20260916/user` was preserved
read-only at `build/hd-slice/nano-level-20260916/polish/bonus-cave-20260916/tester.state`.
Below, `P` denotes that private evidence directory. Saved global frame 13326,
first replay frame 13327, level 9, mode 1, entrance 6, camera (27359,280),
342x224 framebuffer with presentation bias -7. This is the purple Jungle Bonus
1 cave, not evidence that the HD toggle was switched off.

Three separate restrictions explained the original-looking room:

1. The HD scene guard accepted entrance 22 only, excluding all cave objects and
   backgrounds before lookup. Bonus exit selects Jungle entrance 8, so that
   transition also needs source-backed scene selection.
2. The cave uses `CGWSEL=$10`, `CGADSUB=$93` with an inverted empty color-window
   span (`WOBJSEL` flags 3, left 255, right 254). The pinned PPU evaluates this
   as a full-line math region. The HD compositor previously rejected it.
   OBJ palettes 0..3 are exempt from color math; carrying that exemption into
   CPU and Metal removes the remaining source-color mismatch.
3. The private pack contained no registered cave scenery. New terrain, backdrop
   and foreground art was required even after the renderer supported the room.

The full cave tuple is explicit: DA:0000/BF00, mode 1, level 9, entrance 6 and
camera bounds `$6900..$6C00`. Jungle return is recognized by its existing proven
D9:0000/A3C0 source and bounds, rather than one entrance number. World-pack
selection uses the complete cave tuple because the incoming entrance ID can
change before the outgoing map disappears. Both scene packs load together;
there is no texture file I/O during gameplay. A spatial color window still
rejects HD, including changes made by HDMA after frame preparation.

`connected-cave.bin` is a separate `DKHWv002` registration with its own source
palette and world-X origin `$6900`. Every 8x8 source tile is verified before
using connected art. Switching scene registration invalidates cached atlas
bindings. Missing or mismatched content falls back to original pixels.
`HdGpuObject.math_exempt` is part of the CPU/Metal ABI; the delivered bundle
contains its matching header and shader.

## Art pipeline and observed coverage

Three reference-guided Nano Banana Pro 4K generations recreate the extracted
terrain, periodic cave backdrop and periodic foreground. No Magnific Precision
pass was used. Exact prompts/provider results are saved in
`P/generation-requests.json`; raw images are in `P/outputs`, registered RGBA
surfaces in `P/*-registered.png`, and runtime materials in `P/materials`.
Source-constrained contour and matte assembly retains the original layout.
The ROM terrain decoder matched all 806 sampled visible tile entries before
extracting the 1024x512 authored room surface. `P/extract.py`, `assemble.py`,
`connect.py`, `generation-manifest.json`, and `assembly.json` preserve the work.

The installed pack contains 24,197 materials and 1,736,815,040 resident pixel
bytes. Existing Jungle and character materials are retained unchanged.
`tools/build_hd_object_composites.py` assembles 252 observed HUD/banana
combinations from already-installed Nano primitives. Every opaque source pixel
must match and be covered; one unknown pixel rejects the composite. Existing
materials are never overwritten. Provenance is stored in the pack.

### Banana overlap repair

The first cave pass left some bananas pixelated because several runtime object
captures are overlapping HUD/banana primitives rather than standalone sprites.
The compositor now validates a native reconstruction with overlap-aware exact
set-cover, including exhaustive placement for small 16x16 primitives, before
painting the HD material. The updated cave pack adds 209 generated composites;
67 of the 68 banana-colored captures are now covered by generated or existing
materials, and the remaining 40x18 capture is registered from the existing
HUD material plus two BananaStatic layers. The manually registered material is
`4a04cda9dc200cb3d7c77ab692feb9e1a0f1f7c2377d70ec255f6cbbdd89e1a5.dkhd`.
The exact cave state now passes 12 deterministic native/Metal replays with
preload enabled and no gameplay texture reads. The preserved outdoor-to-cave
replay also passes 12 deterministic native/Metal runs with the same pack.

All supported cave frames in both exact-state and fresh-entry routes have zero
visible missing BG samples. Character and ordinary banana replacements are
visible in the native Metal app. This does not mean all OBJ combinations are
covered: clipped balloon parts, overlapping HUD/banana shapes and other unseen
composites can still use original pixels. On the exact 300-frame route, summed
missing OBJ samples are 27,166 native / 26,895 wide. These are repeated visible
samples, not unique missing assets. The overall HD coverage issue stays open.

## Replays, transition closure and visible QA

The exact route is 30 neutral, 120 Right, 120 Left, 30 neutral frames. The
fresh-entry route starts from the preserved earlier outdoor doorway state:
70 Right+Y, 40 Right+B, 45 neutral (break crate and mount Rambi), 1000 Right+Y,
90 neutral. It enters the cave, collects bananas, exits to Jungle entrance 8,
and continues into a later fade. Both use controller inputs only. There is no
state patch or repair. Exact scripts are `P/move.inputs` and `P/fresh.inputs`.

The final build passes **24 replay legs and 7,479 raw CPU/Metal frame comparisons**.
Each route repeats three times with HD off/on at native 256x224 and wide
342x224. Final reports are `P/verify-final-exact/results.json` and
`P/verify-final-fresh/results.json`. They check guest hashes, repeatable HD
images, raw per-frame CPU/Metal equality and complete preload with zero
additional texture reads. Coverage is a separate report, not a generic pass.

The exact route has zero source-reconstruction mismatch. The fresh cave has
84 source samples conservatively restored from the native oracle at isolated
object-overlap frames; broader outdoor traversal has additional established
fallback samples and some wide BG misses. Raw CPU/Metal output still agrees.
The report must not be read as all frames/rooms being visually accepted.

At fresh-route frames 625 (inside cave) and 1000 (after returning outside),
a cold reload plus one frame matches uninterrupted playback in final native
framebuffer, WRAM, VRAM, CGRAM, OAM, OAM shadow and HD image. Evidence:
`P/cold-comparison.json` and `P/cold-check.py`. The frozen pre-change executable
also gives identical guest hashes for the full fresh route; see
`P/baseline-guest-comparison.json`. No supplied state was corrupt or rewritten.

The actual native Metal window was inspected at the exact tester root with
HD terrain, both parallax/foreground layers, DK riding Rambi and bananas.
The final preview is left paused after one neutral frame, with no input
playback. The preceding app and tester save remain untouched. F7 resumes;
F12 restores the new app's private copy. Delivery is
`build/macos/DKC1 Jungle and Bonus HD.app`; exact environment/build/signature
identity is recorded in `P/app-build.json`.

All 33 HD tests pass, including exact/partial source ownership, separate cave
origin, incoming-entrance/outgoing-map isolation, empty/inverted color windows,
OBJ palette math exemption, source-complete object assembly, sanitizers and
immutable CPU/Metal packets. The native and headless targets build, and
`git diff --check` passes. No commit or staging was performed.

## Identities and reproduction

- Supported ROM: `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.
- Cave tester state: `73c611e986d51c8f10d41f0443608bc76b8559ba7880add4aeb5aa7c454791be`.
- Earlier outdoor state: `9e854885e48b9791c3af01ed762e0edba921905bf8f93bfc2c81c1f61d9528f1`.
- Final headless executable: `1bc37f739e29919bdfd783fd9a1696d04d7d7783750e06471a090e1ae0cef120`.
- Signed native executable: `2aeec0ef854e67fe3b50358e387e9515032f5e4d955afadf210014eab9318427`.
- Raster pack: `8bbd8b9aaf3ce19ac3e808a3de5a181101b0e94da7b9633a9c3384aa87969229`.

The verifier additionally hashes `preload.txt`, `background-centers.txt`,
`connected-world.bin` and `connected-cave.bin` in `pack_indices`.

```sh
P=build/hd-slice/nano-level-20260916/polish/bonus-cave-20260916
python3 tools/verify_hd_scene.py "$ROM" "$P/review-new" \
  --pack "$P/materials" --preload --exact-centers --connected-world \
  --metal --polish 65 --coverage --cases replay --state "$P/tester.state" \
  --input-play "$P/move.inputs" --frames 300 --jobs 1
```

For fresh entry, use `../left-tile-20260916/tester.state`, `fresh.inputs`, 1245
frames, and a new output directory. Unsupported bonus rooms, other levels and
the complete 40-entrance release matrix remain outside this HD-only scope.
