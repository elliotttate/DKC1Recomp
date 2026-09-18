# Frame-by-frame animation audit - 2026-09-18

The Windows frame generator did not smooth held character poses at 60 Hz.
It only attempted a new image between adjacent native frames for 120 Hz.
The corrected optional presenter interpolates held OAM artwork using four
frames of look-ahead and supplies both integer and half-frame animation phases.
The user explicitly accepted approximately 67 ms of additional visual delay.
F10 / View > Smooth Animation / Frame Generation enables it; default stays off.

## Fault and correction

In the exact stable Jungle replay, the first observed missing intermediate
pose is source frame 3: DK holds raster hash 1438693185 on frames 2, 3 and 4,
then changes to 1161481482 at frame 5. The original 60 Hz images retain those
holds. Frames 17-19 likewise hold running raster 419407017 before frame 20.
The old OAM address/attribute matching could also miss artwork uploaded into
the same VRAM tile address.

The new host-only queue tracks connected OAM images and actual decoded VRAM
artwork. It searches the known future pose, estimates bounded local motion,
and samples fractional animation phases at 60/120 Hz. An OBJ-disabled native
PPU pass supplies the background; independent re-composition must reproduce
the original surface exactly before any replacement is allowed. Every live
PPU byte is restored after scratch rendering; there is no extra simulation.
Uncertain overlap, unsupported rendering modes and failed re-composition keep
raw artwork. Long holds interpolate their last four frames only, and static
poses remain static. Generated poses can soften detail: this does not create
new original authored assets.

A second fault appeared after adding pose smoothing: the synchronous midpoint
then real-frame wait left the producer only half a period to compute its next
frame. The standard widescreen build submitted only 31/151 measured midpoint
slots in `verified-wide/120-pipeline-timing`. The repaired presenter submits
real F, then an immutable copy of F+0.5 from a worker. The producer has the full
16.67 ms. The worker cannot access game state or producer pixel buffers and
is canceled for history reset, resize, fullscreen/menu changes, pause and exit.
Changing cartridge animation speed or advancing simulation was rejected because
it would change gameplay. Adjacent-frame blending cannot fill earlier held
frames; bounded look-ahead supplies the needed future anchor.

## Identity and reproduction

All private evidence below is relative to `build/framegen-audit-20260918/`.
No ROM, private state, generated code or extracted artwork is added to Git.
The checkout was already dirty at `cb8c322`; unrelated work is preserved.

| Input/build | SHA-256 |
| --- | --- |
| Supported clean USA ROM | `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15` |
| Original desktop baseline | `d74fd0633c48e1fcf432a2186e6509de6e84c2b047a96e1abfaa00e7e107475f` |
| Corrected `build/dkc1_desktop.exe` | `d84045e510aef95585ef3973b6674c492657baf85b5a2d325ba47fe5b20bfdb5` |
| Corrected `build/dkc1_desktop_tools.exe` | `2b21465777d42e0f297ede1700b319e6f3d710563c5e1bcd767798a7605293c1` |
| Stable Jungle root (`fresh-stable/mid.state`) | `134cd9f7b57353390e057a603e2f62cec3a8184d52134a1d9bf0ca6427e23e4b` |
| Clean map root (`fresh-entry/map-root.state`) | `0c14db2aa32acd86c01e77bc34242e1d978a2913d326ce98eef51c29df985a15` |

The stable root was reached from clean boot via `recipes/route_jungle.dks`,
followed by 200 neutral frames after the entry predicates. Its guest frame is
7532, mode $0000, level $0000, entrance $0016, camera (85,174).
The exact input is `0 * 15; 80 * 90; 40 * 90; 0 * 15` (hex masks, 210 frames).
A supplied pre-existing `stock_midlevel.state` proved to be a non-gameplay
state; it was preserved, but is not misreported as character-animation evidence.

The independent fresh-entry branch starts from the controller-reached map,
uses the recorded 319-frame `fresh-entry/resolved.dks` to enter Jungle and
settle, and never substitutes the stable mid-level snapshot. Its original
predicate recipe and root-generation logs are next to it. No historical
serialized corruption was diagnosed or rewritten by this presentation change.

```powershell
.uild_host.bat
.uild_host_tools.bat
python tools/verify_framegen.py --exe build/dkc1_desktop.exe --rom <rom> --state build/framegen-audit-20260918/fresh-stable/mid.state --output <new-native-dir> --wide 0
python tools/verify_framegen.py --exe build/dkc1_desktop.exe --rom <rom> --state build/framegen-audit-20260918/fresh-stable/mid.state --output <new-wide-dir> --wide 1
python tools/verify_framegen.py --exe build/dkc1_desktop.exe --rom <rom> --state build/framegen-audit-20260918/fresh-entry/map-root.state --input build/framegen-audit-20260918/fresh-entry/resolved.dks --frames 319 --output <new-entry-dir> --wide 1
python tools/analyze_pacing.py build/framegen-audit-20260918/accepted-long-timing/pacing.jsonl --warmup 59 --json
```

## Verified results

`accepted-native/report.json`, `accepted-wide/report.json` and
`accepted-fresh-entry/report.json` pass. Each contains an off capture, three
independent 60 Hz captures, three forced-120 captures and separate undumped
timing runs. Across 4,434 enabled-versus-off frame comparisons, every raw image
and every dumped WRAM/OAM byte matches. The final checkpoint also compares
WRAM, VRAM, PPU OAM and WRAM OAM hashes. Generated display/mid images also match
byte for byte across three repeats. All composition mismatch counters are zero.
Whole-frame equality includes left margin, native center and right margin.
The original pre-fix baseline's 210 native frames match the corrected off
frames exactly (`original-native-comparison.json`).

| Sequence | Raw-frame sequence hash | Full WRAM dump SHA-256 |
| --- | --- | --- |
| Native stable | `3dcf9981920c12c24b83665f35c1a1713bd9d093ced3f44f52e22a116096d8c1` | `a095b70b9ad408788d6dc0b7760b9b7b921afe5f9a3b4f9ed3118ef1eae79bed` |
| Wide stable | `888fcfc2f8ac033a5a0d68840ac1788916369acd6f4d2da397f04b875c6edfb9` | `f019e917660ec1afd973b4b77ffd911a0d8d2c1f003a5557e8a7963dd3aa64b1` |
| Wide fresh entry | `a1766feeb3780f18fe02fd4372e9bea35781502b76ffc50716369906ca476b95` | `0aa1385169469f61acf3a2b10403fee66deffa8eb40c262dbe5b3c554ccfb7c8` |

A sequence hash is SHA-256 over the concatenated binary SHA-256 digests of
chronologically sorted raw PPM files. `pose-frame-strip.png` compares original
and generated DK artwork at 17,17.5,...,20. Frames 17 and 20 retain exact DK
keyframes; generated 18 and 19 change the held body, and half phases add the
intervening motion. The C oracle separately verifies strictly ordered pose
centroids, half-pixel translation of unchanged artwork, exact endpoints,
queue warm-up/reset, changing VRAM at the same OAM address, color math and
unsupported-surface fallback and a capture interrupted by debug inspection.
Unqueued ring slots are rejected after inspection, and F8 stepping displays
the exact native frame instead of the delayed smoother output.

| Undumped sequence | Steady frames | Midpoints submitted | Real submit p99 ms | Real-to-mid p99 ms | Mid-to-real p99 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Native 60 | 151 | 0 | 16.677 | - | - |
| Native forced 120 | 151 | 151 | 16.695 | 8.324 | 8.385 |
| Wide 60 | 151 | 0 | 16.696 | - | - |
| Wide forced 120 | 151 | 151 | 16.691 | 8.323 | 8.384 |
| Fresh entry wide forced 120 | 260 | 260 | 16.681 | 8.322 | 8.379 |
| Longer wide forced 120 | 1,341 | 1,341 | 16.712 | 8.324 | 8.402 |

Every listed timing window has zero mid skips, CPU overruns, audio starvations,
drops and internal underflows. The long schedule is ten repeats of
`0 * 60; 80 * 40; 40 * 40`, 1,400 total frames, discarding the first 59.
GDI submission timing is measured, not physical monitor scanout.

Both standard build scripts completed successfully. The new C interpolation
suite and six pacing-analysis tests pass. Full unittest discovery exercised
272 tests, including the added frame-generation and v4 timing tests:
eight errors are existing POSIX `cc` invocations unavailable on this Windows
host, and nine tests skip unavailable compiler, Metal or private-state coverage.
SciPy was installed only under the private audit directory to run the relevant
Python tests. This is not reported as a green full-suite gate. `git diff --check`
passes. Primary/tools build and unit logs are in the evidence root.

Visible QA used the real tools window, PID 52412, with the same root and forced
120 Hz path. `accepted-window-qa/playing/window-52412.png` shows the game and active
67 ms smoother. `accepted-window-qa/restored-paused/window-52412.png` shows the root
restored (one neutral frame after load), paused, input $0000. The route completed
at host frame 361 / guest frame 7533, then closed gracefully with exit 0.
`accepted-window-qa/identity.json` records process, executable and immutable-root identity.

## Limits and remaining coverage

This machine reports 60.00024 Hz. Physical 120/240 Hz scanout is unverified.
The proven scenes are Jungle gameplay, its map-to-level transition, and load/
reset behavior at native 256x224 and wide 342x224. Bosses, underwater, caves,
vertical/rope layouts, bonuses, Dixie/HD art, macOS and the complete entrance
matrix have not been certified. Ambiguous overlaps and unsupported PPU states
intentionally retain original artwork; this does not promise 60 distinct poses
for every actor in every scene. Fine detail can soften in generated frames. Background scroll in the existing
PPU midpoint pass remains quantized to native pixels; odd-pixel camera steps
do not have the fractional sampling now used for tracked characters.

That background limitation was subsequently addressed by the optional
per-layer sampler in [the background smoothing review](BACKGROUND_SMOOTHING_REVIEW.md).
Its exact/fresh, native/wide evidence and separate 60 Hz scanout results
supersede this audit's background result, without expanding full-game coverage.

The widescreen policy and cartridge domains were not changed or promoted;
the complete 40-entrance floor and retained/cold transition sentinel were not
rerun for this default-off presentation feature. Fresh-entry, state-reset and
raw-center gates above are the actual executed coverage. The local byte-exact
reference atlas is unavailable in this checkout; no new cartridge semantics
or disassembly claims are inferred from generated code.
