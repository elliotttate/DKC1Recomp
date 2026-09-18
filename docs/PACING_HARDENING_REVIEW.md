# Windows pacing hardening, September 18, 2026

Status: bounded investigation complete; pacing issue remains open. The user
deprioritized long pacing tests in favor of interpolation image quality.
The planned repeated 30-minute windowed/fullscreen runs were not executed.

## Scope and identity

This changes Windows diagnostics and tests presentation queue depth. It does
not change cartridge clocks, widened streaming, gameplay, native rendering,
or the accepted four-frame animation lookahead. Frame generation remains
opt-in. Only 60 Hz display delivery is available on this host.

Private evidence: `build/pacing-hardening-20260918/`. Supported ROM SHA-256:
`fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.
The immutable cleared Jungle root is
`build/background-pacing-20260918/cleared-root/user.state`, SHA-256
`08bab0286deeed59b5eb5dd331812fb93114d2d64edc28bc7d63db7722dc2537`.
The 570-frame route has 30 neutral, 240 Y+left, 30 neutral, 240 Y+right,
30 neutral frames. Long runs repeat inputs continuously; they do not reload
the root between laps. Each visible run checkpoints, restores its private
root, advances one neutral frame, pauses and closes normally.

Baseline executable SHA-256:
`7030976350b9c57021b7abf65e8e673b21a4a5d4ae54450f4f442691878043d4`.
Initial async candidate SHA-256:
`b8a89fca624d7d3b6268a29e7a5a6766ac256742911e83e2f65a4ab3182d979f`.
Every run also stores its executable, state and input hashes and process ID.

## Implemented safeguards

- `win32_pacing_log.inc` moves pacing file writes to a bounded, preallocated
  worker queue. The frame thread never waits for disk space or the writer.
  Overflow discards diagnostics, not gameplay; the mandatory completion
  sidecar makes missing, dropped or failed evidence reject verification.
- QPC timestamps now include the entire submission interval. Statistics and
  logging time belong to the following measured gap. Audio mixing and device
  submission have separate optional timings.
- Queue depth 1 remains the default; `DKC1_MAX_FRAME_LATENCY=2` permits A/B
  testing without changing buffer count or game speed.
- `live_test_guard.py` serializes visible tests and rejects any overlapping
  DKC1 window, including renamed test executables.
- `verify_pacing_soak.py` preserves failures, checks immutable roots, and
  correlates every steady submission with independent PresentMon API events.
  Complete ETW submission coverage is separate from DXGI display cadence.

## Trace limits

PresentMon 2.5.1 and 2.3.1 produced no records with full display tracking on
this configuration. API-only mode works: use `--v1_metrics --qpc_time_ms
--no_track_gpu --no_track_input --no_track_display`. PresentMon 2.3.1 v1 writes
`QPCTime` in seconds with that option; the correlator determines its scale
only by alignment with absolute host QPC, and fails if ambiguous.

API events prove submission coverage/order, not physical scanout. Display
acceptance uses DXGI present/refresh counts. It cannot certify frames received
by a remote client or an unavailable 120 Hz panel.

The initial GeneralProfile/GPU WPR capture was too heavy (3.8 GB compressed).
Its circular event buffers retained only part of the session despite reporting
zero lost events. Missing early presents are incomplete history, not proof of
game drops. That trace is diagnostic only. The replacement custom WPR profile
was intended to retain bounded scheduling/stack history, but its stop failed
with `0xc5580612` (no providers). It produced no validated wait-stack evidence.
The named `DKC1PacingHardening` session was cancelled successfully and the
trace helper/PresentMon processes were stopped. The `--kernel` path is not
validated and must not be used as acceptance evidence.

## Validation so far

Primary and tools builds pass. The blocked-writer model fills all 8192 slots,
rejects 17 extra records without waiting, then drains the retained records in
order with zero I/O errors. Analyzer tests reject incomplete evidence.

The 108,300-frame headless route completed all 190 laps with no blank frames
or OAM overflow. This validates route duration, not live display pacing.
The full Python suite ran 282 tests with eight existing missing-`cc` errors
and nine skips; the private SciPy dependency path was supplied. This is not
a fully passing suite.

## Short comparisons and remaining uncertainty

`comparison.json` records three counterbalanced six-lap (3,420-frame) runs for
the baseline and async candidate at queue depths 1 and 2. Extra held refreshes
were baseline 2/2/56, queue 1 0/0/21, queue 2 1/0/1. These noisy short samples
do not establish a universally better queue depth; the default remains 1.
Candidate API captures have complete submission coverage. The older baseline
does not emit absolute QPC, so its ETW correlation is unavailable.

Queue-1 repeat 3, frame 2132, identifies a 323 ms message-pump stall inside
`WM_NCLBUTTONDOWN` (title-bar interaction). This explains that particular long
gap, not every historical 399/432 ms gap. Queue-2 repeat 1 also observed a
13.6 ms audio phase, which buffering absorbed without a display miss at that
frame. The separate `audio-waits` run split device submission from mixing:
the largest measured device submission was 8.69 ms, versus 0.032 ms mixing.
Its failed trace stop prevents a kernel-wait conclusion. No audio behavior
was changed. No universal zero-hitch claim is made.

After the terrain interpolation correction, one separate 2,280-frame sample
in `build/framegen-stability-20260918/short-timing.json` still failed timing
acceptance. Frame 1281 reports a 1,609 ms submission interval, with 1,602 ms
inside the message pump and only 7.16 ms frame work. Frame 1185 also records
139 ms in the audio phase. Neither elapsed phase proves a disk-loading cause;
the underlying waits remain unresolved. Pixel-quality acceptance is separate.
