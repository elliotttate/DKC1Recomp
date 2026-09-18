# Connected Nano scenery and clean sprite edges

September 16, 2026. Private experimental assets and launchers; no source commit,
ROM changes, gameplay writes, generated-code changes or submodule changes.

## Result and remaining scope

The new connected pack replaces all visible background pixels on the declared
2680-frame native Jungle Hijinxs route through the level exit and world map.
It removes the independent 32x32 generation grid and reconstructs source
contours before a restrained, optional Metal edge pass. A later user screenshot
identified gray fringes around DK's head/feet and Diddy's tail. An additional
offline sprite-matte pass removes those fringes while protecting real gray art
such as Rambi. The separate `build/macos/DKC1 Jungle Clean Edges.app` is the
current review candidate. The user's previous live apps and save directories
remain intact.

This is not full-game or all-sprite acceptance. Some object composites/poses
still use originals; unsupported bonus rooms and world maps retain the existing
scene guard. The full native route does not establish a full widescreen route:
the same input schedule follows different existing wide behavior and re-enters
after death. Wide validation covers early scrolling/re-entry and separate late
immutable states. There is no live coverage-color overlay yet. GPU timing is
not a demonstrated <=1 ms p99 guarantee.

## Source and identity

Private root: `build/hd-slice/nano-level-20260916/`. Evidence below is relative
to its `polish/` directory unless specified otherwise.

- Clean ROM SHA-256: `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.
- Immutable `build/hd-slice/entry.state` SHA-256:
  `7aeaa4e44e714db99af7cbeb8e79e68ee0c072bf9b4e84ba35f5c927e3cc05da`.
- Headless executable SHA-256:
  `fc891ee08f4c6636da168e5170fb0460f2db4348d54c44a1331cd55847a557cc`.
- Connected v9 pack SHA-256:
  `e4f89617b049d040ffff6bb064d592693b79e196ed5cb7e23221402b25f76c92`.
- Final sprite pack/build identities and all seven per-run guest hashes are in
  `sprite-matte/verification/results.json` and `sprite-matte/app-build.json`.
  Bundle code is ad-hoc signed and passes deep/strict verification.
  The delivered material hash is
  `c0b62055f6754f2e4502b54764e377daf680ddbab008a6e57306034f395a18bd`;
  every bundle raster and all three runtime indices match the tested pack.

`map-source/` preserves source WRAM/VRAM/CGRAM, scene and presentation trace.
The decoded 5376x512 map has 583 unique native terrain centers; 255 were absent
from the earlier captured pack. All 806 visible tile entries at the root match
the actual PPU ring byte for byte. The original 16,434-key inventory was not a
full-map coverage proof.

## Background and renderer changes

Seven connected terrain strips and two complete periodic parallax layers were
generated with the selected Nano Banana Pro model. Nine jobs completed at 150
credits each. Provider results, settings and identifiers are in
`connected/jobs.json` and `connected/pilot-job.json`; source/reference images
remain private. The two older rejected sheets were not retried.

The strips use 128-native-pixel overlap, bounded overlap tone correction and
shared source-coordinate blending. Reconstructed alpha and foreground color
extension replace nearest masks and the old fade back to pixel-art RGB.
`tools/build_hd_connected_pack.py` assembles the grid; the source audit is
`tools/audit_hd_jungle_map.py`. Exact 8x8 Nano subregions fill observed mixed
stream cells through `tools/build_hd_stream_boundaries.py`, with provenance.

Runtime `DKHWv002` registration uses actual PPU scroll, including the
presentation bias, instead of the next-frame WRAM camera. This removes the
one-frame binding gap at 32-pixel boundaries. Each subtile is independently
verified against canonical source pixels. Current palette changes are applied
after identity lookup. Bindings are reused when atlas identity and world window
are unchanged. Exact-center reuse is BG-only; malformed indices and unverified
tiles fail closed. Palette aliases for objects require an exact canonical
raster match; unknown canonical object exports are not accepted art.

The v9 pack contains 23,084 materials / 1,665,650,304 resident pixel bytes.
A later exact-state wide check found eight missing visible BG samples. Its
`sprite-matte/late-missing/` capture supplied 33 further byte-exact mixed-cell
aliases. The final clean-edge pack has 23,117 materials / 1,667,812,992 resident
pixel bytes (1590.6 MiB). These additions are recorded separately from sprite
cleanup in the pack's `boundary-provenance.json`.

The optional Metal pass uses short spatial taps, conservative OBJ/HUD and
fallback protection, immutable frame packets and pooled textures. It has no
temporal history or production readback. The raw compositor remains the
CPU/Metal oracle; deliberate polish differences never weaken it. Normal
defaults are off; the private launcher starts at 65. Escape → Graphics →
HD scenery → Edge cleanup exposes the slider; zero shows raw HD art. F10
retains original/HD comparison.

## Gray sprite fringe diagnosis and repair

The generation plates used #505050. Assembly previously copied the generated
RGB under bicubic original alpha. Nano's contour sometimes sat just inside
that mask, leaving opaque gray strips and gray-contaminated partial pixels.
The error is in prepared assets; a fullscreen blur cannot remove it correctly.

`tools/hd_sprite_matte.py` samples exterior matte near the specified plate
color, estimates foreground/matte mixtures only within two native pixels of
the source edge, and unmattes RGB plus bounded alpha. Original gray paint
vetoes cleanup; interiors are byte-exact. When trimming would exceed the
silhouette limits, clean neighboring foreground color fills the retained
source edge. Ambiguous pixels are preserved. No pose is shifted or recentered.

The pass processes 4,570 registered images / 8,403 available facing materials;
8,384 materials change. All 4,570 pass the automated silhouette limits
(IoU >= .93, contour p95 <= .75 native pixels, bounds error <= .5). For 1,620
images the alpha guard reduces or disables trimming. These are geometry limits,
not a claim that all images were manually accepted. True gray body paint,
small appendages and source canvases have dedicated tests. The builder creates
a new pack and breaks hardlinks before writes, leaving original/live packs
unchanged. No extra runtime work is added by this repair.

Before/after examples are in `sprite-matte/sprite-matte-comparison.png`;
full per-image statistics/hashes are in `sprite-matte/v1/matte-report.json`.
Visual review confirms the gray rim disappears from the tested DK idle pose
and Diddy tail/arms, while Rambi's body retains its shading. The native clean
launcher was opened and inspected paused at the immutable start; the graphics
menu was verified at strength 65 and closed without advancing the game.
The user's screenshot is appearance evidence, not an exact supplied save state.

## Validation

- Connected v9: 60 deterministic replay legs (fresh entry, idle, walk, run,
  cache-pressure/reload; both native and wide), with 28,149 pixel-exact raw
  CPU/Metal frame comparisons. `verification-final/results.json`.
- Full 2680-frame route: three repeats on/off per aspect (12 runs) preserve
  framebuffer, WRAM, VRAM, CGRAM, OAM, OAM shadow and audio hashes.
  `route-verification/results.json`, `connected/full-route.inputs`.
- Coverage: 1,990 supported native frames and 2,078 supported wide frames have
  zero visible missing BG pixels and zero reconstruction mismatch on those
  routes. Native reaches the exit; wide's bounded scope is described above.
  `connected/audit-v9-*/coverage-summary.json` and `connected/exit-check.png`.
- Connected late-position checks: 24 on/off repeat legs preserve guest hashes;
  1,440 raw GPU frames match. The middle-wide eight-sample coverage gap was
  classified and captured, not hidden by the passing determinism result.
- Clean-edge pack: 48 fresh/idle/directions/actions on/off native/wide replay
  legs pass with three-repeat guest/HD determinism, raw Metal equality and no
  gameplay texture reads (12,546 audited GPU frames).
  `sprite-matte/verification/results.json`.
- The final clean-edge pack's middle/end exact-state checks add 24 replay legs
  and 1,440 raw GPU comparisons. All have unchanged guest hashes, zero native
  mismatch and zero missing visible background samples, including the formerly
  missing eight wide samples. `sprite-matte/late-verification/results.json`.
- All 32 HD tests and the graphics settings model pass. The HD suite covers preload ownership,
  eviction, malformed indices, connected source masks, palette correction,
  immutable GPU packets, edge filtering and offline matte cleanup. Test logs
  are under `sprite-matte/` and `hd-tests.log`.
- The synthetic polish fixture changes only intended edges: 1,104 changed
  pixels, zero protected/flat-region changes, deterministic repeats. The
  isolated measured median is around 0.1 ms; concurrent stress produced p99
  values above 2 ms. This does not prove the proposed full-frame p99 budget.
  The final isolated fixture measured median 0.0641 ms / p99 0.3935 ms; this is
  the synthetic edge kernel, not end-to-end scanout latency.

No fresh-entry 40-scene widescreen promotion is claimed: these are isolated HD
presentation/asset experiments, and normal release capability policy is
unchanged. Old states have not been rewritten. Missing object poses, broader
bonus coverage, the proposed diagnostic overlay and wider motion/performance
acceptance remain explicitly open.

## Scrolling follow-up

The tester's later save exposed whole-chunk fallback art replacing verified
connected subtiles during scrolling. The per-subtile correction, exact-state
replay, fresh-entry checks and new preview are documented in
[HD_NANO_POPIN_REVIEW.md](HD_NANO_POPIN_REVIEW.md).
