# Running and background pacing audit — September 18, 2026

**Result: the tested frame generator does not produce uniformly smooth
background motion.** Pixel correspondences prove integer steps and repeated
parallax positions independently of host timing. Separate undumped runs also
contain occasional real-submission hitches. Earlier clean timing samples do
not establish a universal zero-drop result.

This pass adds a read-only motion diagnostic, its tests, and this evidence
record. It does not change the game, frame generator, presenter, or executable.
The user is independently fixing Windows GDI/DWM scheduling, priority, DPI,
scaling and pixel aspect; those changes are outside this pass. Results below
identify the archived tested executable, not any subsequent user build.

## Inputs and identity

Private evidence is under `build/background-pacing-20260918/`; ROMs, states,
images and captured inputs remain ignored and are not committed.

| Item | SHA-256 |
| --- | --- |
| Supported USA ROM | `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15` |
| Archived primary `baseline.exe` | `d84045e510aef95585ef3973b6674c492657baf85b5a2d325ba47fe5b20bfdb5` |
| Tools executable used for manual clearing | `2b21465777d42e0f297ede1700b319e6f3d710563c5e1bcd767798a7605293c1` |
| Preserved user save `cleared-root/user.state` | `08bab0286deeed59b5eb5dd331812fb93114d2d64edc28bc7d63db7722dc2537` |
| Independent fresh-entry `cleared-root/fresh.state` | `241406153eb729657b237d04bdbd205921e8b11f97df3b50fa978d9ddba59ffa` |
| Cleared run input | `94ad82608764793323c6883d00d30987957ebe4a8bac1623fb67f15350b8c763` |

The user's F11 snapshot was copied outside the normal slot directory before
testing. Its sidecar records host frame 2640 / guest frame 10172, Jungle,
mode $0000, level $0000, entrance $0016. DK starts at X=1092; camera is near
(963,150). The 570-frame controller-only schedule is:

```text
000 * 30
042 * 240
000 * 30
082 * 240
000 * 30
```

Masks are hexadecimal; bit $002 is Y/run. This supplies four seconds of
running in each direction. DK remains alive and uses running animation 2
through the sampled stretches. It avoids the earlier `hd-run.dks` route's
enemy interruption, which made its later segment unsuitable as left-running
evidence. Native timing is 256x224; wide is 342x224 (the host's 16:9 setting).

The independent branch starts from the clean map root identified in
`FRAMEGEN_REVIEW.md`, replays its 319-frame entry plus the first 2640 recorded
manual inputs, then executes the same running schedule. This reaches the same
cleared scene but is **not byte-identical to the user's saved machine**: DK X
is 1093 instead of 1092, and ambient actors differ. It is graded separately.
No state-repair pass was added, and no corruption diagnosis is inferred from
that difference. Three repetitions from the exact user save are identical.

## Spatial evidence

`Dkc1FrameGenBuildPlan` uses integer `-Halve(delta)` for each scanline's scroll.
`Halve` truncates toward zero. Characters have fractional sampling, but the
background PPU pass does not. A three-pixel background advance therefore
becomes two pixels then one, rather than two 1.5-pixel advances. A one-pixel
advance necessarily retains one endpoint in one of the two presentations.
At 60 Hz the real background still uses the cartridge's integer parallax
positions. Widescreen edge glide can add an integer presentation-bias change,
but the defect also occurs far from the wall where that bias is zero.

The new `tools/analyze_bg_frame_steps.py` identifies visible textured pixels
using independent same-frame BG captures, then follows exact RGB matches
through display F, midpoint F+0.5, and display F+1. It rejects insufficient
texture or ambiguous matches. It does not derive its answer from `Halve` or
from the timing log. Capture metadata aligns the four-frame display delay.

| Exact-state stretch | BG1 screen steps | BG2 screen steps |
| --- | --- | --- |
| Run left, source 120 onward | `+2,+1,+2,+1,+2,+1,+2,+1` | `+1,+1,+1,0,+1,+1,+1,0` |
| Run right, source 420 onward | `-2,-1,-2,-1,-2,-1,-2,-1` | `-1,-1,-1,0,-1,-1,-1,0` |

Each quoted sequence uses equal half-frame presentation phases. Both layers
have 31 accepted measured transitions per direction. Positive movement means
the scenery travels left to right. The first repeated BG2 position in the
quoted left-running window is source 121.5 to 122. At the real 60 Hz phases,
BG1 usually advances 3 pixels while BG2 alternates 2 and 1. Thus the 120 Hz
path contains a spatial cadence error even with perfect submission timing.

The independent fresh-entry branch reproduces BG1 2/1 steps and BG2 repeats
in both directions, with a different parallax phase. BG3 did not expose enough
unambiguous visible texture for this matcher; it is **ungraded**, not passed.

Evidence: `cleared-left-motion.json`, `cleared-right-motion.json`,
`cleared-left-motion-60.json`, `cleared-fresh-left-motion.json`,
`cleared-fresh-right-motion.json`, corresponding `cleared*-layers-*` raw
planes/masks, and `cleared*-scroll.jsonl` camera/PPU traces. The earlier short
exact/fresh-entry tests reproduce the same class in `left-motion.json` and
`fresh-left-motion.json`.

```powershell
python tools/analyze_bg_frame_steps.py --capture build/background-pacing-20260918/cleared-run-wide --layers build/background-pacing-20260918/cleared-layers-120 --start 120 --count 16 --output build/background-pacing-20260918/cleared-left-motion.json
# Use --cadence 60 to inspect only real display phases.
python -m unittest discover -s tests -p test_analyze_bg_frame_steps.py -v
```

Three exact-state captures have identical raw/display/mid images, full WRAM
and OAM dumps, and final WRAM/VRAM/OAM checkpoint hashes. Frame generation off
versus on preserves every raw image and guest hash over all 570 frames; all
composite mismatch counters are zero. The private scripts and
`cleared-captures.json` record this verification. These integrity passes do
not imply smooth background motion.

## Timing evidence

Timing is collected in separate runs without image/memory dumping. Each row
below combines three 570-frame runs, excluding the first 59 frames per run.
The p99 column is the worst per-run p99; these are GDI submission intervals,
not monitor scanout timestamps.

| Mode | Samples | Worst p99 ms | Maximum ms | Overruns | Midpoints | Audio starvations |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Native 60 | 1533 | 16.773 | 50.186 | 2 | 0 | 1 |
| Native forced 120 | 1533 | 16.727 | 16.801 | 0 | 1533 | 0 |
| Wide 60 | 1533 | 16.684 | 17.049 | 0 | 0 | 0 |
| Wide forced 120 | 1533 | 16.722 | 16.973 | 0 | 1533 | 0 |

All four groups have zero midpoint-skip, audio-drop and internal-underflow
counters. The median is approximately 16.667 ms. The cleared wide samples
are clean at the software scheduling boundary, but additional samples are
not: `fresh-running-timing-60` frame 421 records 50.0322 ms, and the earlier
`baseline-timing-1-120` frame 154 records 50.0104 ms. The latter's left-running
segment is invalidated by an enemy interruption; its timing event remains a
real observation, not evidence of a background-motion cause.

Two useful details for the user's separate host-pacer work:

- Fresh-entry wide 60 frame 421: 16.9303 ms work, 2.277 ms lateness,
  **31.0844 ms additional wait**, then a 50.0322 ms submission interval.
- Cleared native 60 frame 455: 18.5166 ms work, 3.6026 ms lateness,
  **29.9272 ms additional wait**, then 50.186 ms between submissions.
  Frame 322 also has a 33.8068 ms interval.

The second event spends 14.0217 ms inside the measured audio-pump phase; the
fresh-entry event spends 11.3787 ms there. This locates elapsed time, not its
cause: scheduling/preemption or an OS audio call could account for it. These
logs do not prove a cartridge audio-generation defect. Overrun re-anchoring
adds further delay after the work finishes. Exact rows and per-run summaries
are retained in `cleared-timings.json` and the named pacing logs.

## Visible checks and limitations

`live-cleared/window/window-68412.png` captures the real application window
during rightward running. That process completed 571 frames (including one
neutral frame after restoring the preserved user root), paused, and closed
gracefully with exit 0. Its identity/result JSON records the cleanup.

The attempted desktop video is **rejected as timing evidence**: foreground
acquisition failed and the recorder captured only 469 frames over 8.5 seconds
despite requesting 60 fps. It is not offered as a smooth-output demonstration.
The independent image dumps and undumped timing logs above remain valid.
This machine's timing headers report approximately 60 Hz; no physical
120/240 Hz or Windows scanout oracle was tested. No further live runs were
started after the user supplied their concurrent host-fix list.

The motion-diagnostic tests pass for signed movement, repeated frames,
vertical offsets and fail-closed ambiguity/insufficient texture. Runtime
sources and binaries were not edited or rebuilt for this diagnostic pass.
Other levels, HD/Dixie assets, other widescreen geometries and the complete
40-entrance matrix remain outside this audit. Background sampling and observed
host timing outliers remain open issues, not fixes claimed by this report.

Subsequent implementation and combined presenter testing are documented in
[BACKGROUND_SMOOTHING_REVIEW.md](BACKGROUND_SMOOTHING_REVIEW.md). This report
retains the original negative evidence and executable identity.
