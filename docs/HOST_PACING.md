# Desktop pacing and audio continuity

The Windows and native macOS hosts present one emulated frame per display-clock
interval. Emulation remains single-threaded and deterministic; only the
deadline calculation, wait strategy, and audio handoff are host concerns.

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

The native host binds `CADisplayLink` to the application window and requests a
cadence near 60 Hz. Each callback supplies the concrete upcoming scanout target;
the host completes emulation and rendering early, then queues the Metal frame at
a fixed one-millisecond lead. The final absolute wait reserves a bounded 1.5 ms
spin because an otherwise-idle Apple Silicon main thread can wake more than the
old 250 us margin late and produce alternating short/long submissions. Its
cursor-based bridge preserves a callback that arrives while the main thread is
presenting, skips genuinely stale callbacks after a stall, and rejects a
callback with less than three-quarters of a frame of target lead so a long
interval is never followed by a short catch-up frame. If display-link startup is
unavailable, an absolute Mach-clock deadline retains the cartridge cadence as a
bounded fallback. The Windows DWM scheduler is deliberately not shared with
macOS because its clock and compositor API are platform-specific.

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
accepts one through four blocks on both desktop hosts. SDL software-queue
occupancy is not treated as a starvation oracle because CoreAudio may already
own those samples; the Mac host re-prerolls after a long skipped-callback gap,
which is the observable host-stall boundary. A single delayed callback remains
within the normal device buffer and does not reset audio.

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
executable; its overrun count is derived from skipped display-link callbacks.

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

The native macOS 16:10 fullscreen host also passed a 780-frame traversal of the
Slip-Slide Ride cave reproduction after a 60-frame warm-up:

- submit-interval p50/p95/p99: 16.67/17.20/17.21 ms;
- steady deadline overruns: zero;
- device audio starvations and drops: zero;
- engine audio underflows: zero;
- work-time p50/p99/max: 2.57/4.52/5.23 ms.

The same route with a 120 ms stall at frame 400 produced one 150.35 ms interval,
then 16.32 ms and normal cadence. It skipped seven obsolete display callbacks,
performed one expected CoreAudio repreroll, and recorded no audio drop or engine
underflow. Evidence is preserved under
`build/repros/ice-cave-flicker-20260830/pacing-macos/` as
`release-final-spin1500.jsonl` and `release-final-spin1500-stall.jsonl`.

These numbers are regression baselines for this machine, not universal GPU or
scanout guarantees. Acceptance on another system still requires a fresh trace
and visible play test.
