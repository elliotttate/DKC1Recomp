# HD barrel and roll animation continuation

September 6–7, 2026. Private, uncommitted Jungle Hijinxs experiment.

## Result and limits

`build/hd-slice/scene-pack-nano-barrel-roll` adds **58 source poses / 116 facing
materials** to the previous 110-pose selection. The complete pack has **168 DK
poses / 336 facing materials**, with 1,263 total material files including the
previous scenery/object art. The previous 220 DK materials are byte-identical.
No renderer, guest logic, scene guard, generated code or engine changes were
needed. This is another visual iteration, not full-game animation acceptance.

| Added group | Packed poses | Naturally observed in each direction |
| --- | ---: | ---: |
| Roll | 14 | 11 |
| Pickup | 7 | 7 |
| HoldIdle | 3 | 3 |
| HoldWalk | 13 | 10 |
| ThrowPose | 19 | 19 |
| Unknown4Pose (carrying turn) | 2 | 1 |

The two controller routes encounter **51 distinct added poses in both
directions**. Roll 2/12/13, HoldWalk 1/2/3 and Unknown4Pose2 have source-sheet
review only. Unknown4Pose1 was observed during carrying turns (`$4C` animation,
source and mirrored); the historical group name is not a general source-symbol
claim. Reference disassembly is absent in this checkout, and atlas name lookup
returned no matching Donkey symbol.

The earlier 700-pose / 53-group static audit remains in `HD_ANIMATION_REVIEW.md`.
Ropes, swimming, minecarts, animal buddies, scripted sequences, other characters
and other entrances still need runtime/art work. The unchanged entrance `$0016`
guard means this pack does not enable HD rendering throughout the game.
Turn2 remains weak. New Roll11 and ThrowPose9/10/12 still have proportion/limb
placement differences; they are provisional visual candidates, not a passed
internal-landmark geometry gate. Their silhouettes score approximately 0.757,
0.723, 0.663 and 0.768 respectively. Most added candidates exceed 0.82, but that
score alone is not an art-quality or barrel-contact proof.

## Symptom, cause and repair

The previous installed selection omitted rolling, pickup, overhead carrying,
throwing and carrying-turn materials. Controller input from the immutable root
first enters roll at relative frame **31**, right-facing pickup at **187**,
left-facing pickup at **361**, and carrying turns at **301/571** in the two
routes. Missing materials fall back to original enlarged sprites. Old drafts
also substituted standing/running poses, lowered an arm during overhead carrying,
changed facing, or invented a cap/reference figure.

The fix is a private **asset selection and material-coverage change**. Each
selected candidate is uniformly fitted to its original 4× rectangle, preserving
its original anchor; no separate body-part warping or guest-state repair was
used. `hd_sprite_pack.py registered` verifies the original BGRA content key and
writes an exact mirrored copy under the corresponding mirrored original key.
The complete original guest composite remains the comparison oracle.

The user explicitly authorized Magnific/Nano Banana after the built-in image
service returned no carrying image. Nano Banana 2, 4K/High, improved the first
rolling poses but repeatedly distorted carrying anatomy. Nano Banana Pro with
larger original references, explicit per-cell arm constraints and repeated
problem poses produced better carrying/pickup/throw candidates. Wrong cells
were excluded individually. Prior reviewed Roll8/13/14 were retained. Duplicate
cells have distinct filenames, so a rejected later duplicate cannot overwrite
a chosen earlier one.

27 generation jobs and 23 background removals used **4,119 existing credits**
(10,391 → 6,272); no credits were purchased. The private generation ledger records
provider identifiers, prompts, references and cutouts. Raster assets, ROMs,
states and credentials must remain outside commits.

## Reproduction and validation

Evidence root: `build/hd-slice/barrel-roll-20260906/`.

```sh
build/hd-slice/upscale-venv/bin/python \
  build/hd-slice/barrel-roll-20260906/build_candidate.py
python3 tools/verify_hd_scene.py "$DKC1_ROM" NEW_EVIDENCE_DIRECTORY \
  --pack build/hd-slice/scene-pack-nano-barrel-roll \
  --cases fresh barrel-right barrel-left barrel-jump --export-materials
```

- `recipes/hd-barrel-right.dks`, **500 frames**: jump past the first enemy,
  pick up right-facing, carry/turn both ways, throw left, complete recovery.
- `recipes/hd-barrel-left.dks`, **940 frames**: walk past the barrel without Y,
  turn/roll left, pick up left-facing, carry/turn both ways, throw right,
  complete recovery.
- **48 replay legs passed**: three independent repeats × HD off/on × native/wide
  for both barrel routes, the carrying-jump route and the existing fresh controller-entry case. Original
  framebuffer, WRAM, VRAM, CGRAM, PPU OAM, WRAM OAM shadow and audio hashes are
  identical where equality is expected; final HD outputs repeat identically.
- `recipes/hd-barrel-jump.dks`, **870 frames**, adds jumps while carrying left
  and right, followed by throw/recovery; all twelve extra replay legs pass.
  It reaches the same carrying source poses, so the seven static-only poses
  remain unproven in gameplay. `jump-coverage.json` also identifies the original
  16-pose enemy-bounce sequence as a remaining HD gap on the approach.
- **16 existing HD pack/alignment tests pass**; `git diff --check` passes.
- `coverage.json` records exact encountered original raster keys. The probe
  directories retain full WRAM, VRAM, OAM and actor transitions. Guest animations
  end in idle after throwing; the DK barrel breaks and Diddy emerges.
- `endpoints/` preserves 26 exact native/HD runtime image pairs, raw-frame hashes
  and guest hashes. The two `*-runtime.jpg` boards show pickup, carrying,
  turn, throw, recovery and rolling progression with cartridge timing.
- Visible app comparisons separately inspect both rolls, both pickups, overhead
  carrying and throwing. `live-*/window.png`, traces and launch records identify
  those processes and their exact relative frames. F10 comparisons at right roll
  and pickup held the machine state fixed.

Some earlier diagnostic PPMs and these probes' WRAM dumps were losslessly gzip
compressed after disk space was exhausted. `compressed-image-evidence.json` and
`compressed-memory-evidence.json` record their original hashes; each decompressed
stream was verified before removing the redundant uncompressed file. No tester
state or unrelated project data was deleted.

## Identity and preservation

| Artifact | SHA-256 |
| --- | --- |
| Supported ROM | `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15` |
| Immutable entry root | `7aeaa4e44e714db99af7cbeb8e79e68ee0c072bf9b4e84ba35f5c927e3cc05da` |
| Headless executable | `63ba41a4b2334190cde1f791df86b3b55c4aad5908687ddf007b8301e5b4b22f` |
| Candidate material pack | `6b2aafa852dbd11f5ef0c1933218287702e82c0bd7591e0e103c50a403e6637a` |

The prior `scene-pack-nano-reviewed` is preserved. Before live testing,
`tester-quicksave-before.state` preserved the previous quicksave and
`tester-current-paused.state` preserved current gameplay. The original-checkout
game was not operated. No commits or publication were made.

## Installed preview and final live state

The new pack is installed in `build/macos/DKC1 HD Nano Preview.app`; 116 new
material files and the facing manifest were added. The full installed material
directory matches the tested pack byte for byte. The app was re-signed and
`codesign --verify --deep --strict` passed. The final launch used the **bundled**
pack, with no external material override.

Final process: **91571**, paused at the immutable entry root plus one neutral
frame, with no controller schedule. Press **F7** to resume. Re-identify the
process before operating it later. Its executable SHA-256 is
`08712e644cbbbd828f90eab384dec5bec0e38746328a7c7c9aa692a78fa3e742`. `live-final-root/launch.json` records
the complete state/build/window identity. Both live throws were additionally
stepped 80 neutral frames through recovery, with Diddy visibly freed.

Earlier Downloads ZIPs and all-animation viewers retain the previous generation
corpus; the new selected registration, private pack and this report identify
this gameplay revision.
