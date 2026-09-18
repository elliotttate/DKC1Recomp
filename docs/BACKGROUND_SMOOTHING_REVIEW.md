# Fractional background smoothing, September 18

**Follow-up:** user-visible KONG-letter/terrain wobble exposed a registration
gap in this initial acceptance. The terrain plane now retains native 60 Hz
camera endpoints and uses the same two-frame midpoint as world objects;
only parallax planes retain the eight-interval filter. See
[terrain registration review](FRAMEGEN_TERRAIN_STABILITY_REVIEW.md) for the
pixel evidence, revised tests and remaining limits.

The user narrowed acceptance to the available **60 Hz display**. This review
covers the cleared Jungle Hijinxs running route at native 256x224 and widescreen
342x224, with smooth animation enabled. Physical 120 Hz is not certified.

## Change

`runner/dkc1_bggen.inc` samples the three Mode 1 background planes separately.
It consumes priority/palette-index planes from the engine's existing overlay
extraction API during the already-existing scratch BG passes. It does not edit
the engine submodule, run additional emulation, alter cartridge scroll, or infer
missing side art. The complete live PPU is restored after the scratch render operation.

A centered eight-interval filter removes whole-pixel scroll quantization using
the four-frame look-ahead already accepted for pose smoothing. Each layer keeps
its own scroll trajectory. Fractional sampling preserves transparency, tile
priority, sprite occlusion, color windows and applicable color math. Samples
outside proven bounds retain native output; unsupported mosaic, spatial BG
windows, unequal math screens and unproven isolated composition fail closed.
Native raw output remains the independent oracle. Only the optional smoothed
display changes the native center, as explicitly requested for interpolation.

The original composition must reproduce every raw pixel before the generated
surface is accepted. The isolated BG planes have an additional composition
check. High/low-priority samples from the same layer are mutually exclusive,
preventing background leakage where a fractional sample crosses tile priorities.

On Windows one persistent worker overlaps background sampling with the native
oracle and pose-flow work. It reads completed immutable frames and is joined
before sprite shading or reuse of any output/storage. It never owns live PPU,
controller, or save-state operations. The worker uses the host's MMCSS Games
class; the serial fallback produces identical images. MSVC speed optimization
is scoped to the interpolation translation unit, including normal `/O1` builds.

All smoothing remains under the existing default-off `DKC1_FRAMEGEN` switch.
`DKC1_FRAMEGEN_BG=0` disables this additional background stage for A/B.
`DKC1_FRAMEGEN_BG_SYNC=1` selects the serial implementation for determinism checks.
`DKC1_BG_MOTION_LOG=<path>` is a default-off diagnostic with source time,
fractional scroll at row 112, processed pixel count, failed extraction rows and
coarse elapsed time. Unchanged opaque pixels can bypass sampling without being
fallback failures. The log is not visual or scanout proof.

## Evidence and reproduction

Private evidence: `build/bg-smooth-20260918/`.

- ROM SHA-256: `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.
- Exact tester root: `build/background-pacing-20260918/cleared-root/user.state`,
  SHA-256 `08bab0286deeed59b5eb5dd331812fb93114d2d64edc28bc7d63db7722dc2537`.
- Independent fresh-entry root: `cleared-root/fresh.state` in the same directory,
  SHA-256 `241406153eb729657b237d04bdbd205921e8b11f97df3b50fa978d9ddba59ffa`.
  Its map-entry/controller-only provenance is in `BACKGROUND_PACING_REVIEW.md`.
  It is a separate branch, not asserted to be identical to the tester state.
- Final primary executable SHA-256:
  `2781b2d4f89671678a177425047a0949fc5958102af2e8a52aab142d30b4f4f5`.
- Route: `cleared-run.dks`, 30 neutral, 240 run-left (`$042`), 30 neutral,
  240 run-right (`$082`), 30 neutral. Long timing runs repeat this four times.
  Exact scripts, hashes, checkpoints and process results accompany each run.

`accepted60-captures.py` and `accepted60-captures.json` reproduce and record the
final on/off, three-repeat, native-width and fresh-entry checks. All three
widescreen runs have identical displayed images, raw images and guest evidence.
Every captured WRAM/OAM byte and raw frame matches the matching disabled run;
the final WRAM, VRAM and both OAM hashes agree. Native-width on/off and the
independent fresh-entry branch also pass. The earlier `captures.json` adds three
forced-120 software replays and an image-identical serial/worker comparison.
No screenshot or smoothed video substitutes for these raw checks.

The real window was inspected in
`accepted60-wide-2/window/window-82580.png` (host frame 60, composite,
provenance off, smoothing on). `identity.json` binds its process, executable,
root and input hashes. The capture tool now temporarily uses per-monitor DPI
coordinates: its old DPI-unaware capture cropped this 225% DPI window to the
top-left. `visible-window/window-40840.png` is rejected for that reason.

One final-rebuild attempt (`final-wide-60-1`) is also rejected: the provenance
overlay became active at raw frame 126, and smoothing was bypassed thereafter.
Its WRAM/OAM/final machine still match, but its raw images are diagnostic-tinted.
The normal-view retry and both subsequent repeats match the earlier candidate.
This was a view-state change, not a gameplay divergence.

## Frame-by-frame spatial checks

`tools/analyze_bg_frame_steps.py --fractional` fits actual displayed pixels to
independently captured native layers, at sixteenth-pixel resolution. It does
not read the runtime's proposed scroll values. It uses final RGB rounding
tolerance, rejects ambiguous texture and carries surviving correspondences.
The C tests separately exercise all three layers, signed and fractional speeds,
priority boundaries, native color math, fallback and serial/worker equality.

The eight-frame windows at source frames 120 and 420 measure both running
directions, at 60 and software 120 output cadence, from exact and fresh roots.
BG1's old half-frame `2,1,2,1` pattern becomes approximately `1.5,1.5`;
BG2's old `1,1,1,0` becomes `0.75,0.75,0.75,0.75`. At 60 Hz BG2 changes from
alternating `2,1` steps to `1.5,1.5`. Both directions pass. Small smooth changes
to 3.125/1.5625 reflect the route's actual changing scroll, not held frames.
BG3 has insufficient unambiguous visible texture in this scene and is not
claimed as visually measured. Its independent synthetic C coverage passes.
See `candidate-1-motion-*-*.json` and `fresh-motion-*-*.json`.

## Timing and limits

Timing runs are serial, undumped, and separate from window capture/encoding.
They use the user's D3D11 flip-model presenter and swap-chain waitable pacing,
with `DKC1_FRAMEGEN=1`. The 2.5-second presenter warm-up happens before frame 1;
the first 59 simulated frames are separately excluded from steady statistics.
The accepted 67 ms smoother history also fills after load. These startup
intervals are not described as uninterrupted gameplay.

The initial long 60 Hz widescreen sample (`timing-wide-60`) records 2,220
displayed presents with exactly one refresh each, zero CPU overruns and zero
audio errors. An earlier forced-120 sample had three late submissions in 2,221
steady frames; the final optimization overlaps the oracle with background work
to increase headroom. Forced 120 on this 60 Hz display is software coverage only.

The disabled control (`timing-wide-off`) records a 432.2691 ms submission gap
at frame 1178 despite only 1.5689 ms measured game work, and one audio starvation.
The delayed DXGI statistics subsequently report the held refreshes. This is a
real timing outlier outside the measured game work, not proof of a cartridge or
background-generation defect. Its precise host/driver/UI cause is not isolated.
Do not erase it or claim that later clean windows guarantee no future hitch.

The final `2781` build's seven undumped samples are in
`accepted60-timings.json`. Every sample has zero CPU overruns, drops and internal
audio underflows. DXGI nevertheless observes the following held refreshes:

| Run | Displayed presents | Extra held refreshes | Longest submit interval (ms) | Audio starvations |
| --- | ---: | ---: | ---: | ---: |
| Widescreen windowed 1 | 2220 | 0 | 20.949 | 0 |
| Widescreen windowed 2 | 2220 | 1 | 30.060 | 0 |
| Widescreen windowed 3 | 2220 | 24 | 399.553 | 2 |
| Widescreen fullscreen | 2220 | 1 | 33.717 | 0 |
| Widescreen debug panel | 2220 | 1 | 31.949 | 0 |
| Fresh entry widescreen | 510 | 0 | 22.646 | 0 |
| Native width | 510 | 0 | 20.121 | 0 |

These results **do not pass a zero-hitch acceptance criterion**. The verification
harness now gates waitable DXGI captures on the displayed-refresh statistics
as well as CPU/audio counters. Newer concurrent Windows presenter diagnostics
are not part of the `2781` executable; their combined retest is pending.

A pinned completed presenter-diagnostic binary (`b49955f12fdfd19a75b149086be479ff274207daedfc12e6ee7c04424f991a43`)
was also exercised. Another visible `dkc1_desktop_candidate` process (PID 23380)
started during its first run. The launcher was stopped during the second run
to prevent further tests; both children completed their root restore and normal
autoclose (host frame 2281, guest 10173). These runs are excluded from isolated
acceptance in `diagnostic60-overlap-rejected.json`. Run 1's two 33 ms intervals
were in the slot wait (26.43/26.75 ms), with work 6.23/6.60 ms, negligible pump,
and no slow message. It did not reproduce the earlier 400 ms unaccounted gap.
This does not prove the source of every historical stall. A concurrent full
source rebuild hit an unfinished interpreter telemetry declaration; no changes
to that separately owned submodule work were made by this fix.

`smoothed-running-60fps.mp4` contains all 570 verified widescreen display frames
at exactly 60/1 fps (9.5 seconds, 1596x896, SNES 7:6 pixel aspect). Its rendered
frames demonstrate the spatial change; encoding them at fixed cadence does
not prove the live display delivered them without a hitch.

The original capture reports counted nonzero `pose_pixels` under the name
`frames_with_generated_poses`; BG work now contributes to those pixels. The
harness distinguishes that from nonzero `pose_actors`. In the actual 60 Hz
wide/fresh captures 375/570 frames applied pose work and 564/570 modified some
display pixels; native applied pose work on 378/570. Unmodified/static or
rejected poses are not falsely counted as generated poses.

Primary and tools builds pass. Full discovery ran 277 tests: eight pre-existing
errors invoke unavailable POSIX `cc`, and nine tests skip unavailable coverage.
The interpolation, fractional image-registration and pacing tests pass. This
is not a green full-suite claim. Logs are in the private evidence directory.

No widescreen policy, streaming, activation, gameplay, generated source or
reference source was changed. Other levels, bonus/vertical/underwater/boss
layouts, HD/Dixie art, other geometries, macOS and the full entrance matrix
remain uncertified. Unsupported rendering retains the accepted native output.
Ambiguous pose overlaps still retain original artwork; this feature does not
promise 60 distinct poses for every actor in the game. Each automated run ends
by clearing the schedule, restoring its immutable root, taking one neutral
frame, pausing and closing gracefully; roots are preserved outside user slots.
