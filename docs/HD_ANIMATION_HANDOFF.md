# DKC1 HD animation experiment — handoff

> Latest environment continuation (September 7, 2026): [HD_ENVIRONMENT_REVIEW.md](HD_ENVIRONMENT_REVIEW.md). The user-selected sharper Nano Banana 2 look is installed as `scene-pack-nano-environment-v7`: 1,410 materials, including 240 updated and 91 added backgrounds; all 208 DK poses / 416 facing materials are unchanged. 78 final-candidate replay legs pass, and the native right/left barrel route was inspected. Coverage is still a partial Jungle Hijinxs opening-area pilot. The prior preview was already closed before installation. The new preview (PID 50061 at review) is **paused at the immutable opening root with inputs cleared and HD enabled**; **F7** resumes. `build/hd-slice/environment-nano-20260907/live-v7/final.json` records its identity. Older pack and live-process notes below are historical; recheck current app state before acting.

> Latest renderer repair: [HD_MATERIAL_CACHE_REVIEW.md](HD_MATERIAL_CACHE_REVIEW.md). The reported left-walking fallback was a full 4,096-entry material cache refusing installed poses. Bounded reuse fixes the reproduced failure. 120 regression legs and three exact-state warm repeats pass; four walking poses were checked in the native window after filling the cache. The same 208-pose / 416-facing pack is installed. The updated preview is **paused at the preserved tester position** (PID 7863), with inputs cleared; **F7** resumes. Earlier process and running-state notes below are historical. Full-game art coverage remains open.


> Latest bounce/idle repair: see [HD_BOUNCE_IDLE_REVIEW.md](HD_BOUNCE_IDLE_REVIEW.md). The installed private pack is `scene-pack-nano-bounce-idle`: **208 reviewed DK poses / 416 facing materials**. All 16 enemy-bounce and 24 chest-beating poses are covered in both directions; the left-facing chest/idle fallback is repaired. 60 deterministic replay legs and two 6,000-frame idle probes pass. Earlier pack/process identities below are historical; broader art and full-game coverage remain open. The user resumed the installed preview after its clean-root launch; it is running. **F7** pauses/resumes.

Updated: September 6, 2026, 23:35 UTC. This is a local experiment, not a release.

## Latest continuation — September 6, 2026, 23:35 UTC

The **[animation review](HD_ANIMATION_REVIEW.md)** supersedes the runtime pack,
validation, live-state and quality status in the earlier handoff below.

- Fixed opposite-facing material selection in the private pack pipeline: both
  directions now use the same selected art. No renderer or guest code changed.
- Current pack: `build/hd-slice/scene-pack-nano-reviewed`, installed into the same
  **DKC1 HD Nano Preview.app**. It contains 110 selected source poses / 220 facing
  materials: the prior 83 plus Hurt (18) and static-reviewed LookUp (9).
- **108 deterministic replays pass**. All 18 hurt poses occur in both directions.
  Natural routes reached 97 selected source poses, 79 in both directions.
- Every one of the 700 candidates was inspected on the 53 comparison boards;
  this is a contact-sheet review, not a full-game animation/landmark acceptance.
  The new review lists specific rejected poses and remaining problems.
- The newly preserved native-looking live state has entrance `$005F`, outside
  the unchanged `$0016` scene guard. Its original output is expected under that
  guard; no fresh `$005F` route or general renderer expansion was validated.
- The baseline `scene-pack-nano-motion` and tester state are preserved. The older
  `build_motion_pack.py` is canonical-only; use the maintained `registered`
  command documented in the review to avoid repeating the facing-key defect.
- One built-in image-generation attempt for Turn2 was rejected by the image service; no new asset was returned and its pose defect remains open. No external paid provider jobs, commits, publication or changes to `snesrecomp` were made.
- The final preview is **paused at the immutable entry root**, one neutral frame later, with no input schedule. PID 80174 is a historical identifier; `live-final-root/launch.json` has its identity. Press **F7** to resume. The old process and running-state notes below are historical.

The sections below retain the previous generation-pass record and historical
48-replay identities. Consult the new review for current identities and scope.

## Earlier generation-pass result

All **700 distinct Donkey Kong source frames**, organized into **53 named groups**, have HD candidates. They are exported as **65 transparent sheets** with matching source rectangles. This completes a generation pass; it does not establish production quality for every animation.

The separate playable Jungle Hijinxs preview contains **83 Nano Banana 2 motion frames**: Idle 21, Walk 20, Run 20, Jump 11, Land 9, and Turn 2. The final pack passed **48 deterministic replays**. Other scenery and objects retain the earlier restoration pack or enlarged original pixels when no replacement is available.

**332 of the 700 candidates have silhouette overlap below 0.78.** Candidates above that threshold can still have incorrect faces, hands, lighting, cropping, or animation continuity. Even the playable set is experimental; one Turn frame is below this review threshold. No complete internal-landmark acceptance pass has been performed.

The user's latest direction was to process all DK animations, then launch the game with replacements enabled for hands-on testing. The preview was brought forward, restored to its clean starting point, and left **running with visible HD replacements**. Do not pause or reset their current play session merely to read this handoff.

## Workspace and ownership

| Item | Location / identity |
| --- | --- |
| Experiment checkout | `/Users/briantate/Documents/GitHub/DKC1Recomp-HD` |
| Branch | `experiment/dk-hd-slice` |
| Base HEAD | `ee6d662b75021acfdd0592324aab0acf01344f57` |
| Original checkout | `/Users/briantate/Documents/GitHub/DKC1Recomp` |
| Inherited-change record | `build/hd-slice/inherited-worktree.patch` and `inherited-status.txt` in the experiment checkout |
| Detailed chronological record | [HD_SPRITE_EXPERIMENT.md](/Users/briantate/Documents/GitHub/DKC1Recomp-HD/docs/HD_SPRITE_EXPERIMENT.md) |

The fork contains substantial inherited dirty work, including an independently copied dirty engine checkout. Do not attribute every modified file to this experiment, reset the worktree, stage everything, or modify `snesrecomp` as routine cleanup. No commits or publication have been made. Keep ROMs, extracted sprites, generated artwork, states, credentials, and generated game sources out of commits.

Read the checkout's `AGENTS.md`. Before runtime debugging, read `.claude/skills/dkc1-tools/SKILL.md` and its full `TOOLS.md` catalog. Before changing shared widescreen code, apply the repository's source, exact-state, fresh-entry, determinism, native-center, transition, and cross-layout requirements. Use one live automation owner.

All paths below are relative to the experiment checkout unless an absolute path is shown. The private art root is:

```text
build/hd-slice/reimagined/all-animations/
```

## Open the correct app

Use [DKC1 HD Nano Preview.app](</Users/briantate/Documents/GitHub/DKC1Recomp-HD/build/macos/DKC1 HD Nano Preview.app>), bundle ID `com.flat2vr.dkc1recomp.hd.nano-preview`. The older `DKC1Recomp-HD.app` is the baseline experiment and is not the latest Nano motion preview.

```sh
open '/Users/briantate/Documents/GitHub/DKC1Recomp-HD/build/macos/DKC1 HD Nano Preview.app'
```

If an existing preview process is running, use it. A new direct bundle launch loads its bundled clean root and normally pauses after one frame. It asks for the supported USA ROM if needed; no ROM is bundled.

| Control | Action |
| --- | --- |
| F7 | Pause / resume |
| F8 | Advance one frame while paused |
| F10 | Original / HD comparison |
| F12 | Restore the private quicksave; this replaces the current gameplay state |
| Escape | Native pause menu, controls, and graphics settings |

Default save/config directory: `/Users/briantate/Library/Application Support/DKC1 HD Nano Preview`. The current test process instead uses `build/hd-slice/nano-motion-live/user`, because it was launched with `DKC1_USER_DIR`. Its quicksave was initialized from the immutable root. Preserve the root and any tester reproductions separately; the user may subsequently overwrite a quicksave.

At the handoff's process check, the preview was PID **63428** and the ordinary original-checkout game was PID **59571**. These are historical identifiers, not safe future action targets. Re-identify the executable and app before operating either process. The original-checkout game was left untouched.

### Important live presentation observation

During the latest launch request, the preview displayed original-looking graphics even when its title reported **HD sprite experiment ON**. Toggling F10 off/on alone did not visibly restore the replacements. Using the native pause menu's **Quick Load**, then **Resume Game**, restored the clean root and visibly showed HD DK and scenery again. No aspect-ratio setting was changed.

An initial hypothesis blamed display mode; that was **not established**. The app's menu identifies its 16:9 mode as **342×224**, which is within the HD renderer's supported width. Older project documentation contains other geometry conventions, so verify the current implementation instead of using labels alone. The cause of the live fallback remains unresolved. F10's title is not sufficient evidence that replacements are being rendered.

For further diagnosis, preserve an affected state and capture the actual window, renderer decisions and raw PPU/state evidence before reloading. Compare that exact state with a fresh entry. Do not broaden the scene guard or invent a state-repair pass based solely on this observation. The earlier `nano-motion-live/launch.json` says the app was left paused; the subsequent user-requested launch/resume supersedes that final-state description.

## User deliverables

| Artifact | Link |
| --- | --- |
| Animation viewer | [index.html](/Users/briantate/Downloads/DKC1-All-DK-Animations/index.html) |
| HD frames, sheets, originals, metadata and viewer; about 66 MiB | [DKC1-All-DK-HD-Animations.zip](/Users/briantate/Downloads/DKC1-All-DK-HD-Animations.zip) |
| Original native sprites and 4× nearest sheets | [DKC1-All-Original-DK-Animations.zip](/Users/briantate/Downloads/DKC1-All-Original-DK-Animations.zip) |
| Running comparison animation | [running-comparison.gif](/Users/briantate/Downloads/DKC1-All-DK-Animations/running-comparison.gif) |
| Full private workspace viewer, with local raw-result links | [index.html](/Users/briantate/Documents/GitHub/DKC1Recomp-HD/build/hd-slice/reimagined/all-animations/index.html) |

The portable viewer works from local images for its core animation comparison. Its raw 4K result links open the corresponding Magnific creation. It preserves source anchors, supports a frame slider and original overlay, and plays numeric pose order at an adjustable rate. It does **not** reproduce cartridge animation-script frame holds, loops, or transitions.

Both viewers passed JavaScript syntax and asset-reference checks. The browser URL policy blocked automated opening of local HTML, so browser visual QA was not completed. No alternate local server or browser workaround was used. The user can open the delivered file manually. Native game screenshots were inspected separately.

The ZIP contains no ROM, state, OAuth data or app binary. The unpacked Downloads folder also contains a link to the local preview app. Do not delete ignored `build/hd-slice` as disposable build output: it holds the only complete raw generation corpus and several non-portable experimental scripts.

## Source extraction and registration

`extract.py` builds `inventory.json`, `original-frames/`, and `sheets/` from the supported clean ROM. `reference/` was checked first; `reference/disassembly` is currently absent. The historical file below supplies asset ranges and names only:

```text
/Users/briantate/Documents/GitHub/DKC-Widescreen-358x224/rom/overlay/DKC1/AsarScripts/AssetPointersAndFiles.asm
```

The extractor decodes original ROM bytes, including DMA tile placement and OAM piece ordering, then preserves each tight rectangle, signed anchor, and original BGRA content key. It does not import patched-ROM behavior. Unknown/unused pose names from that index are navigation labels, not proven semantics.

All **58 previously captured live sprite rasters match the decoded originals byte for byte**. The remaining 642 originals have not each been independently captured in gameplay. All 700 extracted content keys are distinct.

`process.py` splits provider cutouts using recorded layouts, removes detached remnants and blue/neutral floor shadows, and uniformly fits a candidate into the original 4× rectangle. Placement uses the original bottom and horizontal center; runtime and viewer placement retain the original anchor. It does not warp individual limbs or faces.

Repeated-cell variants are stored separately by cell number. Candidate selection uses the largest silhouette IoU per source key, with a manual exclusion list for ten malformed/invented first-pass sheets. The previously accepted walking mapping is preserved. `registration.json` records the selected source, transform, score, and frame path. `tools/check_hd_sprite_alignment.py` has a stricter separate geometry/landmark gate; the 0.78 review threshold is not that gate.

Important naming trap: `more-animations/landing-layout.json` actually maps **Turn1 and Turn2**. `jump-layout.json` contains both Jump and Land frames in a nontrivial order. Match by immutable source keys, not filenames or presumed numeric order.

## Successful generation method

Use the user's selected appearance reference:

```text
build/hd-slice/reimagined/more-animations/appearance-reference.jpeg
```

This is the same image as `/Users/briantate/Downloads/magnific_retouch_CqjRBu0EEy.jpeg`. It is 4096×4096. The user's successful walking creation is `3zMQJ2LREY`; its transparent sheet is retained under `reimagined/user-walk-sheet/`.

Settings for the main batch:

- **Google Nano Banana 2**, provider model `imagen-nano-banana-2-flash`.
- **4K**, **Thinking High**, one output per submitted sheet.
- Automatic prompt rewriting **off**.
- `@img1`: original sprite sheet, enlarged 4× with nearest-neighbor on gray RGB 80.
- `@img2`: the chosen appearance reference.

Original prompt:

```text
@img1 Reimagine in modern 4k like @img2 this sprite sheet keeping the exact poses.
```

The first broad pass mostly used AUTO aspect ratio. It sometimes altered grids, completed cropped bodies, invented objects or reused the reference pose. Nineteen corrective sheets used explicit **4:5**, a fixed **4-column × 5-row** layout and intentional repeated source frames. Exact corrective prompt:

```text
@img1 Reimagine in modern 4k like @img2 this sprite sheet keeping the exact poses.

Output a 4-column by 5-row sprite sheet with exactly 20 sprites, one matching each source cell. Preserve each cell's camera angle, body orientation, hand and foot positions, silhouette, and spacing. The repeated frames are intentional: render every cell. Use @img2 only for Donkey Kong's appearance and materials, never its pose. Donkey Kong is a gorilla with no tail and no cap. Add no objects, weapons, food, or effects. Plain uniform gray background with no cast shadow, no floor, and no grid lines.
```

This improved grid consistency and removed some invented props. It did not reliably solve every complex pose or partial-body sprite. Per-sprite editing with the same appearance reference is the next useful direction; it was explicitly suggested by the user.

### Magnific access and batch accounting

The official Magnific MCP is configured. The private client is `build/hd-slice/reimagined/magnific_mcp_client.py`, using the normal OAuth flow and official MCP SDK. Run it with `build/hd-slice/mcp-venv/bin/python`. Its private OAuth directory is `build/hd-slice/magnific-client-auth`; never print, copy into a handoff, or publish its contents. Only one client connection should own the local callback port 65421 at a time.

The MCP did not expose Nano's Thinking High setting. Main generations therefore used the browser UI; the MCP handled history reconciliation, downloads, and background removal. A separate MCP generation pilot is exploratory evidence, not the main High-thinking result. No additional API or subscription is currently needed to inspect or process the completed assets. Recheck balance before another paid batch; no credits or subscription were purchased by the agent.

`generation-ledger.json` contains 60 rows. `correction-ledger.json` contains 21 rows: two recovered submissions plus 19 corrections. The two recovered results appear in both ledgers, so these represent **79 distinct new sheet results**, in addition to reused earlier results. All corresponding original downloads and provider cutouts are present. No generation or background-removal jobs remain pending from this batch.

Three initial UI attempts did not actually submit: FootStomped, Hurt, and LookUp. FootStomped and Hurt were resubmitted; LookUp was replaced by a fixed-grid correction. A new history row can belong to an earlier delayed job. Verify the exact source-reference filename in the creation detail before assigning a result or retrying a paid submission. Every correction was audited this way.

Operational details retained from the working client:

- `creations_search` accepts at most 50 results per page; filter timestamps locally because the observed date filter was ineffective.
- `creations_wait` and download registration accept up to eight IDs per call.
- Register downloads before retrieving the returned asset URL.
- `images_remove_background` returns a separate creation ID; wait for it and download the cutout.
- Main sheets cost 150 existing credits each; provider background removal cost 3 each in this run. Verify current pricing before new work.
- `collect_all.py` contains an obsolete 61-result expectation. `collect_later.py`, `collect_corrections.py`, and `reconcile_corrections.py` are tailored to this completed batch, not general-purpose schedulers. Do not blindly rerun them for a new batch.

## Evidence map

Under `build/hd-slice/reimagined/all-animations/`:

| File / directory | Purpose |
| --- | --- |
| `inventory.json`, `original-frames/`, `sheets/` | Exact source keys, anchors, native PNGs, source layouts and generation inputs |
| `generation-ledger.json`, `correction-ledger.json` | Source/result identity and method records |
| `raw-results/` | Downloaded originals, provider cutouts and private provider metadata |
| `generated/`, `cutouts/` | Named links to corresponding raw results |
| `variant-frames/` | Individually registered candidates before per-key selection |
| `registered/`, `registration.json` | All 700 selected candidates and transforms |
| `hd-sheets/` | 65 transparent sheets matching source rectangles |
| `review/`, `review-notes.json`, `quality-summary.json` | Original/HD contact boards, observed problems and geometry summaries |
| `build_gallery.py`, `index.html` | Interactive pose-order viewer |
| `finish_export.py`, `package.py`, `handoff.json` | Sheet export, portable archive and archive identity |
| `build_motion_pack.py` | Explicitly selects the 83 motion frames for the runtime pack |

Many scripts use absolute local paths. Preserve the current outputs before generalizing or rerunning them. `process.py` and packaging scripts overwrite selected/generated deliverables; save any manually curated selections first.

## Runtime architecture and rebuild

`runner/dkc1_hd_scene.c` reconstructs the supported scene from live BG/OAM data, including priority, per-line scroll/palettes and color math. Its low-resolution reconstruction is compared with the original PPU output. Mismatched pixels fall back to enlarged original pixels; unsupported scenes/register combinations fall back to the original frame. Unknown material keys also use original art. There is no gameplay, collision, animation-clock, or save-state rewrite to make art fit.

The current compositor is restricted to the supported Jungle entrance `$0016`, 224 lines, and widths up to 342. It presents a separate 4× buffer: 1368×896 at 342×224, or 1024×896 at native width. Metal retains the SNES pixel aspect. This is not all-level renderer coverage.

Material keys identify exact original rasters. Palette/lighting or composition variants can require different keys. `.dkhd` format is `DKHDv001`, little-endian uint16 width and height, then straight BGRA texels; loaders validate dimensions and exact payload length. Packs are cached for a process lifetime: **restart after changing a pack**.

Current tested pack: `build/hd-slice/scene-pack-nano-motion/`. It contains **1,062 `.dkhd` files**, including the 83 selected Nano motion entries and inherited material coverage. Older notes reporting 829 describe an earlier baseline. The app's embedded material directory matches the current pack exactly.

`runner/macos_file_picker.m` recognizes the Nano bundle ID, selects its private save directory, and enables `DKC1_HD_SCENE` and `DKC1_HD_SPRITES`. `runner/dkc1_hd_sprites.c` retains the earlier sprite-only fallback path. `runner/sdl_host.c` supplies presentation and F10 comparison. Do not change the guest simulation to compensate for generated pose drift.

Reprocess existing private art only when intended:

```sh
cd /Users/briantate/Documents/GitHub/DKC1Recomp-HD
build/hd-slice/upscale-venv/bin/python build/hd-slice/reimagined/all-animations/process.py
build/hd-slice/upscale-venv/bin/python build/hd-slice/reimagined/all-animations/finish_export.py
python3 build/hd-slice/reimagined/all-animations/build_gallery.py
build/hd-slice/upscale-venv/bin/python build/hd-slice/reimagined/all-animations/package.py
build/hd-slice/upscale-venv/bin/python build/hd-slice/reimagined/all-animations/build_motion_pack.py
```

The upscale venv has NumPy, Pillow, SciPy, Torch and Spandrel; the MCP venv is separate. Run from the checkout root because some imports are relative.

`./build_macos.sh` builds the baseline **DKC1Recomp-HD.app** and embeds `scene-pack`, not automatically the Nano motion pack. After an intentional code rebuild, update the separate Nano bundle while preserving its unique identifier, install `scene-pack-nano-motion` as `Contents/Resources/HDScene/Materials`, preserve its `entry.state`, re-sign and verify the bundle, and restart it. Do not overwrite a bundle while the user is testing. Avoid treating the baseline launcher as the latest Nano launcher.

## Validation and identities

The saved results were inspected again for this handoff, and the current binaries, root, reference image and both material directories were rehashed. Tests were not rerun for this documentation-only task.

| Evidence | Result / scope |
| --- | --- |
| `build/hd-slice/nano-motion-validation/results.json` | 36 passing replays: native/wide × fresh/idle/walk × HD off/on × three repeats |
| `build/hd-slice/nano-running-validation/results.json` | 12 passing replays: native/wide × running/jumping × HD off/on × three repeats |
| Guest equality | Original frame, WRAM, VRAM, CGRAM, PPU OAM, OAM shadow and audio hashes match paired baselines/repeats |
| HD equality | Repeated HD output is deterministic |
| Reconstruction fallback | Zero on exact-root idle/walk/running routes; 184 original-pixel fallbacks per fresh-entry run |
| Actual running material keys | Run4–Run19, Jump1–Jump11, Land1–Land6, plus idle/walk poses; not proof every installed frame was exercised |
| Visible game QA | F10 original/HD comparison, plus the later user-requested live launch and restored-root HD view |
| Static delivery checks | 700 candidate paths, 65 HD sheets, viewer syntax/references, ZIP integrity and no ROM/state in archive |

Running input schedule: 30 neutral frames, 60 Right+Y, 25 Right+Y+B, 35 Right+Y, 30 neutral. Input masks are `82` for Right+Y and `83` for Right+Y+B. Its file is `build/hd-slice/run-jump.inputs`.

Reproduction commands, using new output directories to preserve the accepted evidence:

```sh
cd /Users/briantate/Documents/GitHub/DKC1Recomp-HD
python3 tools/verify_hd_scene.py \
  '/Users/briantate/Downloads/Donkey Kong Country (USA).sfc' \
  build/hd-slice/nano-motion-validation-next \
  --pack build/hd-slice/scene-pack-nano-motion --jobs 3
python3 build/hd-slice/verify_running.py \
  '/Users/briantate/Downloads/Donkey Kong Country (USA).sfc' \
  build/hd-slice/nano-running-validation-next \
  --pack build/hd-slice/scene-pack-nano-motion --jobs 3
git diff --check
```

These tests do not establish every animation, every level, unrestricted exploration, transitions, or full-game material coverage. Broad renderer changes still require the repository's additional promotion gates.

SHA-256 identities:

```text
Supported USA ROM:
fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15

Immutable build/hd-slice/entry.state:
7aeaa4e44e714db99af7cbeb8e79e68ee0c072bf9b4e84ba35f5c927e3cc05da

Nano preview executable, after resource re-signing:
1626ffc8e5db0da4b2dc3c72e030da2ae99e07828259a70cb5f2f0bd0c3056d7

Headless executable used for the 48 replays:
63ba41a4b2334190cde1f791df86b3b55c4aad5908687ddf007b8301e5b4b22f

Nano motion pack and app-embedded pack:
ede217aa0c82b72e6b842e0ed3455c96920d763c4c72c74079b4ff5a4a82bcef

Appearance reference:
9503d61c5a041058d8d41311d7531b0eec80e0a2d6469030c60c88368d95ebf5

DKC1-All-DK-HD-Animations.zip:
e63e5d47aec823849cbcd7ce89d9bdca5be24811871a5d6cbd3f5f84fb8354a4
```

Pack hashing sorts `.dkhd` filenames, then hashes each filename followed by its file bytes. The immutable root is Jungle entrance `$0016`, mode `$0000`, camera `(85,177)`, captured at boot frame 8,000. `recipes/hd-jungle-entry.dks` provides controller-only fresh entry, completing its route at boot frame 8,532.

Visible evidence is in `build/hd-slice/nano-motion-live/`: `native-running.png`, `hd-running.png`, `initial-window.png`, `restored-root.png`, audit/trace files and `launch.json`. The app process continued into user testing afterward, so logs and quicksaves there may evolve; do not treat them as immutable final-state evidence. Earlier walking movies and comparisons remain under `reimagined/user-walk-sheet/runtime/`.

## Previous approaches and what they established

- **Seedream 4.5:** some individual frames had a promising old prerendered look. Sheet trials drifted in layout/pose or returned pixel/voxel treatment. The full delivered batch is Nano Banana 2, not Seedream.
- **Nano Banana 2:** the user's walking sheet established the useful reference-plus-original method. It is strongest on common motion poses and weaker on carrying, partial bodies and unusual rotations.
- **Local preprocessing:** Real-ESRGAN x2plus and SGI produced useful cleaner inputs; SwinIR-M x2 emphasized palette noise in the tiny-sprite comparison. These are preprocessing results, not proof of reimagined-art quality.
- **Topaz:** Photo AI CLI reported invalid authentication; Gigapixel CLI reported an enterprise-license requirement. No successful Topaz comparison was produced. These were access limitations, not image-quality findings.
- **Magnific Creative / Precision:** earlier probes are preserved under `reimagined/magnific-upscale/`; they did not replace the chosen animated Nano result.
- **Dense optical flow / thin-plate-spline warping:** rejected because better outline scores came with distorted or clipped faces/features. Do not optimize the metric by damaging the sprite.

The chronological experiment document contains exact earlier artifacts and observations. Recheck authentication, API settings, prices and model availability before relying on historical provider details.

## Recommended continuation

1. **Collect the user's current playtest findings.** Preserve any reproduction state, exact inputs, animation and window capture. Reproduce the HD-ON/native-looking presentation issue with existing diagnostics before assuming its cause.
2. **Review promising uninstalled groups.** Hurt (18 frames, mean IoU about 0.891) and LookUp (9, about 0.918) are useful next candidates. Swimming (15, about 0.877) looks promising, but underwater renderer coverage is not implemented or validated by this Jungle slice.
3. **Repair difficult poses individually or in small coherent batches.** Keep the shared appearance reference, exact source direction/contact points and original anchors. Explicitly preserve partial-body cropping and missing/occluded parts; do not let a full-body reference invent legs below minecart torsos. Review carrying hands, rope grips, flips, face motion and consistent lighting.
4. **Keep the working 83-frame pack as an A/B baseline.** Do not install all 700 drafts merely because a PNG exists. Compare variants visually and temporally; add internal landmarks for eyes, muzzle, hands and feet. The remaining 368 frames above the review threshold are not automatically accepted.
5. **Promote artwork separately from renderer expansion.** Add only reviewed keys, rerun matching deterministic routes, and inspect the actual running app. Expand scenes/default behavior only after applicable fresh-entry and cross-layout gates. Missing keys and unsupported scenes must continue to use stock output.
6. **Make the pipeline reusable after quality improves.** Parameterize hardcoded paths and batch ledgers, preserve provenance and manual selections, and move suitable scripts into maintained tools without committing private assets. Keep changes uncommitted until the user approves publishing or committing.

The user wants continued practical progress toward original-looking, higher-resolution art with matching poses—not another blanket upscale pass or a claim that all 700 drafts are finished assets.
