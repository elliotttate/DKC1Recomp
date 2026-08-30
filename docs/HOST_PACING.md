# Desktop pacing and audio continuity

Emulation remains single-threaded and deterministic. Windows presents one
emulated frame per selected compositor interval. Native macOS keeps emulation
at fixed 60 Hz while a separate host-only Metal presenter may scan the same
immutable frame more than once on a higher-refresh panel.

## Frame clock

### Windows

The host queries the desktop compositor clock through
`DwmGetCompositionTimingInfo(NULL, ...)`, chooses the integer display divisor
nearest 60 Hz, and submits approximately 1 ms before that compositor boundary.
For example, 60 Hz uses every refresh and 120 Hz uses every second refresh.
This avoids the slow beat produced by presenting the SNES hardware rate
(approximately 60.0988 Hz) to a 60 Hz desktop.

If DWM timing is unavailable, the host uses the same integer-divisor rule with
the display's reported refresh rate. Unusual displays that cannot produce a
rate between 59.5 and 60.5 Hz retain the hardware cadence. Set
`DKC1_PRESENT_HZ` to a value from 30 through 240 only for a deliberate test.

Deadlines are absolute, not accumulated from the completion time of the last
frame. A missed compositor boundary is skipped and the clock is re-anchored;
the host never follows a long frame with a short catch-up frame. A high
resolution waitable timer handles the coarse wait and a bounded final spin
removes scheduler wake jitter.

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

Set `DKC1_PACING_LOG` to write the cross-platform version-3 JSONL trace. On
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
`submit_interval_ms` is the relevant cadence measurement. The host completion
timestamp is diagnostic and is not proof of physical scanout time.

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

The current Windows build passed 450-frame visible-host captures in Jungle
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
