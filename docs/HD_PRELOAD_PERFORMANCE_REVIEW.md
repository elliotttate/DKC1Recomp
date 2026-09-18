# HD level residency and performance investigation

September 16, 2026. Private Magnific Precision V2 experiment; uncommitted.

The pixelated output after leaving Jungle Hijinxs is a scene-coverage limit.
`Dkc1HdScenePrepare` requires entrance `$0016`, full brightness and the supported
Mode 1 composition. Other rooms and fade/mosaic transitions fall back to
original pixels. The shipped ARM64 executable confirms the entrance comparison
at `0x1007acb24`. It does not reset the F10 setting. Reusable sprite artwork
being present is insufficient while this whole-scene guard rejects the room.

The user's live process was the older `DKC1 Jungle Precision V2.app`, PID 75642,
observed at Cranky's cabin in 16:9. It was left untouched. Its executable text
matched the full preview, but its resources lacked the expanded backgrounds
and banana/HUD pack. Both applications had the same scene restriction.

## Before the change

Evidence: `build/hd-slice/precision-v2-level-20260915/performance-investigation-20260916/`.
Native runs used one immutable root, fixed inputs, 342×224 presentation,
nearest/flat/raw graphics, isolated user directories, normal audio, and serial
interleaved order on this Apple M3 Max. The user's original process remained
running. Each run has pacing, scanout and final-state evidence.

Three repetitions of `recipes/hd-run.dks` (330 frames) produced these medians
of post-warmup means:

| Setting | Rendering ms/frame | Total work ms/frame |
| --- | ---: | ---: |
| Both HD switches disabled | 0.543 | 1.710 |
| F10-equivalent output disabled, scene preparation enabled | 1.943 | 3.038 |
| Older sprite pack enabled | 5.362 | 6.512 |
| Full sprite/background pack enabled | 5.437 | 6.533 |

Most warmed submissions stayed near 16.67 ms. Tenfold rendering cost does not
mean tenfold FPS loss. F10 gates output but leaves scene preparation running.
Use 60 startup frames: an initial 30-frame cutoff included unrelated AppKit
setup stalls around frames 31–39 in both stock and HD runs. Those are excluded
from the corrected results.

The 1,200-frame `traverse-capture/route.dks` comparison, three repeats per mode,
exposed first-pass hitches. The first full-pack run had 78.684 ms p99 rendering,
173.167 ms at frame 167, and 172.612 ms at frame 123. Worst submission spacing
was 181.350 ms; 21 audio starvations were recorded. Warmed HD repeats had
12.297/14.933 ms p99 rendering and no audio starvations. Stock p99 rendering
was 1.20–1.28 ms. All six final states matched byte-for-byte. Scanout traces
include skipped/occluded drawables, so their raw physical-Hz totals are not a
clean game-FPS estimate.

An independent 3,347-frame headless profile confirmed blocking stacks through
`GetMaterialKey -> fopen/open` and `GetMaterialKey -> fread/read`. Separately,
the CPU compositor reconstructs each native pixel and samples sixteen HD
texels, producing 1,225,728 output texels per frame at this width. Full
background decoding/hashing, linear material searches and object scans add
CPU cost. Magnific runs offline; there is no AI inference during gameplay.

## Resident level pack

`DKC1_HD_SCENE_PRELOAD=1` is default-off, enabled in the private full preview.
`tools/build_hd_preload_manifest.py PACK` validates each DKHD header, dimensions
and exact length, then atomically writes a sorted `preload.txt`. Before the
first supported frame, the runtime reads all indexed art into owned memory:
16,434 rasters, 1,229,835,904 pixel bytes (1,172.9 MiB / about 1.15 GiB).

The resident rasters live independently of the 4,096 decoded-material slots.
Materials borrow immutable HD pixels through binary search of the resident
index; eviction only frees owned data. Missing keys use original art without
disk access. Malformed/incomplete preload fails closed with a diagnostic,
without silently returning to gameplay I/O. Rebuild the index after art changes.
The level pack remains resident until process exit, including across room visits.

This is CPU/shared-memory residency, not GPU texture upload. Composition,
priorities, color math, source art, native oracle, guest behavior and scene
allowlist remain unchanged. Increasing the old cache would not eliminate
first-use reads; mmap alone would still allow file-backed page faults in play.

## Validation and identity

- Native/headless builds pass. Existing SDK deprecation/linker warnings remain.
- Full unit suite: 262 tests, one platform skip, otherwise passing; all 21 HD
  tests pass. New ASan/UBSan tests remove source files after preload, force cache
  eviction, verify borrowed-image lifetime, and reject missing/truncated art,
  malformed/duplicate keys, trailing index data and oversized dimensions.
- `preload-validation/results.json`: 36 passing replays, three repeats each of
  fresh entry, movement and pressure/re-entry, native/wide and HD off/on. Guest
  hashes match; HD images repeat. Every eligible preloaded frame reports 16,434
  resident materials and 16,434 cumulative texture-file opens. The wide pressure
  run evicts 3,859 decoded entries without another texture read or mismatch.
- `baseline-equivalence.json`: all 18 guest/HD-image comparisons against the
  preserved previous executable with the same art pack pass. Existing missing
  artwork is unchanged; residency cannot fill absent material identities.
- Six native 1,200-frame runs on the rebuilt binary compare preload off/on in
  interleaved order. Warmed median rendering means are 4.686/4.426 ms; p99
  rendering medians are 8.297/7.313 ms. All final states equal the prior native
  baseline. Every preloaded run holds 16,434 images and a constant 16,434 file
  opens through 774 decoded-cache evictions. See `resident-summary.json`.
  These warmed timings do not recreate the initial cold-file condition or
  imply that all host scheduling/audio stalls are eliminated.
- `git diff --check` passes. Original full-preview binary/plist are preserved
  in `original-full-app/`. No commits or source-oracle/submodule edits.

ROM: `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.
Root state: `7aeaa4e44e714db99af7cbeb8e79e68ee0c072bf9b4e84ba35f5c927e3cc05da`.
Unchanged art: `486eab820d968ac7015e6647308b5da69e7fce9d7798572e1a3d09d6ea238d76`.
Candidate headless: `66c4db5dd029d381095bb7dab43fa75d61ccfd2942f65ccbba664bf228471a5d`.
Packaged executable: `c0442dd6066a5bdfaeae01d6731a8bc13e36d8699e044832e547f802c919a60e`.

The signed `DKC1 Jungle Full Precision V2.app` was launched and its real window
inspected, paused after frame 1 from the immutable Jungle root with HD enabled
and no input schedule. Final PID 5729 used approximately 1.4 GiB RSS. The user's
older process remained PID 75642. `resident-preview-identity.json` and
`final-processes.txt` record the final handoff; the parent `final-identity.json`
and launch-environment file now describe this resident build. The pre-change
identity is preserved as `final-identity-before-preload.json`.

## GPU follow-up

A Metal compositor is justified by the remaining CPU cost. Keep guest execution,
placement/provenance and the native oracle on CPU; upload art once and send
per-frame layer/object/scanline data to Metal. Preserve palette correction,
color math, priority, transparency and original-pixel fallback. Compare CPU/GPU
images and deterministic guest outputs before enabling it. This change does not
implement that port or expand HD support into bonus rooms.

The subsequent opt-in GPU implementation and its current validation/preview
identity are recorded in [HD_METAL_COMPOSITOR_REVIEW.md](HD_METAL_COMPOSITOR_REVIEW.md).
The CPU-only measurements and process identities above describe the preceding
resident-pack checkpoint.
