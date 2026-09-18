# HD Metal layer compositor

September 16, 2026. Private Magnific Precision V2 experiment, opt-in and uncommitted.

## Existing DKC2 / DKC3 work

The current DKC3 checkout (`eeb16d0853c641189fec8075e94fa71c75fee2a2`)
uses a Metal presenter and reconstruction shaders in
`runner/macos_metal_presenter.m`. Its source credits the DKC1 Metal presenter
and graphics shaders. It uploads an already composed CPU framebuffer; it does
not provide an HD material/layer compositor to copy. DKC2's current checkout
(`c9ce402ff4559e72f89a7d1301b6b0cab80e2767`) uses OpenGL in
`runner/desktop_present_sdl.c`. Both sibling checkouts were inspected read-only.

DKC1-HD already displayed completed frames with Metal. Its expensive HD
composition still ran on CPU, followed by frame copies and texture upload.
This change feeds a GPU-composed texture into that existing presenter.

## Implementation and boundaries

`DKC1_HD_METAL=1` opts the Mac HD preview into the new path, requiring the
indexed pack and `DKC1_HD_SCENE_PRELOAD=1`. All 16,434 rasters are uploaded once
into a Metal buffer: 1,229,835,904 bytes. The CPU resident copy remains available
for fallback. The tested Apple M3 Max has 48 GiB unified memory. Allocation or
shader failure retains the CPU path; this experiment does not assume every
Mac can hold the full pack.

The producer captures immutable BG/material identities, scanline palettes and
scrolls, object placement/priority and native pixels in pooled buffers. The
Metal thread never reads live PPU or guest state. Eight retained frame slots
cover queued/current/in-flight work; drops, geometry changes, pause/menu
visibility and timeline flushes release ownership correctly. GPU completion
holds every packet/texture until sampling is finished. Pool exhaustion falls
back to the CPU compositor. A GPU error disables further GPU capture and uses
the original native pixels for already queued packets.

Two compute passes mirror the CPU compositor's integer arithmetic: first the
native-resolution reconstruction oracle, then the 4x HD composition. They
preserve transparency, palette corrections, fixed-color add/subtract/half
math, BG/OAM ordering, scroll wrap, and original-pixel fallback where native
reconstruction does not match. No guest code, logical coordinates, streaming
policy, sprite art or scene capabilities change.

The HD output stays in a Metal texture through scanout; production does not
read it back. Flat, nearest/bilinear/sharp-bilinear, reconstruction and CRT
presentation reuse the existing shaders. Filtered output uses the same
origin-zero intermediate as the CPU-upload path so inset viewport interpolation
and CRT mask/dither phase remain exact. Non-raw screen color models still use
CPU composition/color filtering. F10 continues to toggle original/HD output.

Jungle Hijinxs remains the supported HD scene. Bonus rooms and other levels
still require scene/art coverage; this optimization cannot supply missing art.
There is no claim of whole-game coverage, Windows GPU support, or a promoted
widescreen capability change. Default shared/headless/release behavior stays off.

## Verification

Evidence directory:
`build/hd-slice/precision-v2-level-20260915/performance-investigation-20260916/metal/`.

- `tests/test_hd_metal.py`: actual Metal versus the CPU compositor, 128 immutable
  synthetic frames at 256/342 widths, with translucent/transparent art, overlapping
  OAM, palette changes, add/subtract/half color math, missing materials, native
  mismatch fallbacks, empty object lists, and exhaustion/reuse of all eight slots.
  The CPU state is deliberately overwritten after capture. ASan/UBSan pass.
- The Metal graphics harness compares CPU-upload and GPU-texture inputs across
  twelve filter/display cases, both normal and inset viewports, plus its existing
  repeat/cache invalidation checks: zero pixel mismatches.
- Full Python suite: 263 tests, one platform skip, otherwise passing.
- 36 deterministic replays pass: fresh entry, movement and cache-pressure/reload,
  native/wide, HD off/on, three repeats. All 25,539 eligible GPU frames match every
  CPU output pixel. Guest WRAM/VRAM/CGRAM/OAM/native-frame/audio hashes match;
  all 36 guest/final-HD comparisons also equal the preceding CPU resident build.
  The existing audited native mismatch fallback remains unchanged.
- Native and headless builds pass; existing SDK deprecation/linker warnings remain.
- `verify_hd_scene.py --preload --metal` reads every eligible GPU image back only
  for validation, comparing every 32-bit pixel with the CPU reference. This is
  deliberately excluded from native performance measurements.

Commands:

```sh
cmake --build build/macos --target dkc1_macos dkc1_snesrecomp_headless test_macos_graphics --parallel 6
build/macos/test_macos_graphics runner/macos_graphics.metal
PYTHONDONTWRITEBYTECODE=1 build/hd-slice/upscale-venv/bin/python -m unittest discover -s tests -v
build/hd-slice/upscale-venv/bin/python tools/verify_hd_scene.py ROM OUTPUT --pack PACK --preload --metal --cases fresh run cache-pressure --jobs 2
git diff --check
```

The native timing harness uses serial, interleaved CPU/GPU runs with the same
resident pack, immutable state, controller schedule and warmed-up sampling.
`DKC1_HD_METAL_TRACE` GPU timings include the final display pass; scanout records
`hd_gpu` to prove that the actual displayed frame used GPU composition.

## Measured result and delivery

Three 1,200-frame native CPU/GPU pairs (interleaved order, 60-frame warmup,
raw/nearest/flat, 342x224, identical indexed pack) produced these medians:

| Metric | CPU compositor | Metal compositor |
| --- | ---: | ---: |
| CPU rendering mean | 4.407 ms | 1.848 ms |
| CPU rendering p99 | 7.123 ms | 3.455 ms |
| Total CPU work mean | 5.226 ms | 2.692 ms |
| Submission interval mean | 16.677 ms | 16.674 ms |
| Audio starvations after warmup (all three runs) | 0 | 0 |

CPU rendering cost fell 58.1%. The GPU composition/display command had median
0.806 ms and p99 1.465 ms. There were 5,628 GPU-composed scanouts and no GPU
errors. This is reduced rendering cost at the same 60-Hz guest cadence, not a
claim of doubled game FPS. The route includes unsupported transition/world-map
frames, which preserve the CPU/original fallback. `summary.json` and all six
pacing/scanout traces are retained. An initial run hit the experimental bundle's
pause-after-one-frame default; it was discarded as `discarded-startup-paused`.
The timing harness explicitly sets `DKC1_PAUSE_AFTER_FRAME=0`.

Every final native state matches the pre-change replay:
`4637eb8a80a5fc7e26f9f13d35046e7af7751fdf76d33794d23f2e5a41dc51a9`.

The signed `DKC1 Jungle Full Precision V2.app` now enables preload and Metal
composition. Visible QA verified HD scenery/character/bananas, F10 original/HD,
F7 pause/resume, and 4:3/16:9 resizing. While paused, a size change uses original
pixels until the next completed frame (F8/resume), then HD returns. A traced
instance confirms both GPU HD and original scanouts without GPU errors. The
diagnostic instances were closed gracefully; the final preview was relaunched
at the clean Jungle root, paused after one frame, with no input schedule or
diagnostic logs. The older user's live preview, PID 75642, was left untouched.

ROM SHA-256: `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.
Root state: `7aeaa4e44e714db99af7cbeb8e79e68ee0c072bf9b4e84ba35f5c927e3cc05da`.
Unchanged pack: `486eab820d968ac7015e6647308b5da69e7fce9d7798572e1a3d09d6ea238d76`.
Headless: `4bb9d64041a10bda641a3b884816326e794ed186bbc8d2667877233dbffac0c0`.
Packaged executable: `f3ecbfa72491625b01b70c534c3491fe426d3b4caff6ea1c5a921dc3a2471b0f`.

The preceding CPU resident executable/plist are preserved in `resident-cpu-app/`.
The parent `final-identity.json` and launch environment are updated to the Metal
preview; `final-identity-before-metal.json` preserves the preceding checkpoint.
No art files, ROMs, source oracles, submodules or sibling repositories were
changed by this GPU port, and no commits were made.
