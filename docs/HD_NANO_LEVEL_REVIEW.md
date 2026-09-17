# Jungle Hijinxs Nano Banana study

September 16, 2026. Private, optional art experiment; no release promotion or commits.

The user selected Nano Banana alone after the single-sheet model comparison.
This rebuild uses the existing Jungle Hijinxs source inventory and exact runtime
material keys. It includes DK, Diddy, enemy and effect families, observed OAM
composites, foreground/background scenery, gap captures, and the normal banana
counter. No Precision or other AI upscale pass is applied. The host executable,
scene guard, guest code, cartridge state, and widescreen policies are unchanged.

## Generation and registration

Evidence root: `build/hd-slice/nano-level-20260916/`.
`jobs.json` preserves all prompts, input hashes, reference and creation IDs,
provider responses, terminal status, and local paths. Untouched provider images
are in each stage's `provider-results/` directory. Original source sheets and
frames were copied from the preceding private experiment and hash-checked.

There were 43 source sheets: 11 DK, 10 family, 3 observed composite, 1 supplement,
17 background, and 1 gap. The model is Nano Banana Pro
(`imagen-nano-banana-2`), 4K square, seed 16092026. Each submission was quoted at
150 credits; 43 requests were quoted at 6,450 total. Forty-one completed images
represent 6,150 quoted credits. An account-balance change is not a reliable net
charge calculation because other account activity occurred during the run.

Two foliage requests, background plates 03 and 14, failed the provider's content
filter. They were not resubmitted. Their 512 centers were assembled from existing
successful Nano images using exact byte-equal source RGBA tiles: 1,748 regions
of 16x16 and 1,200 regions of 8x8. Every target pixel is covered, with no overlaps
or unmatched tiles. `background-reuse-plan.json` and
`background-reuse-provenance.json` retain each source region. These two sheets
are labeled derived results, not successful provider outputs. Exact source
matches preserve tile identity but do not guarantee seamless generated detail.

The 4096-square provider images are resampled to the existing 3072-square
registration canvas, then cut into 4x runtime assets. Source alpha and anchors
are restored through the maintained packers. Original colors are blended into
the outer two native pixels of background chunks by the existing boundary
feather; this is not a second AI pass. HUD digits and banana phases are composed
from the new Nano primitives into all 800 normal count/phase combinations.

`assemble_level.py` is a private, reproducible assembly recipe. `complete/`
contains the registered viewer, coverage report, per-key provenance, and pack.
Raw generation completion and source-alpha registration do not constitute
frame-by-frame animation acceptance.

## Validation and native review

The initial pack passed 60 maintained verifier replay legs (fresh entry,
directions, run, barrel-right, cache pressure; native/wide, HD off/on, three
repeats) plus 12 banana-pickup legs. All seven guest hashes are unchanged and
31,485 eligible GPU frames match the CPU compositor. All 16,434 materials are
resident before play, with 1,229,835,904 pixel bytes and no gameplay texture
reads. Coverage and dimensions match the earlier pack. `git diff --check` passes.

Visible native QA confirmed the Nano scenery, DK, enemy and counter at three
bananas, and F10 original/HD comparison. The user then took control and played;
that session was left running without further controller input, reset or quit.
The clean separate launcher is `build/macos/DKC1 Jungle Nano.app`, with test
inputs/logging removed; its strict deep signature check passes. The live QA app
is `DKC1 Jungle Nano Banana.app`. The clean launcher was not opened over the
user's active play session.

Initial pack SHA-256: `d7c1d91e782e161c7dee0af4a04eceb7e090254ccd23843e20a584c88d583d60`.
Clean signed executable: `b35cb3143b15a0a2ff37b33e603c4e6aa5a24731f8168f5f8a4c2b67bc675773`.
ROM, root state and source executable identities are in `source-identities.json`.

The user's subsequent screenshots demonstrate visible replacement gaps, blocky
background alpha and patch seams. The current pack is not accepted as complete
full-level art coverage. Inventory parity did not close those defects. See
`docs/HD_NANO_POSTPROCESS_PLAN.md` for the revised coverage-first plan and a
separate contour/overlap prototype; that prototype has not been installed.

The existing 17 HD sprite pack/alignment tests passed. Sampled pose sheets were
reviewed across all 25 sprite sheets. The model produces smoother CGI surfaces,
but some hands, faces, balloon prints, and tiny effects are reinterpretations.
Derived background tiles can retain visible detail changes at their boundaries.

## Scope

This candidate has the same source-key coverage as the previous full main-level
experiment. Inventory families contain some poses not encountered on the test
routes. Unseen background/OAM combinations can still use original fallback.
The existing runtime guard supports Jungle Hijinxs entrance 0016 at widths up
to 342. Bonus rooms and other levels retain stock output. This task does not
claim complete-game coverage, all-animation acceptance, or fix those scene guards.

The candidate app uses its own bundle identifier, save directory, immutable
entry state, eager material preload, and the existing Metal compositor. The
prior comparison gallery, previous app, and user save slots remain preserved.

## Floating balloon follow-up

The blank life-balloon candidates have a focused replacement that restores DK's
sculpted facial features. See [HD_BALLOON_FACE_REVIEW.md](HD_BALLOON_FACE_REVIEW.md)
for exact registration, bounded runtime validation and the corrected preview.
