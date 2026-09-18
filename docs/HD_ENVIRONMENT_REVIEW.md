# Nano Banana environment pilot — September 7, 2026

The user selected the sharper Nano Banana 2 foreground shown in creation
`SyRFjsJUb8`. The opening terrain uses that exact full-resolution output;
adjoining terrain and distant palms use new generations referenced to it.
This is a private Jungle Hijinxs experiment, not a full-level or full-game
replacement pack.

## Result and scope

- Candidate: `build/hd-slice/scene-pack-nano-environment-v7`.
- 1,410 materials: 240 existing background materials replaced and 91 added.
- All 208 reviewed DK poses / 416 facing materials remain byte-identical to
  `scene-pack-nano-bounce-idle`. Other object materials are unchanged.
- Coverage targets the first 896 source columns and repeating distant palms
  at entrance `$0016`. The existing scene guard is unchanged.
- Some lower-edge contexts and later terrain still use older restoration art
  or enlarged original pixels. Small foliage cutout artifacts remain visible.
  This does not close the outstanding animation or environment coverage work.
- No renderer, streaming, collision, game logic, reference source, or engine
  changes were made for this environment pass. The existing bounded material
  cache repair remains in the executable.

All private source plates, generated art, states, and evidence are under
`build/hd-slice/environment-nano-20260907/` (called `E` below). Keep them out
of commits. Existing unrelated dirty work is preserved; nothing was committed
or published.

## Image provenance and selection

The selected opening image is `E/controlled-nb2.jpg`, a 4800×3584 download,
decoded without further JPEG compression to `jungle-layer-0-selected.png`.
`E/user-selected-look.jpg` preserves the user's screenshot. The generated
panorama and background reference the selected image for consistent detail.

| Creation | Model / purpose | Disposition |
| --- | --- | --- |
| `tC5WNipmZJ` | Nano Banana 2, raw-pixel foreground | Rejected: retained blockiness |
| `5j7I0pRKxe` | Nano Banana 2, raw-pixel background | Rejected: retained blockiness |
| `lJiuhGIgv9` | Pro, smoothly restored foreground reference | Earlier comparison candidate |
| `gO3QvcISXO` | Pro, smoothly restored background reference | Earlier comparison candidate |
| `SyRFjsJUb8` | Nano Banana 2, same foreground input/prompt as Pro | User-selected opening artwork |
| `u5seuzDQLD` | Nano Banana 2, connected foreground extension | Superseded by selected style |
| `BhFzk5doQR` | Nano Banana 2, background with selected style reference | Accepted intact periodic band |
| `UPZqWXSwny` | Nano Banana 2, connected extension with selected style reference | Accepted adjoining terrain |

Each job was cost-simulated at 150 existing Magnific credits, submitted,
shown, awaited to completion, and registered for download: **1,200 credits
total**, including rejected and comparison candidates. Prompts, input IDs,
settings, and output records are in `generation-requests.json`,
`pro-generation-requests.json`, `controlled-model-test.json`,
`panorama-generation-request.json`, `selected-look-requests.json`, and
`selected-look-generations.json` under `E`.

Magnific's Nano Banana 2 mode is `imagen-nano-banana-2-flash`; its Pro mode is
`imagen-nano-banana-2`. These provider names are recorded to avoid confusing
the models. The controlled pair favored Nano Banana 2's bark and leaf detail
for this input; it is one stochastic comparison, not a general benchmark.

## Geometry, context matching, and corrections

The opening foreground and background came from exact exported 512×256
source atlases. References were unwrapped with documented circular shifts
and padding before generation, then normalized back to their 4× source grid.
The previously accepted restoration supplies alpha, preserving silhouettes
and placement instead of treating the generator's gray background as alpha.

The extended plate joins observed source strips from entry and barrel-route
frames 100, 170, and 260. This provides an 896-column core with right context
observed to column 960. Some offscreen physical rows differ between exports
because the VRAM ring continues streaming. Consequently, the stitched plate
is not claimed to be a globally canonical atlas: replacements are registered
only through complete, exact 48×48 BGRA context hashes. There are 214 unique
panorama contexts. No approximate tile or partial-context matching is used.
The exact user-selected opening keys take precedence when an extension
contains the same context. Unsupported contexts retain the prior behavior.

Three presentation-art defects were corrected during review:

1. A thin line at physical row 0/256 came from nonperiodic pre-restoration.
   Circular restoration plus an eight-native-row blend at the periodic join
   removed it. No game coordinates or source keys changed.
2. Generated gray matte intruded into opaque foliage. Pixels close to the
   known gray reference background blend back to the source restoration.
   This is restricted to matte-colored areas; the selected detail remains.
3. The two-reference background generation copied foreground imagery into
   its lower repeated padding. v7 uses the intact complete 1024-pixel period
   at normalized crop `(256,256)-(2304,1280)`, restores its original row
   phase, and blends the repeat boundary. The defective padding is excluded.

The rejected raw-pixel candidate did not gain enough detail. Repeated
independent tiny-tile generation was avoided because connected foliage and
ground need consistent joins. Wider renderer or approximate-key changes
were unnecessary and would expand the scope beyond this art experiment.

## Identities

| Item | SHA-256 |
| --- | --- |
| Supported USA ROM | `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15` |
| Immutable entry state | `7aeaa4e44e714db99af7cbeb8e79e68ee0c072bf9b4e84ba35f5c927e3cc05da` |
| Earlier exact tester state | `fddce5a8ea8f0c49485d03201ee2c60b70d4c2958236f9dd509e457971af1c91` |
| Headless executable | `eda806fed104bc00064e7a9c43b77f5eed701b7dc83e447b6b4fa027e48d4ea2` |
| Final v7 material pack | `cd998b420b585b31406b68ee582a386b27f5855554a5539a0b01fcbb9af518a9` |
| Signed native executable | `090716bd8aa6bcea15b75ffd993c912588db5ed83f8b8f5575109a7560c060a2` |

Pack hashes concatenate sorted `.dkhd` filenames and their contents.
`E/candidate-v7.json` records every changed/added key and its panorama mapping.
`E/root-v7.png` and `E/late-v7.png` show actual headless renders, not mockups.

## Validation and live preview

**78 final-candidate replay legs passed:** 72 from the maintained verifier
plus six from the preserved exact tester state. The 72 cover fresh entry,
both directions, running, cache pressure/save reload, barrel-right, and
barrel-left, each in native 256×224 and wide 342×224, HD off/on, with three
independent repeats. Seven guest hashes agree within every corresponding
case and across the previous v5 and selected v7 packs; HD outputs are
deterministic. This does not establish 398×224 or other-level coverage.

Evidence: `E/verification-v7/results.json`, `E/exact-v7/results.json`,
their per-leg raw memories/logs, and `E/motion-v7/` (25 sampled frames of
the 500-frame barrel route). The exact-state branch uses the earlier
immutable tester state; the fresh case enters through the controller route
instead of loading a post-entry snapshot.

Reconstruction fallback counts are unchanged from v5. They are not all zero:
per enabled replay, fresh has 184 pixels; cache pressure has 799 native / 720
wide; barrel-right has 5 native / 90 wide; barrel-left has 22. Directions and
run have zero. All 966 post-reload cache-pressure frames have zero mismatch
and the cache reports no failures. These audit fallbacks are distinct from
missing art keys, which account for the visibly older terrain patches.

The installed bundle's 1,410 materials were compared byte-for-byte to v7,
then ad-hoc signed; strict deep signature verification passed. No code
rebuild was needed for this asset-only change. The native 500-frame input
route ran right, jumped on the enemy, picked up/carried the barrel, returned
left, and threw it. Native snapshots show rightward rolling, the left-facing
throw, and the paused end. The trace records 90 fallback pixels at audit
frames 418–419, matching the headless wide route, and zero cache failures.
The sample boards also show the remaining older-material patches as the
camera reaches and returns from the edge of this pilot's coverage.

The preview was already closed before v7 installation. Its earlier private
quicksave was left untouched in `E/live-final/user`; there was no active play
position to replace. The new process is PID **50061** at this review, using
`E/live-v7/user`. Its route-end state was preserved as
`E/live-v7/barrel-end.state` (SHA-256
`adeced6c8e5bd921262d689ee13fca92cfb146d343d8c1e34ddff0a3c52fff69`).
After QA it was restored to the immutable opening root, **paused with input
schedules cleared and HD enabled**. `E/live-v7/final.json` records the
identity; recheck the process before later actions.

Native captures are `E/live-v7/original-window.png`,
`barrel-end-window.png`, and `selected-root-window.png`. F7 resumes; F10
switches HD/original. A paused F10 toggle currently needs F8 (one frame) or
resume before the image refreshes. The original comparison capture was
therefore stepped once, not claimed to be the identical paused frame.

`git diff --check` passes. Shared widescreen promotion gates were not rerun:
this pass changes private presentation art only, with the existing scene
guard and cartridge behavior unchanged. Open coverage is tracked as
`hd-environment-nano-coverage` in `docs/KNOWN_ISSUES.json`.

## Reproduction

Run from `/Users/briantate/Documents/GitHub/DKC1Recomp-HD`. Asset assembly
requires the retained private inputs; it does not make new provider requests.

```sh
python3 build/hd-slice/environment-nano-20260907/assemble_candidate.py \
  --variant selected-root-v2 --image-suffix selected --baseline-alpha \
  --clean-matte --safe-background-band --credits 1200
python3 build/hd-slice/environment-nano-20260907/build_panorama_candidate.py \
  --periodic-seam --clean-matte --selected-look --safe-background-band
python3 tools/verify_hd_scene.py '/Users/briantate/Downloads/Donkey Kong Country (USA).sfc' \
  build/hd-slice/environment-nano-20260907/verification-v7 \
  --pack build/hd-slice/scene-pack-nano-environment-v7 \
  --cases fresh directions run cache-pressure barrel-right barrel-left --jobs 3
git diff --check
```

Rollback art is preserved in `E/installed-materials-before`. With the preview
closed, restore that folder as its `Contents/Resources/HDScene/Materials`,
then ad-hoc sign and verify the bundle. Preserve any active play session
before restarting. No ROM or save-state rewriting is part of rollback.
