# Enemy-bounce spin and idle animation repair

> The later left-walking report exposed a separate material-cache saturation defect, now repaired in [HD_MATERIAL_CACHE_REVIEW.md](HD_MATERIAL_CACHE_REVIEW.md). The art files and validation recorded here remain unchanged.


September 6–7, 2026. Private, uncommitted Jungle Hijinxs HD experiment.

## Result

`build/hd-slice/scene-pack-nano-bounce-idle` adds **16 Bounce and 24 BeatChest
source poses**, with both facing keys. The reviewed selection is now **208 DK
poses / 416 facing materials**. All previous 336 reviewed materials remain
byte-identical. The complete pack contains **1,319 material files**.

The former pack had no Bounce materials in either direction. It also retained
24 older right-facing BeatChest drafts outside its reviewed selection, while
all 24 left-facing BeatChest keys were absent. Thus the right-facing chest
sequence used stale art and the left-facing sequence reverted to enlarged
original pixels. Counting only `registered-directions.json` did not describe
every inherited material in the pack. This repair adds 56 missing files and
replaces 24 old drafts; it changes 80 material files in total.

This fixes those missing-animation and left-idle fallback cases in the
supported Jungle slice. It is not full-game animation acceptance. Other
characters, other entrances, swimming, ropes, animal buddies, minecarts and
scripted sequences still require work. The `$0016` scene guard is unchanged.

## Reproduction and cause

The immutable root is `build/hd-slice/entry.state`. Controller-only routes:

| Route | Frames | Required displayed material coverage |
| --- | ---: | --- |
| `recipes/hd-idle-cycle-right.dks` | 600 | 21 Idle + 24 BeatChest, right-facing |
| `recipes/hd-idle-cycle-left.dks` | 660 | 21 Idle + 24 BeatChest, left-facing |
| `recipes/hd-bounce-right.dks` | 340 | all 16 Bounce, right-facing |
| `recipes/hd-bounce-left.dks` | 340 | all 16 Bounce, left-facing |

Chest-beating first changes the WRAM pose at relative frame 135 on the right
route and 306 on the left; its first displayed raster is at **136/307**.
The enemy-bounce state begins at 148; the first complete displayed Bounce
raster is at **150**. Displayed OAM timing must not be confused with the
earlier WRAM pose transition. Both bounce routes kill the first enemy, finish
the spin, land and return to idle. The left route reverses direction before
contact, without any state edits.

Two 6,000-frame idle probes identify every encountered DK raster. Normal Idle
already has all 21 poses in both directions. The extra unmatched object rasters
are bananas and butterflies, not detached DK components. This is a material
coverage/art-selection defect; no compositor, OAM, animation clock, guest
logic, generated code, engine or widescreen changes were needed.

The user's current paused state and prior quicksave were preserved separately
before replaying the clean root. The current scene contained Diddy; it was
retained as tester evidence rather than used to manufacture a DK reproduction.

## Art selection

The user authorized Magnific/Nano Banana. Sixteen Nano Banana Pro 4K generation
jobs and sixteen provider background removals completed, costing **2,448
credits**. No credits were purchased by the agent. Account plan credits changed
independently during the run, so spending is calculated from the completed job
ledger rather than the net account balance.

Each reference sheet preserves four original poses at large nearest-neighbor
scale. Incorrect knuckle-standing substitutions, visible faces in back-facing
spin frames, incorrect arm positions and premature wide-open mouths were
rejected. Corrective sheets preserve the tucked rotation, bent elbows, mouth
opening/closing and return from chest-beating. Selection is explicit by cell;
duplicate cells cannot overwrite one another.

Candidates are uniformly fitted to their original 4× rectangles with the
original anchor. The maintained packer verifies original BGRA keys and writes
an exact mirrored candidate under the mirrored original key. No body-part
warping is used. The 40 chosen silhouettes have median IoU **0.901**, minimum
**0.808**; these are provisional art selections, not strict landmark acceptance.
Bounce3/5 and BeatChest9/12/16/21/22 retain proportion/hand differences needing
future refinement. Their complete motions and opposite-facing counterparts
were reviewed against actual gameplay captures.

## Validation

Evidence root: `build/hd-slice/bounce-idle-20260907/`.

```sh
build/hd-slice/upscale-venv/bin/python \
  build/hd-slice/bounce-idle-20260907/build_candidate.py
python3 tools/verify_hd_scene.py "$DKC1_ROM" NEW_EVIDENCE_DIRECTORY \
  --pack build/hd-slice/scene-pack-nano-bounce-idle \
  --cases fresh idle-cycle-right idle-cycle-left bounce-right bounce-left \
  --export-materials
```

- **60 deterministic replay legs pass**: native/wide × HD off/on × three
  repeats, across the four targeted routes and fresh controller entry. All
  seven guest/native-frame/memory/audio hashes match their paired baselines;
  HD output repeats identically.
- The four new cases require every expected pose to exist in the registered
  pack and be encountered by exact raster key in each aspect's first enabled
  repeat. Missing assets or skipped poses fail even if guest hashes agree.
  The previous pack fails this preflight. `verification/results.json` records
  45/45 idle-cycle and 16/16 spin coverage in each direction and aspect.
- Targeted routes have zero reconstruction mismatches. Fresh entry retains
  the previous 184 mismatched source-pixel samples per enabled replay, exactly
  matching the prior pack; this existing transient fallback is unchanged.
- Additional **6,000-frame candidate idle runs in both directions** have zero
  reconstruction mismatches, zero missing encountered DK materials and guest
  hashes identical to the corresponding old-pack probes. See
  `long-idle-results.json` and `coverage.json`.
- Sixteen existing HD pack/alignment tests pass; `git diff --check` passes.
- `sequences/` preserves **514 paired native/HD runtime frames**, losslessly
  encoded as PNG, with original PPM hashes. Four comparison GIFs retain the
  recorded sequence order; GIF timing is quantized around 60 fps.
- `*-pose-pairs.jpg` and `*-displayed-poses.json` identify all 40 poses in each
  direction from actual displayed raster keys. Crops are located against native
  opaque pixels; enemy-contact effects can occlude a small part of early spin
  poses. These matching scores locate crops, not approve HD geometry.
- Actual native-window captures show both spins, both chest-beating directions,
  right-spin landing, left-idle recovery and a fixed-frame F10 comparison.
  `live-*/launch.json`, traces, screenshots and saved states retain identity.

## Installation and identity

The tested payload is installed in `build/macos/DKC1 HD Nano Preview.app`.
Every `.dkhd` and current candidate metadata file matches the tested pack.
The bundle's historical `user-walk-manifest.json` is preserved separately.
The main pack manifest now distinguishes its historical base counts from the
current 1,319-material total and reviewed selection. Signing and strict deep
signature verification pass. No commits or publication were made.

| Artifact | SHA-256 |
| --- | --- |
| Supported ROM | `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15` |
| Immutable root | `7aeaa4e44e714db99af7cbeb8e79e68ee0c072bf9b4e84ba35f5c927e3cc05da` |
| Headless executable | `63ba41a4b2334190cde1f791df86b3b55c4aad5908687ddf007b8301e5b4b22f` |
| Material pack | `f797992a1106b871efbc54e00f936b5777903854f1e5297cbce6582ef341d876` |
| Signed preview executable | `9a173eec0556ee0b85bb376a0a6448264c339327ed8e69f021ad17985895e9dc` |

Final process **3922** uses the bundled pack without an external pack override.
It launched paused at the immutable root plus one neutral frame, with controller
schedules cleared and the immutable root in its private quicksave. The user
subsequently resumed and moved through the level while this record was being
written. The final observation is **running gameplay**, which was left in place.
Re-identify the process before future operations; the original-checkout game was
not operated. `live-final-root/launch.json` and `final-observation.json` record
the launch and later observation separately. F7 pauses/resumes.
