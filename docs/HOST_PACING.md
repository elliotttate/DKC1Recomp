# Desktop pacing and audio continuity

Emulation remains single-threaded and deterministic. Windows presents one
emulated frame per selected compositor interval. Native macOS keeps emulation
at fixed 60 Hz while a separate host-only Metal presenter may scan the same
immutable frame more than once on a higher-refresh panel.

## Frame clock

### Windows

The host reads the display refresh once (the compositor's rational rate from
`DwmGetCompositionTimingInfo`, else the device mode) and chooses the integer
divisor nearest 60 Hz: 60 Hz uses every refresh and 120 Hz every second
refresh. This avoids the slow beat produced by presenting the SNES hardware
rate (approximately 60.0988 Hz) to a 60 Hz desktop. The refresh value only
selects the divisor and the nominal audio request rate; it is never used to
schedule frames.

Frame slots come from the presentation path itself, in one of three modes
recorded as `pacing` in the log header:

- `waitable` (default): the Direct3D 11 flip-model swap chain is created with
  a frame-latency waitable object and a maximum latency of one. The host
  blocks on that object, which is signalled when the previous image has been
  consumed by the compositor or flipped to the screen, then polls the
  controller, emulates, renders and presents with a sync interval equal to
  the divisor. There is no period estimate to drift, no submit lead to tune
  and no vblank timestamp to interpret. Input is sampled after the wait, so
  the controller is read about one millisecond before the present instead of
  a frame earlier.
- `dwmflush`: the GDI fallback blocks on `DwmFlush`, the compositor's own
  frame boundary, and blits immediately afterwards. The GDI update therefore
  lands at the start of the compositor's window rather than racing its
  sampling point.
- `timer`: `DKC1_PRESENT_HZ` overrides, displays with no integer divisor in
  59.5-60.5 Hz, and `DKC1_FRAMEGEN=force` keep the absolute QPC schedule
  with a high-resolution waitable timer and a bounded final spin. A missed
  deadline restarts from now and is never followed by a short catch-up
  frame. The Direct3D presenter uses sync interval 0 here so the timer alone
  sets cadence.

The emulation thread joins the Multimedia Class Scheduler "Games" class
(`thread_priority` in the log header; above-normal priority when `avrt.dll`
is unavailable) and the process opts out of power throttling.

The retired path submitted GDI content one millisecond before an estimated
vblank from a period pinned at start-up. On this machine's displays the
compositor's reported period alternates between two values 84 ppm apart,
its `qpcVBlank` is the upcoming vblank rather than the previous one, and the
GDI update itself took 1.7-2.8 ms with the debug panel, so frames landed on
either side of the compositor's sampling point while the host's own timers
reported a perfect 16.667 ms cadence. See `docs/FRAMEGEN_REVIEW.md` and the
September 18 entry below for the measurements that replaced it.

### Windows presenter

`runner/win32_present.inc` owns presentation. The process is per-monitor
DPI aware (V2 on Windows 10 1703+), so the desktop compositor never
bitmap-stretches the window; `dpi` and `dpi_awareness` are in the log
header. The windowed integer scale follows the DPI (2x at 100%, 5x at 225%,
override with `DKC1_WINDOW_SCALE=1..8`) and `WM_DPICHANGED` re-fits the
window when it moves between monitors.

SNES pixels present at their 7:6 aspect, the same policy as the macOS host
and the engine's `display_aspect.h`, so the 342x224 widescreen frame fills
16:9 and the 256x224 frame is 4:3. `View > Pixel Aspect > Square pixels`
(or `DKC1_SQUARE_PIXELS=1`) selects the engine's 8:7 square-pixel mode for
comparisons. Fullscreen uses the engine's aspect-preserving fit, including
its sub-source-pixel seam suppression, and click-to-provenance maps through
the same rectangle.

`View > Scaling` (or `DKC1_SCALING=sharp|nearest|linear`) chooses the
sampler. The default sharp-bilinear pixel shader keeps flat texel interiors
with an approximately one-output-pixel transition, which removes the uneven
column widths that nearest-neighbour produces at fractional and 7:6 scales.
The GDI fallback offers nearest and linear only.

The debug panel and FPS badge are rasterized with GDI into DIB sections on
the producer thread, then drawn as textures (Direct3D) or blitted (GDI); the
panel is recomposed only for real frames, never for a half-frame present.

`DKC1_PRESENTER=gdi` forces the GDI path. The host also falls back to it
when no Direct3D 11 device, flip-model swap chain, waitable object or
`d3dcompiler_47.dll` is available, and if the device is lost mid-run; the
reason is shown in the status line and recorded as `presenter_error`.

About 1.5 s after a flip-model window starts presenting, the desktop
compositor changes its presentation path once. On this machine that is a
single 58-81 ms `Present` call at frame 91-92 of a cold start, independent
of screen content and absent from the GDI path; it exhausts the 50 ms device
audio queue once, which the host recovers from with the usual starvation
preroll. `DKC1_PRESENT_WARMUP_MS=2500` presents the initial framebuffer for
that long before frame 1 so evidence runs (and `tools/verify_framegen.py`,
which sets it) start after the transition. Play sessions leave it at 0.

### macOS

The native host uses one absolute Mach-clock schedule at exactly 60 Hz for
input, cartridge emulation, PPU rendering, and audio. A separate
`CAMetalDisplayLink` owns a `CAMetalLayer` overlay and requests 120 Hz from the
active screen. The main thread copies each complete frame plus immutable
camera/PPU metadata into a three-slot host queue. The display thread starts
with one buffered frame and normally presents every source frame for exactly
two 8.333 ms drawables. A host stall repeats the current completed frame; it
never advances or catches up cartridge state. Focus/minimize events pause the
link and discard stale queued presentation frames before resume.

The SDL Metal renderer remains as a fail-closed compatibility path and has
blocking vsync off. `DKC1_DISABLE_METAL_PRESENTER=1` selects that path. Its
absolute schedule retains the four-millisecond submit lead and bounded 1.5 ms
spin used by the version-3 CPU pacing trace.

The View menu's `Full Screen Scaling` preference changes only the native Metal
fragment sampler. `Sharp Bilinear` is the default and narrows filtering to an
approximately one-output-pixel transition around source-texel boundaries.
`Smooth (Linear)` uses conventional bilinear filtering, while `Pixel Sharp
(Nearest)` retains hard pixel edges and the fractional fit's uneven column
widths. None changes the frame clock, source framebuffer, emulation state, or
audio cadence.

`DKC1_USE_DISPLAY_LINK_PACING=1` restores the window-bound `CADisplayLink` path
for A/B diagnosis on macOS 14 and newer. That bridge requests 60 Hz, consumes
real ProMotion half-interval callbacks, and accepts a normally spaced callback
only when its remaining target lead covers measured work plus the submit lead.
`DKC1_KEEP_RENDERER_VSYNC=1` separately restores SDL's blocking Metal vsync for
diagnosis. Neither is a release default. The fixed clock became authoritative
after the tested ProMotion callback stream produced intermittent 29 ms target
intervals during traversal while the same build's fixed schedule did not.

## Smooth animation and frame generation (Windows)

`DKC1_FRAMEGEN=1`, F10, or View > Smooth Animation / Frame Generation enables
host-only pose smoothing at 60 Hz. On 120/240 Hz displays it also submits an
intermediate image every 8.33 ms, for 120 images per second. The feature remains
off by default. Simulation, input polling and audio retain the 60 Hz clock.
The untouched native framebuffer remains the evidence oracle.

A four-frame queue adds approximately 66.7 ms of visual delay. It observes the
next changed sprite artwork before synthesizing the intervening held poses.
Connected OAM pieces are tracked using palette, priority, facing, position and
the actual decoded VRAM raster. Reusing a tile address does not conceal a pose
change. A bounded block motion estimate warps both pose endpoints; fractional
sampling preserves subpixel motion. This is approximate generated artwork and
can soften fine detail. Holds longer than four frames retain their early held
portion, then interpolate the last four frames once the next pose is known.
Static poses remain static.

The background under a replaced sprite comes from an additional native PPU
render with OBJ disabled. Per-line palettes, priority, windows and color math
reconstruct the original composite first: a single mismatch rejects smoothing
for that surface. Unsupported modes, OBJ windows, interlace, uncertain ownership
and overlapping sprite groups retain cartridge output. Layout changes, large
camera discontinuities, load/rewind and aspect changes discard queued history.
The first image repeats during the four-frame buffer warm-up.

For 120 Hz motion, the existing PPU pass produces halfway scroll and OAM
positions, with the margin shadow's live-scroll policy enabled. Buffered pose
interpolation then supplies the animation phase for that intermediate time.
Presentation order is delayed frame F, F+0.5, F+1. A worker receives an immutable
copy of F+0.5 and presents it on the next swap-chain slot after F (half a
period after F in timer mode) while the main thread computes the next frame;
the main thread collects the worker before waiting for its own slot, which
fixes that order without a timer. This removes the previous half-period CPU
budget that caused intermediate frames to be skipped even when the complete
work fit within 16.67 ms. The worker never accesses the cartridge or mutable
producer buffers; UI changes and shutdown cancel its pending image. The DXGI
frame statistics in the pacing log report the refresh each image actually
landed on; this is not a universal perfect-cadence claim for every display.

The scratch passes restore the live PPU byte for byte and do not run emulation
or write WRAM. Debug layer isolation, pixel inspection and the provenance
overlay and exact stepping bypass smoothing. Unqueued inspection captures
discard buffered history before smoothing resumes. Enabling with `DKC1_WS_TRACE`,
`SNESRECOMP_WS_CACHE_LOG` or `SNESRECOMP_WS_RETRODICT` is rejected because extra
render passes would change those diagnostic counters.

Evidence and controls:

- `DKC1_FRAMEGEN=force` exercises the 120 Hz submission path on a 60 Hz monitor;
  it does not prove that monitor displayed 120 images per second.
- `DKC1_FRAMEGEN_DUMP_START`, `_COUNT`, `_DIR` export `_cur.ppm` (raw N),
  `_prev.ppm` (raw N-1), `_display.ppm` (delayed, smoothed F), `_mid.ppm`
  (following F+0.5), and JSON metadata. `pose_source_frame` identifies F in
  the capture sequence, which resets on explicit history invalidation.
  These files are no longer an adjacent prev/mid/cur triplet. Image dumping
  is deliberately separate from timing measurement.
- `DKC1_POSE_LOG` records OAM artwork tracks and candidate pose intervals as
  JSONL; overlap rejection can still discard a candidate. Applied work is
  counted by `pose_actors` / `pose_pixels` in the dump and pacing metadata.
  `pose_mismatch` counts rejected composite pixels. `DKC1_POSE_DEBUG=1`
  prints the first mismatch's registers for diagnosis.
- Windows `dkc1.pacing.v5` retains real-frame timing and adds `mid_after_frame`,
  `real_to_mid_ms`, `mid_to_real_ms`. Mid statistics in row N describe the
  midpoint after the previous real submission. `mid_presented`, `mid_skips`
  and both intervals expose skipped or uneven software submissions.
- `DKC1_ANIM_CADENCE_LOG` / `tools/analyze_anim_cadence.py` measure original
  WRAM pose cadence, independently of generated display artwork.
- `DKC1_FRAMEGEN_TWEEN=1/2` retains the older adjacent-frame flow/dissolve for
  diagnostic comparison only; default 0 disables that earlier pass. It can
  intentionally fail the new composition oracle. `DKC1_FRAMEGEN_LIVE_SCROLL=0`
  is also an old-path A/B control, not the supported configuration.
- `tools/verify_framegen.py --exe build/dkc1_desktop.exe --rom <rom>
  --state <immutable.state> --output <new-directory> --wide 1` checks every
  raw/display/mid frame, WRAM/OAM equality, final VRAM checkpoint hashes, three repeated replays, composition
  correctness and a separate undumped timing run at 60 and forced 120 Hz.
  It restores its private root and gracefully closes every visible run.

See [the frame-by-frame audit](FRAMEGEN_REVIEW.md) for exact build identities,
input schedules, results and validated scope. macOS pose generation is not
implemented; its native presenter still repeats source frames.

The subsequent [running/background audit](BACKGROUND_PACING_REVIEW.md) found
repeatable whole-pixel background judder and occasional 50 ms real-submission
gaps in additional runs. Earlier clean timing windows are bounded samples,
not a zero-drop guarantee. The BG midpoint renderer still truncates scroll
halves to integers; pose interpolation does not remove that spatial stepping.

## Audio continuity

The audio request rate is derived from the selected host frame rate, so the
32,040 Hz stream remains synchronized when presentation is exactly 60 Hz.
Cold launch pauses `waveOut` until the engine's native sample ring reaches its
normal target, then starts after one queued device buffer. The optional
`DKC1_AUDIO_PREROLL` setting accepts one through four device buffers; one is the
tested default and gives the lowest latency without startup underflow.

Save states serialize the SPC/DSP and native sample ring, but not the
host-only APU port queue or its guest-clock mapping. Both file and in-memory
snapshot loads therefore rebase that mapping at the restored frame, discard
commands from the abandoned future timeline, and reset resampler history.
The Windows host also discards already-queued `waveOut` buffers. Without both
halves of this reset, a rewind can leave the SPC apparently many frames ahead
and starve the restored timeline.

After a real host stall drains the device, playback enters a short preroll
instead of repeatedly resuming on an empty one-buffer boundary. The macOS host
uses the same runtime timeline rebase, clears stale queued SDL audio on load or
resume, and defaults to a two-block CoreAudio preroll. `DKC1_AUDIO_PREROLL`
accepts one through four blocks on both desktop hosts. External MSU-1 tracks
are memory-mapped once when the pack opens, so the frame-critical mixer never
waits on a stdio refill. Controller rumble is dispatched by a worker rather
than blocking the emulation/presentation thread. SDL software-queue
occupancy is not treated as a starvation oracle because CoreAudio may already
own those samples; the Mac host re-prerolls after a long skipped-callback gap or
fixed-clock deadline miss, which is the observable host-stall boundary. A
single delayed frame remains within the normal device buffer and does not reset
audio.

## Capturing evidence

Set `DKC1_PACING_LOG` to write a JSONL trace (Windows v5, macOS v3). On
Windows:

```powershell
$env:DKC1_PACING_LOG = "build/pacing.jsonl"
.\build\dkc1_desktop.exe "C:\private\dkc1.sfc"
python tools\analyze_pacing.py build\pacing.jsonl --warmup 60
```

On macOS:

```bash
DKC1_PACING_LOG=build/pacing-macos.jsonl \
  build/macos/DKC1Recomp.app/Contents/MacOS/DKC1Recomp "$ROM"
python tools/analyze_pacing.py build/pacing-macos.jsonl --warmup 60
```

Each frame records setup, emulation, rendering, diagnostics, audio, waiting,
host presentation, deadline error, device-queue occupancy, engine-ring
occupancy, underruns, drops, starvations, and cumulative deadline overruns.
`submit_interval_ms` is the host-side cadence measurement. In the
compositor-locked modes `work_ms` runs from the slot signal to the present
call and `late_ms`/`overruns` count frames whose work exceeded one period;
`submit_error_ms` is only meaningful in timer mode; `wait_timeout` marks a
slot wait that timed out.

Windows v5 rows also carry the swap chain's DXGI frame statistics, the
Windows scanout oracle: `present_count` (our latest present),
`stat_present_count` and `stat_present_refresh` (the last image the display
showed and the refresh it landed on), `stat_sync_refresh` /
`stat_sync_qpc_ms` (a refresh counter and its timestamp, from which the
analyzer measures the real refresh rate), `stat_disjoint` (statistics reset
by a mode or monitor change) and `stat_lag_presents` (how far the statistics
trail the present count; not a queue depth). `tools/analyze_pacing.py`
prints the refreshes per displayed present against the expected divisor and
counts repeated refreshes; the GDI presenter has no such oracle. The host
completion timestamp alone is not proof of physical scanout time.

On macOS, record actual drawable completion independently:

```bash
DKC1_SCANOUT_LOG=build/scanout-macos.jsonl \
DKC1_PACING_LOG=build/pacing-macos.jsonl \
  build/macos/DKC1Recomp.app/Contents/MacOS/DKC1Recomp "$ROM"
python tools/analyze_scanout.py build/scanout-macos.jsonl --warmup 120
```

The scanout trace is `dkc1.scanout.v1`. Each completed drawable records
`targetTimestamp`, `targetPresentationTimestamp`, actual `presentedTime`,
source sequence/host frame, repeat index/goal, queue loss counters, camera X/Y,
and BG1-BG3 PPU scroll. A zero `presentedTime` means Core Animation skipped an
occluded drawable and is never counted as physical presentation. Keep the real
game window visible for scanout evidence; a fully covered window is not a
display oracle.

A deterministic recovery test can inject one host stall:

```powershell
$env:DKC1_PACING_TEST_STALL_FRAME = "400"
$env:DKC1_PACING_TEST_STALL_MS = "120"
$env:DKC1_PACING_LOG = "build/pacing-stall.jsonl"
.\build\dkc1_desktop.exe "C:\private\dkc1.sfc"
python tools\analyze_pacing.py build\pacing-stall.jsonl --warmup 60
```

The expected signature is one long interval and one overrun, followed by
normal approximately 16.67 ms intervals. There must be no short catch-up
burst, audio drop, or internal engine underflow. One device starvation and
preroll is expected when a 120 ms stall exhausts the queued device audio.
The same environment variables and analysis command apply to the native macOS
executable. Its default overrun count comes from fixed-clock deadline misses;
the opted-in display-link path instead counts skipped callbacks and timeouts.

## Current measured gates

### September 18 Windows presenter (Direct3D 11 flip model)

Measured on this machine's Parsec virtual displays (60.000 Hz, 225% DPI)
from the frame-20000 Jungle Hijinxs state, 420 frames, 150 frames of
warm-up, widescreen:

| Mode | submit p50 / p99 / max (ms) | work p99 | present p99 | DXGI refreshes per present | audio |
|---|---|---|---|---|---|
| waitable, windowed, panel off | 16.676 / 17.165 / 17.417 | 1.47 | 0.29 | 1.000, 0 repeats over 269 presents | clean |
| waitable, windowed, panel on | 16.687 / 17.412 / 33.309 | 1.46 | 3.04 | one repeated refresh | clean |
| waitable, fullscreen | 16.689 / 17.417 / 17.866 | 1.46 | 0.27 | 1.000, 0 repeats | clean |
| dwmflush (GDI fallback) | 16.678 / 18.133 / 18.447 | 1.24 | 3.64 | no oracle | clean |
| timer, `DKC1_FRAMEGEN=force` | 16.667 / 16.667 / 16.673 | 5.95 | 3.17 | 538 presents, 0.5 per refresh as expected | clean, 270/270 half-frames |

With `DKC1_PRESENT_WARMUP_MS=2500` the same waitable run has no interval
above 17.6 ms from frame 1 and no audio starvation. A 600-frame capture of
the rebuilt executable (panel off, warm-up) showed 539 presents with two
repeated refreshes: two 33.5 ms slots in which the waitable object was not
signalled and the statistics show the compositor holding the image for a
second refresh. Those are compositor or virtual-display events, now visible
as such instead of hidden behind a clean host timer. Without it, the
compositor's one-time path change appears as a single 58-81 ms present at
frame 91-92 followed by one device starvation and preroll. The
`submit_interval_ms` spread here is the compositor's slot jitter on a
virtual display; the DXGI statistics show every present on its own refresh.
The previous GDI/timer host measured 16.85-17.02 ms p99 by its own clock
while its GDI update finished after the targeted vblank on every frame.

### Earlier GDI/timer host

The previous Windows build passed 450-frame visible-host captures in Jungle
Hijinxs (wide and native), Coral Capers (wide), and an edge-of-map wide route:

- submit-interval p99: 16.85-17.02 ms;
- steady deadline overruns: zero;
- device audio starvations and drops: zero;
- engine audio underflows: zero;
- work-time p99: 2.21-2.71 ms.

A 1,200-frame cold-start run also had zero audio errors and a 17.05 ms
submit-interval p99. A scripted save, 60-frame advance, rewind, and 600-frame
continuation had zero audio errors and a 17.05 ms p99. The 120 ms injected
stall produced one 166.78 ms interval, then 16.56 ms and normal cadence,
without an internal audio underflow.

The native macOS 16:10 fullscreen host passed a 780-frame uphill/downhill
traversal of the Slip-Slide Ride cave reproduction after a 60-frame warm-up:

- submit-interval p50/p95/p99/max: 16.6667/16.6696/16.6764/16.6908 ms;
- steady deadline overruns: zero;
- device audio starvations and drops: zero;
- engine audio underflows: zero;
- work-time p50/p99/max: 2.84/4.00/4.30 ms.

The direct A/B on the same build and route explains the policy change. The
display-linked path produced 11 steady submit intervals over 20 ms, with p99
29.1673 ms and max 45.8350 ms, even though steady work remained below 5.7 ms.
The final fixed-clock path produced none and had 0.0031 ms submit-interval standard
deviation. Evidence is preserved under
`build/repros/macos-motion-pacing-20260830/` in `candidate-fullscreen/` and
`final-clean/`.

The follow-up native Metal presenter preserves that 60 Hz game clock while
requesting a separate 120 Hz drawable stream. Three complete 780-frame cave
replays are byte-identical in framebuffer, WRAM, VRAM, CGRAM, both OAM copies,
and audio. In a clean visible sample after a 120-presentation warm-up, 271
physical presentations held p50/p95/p99/max intervals of
8.333333/8.337395/8.337888/8.339292 ms, with 135 source frames repeated exactly
twice, no missing or backward source frames, and no queue drops, skips, or
starvation. Two later current-build visible samples kept those integrity
counters at zero but reached approximately 12 ms p99 drawable spacing and
20.5 ms p99 source-transition spacing. The matching CPU traces remained near
16.667 ms. This isolates the remaining measured variability to physical host
presentation rather than emulation cadence; it is still an open visible-QA
item, not a claim of perfect motion.

These numbers are regression baselines for this machine, not universal GPU or
scanout guarantees. Acceptance on another system still requires a fresh trace
and visible play test.

## September 6 host ports

[Host adoption implementation](HOST_ADOPTION_IMPLEMENTATION.md) records stable refresh qualification, bounded audio drift correction, controls, and rewind. The fixed 60 Hz guest clock remains unchanged; physical ProMotion variability is still an open issue.
