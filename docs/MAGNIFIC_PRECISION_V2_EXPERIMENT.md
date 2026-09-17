# Magnific Precision V2 sprite experiment

Private experiment, September 15–16, 2026. No commits or release-default changes.

The September 16 performance follow-up adds full-level art preloading to the
private full preview. See [HD_PRELOAD_PERFORMANCE_REVIEW.md](HD_PRELOAD_PERFORMANCE_REVIEW.md)
for measured stalls, resident-pack validation, unchanged bonus-room limits and
the separate Metal compositor proposal.

## Donkey Kong pass

All 700 source poses in the existing 53-group inventory were verified against
their original BGRA SHA-256 keys and processed through the Magnific MCP.
Eleven native 768×768 plates produced eleven exact 3072×3072 outputs using
`images_upscale`: `mode=ultra-sublime`, `scale=4x`, `sharpness=7`, `grain=0`.
Magnific identifies Sublime as a Precision V2 model:
https://www.magnific.com/ai/docs/image-upscaler

Private evidence: `build/hd-slice/precision-v2-20260915/`.
`manifest.json`, `ledger.json`, `batch-jobs.json`, and `completed-results.json`
record source rectangles, hashes, model parameters, and provider results.
The downloaded JPEGs are retained alongside losslessly decoded PNGs.

`tools/magnific_precision_experiment.py prepare OUTPUT --source SOURCE` verifies
the source inventory and creates fixed-grid inputs in a new directory.
Submission remains a separate MCP operation with cost simulation, upload,
completion checks and download registration. `assemble OUTPUT` validates every
output dimension, crops exact source rectangles, restores bicubic source alpha,
and calls the existing registered packer for both facing directions. There is
no pose fitting, guest-state edit, renderer edit, or inferred frame selection.

Outputs: 700 transparent PNGs; 1,400 source/mirrored hash-keyed `.dkhd`
materials; an interactive original/candidate pose viewer at `index.html`.
The viewer uses numeric pose order, not cartridge animation timing. Alpha
comes from original sprites, so silhouette scores do not establish AI RGB
landmark fidelity. The result retains substantial original pixel structure.

Validation: 700/700 silhouette passes (minimum IoU 0.9857538117), 16 existing
sprite alignment/packing tests, and 60 deterministic replay legs: native/wide ×
fresh/idle/directions/run/barrel-left × HD off/on × three repeats. Guest frame,
WRAM, VRAM, CGRAM, OAM, OAM shadow and audio checks match; HD results repeat.
Details and exact hashes are in `alignment.json` and `replay-validation/results.json`.
This is not a full-game visual or internal-landmark acceptance pass.

Preview: `build/macos/DKC1 Magnific Precision V2.app`, a separately identified,
ad-hoc signed copy of the existing HD executable, containing only the new DK
materials. Scenery and other objects use original pixels. The existing renderer
is still restricted to Jungle Hijinxs entrance `$0016`; runtime coverage has
not been expanded. The actual window was inspected with HD enabled and with
F10 original comparison. Use F7 to pause/resume, F10 to compare, and F12 for
the experiment's private quicksave. The normal and Nano preview bundles remain
unchanged. Local `LSEnvironment` entries set private art/state/user paths.

Supported clean ROM SHA-256:
`fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.
Immutable entry state SHA-256:
`7aeaa4e44e714db99af7cbeb8e79e68ee0c072bf9b4e84ba35f5c927e3cc05da`.
Source preview executable before resource re-signing:
`84b91d5fbca4d3fd67e3d87667ea8e4ec43707d8718792e48440b13a6a915572`.

Billing discrepancy: each of the eleven creation records reports 270 credits,
total 2,970. Account snapshots changed from 14,609 to 6,689 available (7,920
delta). The difference cannot presently be attributed to these jobs versus
other account activity or provider accounting. Do not present 2,970 as a verified
account debit. No credits were purchased.

## Jungle Hijinxs level pass

Private evidence root: `build/hd-slice/precision-v2-level-20260915/`.
The user requested the entire level's sprites, then pointed out missing
backgrounds and pixelated banana collection. The final candidate is `complete/`.

- The checksum-locked extractor produced 2,132 unique additional rasters across
  37 families, including inactive DK, both Diddy palettes, Rambi, enemies,
  barrels, collectibles, decorations, and effects. This is a family superset;
  some poses belong to other scenes and are not claimed as encountered here.
- Native and wide 2,380-frame controller traversals exported 1,397 distinct
  object keys and 7,961 background context keys. The final pack contains every
  exported key, with dimensions checked. The routes include death/re-entry;
  this is not proof of a clean level completion or every possible path.
- 722 native captured components and 264 supplemental wide components/digit
  primitives cover clipped sprites, touching objects, and captured HUD forms.
  Original/mirror equivalence is normalized before registration.
- Backgrounds have 4,109 exact center rasters (4,108 nonempty). Each identical
  32×32 center shares one canonical captured 48×48 neighborhood. Seventeen
  native plates supply eight pixels of context, then crop exact 128×128 results.
  Alpha stays exactly source-derived. The outer two native pixels blend toward
  source colors to reduce seams between independent AI patches; raw outputs
  remain available. Context-key aliases are explicit, not runtime guesses.
- The normal banana HUD is one combined banana-and-number material. Eighteen
  ROM-decoded primitives supply eight banana phases and ten digits. The layout
  matches 85 captured complete HUD rasters. All 800 count/phase combinations
  (0–99 × 8) are composed from the selected Magnific primitives, so unvisited
  ordinary counts do not require another cloud call. All 36 primitive reference
  PNGs reproduce byte-identically with `magnific_banana_hud.py prepare`.

Final asset counts: **16,434 material keys**, **8,790 viewer rasters** (sprite,
HUD, and background candidates), and **800 normal banana HUD combinations**.
`complete/coverage.json` records zero missing keys from the two captured routes.
The viewer uses pose order, not cartridge timing, and source canvases for captured
composites use display anchors rather than inferred guest object anchors.

Source navigation uses the clean source at Yoshifanatic1's DKC1 disassembly
commit `c2080f40469c716923f550706509a0d354229841` and the existing local asset
address index. The latter supplies names/addresses only. Pixels and palettes
come exclusively from the checksum-locked clean ROM. `source-record-audit.json`
proves all 576 bytes at ROM offsets `3d95dc..3d981c` match the source's main-level
records and child group. No source oracle, ROM, generated code, or engine was
modified.

Two sparse effect silhouettes initially failed the IoU threshold after bicubic
alpha interpolation. Assembly now uses exact nearest alpha for those rasters;
the rechecks pass. Fully opaque one-pixel source effects are accepted by the
registered packer only when their verified original is also opaque. A new test
keeps flattened transparent sprites rejected. This change affects the private
packer, not runtime composition. Internal RGB landmarks remain a visual review
item; matching alpha alone is not artwork acceptance.

### Reproduce and review

Use the private upscaling venv and the existing immutable entry state:

```sh
build/hd-slice/upscale-venv/bin/python tools/extract_jungle_sprite_inventory.py ROM ASSET_INDEX NEW_SOURCE
build/hd-slice/upscale-venv/bin/python tools/magnific_precision_experiment.py prepare NEW_UPSCALE --source NEW_SOURCE --tight
# Submit through Magnific MCP; retain IDs, raw provider files, and decoded PNGs.
build/hd-slice/upscale-venv/bin/python tools/magnific_precision_experiment.py assemble NEW_UPSCALE
build/hd-slice/upscale-venv/bin/python tools/magnific_scene_materials.py prepare NEW_BACKGROUNDS --captures NATIVE_MATERIALS WIDE_MATERIALS
# Submit its inputs through the same MCP settings.
build/hd-slice/upscale-venv/bin/python tools/magnific_scene_materials.py assemble NEW_BACKGROUNDS --edge-feather 2
build/hd-slice/upscale-venv/bin/python tools/magnific_banana_hud.py prepare ROM NEW_HUD_SOURCE
build/hd-slice/upscale-venv/bin/python tools/magnific_banana_hud.py assemble HUD_SOURCE NEW_HUD_OUTPUT --pack MATERIALS_WITH_ALL_PRIMITIVES
build/hd-slice/upscale-venv/bin/python tools/verify_hd_scene.py ROM OUTPUT --pack COMPLETE_MATERIALS --cases fresh idle directions run barrel-left cache-pressure --jobs 3
build/hd-slice/upscale-venv/bin/python -m unittest discover -s tests -p 'test_hd_sprite*.py' -v
```

The final preview is `build/macos/DKC1 Jungle Full Precision V2.app`. It reuses
the existing HD executable, includes the final private materials, has a separate
bundle identity/user directory, and starts paused at the immutable Jungle root.
F7 resumes, F10 compares original/HD, F12 reloads its private root. The normal
release, old Nano app, and earlier sprite-only preview remain separate. The new
bundle was ad-hoc signed and verified; the actual native window was inspected.
This was resource packaging, not a fresh recompilation of inherited dirty code.
`complete-launch-environment.json` records the launch configuration.

Validation results and executable/ROM/state/pack hashes are in
`final-validation/results.json`: native and wide, six cases, HD off/on, three
repeats (72 legs). Guest framebuffer, WRAM, VRAM, CGRAM, OAM, OAM-shadow and
audio hashes must match; HD output must repeat. A final additive gap pass has 12 further native/wide run replays at
`gap-validation/results.json`, including the final pack hash. Seventeen sprite
pack/alignment contract tests pass. Earlier matrices remain as intermediate evidence.
`banana-pickup/` additionally preserves a controller-only pickup route, state,
WRAM, trace, and rendered frame. First-pass raw PPM/log evidence was losslessly
gzipped after disk space ran out; no user files were removed.

### Limits and accounting

The renderer remains default-off and restricted to Jungle Hijinxs entrance
`$0016`, supported modes and widths up to 342 pixels. Background keys include
neighboring context, so camera, palette, or overlap combinations absent from
the captures can still fall back to original pixels. An independent short run
exposed 90 missing keys (`gap-capture/missing.json`): 26 background aliases,
44 new background contexts, and 20 object forms. These are now covered by 110
additional facing/context materials. The repeated native run has zero material
misses on every frame; its wide counterpart still has uncaptured combinations
(maximum 2,287 missing source pixels, final 1,551). Do not describe the
experiment as universally complete or zero-fallback. Bonus rooms and other
levels retain stock output. No widescreen behavior, guest state logic, allocator,
collision, timing, source checksum rule, or release default was changed.

Magnific MCP creation records report 270 credits per plate: 11 DK, 10 family,
3 captured-component, 18 scenery/supplement plates, and one independent-route gap plate = 43 jobs / 11,610
reported credits. Account snapshots also changed plan totals and spending during
this session, so that number is not a reconciled account debit. This task did
not purchase credits. `scene-jobs.json`, `scene-completed.json`, per-pass ledgers,
input hashes, and retained provider JPEGs preserve provenance.

The final window was relaunched and observed paused with HD enabled at the
immutable entry. LaunchServices initially retained an obsolete Downloads ROM
path after resource signing; the stalled test process was blocked in
`Dkc1ReadVerifiedRom`/`fopen`, before snapshot loading. The final bundle uses a
SHA-256-verified private ROM copy and private user directory under
`~/.cache/dkc1-precision/`, with refreshed app registration. It launches cleanly.
The ROM is not bundled or committed. Native-window pickup-snapshot inspection
was not completed; the controller-only pickup was verified by headless evidence.
`final-identity.json` records final bundle, executable, state, pack and launch
identities; installed asset bytes match the final 12-leg validation pack exactly.

The optional native Metal layer compositor and its CPU/GPU oracle, performance
comparison and packaged preview identity are documented in
[HD_METAL_COMPOSITOR_REVIEW.md](HD_METAL_COMPOSITOR_REVIEW.md).
