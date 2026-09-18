# Connected Nano scenery: partial left-edge tile

September 16, 2026. Opt-in HD presentation only. No cartridge streaming,
activation, collision, global widescreen policy, save format, or asset changes.

## Reproduction and cause

The tester's latest `balloon-face-20260916/user/quicksave.state` was copied
read-only to `build/hd-slice/nano-level-20260916/polish/left-tile-20260916/tester.state`
before testing. Below, `P` denotes that evidence directory. Jungle entrance
`$0016`, saved global frame 10414, first replay frame 10415, camera (2592,177).
The issue is already visible at relative frame zero. Input is 30 neutral,
90 Right (`080`), 90 Left (`040`), and 30 neutral frames (`P/move.inputs`).

The visible left world coordinate is 2549 in a 342x224 framebuffer. Its partial
32px chunk starts at 2528 and uses ring column 15 in the 512px BG0 ring. The old
lookup lifted ring coordinates using the native camera's period (base 2560),
producing world X 3040. Its wrap condition did not pull that partially visible
cell back to 2528. The intended connected doorway material was therefore lost;
exact aliases/original fallback displayed foliage across the doorway.

This is an HD material identity defect. The source layer itself legitimately
contains foliage; the connected generated art depicts a doorway there. The
texture pack was fully resident, so loading more textures could not repair the
lookup. `runner/dkc1_hd_scene.c:ConnectedChunk` now anchors the horizontal period
at the visible left edge, including its partial cell. Existing per-8x8 source
verification, palette correction, scene guard and bounds rejection remain in
force. Negative world coordinates fail closed. Vertical binding is unchanged.
No blur, replacement art, broad state repair or cartridge changes were needed.

The old state reproduces the rendering defect without evidence of serialized
corruption. Both old and fresh-entry states use the corrected lookup directly.

## Same-pack evidence

`P/baseline-headless` freezes the old executable. `baseline-hd`,
`baseline-original`, and `candidate-hd` retain the 240-frame route, raw exported
WRAM/VRAM/CGRAM, original plane atlases, audit/coverage logs, final machine hashes,
and six-frame image samples. `P/replay.py` and `P/candidate-replay.py` record the
exact capture environment. No baseline evidence was overwritten.

The first candidate frame removes the green rectangle from the doorway and
agrees visually with `P/hd-world-expected.png`, the connected-world art crop.
Only output X 0..43 changes (11 source pixels). The first-frame difference has
18,598 changed HD pixels; no native-center pixel changes. All 40 sampled frames
have an identical HD center between baseline and candidate. See
`P/first-frame-comparison.json`, `P/ab-comparison.json`, `P/candidate-start.png`
and `P/candidate-contact.png`.

Final guest framebuffer, WRAM, VRAM, CGRAM, OAM, WRAM OAM shadow and audio hashes
match the frozen baseline exactly. Those values are retained in
`P/ab-comparison.json`. Source reconstruction has zero mismatches throughout
this exact route. The candidate's first-frame visible BG misses fall from 198
to zero. Six unrelated frames retain 362 summed missing BG samples over the
240-frame wide route; these same six misses exist in the baseline. This is not
full asset-coverage acceptance.

## Validation

The macOS app and headless target build successfully. All 32 HD unit/model tests
pass, including ASan/UBSan and actual Metal tests. `test_hd_preload.c` now checks
wide partial-left lookup on both sides of the 512px camera boundary, correct
right wrapping and negative-world rejection. The existing ownership/source
verification tests remain enabled. `git diff --check` passes.

Exact-state verification repeats three times in native 256x224 and wide 342x224,
with HD off/on: 12 legs and 1,440 raw CPU/Metal frame comparisons. All pass;
gameplay texture reads are zero, guest/HD repeat hashes are deterministic, and
raw GPU mismatch pixels are zero. The native Metal window was also inspected
at the immutable tester root after one neutral frame: the doorway patch is
absent, with the app paused in 16:9/composite.

The fresh-entry suite passes all 12 legs and 12,204 raw CPU/Metal frames,
with zero visible BG misses, zero GPU mismatch pixels and zero gameplay reads.
Together the two suites cover 24 replay legs and 13,644 raw GPU frames.
Fresh-entry results are recorded in `P/verify-fresh-route/results.json` using
`build/hd-slice/entry.state` and the 2,680-frame
`polish/connected/full-route.inputs` route. This scene is also tested from an
earlier clean entry rather than only the tester state. The native route reaches
the world map; the wide route includes death/re-entry and is bounded traversal,
not full wide-level completion. This experimental HD lookup does not promote
any global widescreen capability. Other levels, unsupported bonus rooms and the
40-entrance release matrix are outside the validated scope.

## Identities and commands

- ROM: `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.
- Tester state: `9e854885e48b9791c3af01ed762e0edba921905bf8f93bfc2c81c1f61d9528f1`.
- Earlier entry: `7aeaa4e44e714db99af7cbeb8e79e68ee0c072bf9b4e84ba35f5c927e3cc05da`.
- Inputs: `adb64c36488958dac6f9473b0bb541278c1a8674fc827b113f93442c562c1529`.
- Baseline headless: `86cb6a5ea7bf67f2681475d89669d7626700253d2285d34665dfee3a8c263a6f`.
- Candidate headless: `57345dfe9c2438884ce7b4af812442f8be1f406353484d7bad100145d4f5dc92`.
- Unchanged balloon-face pack: `405cb0829a27648a9b16c238460a688428259512ba08a230e9314333627a764c`.
- Signed native executable: `b42fc957b332ee94456765757a19abad585eb7dd3321fd9276d33b286ac65749`.

```sh
P=build/hd-slice/nano-level-20260916/polish/left-tile-20260916
PACK=build/hd-slice/nano-level-20260916/polish/balloon-face-20260916/materials
python3 tools/verify_hd_scene.py "$ROM" "$P/verify-exact-new" \
  --pack "$PACK" --preload --exact-centers --connected-world \
  --metal --polish 65 --coverage --cases replay --state "$P/tester.state" \
  --input-play "$P/move.inputs" --frames 240 --jobs 1
```

For fresh entry, use `--state build/hd-slice/entry.state`, the full-route input
above, `--frames 2680`, and a new output directory. Reports retain input, ROM,
state, executable, pack, framebuffer and memory hashes plus coverage counts.
Coverage remains a separate diagnostic, not an automatic zero-miss gate.

## Delivery and cleanup

`build/macos/DKC1 Jungle Edge Fix.app` is a separately signed preview with its own
`P/user` directory and a copy of the tester quicksave. The prior Balloon Faces
app and original tester save are preserved. All unchanged `.dkhd` assets are
hardlinked; mutable manifests are copied. No shared asset was rewritten.

The new preview is left paused at the preserved spot after one neutral frame,
with no input playback schedule. F7 resumes and F12 restores its private save.
`P/app-build.json` records the bundle environment and visible QA. No commit or
staging was performed. Remaining general coverage limitations are tracked in
`hd-environment-nano-coverage`; this narrow fix does not close that issue.
