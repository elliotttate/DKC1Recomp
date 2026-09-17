# Connected Nano scenery: saved-state scrolling review

September 16, 2026. Private, opt-in HD presentation only; no cartridge, collision,
activation, global widescreen capability or default release changes.

## Reproduction and cause

The tester's `sprite-matte/user/quicksave.state` (09:57:17 local) is preserved
read-only as `build/hd-slice/nano-level-20260916/polish/popin-20260916/tester.state`.
Below, `P` denotes that `popin-20260916` directory. Jungle entrance `$0016`, saved
global frame 10070, initial camera (2191,222). `P/right.inputs` is exactly 30
neutral frames, 180 Right frames (`080`), then 30 neutral frames. No save was
rewritten. The original tester state reproduces a renderer defect, not known
serialized corruption requiring repair.

Baseline and candidate use the same existing clean-edge pack for the isolated
A/B. Baseline flower/foliage detail changes within scenery already on screen.
No gameplay texture reads occur: the art was resident. When a 32x32 connected
chunk contained an unresolved 8x8 source tile, its exact mixed-cell alias could
replace the entire connected material. Identical original pixels occur at
multiple world positions with different generated detail, so replacing the
whole material changed already verified scenery as streaming progressed.

`runner/dkc1_hd_scene.c` now retains the connected material and its source-valid
mask, with a separate fallback material for unresolved subtiles. Both materials
are pinned across cache lifetime. CPU and Metal select the same per-subtile
material and retain canonical palette correction. The immutable GPU ABI adds
`bg_fallback_material[3][256]`; the delivered shader/header match the build.
Original reconstruction still uses current source pixels. A whole-chunk swap
was rejected because it discards valid world identity; extra preloading cannot
solve this already-resident selection error.

A separate one-frame miss at relative frame 69 (41 native samples) was covered
with 136 exact mixed-cell aliases assembled from existing connected Nano art.
There were zero unmatched subtiles and no new generation calls. Provenance is
`P/materials-v2/boundary-provenance.json`. This pack extension is separate from
the same-pack renderer A/B.

## Motion evidence

`P/baseline-hd`, `P/baseline-original`, and `P/candidate-hd` preserve 240-frame
replays, six-frame image samples, raw source WRAM/VRAM/CGRAM, audit/coverage logs,
and final machine hashes. PPU scroll aligns adjacent samples. Only pixels whose
original RGB is unchanged throughout a 5x5 neighborhood are counted; HD changes
must exceed 40 in at least one RGB channel. `P/temporal-comparison.json` records
the per-pair results; `P/pop-comparison-180.png` marks changes in magenta.

- Total sampled stable-source changes: **29,714 -> 4,914** (83.5% reduction).
- Relative frames 180 -> 186: **8,816 -> 258**.
- 150 -> 156: **4,448 -> 0**; 198 -> 204: **2,268 -> 0**.
- Earliest sampled pair improved by this fix: 114 -> 120, **2,048 -> 1**.

These are sampled diagnostics, not a precise first-bad-frame claim. Remaining
counts include independently scrolling parallax and animated objects; they are
not proof of 4,914 remaining defective pixels. Still images and native-window
motion were reviewed alongside this metric. The original and candidate final
framebuffer, WRAM, VRAM, CGRAM, OAM, OAM shadow and audio hashes all match.

## Validation

All 32 HD unit/model tests pass (`P/unit-tests.log`), including partial source
masks, primary/fallback ownership, cache pinning, actual Metal output, palette
correction, immutable packets and ASan/UBSan coverage.

| Suite | Replay legs | Raw CPU/Metal frames | Visible BG misses |
| --- | ---: | ---: | ---: |
| Exact tester state, 240 frames | 12 | 1,440 | 0 |
| Earlier clean entry, 2,680-frame route | 12 | 12,204 | 0 |
| Fresh boot/entry and cache-pressure/reload | 24 | 23,559 | 0 |
| Total | 48 | 37,203 | 0 |

Each case repeats three times with HD off/on, native 256x224 and wide 342x224.
Guest and HD repeat hashes are deterministic; every raw GPU comparison is exact,
and all enabled legs have a complete preload and zero gameplay texture reads.
The fresh-entry full-route hashes also match all 12 pre-change route results.
The cache route validates 966 frames after reload; the wide run exercises 56
actual evictions. Synthetic ownership tests separately cover full saturation.
Coverage totals are native visible samples, not a proof of subpixel alpha/art
quality. Wider routes retain their existing reconstruction fallback; this fix
does not claim to remove those unrelated oracle mismatches.

The native full route reaches the exit/world map. The same wide input route
includes death and re-entry and remains a bounded traversal (maximum X 1698),
not proof of wide full-level completion. Missing object/composite art remains:
the exact route reports 56,796 native / 60,034 wide missing OBJ samples summed
over 240 frames, including visible original-looking enemy poses. These counts
are not unique assets. Unsupported bonus rooms, other levels and the complete
40-entrance matrix remain outside this HD fix's acceptance scope.

## Identities and reproduction

- ROM SHA-256: `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.
- Tester state: `279a87174f94b66d1a7ed4d47ca1772d130ef47655f8131f4c21651aea742e4a`.
- Earlier entry: `7aeaa4e44e714db99af7cbeb8e79e68ee0c072bf9b4e84ba35f5c927e3cc05da`.
- Right-input SHA-256: `39718af4a55afed66a7795dcd1c5c27234451b26939efd22bc449e64572fb3c7`.
- Baseline headless: `fc891ee08f4c6636da168e5170fb0460f2db4348d54c44a1331cd55847a557cc`.
- Candidate headless: `86cb6a5ea7bf67f2681475d89669d7626700253d2285d34665dfee3a8c263a6f`.
- Same-pack A/B: `c0b62055f6754f2e4502b54764e377daf680ddbab008a6e57306034f395a18bd`.
- Final pack: `7206f51ff6995c5d897c72528002bade45492b96b3bd55c361fb76a0a6a2fecb`.
  23,253 materials, 1,676,725,888 resident pixel bytes.

Full individual memory/image hashes and commands are retained in the three
`P/*-verification/results.json` reports and per-run stdout. Build log is
`P/build.log`. Exact reproduction from repository root:

```sh
P=build/hd-slice/nano-level-20260916/polish/popin-20260916
python tools/verify_hd_scene.py "$ROM" "$P/exact-verification-new" \
  --pack "$P/materials-v2" --preload --exact-centers --connected-world \
  --metal --polish 65 --coverage --cases replay --state "$P/tester.state" \
  --input-play "$P/right.inputs" --frames 240 --jobs 1
```

For clean-entry traversal, omit `--state`, use
`build/hd-slice/nano-level-20260916/polish/connected/full-route.inputs` and
`--frames 2680`. For boot/reload, replace replay/input/frame arguments with
`--cases fresh cache-pressure`. Use a new output directory to preserve evidence.
`verify_hd_scene.py --coverage` reports misses independently; it does not turn
all coverage misses into a generic pass/fail assertion.

## Native delivery and cleanup

`build/macos/DKC1 Jungle Stable Scrolling.app` is a separate signed preview with
its own `P/user` directory and a copy of the tester quicksave. Its executable
hash is `4eb0d14d52109260d19b569195b21e6f84da9d4954a8e94afe1443f47a62feb5`.
The existing Clean Edges app and tester save remain intact. The final preview
starts paused after one neutral frame from the immutable tester root, without
an input playback schedule. F7 resumes; F12 restores its private quicksave.
The rightward end scene and restored starting scene were inspected in the native
window. `P/app-build.json` records the exact bundle environment and cleanup.
No commits were made and no generated/private game assets are staged.

A later partial-left ring-wrap lookup defect has a separate same-pack A/B and
native preview in [HD_NANO_LEFT_EDGE_REVIEW.md](HD_NANO_LEFT_EDGE_REVIEW.md).
