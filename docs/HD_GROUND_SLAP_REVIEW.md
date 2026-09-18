# HD ground slap and crouch transition review — September 7, 2026

This local art-pack correction covers the supported Jungle Hijinxs slice.
It adds all 30 GroundSlap and 22 Duck poses in both directions. No renderer,
guest logic, timing, scene guard or engine code changed in this continuation.
The newer idle selection is recorded in [HD_IDLE_CONTINUITY_REVIEW.md](HD_IDLE_CONTINUITY_REVIEW.md).

## Failure and correction

The environment-v7 pack had no GroundSlap materials. Its first missing pose
occurs at displayed frame 32 in the right route, or 138 in the safe left
route. The first slap candidate installed all 30 poses, but the user's
follow-up still reproduced a brief original sprite: the normal Duck poses
used around Down+Y were not installed. Checking only the named slap group
had missed this transition. All 22 Duck poses now have exact source and
mirrored raster keys.

GroundSlap 1–20 reuse previously reviewed candidates. Recovery 21–30 received
Nano Banana 2 corrections; an extra-arm frame 27 and misplaced recovery
alternatives were rejected. Duck 1–8 reuse reviewed art; 9–22 were replaced
because the old drafts substituted head scratching or idle for raised arms.
The selected sprites retain the original canvas/anchor and are mirrored by
`hd_sprite_pack.py registered`. Their internal landmark geometry is still
provisional: slap silhouette IoU ranges down to 0.829, with median 0.881.

## Reproduction and evidence

Private evidence root: `build/hd-slice/ground-slap-20260907` (E).

- `right-runtime-poses.jpg` and `left-runtime-poses.jpg` cover all 30 slap
  poses; the corresponding displayed-pose JSON files retain frame mappings.
- `native-qa/right-contact.png` and `left-contact.png` show the native window
  at both-palms contact; `right-duck.png` and `left-duck.png` show raised arms.
- `verification-final/results.json` passed 96 replay legs on the subsequent
  idle-continuity candidate: fresh entry, slap both ways, button transitions
  both ways, complete idle/chest cycles both ways, and actions.
- `exact-user-final/results.json` passed 12 legs from
  `tester-slam-flash-v1.state`: all 330 frames audited with zero reconstruction
  mismatch. An earlier supplied state at entrance `$005F` was outside the
  unchanged `$0016` guard and is historical evidence, not a passing HD test.
- The old 380-frame left route entered the cave. It is retained as
  `original-left-cave-route.dks`; the replacement moves right first and keeps
  all 436 frames in the supported scene. The verifier now rejects a route
  that leaves the eligible scene even if its guest outputs are deterministic.
- `rejected-v1.log` records rejection of the first slap-only pack for missing
  Duck coverage.

Each case compares native/wide × HD off/on × three independent repeats.
Native framebuffer, WRAM, VRAM, CGRAM, PPU OAM, OAM shadow and audio hashes
match the paired guest baseline; HD output repeats exactly. The fresh route
retains the earlier 184 reconstruction mismatch samples per run. This pass
does not claim to repair those or cover the whole game.

```sh
build/hd-slice/upscale-venv/bin/python tools/verify_hd_scene.py \
  '/Users/briantate/Downloads/Donkey Kong Country (USA).sfc' PRIVATE_OUTPUT \
  --pack PRIVATE_PACK --cases fresh ground-slap-right ground-slap-left \
  slap-transitions-right slap-transitions-left idle-cycle-right idle-cycle-left actions
```

Use `--state E/tester-slam-flash-v1.state --cases ground-slap-right` for the
exact tester branch. `tools/verify_hd_scene.py` additionally indexes the
complete 700-frame private original corpus and requires installed art for
every known DK raster encountered during slap routes, including rolling
when attack precedes Down. Inputs use spaces around `*` in `.dks` files.

## Identities and provenance

| Item | SHA-256 |
| --- | --- |
| Supported USA ROM | `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15` |
| Immutable entry.state | `7aeaa4e44e714db99af7cbeb8e79e68ee0c072bf9b4e84ba35f5c927e3cc05da` |
| Exact tester state | `d899ef2df3f809d51047adcb45843dc35b10d72c2e2a683b2042801c0934b415` |
| Headless executable | `eda806fed104bc00064e7a9c43b77f5eed701b7dc83e447b6b4fa027e48d4ea2` |
| Slap + Duck v2 pack | `ff4b94148926ff7b94f61eadf90c4a6ed97d76674bf96bf80a9b4eaa5eb8c479` |

The v2 pack has 1,514 materials, including 260 DK poses / 520 facing
materials. All 1,410 environment-v7 materials remain byte-identical; 60 slap
and 44 Duck keys were added. E contains `build_candidate.py`,
`selected-registration.json`, `candidate-identity.json`, source manifests,
prompts, provider results, cutouts, rejected alternatives and replay hashes.
The selected environment remains the user's sharper Nano Banana 2 v7 art.

Authorized provider spend for slap and Duck was 1,224 Magnific credits:
eight Nano Banana 2 jobs at 150 and eight background removals at 3. The later
idle work is accounted for separately. No purchases, commits or publication
were made; ROMs, art, extracted assets and private states remain outside
tracked deliverables. No old machine-state repair was applied.
