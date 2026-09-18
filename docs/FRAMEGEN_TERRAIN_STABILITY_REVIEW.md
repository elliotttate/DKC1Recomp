# Frame generation: terrain and pickup registration, September 18, 2026

The user reported shaking around the KONG letter and the barrel hatch during
the cleared Jungle running sequence. Isolated layers show the hatch in BG1
and the K letter in OBJ. This is a presentation error, not cartridge state
corruption or evidence of dropped display frames.

## Cause and correction

The eight-interval background filter moved the terrain away from its native
camera positions. Static OBJ artwork still followed native integer camera
positions at real 60 Hz phases. Fine terrain texture consequently changed
between sharp and bilinearly softened phases, and the pickup slid relative to
the ground. Source frame 120 (host display 124) is the first confirmed bad
frame in the measured 120–135 window; this is not a claim about the earliest
occurrence in the entire route. Independent image registration measures
0.875 native pixels peak-to-peak relative drift over that window.

The frame capture now carries the terrain-plane identity from the existing
streamer/tilemap-base lookup, independent of the selected display aspect.
It does not hardcode BG1. That plane retains exact native real-phase scroll
positions; generated half phases use the same two-endpoint interpolation as
world OBJ. Only the other parallax planes retain the eight-interval filter.
Unknown terrain ownership keeps endpoint interpolation for all planes, and a
terrain-identity change clears pose history. The four-frame lookahead,
character-pose interpolation, composition oracle, bounds checks and worker
remain intact. All changes remain inside optional frame generation.

Moving only the letter would leave the fine terrain's changing blur. Shifting
the entire finished image would also shift HUD elements and parallax, requiring
a broader renderer change. Keeping shared terrain/OBJ endpoints resolves the
demonstrated registration error without either change. This deliberately
retains cartridge camera quantization at real phases; it is not a promise of
a perfectly uniform camera trajectory in every scene.

## Identity and reproduction

Private evidence: `build/framegen-stability-20260918/`.

- ROM SHA-256: `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.
- Primary executable: `a37133f820d3bcca37f9acebde6789072c5067903633def26d32c6b85561bc02`.
- Tools executable: `b71e8f8b153610c5afb99f9074efe63e2cc7c49ac508e5b9a0d0b78b5dcabd5e`.
- Exact root: `build/background-pacing-20260918/cleared-root/user.state`,
  SHA-256 `08bab0286deeed59b5eb5dd331812fb93114d2d64edc28bc7d63db7722dc2537`.
  Guest frame 10172, level 0, entrance 22, camera 963/150.
- Independent controller-entry root: `cleared-root/fresh.state`, SHA-256
  `241406153eb729657b237d04bdbd205921e8b11f97df3b50fa978d9ddba59ffa`.
  Its entry provenance is in `BACKGROUND_PACING_REVIEW.md`.
- `cleared-run.dks`: 30 neutral, 240 run-left (`042`), 30 neutral,
  240 run-right (`082`), 30 neutral; 570 guest frames.
  Original route SHA-256: `94ad82608764793323c6883d00d30987957ebe4a8bac1623fb67f15350b8c763`.

`python build/framegen-stability-20260918/captures.py` records executable,
input and root hashes with every run. Its output directories must be new.
Every run checkpoints final WRAM/VRAM/OAM, restores the immutable root,
advances one neutral frame, pauses and closes gracefully. Roots are unchanged.

## Results and limits

`captures.json` passes three byte-identical 60 Hz wide replays and three
byte-identical forced-120 software replays, plus serial/worker equality,
native-width and fresh-entry checks. Each has 570 raw/display frames and zero
composition-oracle mismatches. All raw images and recorded WRAM/OAM bytes,
and final WRAM/VRAM/OAM hashes, match the corresponding earlier oracle.
`raw-regions.json` separately hashes both margins and the native center.
Generated poses remain active in 375/570 wide 60 Hz frames and 533/570
forced-120 frames. Native on/off equality concerns raw cartridge output;
optional smoothed display pixels remain the separately authorized change.

`letter-terrain-registration.json` measures zero relative drift after the fix,
versus 0.875 native pixels before it, while independently checking the K
letter's center pixels against the raw image. `pixel-registration.json` and
`secondary-pixel-registration.json` use actual RGB against isolated layers,
not proposed runtime offsets. The measured 120-frame windows in both running
directions advance BG1 by 1.5 pixels and BG2 by 0.75 pixels per generated
phase. The fresh branch also passes these spatial checks at 60 Hz.
BG3 lacks enough visible texture for a scene-specific grade.

The actual primary window was inspected in
`final-wide-60-1/window/window-26372.png`, with identity in its sidecar.
`terrain-before-after-60fps.mp4` contains 570 generated source images at
60 fps, 9.5 seconds, before left/after right. It is an image-quality comparison,
not a recording of physical scanout.

Primary and tools builds and the relevant 17 Python/model tests pass. The
new C regression exercises fixed OBJ against fine terrain during acceleration,
stops, reversals, vertical motion, scroll wrapping, BG1/BG2 ownership and
unknown ownership, at real and half phases. It checks pixel-exact real frames
and invariant relative positions; existing pose, priority, fallback and
serial/worker tests still pass. The earlier full Python suite has eight
environment errors from missing POSIX `cc`, documented in the pacing review.

Validated gameplay scope is this Jungle route at 256x224 and 342x224. The
complete 40-entrance matrix, other gameplay layouts, and physical 120 Hz
delivery have not been certified. This does not promote frame generation to
the default or close the separate Windows pacing issue. No supplied save
state was rewritten to repair historical corruption.

A separate 2,280-frame undumped sample (`short-timing.json`) failed timing
acceptance: two CPU overruns and ten audio starvation events. Its longest
1,609 ms submission interval spent 1,602 ms in the Windows message pump;
another frame spent 139 ms in the audio phase. The retained phase diagnostics
do not fully identify the underlying waits. These are not image-registration
failures, and no zero-hitch claim follows from the successful pixel tests.
