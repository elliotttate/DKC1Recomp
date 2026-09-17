# DKC1 HD texture and level-fix handoff

Updated September 17, 2026. This maps the current HD texture pipeline, private
evidence, runnable build, and the workflow for continuing fixes in other levels.
The verified scope at handoff is Jungle Bonus 1 cave plus its outdoor transition,
Jungle Hijinxs night-palette object reuse (signs, Rambi, Kongs), level-load
fade HD, and Kong's Banana Hoard 16:9 + HD. See
[HD_NIGHT_PALETTE_REVIEW.md](HD_NIGHT_PALETTE_REVIEW.md) and
[HD_HOARD_REVIEW.md](HD_HOARD_REVIEW.md). The current review build also adds
the Treehouse fixed plate, the complete Diddy hat-stomp animation, and working
Grounded finish on CPU-composited fixed rooms. The latest follow-up also repairs
all eight rotating banana silhouettes, propagates them through the HUD and
captured composites, and smooths the remaining late Diddy impact overlays.

## Canonical checkout and runtime

```text
/Users/briantate/Documents/GitHub/DKC1Recomp-HD
build/macos/DKC1 Hoard and Tree House HD.app
build/macos/DKC1 Hoard and Tree House HD.app/Contents/Resources/HDScene/
build/macos/DKC1 Hoard and Tree House HD.app/Contents/Resources/HDScene/Materials/
```

The app bundle is synchronized from the private pack below. Update the pack,
rebuild its indices, and then synchronize changed materials and indices into the
bundle; do not hand-edit individual runtime assets.

## Current pack and evidence

```text
build/hd-slice/nano-level-20260916/polish/bonus-cave-20260916/
build/hd-slice/nano-level-20260916/polish/bonus-cave-20260916/materials/
build/hd-slice/hoard-treehouse-20260917/qa-late-diddy-banana-20260917/app-build.json
```

Important pack files are `materials/preload.txt`,
`materials/background-centers.txt`, `materials/object-silhouettes.txt`,
`materials/object-bases.bin`, `materials/connected-world.bin`,
`materials/connected-cave.bin`, and `materials/object-composite-provenance.json`.
The pack has 24,263 materials and 1,741,595,200 resident pixel bytes after
the Banana Hoard sign, Diddy hat-stomp animation, banana silhouette/HUD pass,
and twelve cave-chunk registrations. Its current raster digest is
`b47a6b39bcf96782bcb7dd719eeaa21520de4914114633ae46960da9ed5d0252`.
The silhouette index has 4,924 exact object masks; `object-bases.bin` holds
3,673 authored native rasters so night CGRAM can tint that art.

Exact cave save and preserved outdoor state:

```text
build/macos/DKC1 Jungle and Bonus HD.app/Contents/Resources/HDScene/entry.state
build/hd-slice/nano-level-20260916/polish/left-tile-20260916/tester.state
build/hd-slice/nano-level-20260916/polish/bonus-cave-20260916/fresh.inputs
```

Current verification outputs:

```text
build/hd-slice/nano-level-20260916/polish/bonus-cave-20260916/verify-banana/
build/hd-slice/nano-level-20260916/polish/bonus-cave-20260916/verify-banana/results.json
build/hd-slice/nano-level-20260916/polish/bonus-cave-20260916/verify-fresh-banana/
build/hd-slice/nano-level-20260916/polish/bonus-cave-20260916/verify-fresh-banana/results.json
build/hd-slice/nano-level-20260916/polish/signs-20260916/hoard-fix/
build/hd-slice/nano-level-20260916/polish/signs-20260916/hoard-mirror/
build/hd-slice/nano-level-20260916/polish/signs-20260916/hoard-right-clamp/
build/hd-slice/nano-level-20260916/polish/signs-20260916/hoard-sharp/
build/hd-slice/nano-level-20260916/polish/signs-20260916/treehouse-outdoor/
build/hd-slice/hoard-treehouse-20260917/qa-late-diddy-banana-20260917/
build/hd-slice/hoard-treehouse-20260917/qa-late-diddy-banana-20260917/verify-diddy-final/results.json
build/hd-slice/hoard-treehouse-20260917/qa-late-diddy-banana-20260917/verify-banana-final/results.json
```

The current Hoard exact, clean-entry, and Treehouse branches each pass 12
deterministic native/wide CPU-oracle replays with preload enabled and zero
fallback pixels. Hoard wide output has zero visible material misses. Fixed
rooms intentionally use the CPU compositor, so they do not emit Metal scene
validation records; the ordinary-scene Metal matrix remains in the earlier
cave evidence.

## Art sources and generated work

```text
build/hd-slice/precision-v2-level-20260915/banana-source/original-frames/
build/hd-slice/nano-level-20260916/complete/originals/
build/hd-slice/nano-level-20260916/polish/bonus-cave-20260916/capture-exact/
build/hd-slice/nano-level-20260916/polish/bonus-cave-20260916/missing-objects.jpg
build/hd-slice/nano-level-20260916/background-fix/
build/hd-slice/hoard-treehouse-20260917/diddy-hat-stomp-final-probe/
build/hd-slice/hoard-treehouse-20260917/diddy-hat-stomp-final-sequence/
build/hd-slice/hoard-treehouse-20260917/qa-late-diddy-banana-20260917/generation-prompts.json
build/hd-slice/hoard-treehouse-20260917/qa-late-diddy-banana-20260917/banana/registered-v1/
build/hd-slice/hoard-treehouse-20260917/qa-late-diddy-banana-20260917/diddy/late-effects-v1/
```

For the cave banana issue, 209 composites were generated from existing
primitives. The final overlap-heavy capture is registered as:

```text
build/hd-slice/nano-level-20260916/polish/bonus-cave-20260916/materials/4a04cda9dc200cb3d7c77ab692feb9e1a0f1f7c2377d70ec255f6cbbdd89e1a5.dkhd
```

## Source tools

```text
tools/build_hd_object_composites.py   # exact native reconstruction and compositing
tools/build_hd_object_silhouettes.py # exact object masks plus native color bases
tools/build_hd_preload_manifest.py   # rebuild preload/index files
tools/build_hd_scene_pack.py         # assemble/register a scene pack
tools/build_hd_connected_pack.py     # source-verified connected scenery
tools/verify_hd_scene.py             # deterministic native/Metal replay checks
tools/upscale_hd_assets.py           # image upscale/material preparation
tools/hd_sprite_matte.py             # sprite matte and contour preparation
tools/hd_sprite_pack.py              # sprite material packaging
tools/run_hd_slice.sh                # common HD slice runner
```

`build_hd_object_composites.py` is source-first: every opaque native pixel must
be explained by selected primitive layers before HD art is painted. Small 16x16
primitives may overlap and are exhaustively placed; larger primitives use rare
anchor placement. Unknown pixels reject a composite by default. It resolves
registered silhouette aliases with their native color bases. The explicit
`--reconstruct-unowned` option is reserved for source-proven OAM interleaving;
it keeps approved HD cells, reconstructs only the otherwise unowned remainder
as one continuous Lanczos contour and ownership mask, caps it at 25 percent,
and records the exact count in provenance. Diddy's
hat-stomp uses this path for 18 native cells across five of 25 combined poses.

## Workflow for the next level

1. Preserve an immutable exact save and a clean fresh-entry state. Record ROM,
   state, executable, level, entrance, camera, aspect, and input schedule.
2. Capture the real window and raw native/HD planes. Keep exact-state and
   fresh-entry evidence separate; an old save can contain historical VRAM or
   object state that should not be repaired globally.
3. Create a dated directory under `build/hd-slice/`. Keep original captures,
   generated images, registration metadata, missing-object sheets, and replay
   output together.
4. Register background/foreground art with the scene-pack tools. For OBJ
   captures, determine whether each raster is standalone or an overlap composite
   before creating bespoke art.
5. Rebuild `preload.txt` and connected indices. Confirm material count and
   resident bytes, then synchronize only changed files into the app bundle.
6. Run exact and fresh `verify_hd_scene.py` with `--preload --metal
   --exact-centers --connected-world`, then inspect `results.json`. Omit
   `--metal` for the Hoard and Treehouse fixed plates: they intentionally use
   the CPU compositor and are validated by its reconstruction oracle.
7. Relaunch the visible app, load the exact state, pause after one neutral frame,
   inspect the actual window, and manually test transitions, pickups, and
   animated objects after the headless run.

Example exact-state check:

```sh
P=build/hd-slice/nano-level-20260916/polish/bonus-cave-20260916
python3 tools/verify_hd_scene.py \
  /Users/briantate/.cache/dkc1-precision/verified-dkc1-usa.sfc \
  "$P/verify-next" --pack "$P/materials" --preload --metal \
  --exact-centers --connected-world --polish 65 \
  --state "build/macos/DKC1 Jungle and Bonus HD.app/Contents/Resources/HDScene/entry.state" \
  --cases idle --jobs 1
```

## Existing reviews and issue history

```text
docs/HD_NANO_BONUS_REVIEW.md       # cave/background/banana status
docs/HD_NIGHT_PALETTE_REVIEW.md    # night CGRAM objects, signs, level-load fade
docs/HD_HOARD_REVIEW.md            # Banana Hoard 16:9 + HD, treehouse door
docs/HD_HOARD_TREEHOUSE_REVIEW.md  # fixed-room art, HUD/sign ordering, exact/fresh evidence
docs/HD_NANO_POPIN_REVIEW.md       # streaming and pop-in investigation
docs/HD_NANO_POSTPROCESS_PLAN.md   # anti-aliasing/post-process plan
docs/HD_PRELOAD_PERFORMANCE_REVIEW.md
docs/HD_MATERIAL_CACHE_REVIEW.md
docs/HD_METAL_COMPOSITOR_REVIEW.md
docs/HD_BALLOON_FACE_REVIEW.md
docs/HD_ANIMATION_HANDOFF.md
docs/WIDESCREEN_HANDOFF.md
```

Read `AGENTS.md`, `docs/WIDESCREEN.md`, and
`docs/WIDESCREEN_DEBUG_TOOLS.md` before shared renderer or streaming changes.
`reference/` is read-only; do not hand-edit `generated/` or the `snesrecomp`
submodule. Keep diagnostics default-off.

## Current identities

```text
ROM SHA256:        fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15
Cave state SHA256: 73c611e986d51c8f10d41f0443608bc76b8559ba7880add4aeb5aa7c454791be
Headless SHA256:   189565d327e8c0a77ff60ed9114a841e03193925ffd4c6506ab7afa605ee0844
Signed app exe:    8ee718fdd54eb1b96f05c4893a51f200a95d3485071258b3a2ef82fd50b83b44
```

The worktree contains pre-existing source changes and private generated assets.
Do not reset or clean it while taking over. Check `git status` first, preserve
the evidence directories, and keep each new level change independently
reversible.
