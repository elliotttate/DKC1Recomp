# DKC1 HD jungle vertical slice

The isolated Mac experiment restores the opening Jungle Hijinxs clearing at
four times the cartridge resolution. Donkey Kong, nearby Kritters, butterflies,
bananas, foreground foliage, ground, tree trunks, and the distant jungle use
restored materials. The original game still owns motion, animation, collision,
object lifetime, audio, and saves.

## Run and compare

Open `build/macos/DKC1Recomp-HD.app` and select the supported USA ROM, or run
`tools/run_hd_slice.sh [ROM]`. The app opens the preserved clearing, paused.
F7 resumes/pauses, F10 compares original and HD at the same state, F8 steps,
and F12 restores the private quick-save root. The native Controls menu shows
keyboard/controller bindings. The launcher uses `build/hd-slice/user`; opening
the bundle directly uses `Application Support/DKC1Recomp HD Experiment`.

The private pack contains 829 materials (602 background contexts and 227 object
rasters). The pack and starting snapshot are embedded in this local app. The ROM
is not included. The original checkout and its save slots are separate. No
commits or publishing have been performed.

## Scope and rendering

This is a local fork at `/Users/briantate/Documents/GitHub/DKC1Recomp-HD`, branch
`experiment/dk-hd-slice`, based on `ee6d662` plus the original checkout's dirty
September 6 snapshot. `build/hd-slice/inherited-worktree.patch` and
`inherited-status.txt` distinguish inherited work. The engine checkout is an
independent copy with inherited changes; the HD experiment adds no engine edits.

The normal PPU image remains byte-exact. `runner/dkc1_hd_scene.c` reconstructs
BG tilemaps and complete OAM components from the live cartridge graphics. It
reads the existing world-keyed shadow cache for verified widescreen tiles and
captures per-scanline scroll, palette, priority, and color math. It compares
its low-resolution result against every original visible pixel. Any mismatch
uses the original pixel, enlarged; unsupported scene/register combinations
use the original frame. No game state is rewritten to repair presentation.

The HD compositor uses a separate 1368×896 RGBA buffer in the 16:9 mode. Metal
receives this real 4× texture, retaining the SNES 7:6 pixel aspect on screen.
The corresponding native mode is 1024×896. Alpha edges and layer composition
are evaluated at HD resolution. The sky's HDMA gradient is interpolated in the
HD buffer. The original native-resolution sky and palette remain unchanged.

Material restoration uses the official Real-ESRGAN x4plus model locally through
Spandrel/PyTorch on Apple MPS. The general-image and illustrated-image models
were compared on the same DK pose; the general model retained more fur texture.
This is a neural restoration of the cartridge art, not recovered Rare source
renders. It smooths original dithering and cannot recover guaranteed original
fine detail. No cloud API or additional user credentials were needed.

Sources: [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN),
[official model zoo](https://github.com/xinntao/Real-ESRGAN/blob/master/docs/model_zoo.md),
[Spandrel](https://github.com/chaiNNer-org/spandrel).

Background atlases are restored with circular context, then cropped into 32×32
native / 128×128 HD materials. Keys include eight native pixels of neighboring
context. The first draft exposed a horizontal seam at the physical tilemap
wrap (visible row 78); circular padding removed it. Including neighbors in the
key prevents identical local chunks from selecting incompatible surrounding
restoration. Sprites retain their exact pose-specific raster identity and alpha.
The earlier illustrated model's smoother appearance and independent edge-padded
atlas restoration were rejected after visual comparison.

Only immutable completed frame data is read by the parallel compositor. Its
16 row bands write disjoint output regions, finish before Metal queues the
frame, and never execute another game frame or call the PPU from worker threads.
Unchanged atlas hashes avoid redoing material lookup work. SHA-256 identity is
identical across the portable and accelerated Mac implementations.

## Private art pipeline

1. Export an exact state or deterministic route with
   `DKC1_HD_SCENE_EXPORT=<existing directory>` using the headless runner.
2. Convert exported PAM atlases/object rasters to RGBA PNG. Preserve metadata.
3. Run `tools/upscale_hd_assets.py MODEL SOURCE OUTPUT` in the local upscale
   venv. Atlas names select circular padding; RGB and alpha are restored
   separately, with edge-color extension to prevent dark halos.
4. Run `tools/build_hd_scene_pack.py SOURCE RESTORED OUTPUT` to write the pack.
5. Run `./build_macos.sh` to build, bundle the private pack/root, and sign.

`.dkhd` files contain `DKHDv001`, little-endian uint16 width/height, and straight
BGRA texels. Loaders validate dimensions, exact payload length, and trailing
bytes. Missing entries stay original. Packs are immutable for a process
lifetime; restart after replacing them. Private ROMs, source rasters, models,
replacement art, captures, and snapshots remain under ignored `build/hd-slice`.
The local upscale venv contains Torch, Spandrel, NumPy, Pillow, and SciPy.

## Evidence and limits

- ROM SHA-256: `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.
- Immutable root: `build/hd-slice/entry.state`, captured at boot frame 8,000,
  SHA-256 `7aeaa4e44e714db99af7cbeb8e79e68ee0c072bf9b4e84ba35f5c927e3cc05da`.
  Entrance `$0016`, mode `$0000`, camera `(85,177)`, 16:9/glide.
- `recipes/hd-jungle-entry.dks` enters from boot using controller inputs and
  completes at frame 8,532. `build/hd-slice/walk.inputs` is a fixed 315-frame
  neutral/right/left/jump schedule, ending with three collected bananas.
- `tools/verify_hd_scene.py ROM OUTPUT`: 36 replays, covering fresh entry,
  120-frame idle, and 315-frame movement, HD off/on, native/16:9, three repeats.
  Original framebuffer, WRAM, VRAM, CGRAM, PPU OAM, OAM shadow, and audio hashes
  match each paired baseline and repeat. HD outputs are deterministic too.
  Evidence: `build/hd-slice/scene-validation-final/results.json`.
- Three additional independent fresh-entry replays of the final art pack are
  identical, with complete HD coverage at the settled clearing. Evidence:
  `build/hd-slice/fresh-complete-results.json`.
- Complete 16:9 art coverage and zero reconstruction fallback pixels across
  all 1,800 idle frames and all 315 movement frames. See
  `final-idle-trace.jsonl`, `video-trace.jsonl`, and `live-hd.jsonl`.
- The boot-to-scene audit uses conservative original-pixel fallback for 184
  pixels total during entry. The settled clearing reconstructs exactly. Native
  4:3 gameplay hashes also pass, but some native-view material contexts still
  fall back to original art. Full HD art coverage is asserted for the 16:9 slice.
- Actual Metal app observed through the movement route and paused F10 comparison.
  Median render time 4.93 ms; p95 5.99 ms; normal presentation interval 16.67 ms.
  One roughly 596 ms UI-inspection/setup stall occurred during automation;
  no internal audio underflows, one reported starvation. This is a short live
  measurement, not an extended performance certification.
- Python suite: 246 tests, 245 passed and one unavailable fixture skipped.
  App build, strict code-signature verification, and `git diff --check` pass.
- `scene-identity.json` records executable, ROM, root, model, pack, screenshot,
  movie, and final live-process identity. `Live-HD-Jungle.png` is the actual
  window. `Jungle-HD-Slice.mp4` is a deterministic rendered playback with audio.

The initial DK-only prototype remains in `dkc1_hd_sprites.c` as an independent
fallback experiment. Its old 18-replay oracle evidence was native 4:3, and the
synthetic subpixel proof was 1024×896; the earlier claim of a 1368-wide capture
was incorrect. Current full-scene 16:9 evidence above supersedes that claim.

Other levels, unrestricted exploration, every animation/interaction, and all
presentation modes are outside this slice. Uncaptured material stays original.
No shared widescreen capability was promoted and no 40-entrance HD claim is
made. The opening root has no demonstrated historical corruption; it is never
overwritten by the launcher's automated route. After live QA the app is paused,
its controller schedule is cleared, and the private immutable root is restored.

## Reimagined-art follow-up

The user rejected the first restoration's quality and requested a substantial
improvement using ChatGPT image generation. The first new request used the
original 37×40 idle sprite `1eb37c849e8ce07826f4340d226ac7f29ef3e8ce86787249090e776bda9944f5`
as the edit target, requesting new fur/skin/tie material detail while locking
the original silhouette, camera, stance, muzzle and contact points. The built-in
image tool returned HTTP 400 `moderation_blocked`, output-stage category `other`,
request ID `ce1087cd-8cce-4bd3-8dbf-151396435e4e`. No image was produced. The
existing app, active pack, and rendering code were not replaced by this attempt.
The quality goal remains unfinished.

`tools/check_hd_sprite_alignment.py` now provides a separate geometry gate for
future artwork. It rejects stretched canvases and displaced outlines, reports
errors in native pixels, and checks named source/candidate landmarks. Exact
silhouette matching alone cannot pass the internal-feature review. It never
modifies or silently recenters the art. The current-upscale baseline and the
new generation brief are in `build/hd-slice/reimagined/`. A new candidate still
needs visible quality review, contact-point registration, and an in-game
temporal comparison before replacing the existing art.

### Provider and preprocessing experiments (2026-09-06)

The generation block above was worked around using the user's existing Magnific
session, with the explicitly requested Seedream 4.5 and Google Nano Banana 2
models. No new API credential or subscription was purchased. All inputs, raw
4K outputs, prompts, model hashes and local review artifacts are private under
`build/hd-slice/reimagined/`. Runtime rendering and the accepted baseline pack
have not changed during these artwork comparisons.

- `model-comparison/manifest.json` identifies the initial fourteen single-frame
  candidates. The first four Nano outputs had automatic prompt rewriting on;
  the two `nano-v1-controlled` outputs repeat the common prompt with rewriting
  off. Subsequent trials use rewriting off and 4K square outputs; Nano uses
  High thinking. Treat the exploratory batch separately from the controlled pair.
- The best initial Seedream image (`seedream-v1-1.png`) has a face/material
  treatment closer to the intended old prerendered character. The initial Nano
  outputs preserve the supplied body silhouette more closely but reinterpret
  the eyes/brow. Neither observation establishes full sprite acceptance.
- `model-comparison/geometry-comparison.json` is an approximate color-segmentation
  diagnostic, without recentering or scale correction. The leading initial Nano
  silhouette overlap is 97.25%; initial Seedream's detailed output is 75.13%.
  Shadows and segmentation errors limit these figures. Internal features and
  contact points require separate review; this is not the alpha/landmark gate.
- Both generators were tested on sixteen-frame 4-by-4 idle sheets. Nano kept the
  grid and a coherent broad appearance but reduced source head movement and
  invented expressions/tie details. Seedream returned pixel/voxel art; one sheet
  also changed the grid and invented front/back poses. None of these sheets is
  accepted for game integration. Raw results and animated inspection previews
  are in `animation-comparison/`.
- Dense optical-flow and manually anchored thin-plate-spline registration trials
  are rejected: facial distortion and clipped/fringed features outweighed their
  contour improvements. Original silhouettes must not be used to disguise a
  failed internal-feature match.
- The user's individual-frame strategy is now being tested using a shared good
  appearance reference and one source pose at a time. Full-character appearance
  references can dominate the requested pose, so material-only references are
  also being tested. `individual-poses/layout.json` records all sixteen source
  frames, immutable content keys, canvas sizes and placement. The sixteen keys
  were verified by exact source-pixel comparison against the scene material
  exports (`idle-scene-key-map.json`).

The user's Topaz preprocessing suggestion was tested against the installed
CLIs. Topaz Photo AI 4.0.1 exits 254 with `Invalid auth token. Please login through
the app.` Gigapixel 1.3.2 reports `CLI access requires enterprise license.` No
Topaz image was produced. These are local authentication/license limitations,
not evidence that Topaz's image quality is unsuitable. See
`preprocessing/trials.json`; work continues using local open models.

Local preprocessing now compares four distinct idle frames (poses 156, 172,
200, 216) at an effective 2x scale using Lanczos, Real-ESRGAN x2plus, SwinIR-M
DF2K x2 and the SGI x4 model reduced to 2x. The existing
`tools/upscale_hd_assets.py` handles inference on Metal/MPS with source alpha,
edge extension and padding. No duplicate runtime renderer was added.
`preprocessing/local-models.json` records weights and output hashes;
`local-upscaler-comparison.jpg` shows the same-size results. Real-ESRGAN and
SGI provide cleaner inputs in this test; classical SwinIR emphasizes palette
noise/speckling on these tiny sprites. These remain preprocessing candidates,
not reimagined production textures.

Authoritative local-model sources:

- Real-ESRGAN x2plus: https://github.com/xinntao/Real-ESRGAN/blob/master/docs/model_zoo.md
- SwinIR-M DF2K x2: https://github.com/JingyunLiang/SwinIR/releases/tag/v0.0
- SGI: https://openmodeldb.info/models/4x-SGI

The outstanding acceptance step is still a visibly improved, consistently
registered animated sequence in the real game window. The baseline's previous
determinism/coverage results do not certify any newly generated artwork.

### Original sprite handoff and direct Magnific MCP (2026-09-06)

The user requested original sprite sheets for independent experiments. The
private export script `build/hd-slice/reimagined/export_original_sheets.py`
writes `~/Downloads/DKC1-Original-Sprite-Sheets-2026-09-06.zip` and its unpacked
folder: 16 idle frames, 20 walking raster variants, and 22 jumping/movement
variants (58 total). Each sheet has native transparent pixels, exact 4x nearest
versions, uniform-gray references, labeled previews, individual native/768px
references, and source-key/rectangle JSON. All 58 atlas crops and all 4x sheets
round-trip exactly. These are the captured slice's frames, not the complete
cartridge sprite library. Cell centering is a browsing layout, not a game pivot.

The official remote MCP endpoint `https://mcp.magnific.com` is configured as
`magnific` in the user's Codex configuration. The PATH CLI 0.145.0 failed OAuth
with `Authorization server response missing required issuer`; the bundled
`/Applications/ChatGPT.app/Contents/Resources/codex` 0.153.4 completed sign-in.
The active tool inventory did not refresh mid-turn. A private client using the
official Python MCP SDK 2.1.1 and normal OAuth now accesses the same official
remote server for reproducible experiments without operating the user's UI.
It does not extract or reuse Codex credentials. Its separate OAuth state is
private, mode 0700 directory / 0600 files, under ignored build output.

The provider's `simulate_cost` response has one observed schema discrepancy:
`range.tiers` is a numeric object, while its declared schema says array. The
private SDK adapter validates that map's values and checks a copied array form
against the rest of the published schema. Authentication and other response
validation remain intact. Local upload follows request-upload, raw HTTPS PUT,
and finalize-upload; TLS verification is enabled.

Initial MCP probe: `reimagined/magnific-upscale/` preserves exact arguments,
estimates and creation results for two Creative/ThreeDRenders/illusio 4x runs
(creativity -3 or +2, resemblance +8, HDR -2, fractality 0) and one Precision
Sublime 4x run (sharpness 7, grain 0). Source is the existing 192px framed
Real-ESRGAN reference. Actual charge was 180 credits each, 540 total. No result
is accepted or installed merely because generation completed; pose, facial
features and animated runtime behavior remain the acceptance criteria.

Official references: https://docs.magnific.com/modelcontextprotocol and
https://github.com/modelcontextprotocol/python-sdk . Codex OAuth issue:
https://github.com/openai/codex/issues/33354 .

### User Nano Banana 2 walking sheet: playable preview (2026-09-06)

The user's successful 4-by-5 walking sheet is creation `3zMQJ2LREY`, model
`imagen-nano-banana-2-flash` (Nano Banana 2), seed 266008. Its prompt is:
`@img1 Reimagine in modern 4k like @img2 this sprite sheet keeping the exact poses.`
The screenshot shows Thinking High. The MCP does not expose that setting, so
this is integration of the user's existing output, not a claimed reproduction
of every generation setting. The downloaded original is 3712x4608, SHA-256
`6d3e4562c094a5ec429ca95d3808a0cb48bb7664d02bf87cfab7d3bd3e0a1ba1`;
its API metadata reports a different size. Screenshot matching identified this
specific output among four candidates. All private evidence is under
`build/hd-slice/reimagined/user-walk-sheet/`.

Magnific background removal (`CqjRt5VEEy`, 3 credits) produced a 3222x4000
transparent sheet. Per-cell extraction removes detached remnants and blue cast
shadow pixels. Twenty cutouts are fitted with uniform scale and bottom/center
placement into the original sprite rectangles, without deforming faces or limbs.
`registration.json` preserves transforms and geometry measurements. Raw placement
drift was approximately 1-2 native pixels. Registered silhouette IoU is
0.810-0.922, and internal landmarks have not been certified. Thus this is a
visually improved walking preview, not an exact-pose geometry-gate pass.

`build_user_walk_pack.py` copies the 829-material baseline into the separate
`build/hd-slice/scene-pack-user-walk` pack and replaces exactly 20 matching walking
materials. Idle, jumping and other animations retain earlier materials. The
baseline pack remains available. Candidate pack SHA-256:
`244fa0c1d9462371241525dd367df6f3032927fee7da3bbf4272df31535d566b`.

Verification:

```sh
python3 tools/verify_hd_scene.py \
  '/Users/briantate/Downloads/Donkey Kong Country (USA).sfc' \
  build/hd-slice/user-walk-validation \
  --pack build/hd-slice/scene-pack-user-walk --jobs 3
```

All 36 native/wide, fresh/idle/walk replays passed the existing equality checks
for original frames, WRAM/VRAM/CGRAM/OAM, audio and repeated HD output. The six
HD walking replays had zero reconstruction fallback pixels. This does not imply
complete game-wide material coverage. `--pack` now selects the candidate and
records its hash; HD runs render every frame to exercise walking frames before
the idle endpoint. Headless executable SHA-256 remains
`63ba41a4b2334190cde1f791df86b3b55c4aad5908687ddf007b8301e5b4b22f`.

Baseline/candidate capture of frames 30-104 using `walk.inputs` shows changes in
74 of 75 frames, confined to Donkey Kong's region. Frame 30 is still idle.
`runtime/changed-frames.json`, `in-game-walk-comparison.gif`,
`Nano-Banana-Walking-In-Game.mp4` and raw frame sequences retain this evidence.
The full-frame movie uses 60 fps and SNES pixel-aspect-correct 1596x896 output.
The side-by-side GIF uses slower 20ms frame holds for inspection.

The actual Mac app was inspected at frame 90, with F10 original/HD comparison;
that run was restored with F12 and closed normally. The subsequent standalone
`build/macos/DKC1 HD Nano Preview.app` includes the same 20 verified materials.
Its unique bundle ID is `com.flat2vr.dkc1recomp.hd.nano-preview`, now explicitly
recognized by the HD initializer, with its own default Application Support
directory. Explicit `DKC1_USER_DIR` also receives the initial quicksave rather
than creating it in a different default folder. No gameplay code changed.
The final app binary SHA-256 is
`877c56a2c67744f0f32caeee0d800fea478ae29e03d22cab94812fe57e291dbd`;
the build and deep/strict code-signature checks passed. An intermediate duplicate
bundle-identity launch was closed through SDL's SIGTERM-to-SDL_QUIT handler,
exiting normally with status 0.

Final packaged launch used `build/hd-slice/user-nano-preview`, `walk.inputs`,
pause-at-90, and `unique-bundle-live-trace.jsonl`. It reached frame 90 with zero
reconstruction mismatches/material misses. The app was subsequently resumed;
CUA reported user activity, so further automated controller operations stopped.
The live process was left for the user's interaction. The immutable bundled
root and private quicksave remain SHA-256
`7aeaa4e44e714db99af7cbeb8e79e68ee0c072bf9b4e84ba35f5c927e3cc05da`.

User handoff: `~/Downloads/DKC1-Nano-Walking-Preview/` contains the original 4K
sheet, transparent sheet, game-size frames, registration record, comparison GIF,
gameplay movie and a link to the runnable app. Assets, ROMs, states and generated
outputs remain private/ignored. No commits or publication were made.

Three additional Magnific Creative +2 single-pose probes completed (180 credits
each, 1080 total for six upscale probes). They remain exploratory evidence under
`reimagined/magnific-upscale/`, not installed game assets. The user-provided
Nano walking sheet is the current best animated result.


### Complete Donkey Kong animation generation pass (2026-09-06)

The private inventory now contains 700 distinct native frame content keys in
53 named Donkey Kong groups, laid out in 65 original sheets. This includes the
unknown/unused poses identified by the historical pointer map; those names are
navigation aids, not independently proven animation semantics. The clean ROM
SHA-256 remains
`fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.

`reference/` was checked first; this fork and the main checkout lack the nested
DKC1 disassembly. The existing historical
`DKC-Widescreen-358x224/rom/overlay/DKC1/AsarScripts/AssetPointersAndFiles.asm`
provided asset ranges and labels only. The extractor reads graphics bytes from
the clean supported ROM, decodes DMA tile placement and OAM piece ordering, and
preserves each tight rectangle and its original signed anchor. All 58 previously
captured live sprite images match the extracted originals byte for byte. The
other 642 source images have not each been independently captured in gameplay.
No historical patched-ROM behavior or generated game code was imported.

Private reproduction scripts and evidence are under
`build/hd-slice/reimagined/all-animations/`. `extract.py`, `inventory.json`,
`original-frames/`, and `sheets/` retain the source inventory. The source images
are 4x nearest-neighbor on uniform gray, with the exact user-selected appearance
reference as the second image. Browser generation uses Nano Banana 2, 4K,
Thinking High, one result per submission, and AI prompt rewriting disabled.
The initial prompt remains:

> @img1 Reimagine in modern 4k like @img2 this sprite sheet keeping the exact poses.

The complete batch produced 58 initial sheets plus two reconciled missing
submissions, reused the earlier walking/jumping/landing/turning results, and
added 19 corrective sheets. The original AUTO aspect-ratio pass sometimes
changed the layout, completed partial bodies, invented props, or favored the
appearance reference's pose. The corrective pass uses explicit 4:5, a fixed
4-column by 5-row grid with intentional repeated source frames, and additional
pose and no-extra-object constraints in `retry-prompt.txt`. Repeated source
frames provide alternate candidates without changing the original frame keys.
Every correction's source filename was checked in its creation-detail UI;
`generation-ledger.json` and `correction-ledger.json` record the identities.
One extra MCP pilot was exploratory; MCP did not expose Thinking High, so the
main pass was performed in the UI. No subscription or credit purchase was made.

All results and provider background removals have been downloaded. `process.py`
removes detached/blue/neutral floor shadows, uniformly fits each result into its
original 4x rectangle, and places it using the original bottom and center. It
does not deform individual limbs or faces. The best silhouette-overlap candidate
is selected per frame, excluding ten first-pass sheets with malformed grids,
wrong characters, or visibly invented objects/effects. All raw results remain
available. `registration.json` contains all 700 selected candidates and zero
missing frames; 332 score below 0.78 silhouette IoU. A higher score is not a
feature-alignment or temporal-consistency certificate. Carrying, partial-body
minecart sprites, some rope rotations and unusual poses still need targeted
correction. In particular, the model often invents legs below a cropped torso.

`hd-sheets/` contains 65 transparent sheets with the exact source cell rectangles.
`review/` contains all 53 original/candidate contact boards. `index.html` provides
animation playback, a frame slider, original overlay, and per-frame/source-sheet
links while preserving the ROM anchors. Playback is numeric pose order at an
adjustable rate, not the original cartridge animation-script timing. JavaScript
syntax and all local asset references passed static checks. Automated opening
of local HTML was explicitly blocked by browser URL policy; no alternate local
server or browser workaround was used, and browser visual QA of this viewer is
not claimed. The user can open the delivered HTML manually.

The standalone Nano preview now contains 83 replacements: Idle (21), Walk (20),
Run (20), Jump (11), Land (9), and Turn (2). The material pack is isolated at
`build/hd-slice/scene-pack-nano-motion/`; its SHA-256 is
`ede217aa0c82b72e6b842e0ed3455c96920d763c4c72c74079b4ff5a4a82bcef`.
The final app is `build/macos/DKC1 HD Nano Preview.app`. Re-signing its changed
resources produces executable SHA-256
`1626ffc8e5db0da4b2dc3c72e030da2ae99e07828259a70cb5f2f0bd0c3056d7`;
its code was not changed during this batch. Deep/strict signature validation
passes. The material pack embedded in the app matches the tested pack exactly.

Validation commands and outputs:

```sh
python3 tools/verify_hd_scene.py   '/Users/briantate/Downloads/Donkey Kong Country (USA).sfc'   build/hd-slice/nano-motion-validation   --pack build/hd-slice/scene-pack-nano-motion --jobs 3
python3 build/hd-slice/verify_running.py   '/Users/briantate/Downloads/Donkey Kong Country (USA).sfc'   build/hd-slice/nano-running-validation   --pack build/hd-slice/scene-pack-nano-motion --jobs 3
```

All 48 native/wide, HD-off/on replays passed three-repeat equality for original
video, memory and audio hashes and deterministic HD output. The added running
route is 30 neutral frames, 60 Right+Y, 25 Right+Y+B, 35 Right+Y, and 30 neutral
frames. Captured material keys prove that Run4-Run19, Jump1-Jump11 and Land1-Land6
were exercised, alongside idle/walk poses. Exact-root idle/walk/running runs had
zero reconstruction fallback pixels. Fresh-entry runs retained 184 stock
fallback pixels in each repeat; these were not concealed or promoted. This is
Jungle Hijinxs slice validation, not a full-game or all-animation runtime gate.
The renderer's existing scene guard and stock fallback remain unchanged.

Visible QA used an isolated user directory and the immutable root with SHA-256
`7aeaa4e44e714db99af7cbeb8e79e68ee0c072bf9b4e84ba35f5c927e3cc05da`.
The first inactive-window image was stale at the root; F10 off/on refreshed the
visible frame-70 comparison. `nano-motion-live/native-running.png` and
`hd-running.png` retain that same-frame original/HD evidence. After comparison,
F12 restored the private root and left the app paused with controller playback
cleared. `restored-root.png` and `launch.json` record the final process/build/state.
The ordinary main-checkout DKC1Recomp process and its state were not touched.

Handoff files are `~/Downloads/DKC1-All-DK-Animations/index.html`,
`~/Downloads/DKC1-All-DK-HD-Animations.zip`, and the refreshed
`~/Downloads/DKC1-All-Original-DK-Animations.zip`. The HD archive includes the
700 frames, 65 matching transparent sheets, originals, review boards, metadata
and offline animation viewer; raw 4K links open the corresponding Magnific
creation. It contains no ROM, save state, OAuth files or app binary. All assets
and generated evidence remain private/ignored. No commits or publication were
made; unrelated fork changes remain untouched.
