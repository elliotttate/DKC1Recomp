# DKC2/DKC3 host adoption review — 2026-09-06

Several focused host improvements are worth adopting. Keep DKC1's independent 60 Hz simulation clock and Metal display queue; borrow the newer projects' tested refresh qualification, audio rate control, input routing, and bounded rewind. A wholesale host or engine replacement is not supported by this comparison.

This is a source review with donor-model validation, not an implemented pacing change or a fresh live performance benchmark. The running widescreen candidate was not operated during this review.

## What DKC1 already has

[DKC1's host pacing record](HOST_PACING.md) and the current implementation already provide absolute Mach deadlines, bounded final spinning, stall re-anchoring without short catch-up bursts, audio preroll/recovery, asynchronous haptics, memory-mapped MSU-1 playback, and a separate `CAMetalDisplayLink` presenting immutable completed frames. Physical drawable timing is recorded through `presentedTime`, separately from CPU submission timing.

DKC2/DKC3 still pace the canonical emulation/render/audio work together in their SDL host loop. Their display pacer can follow a qualified refresh divisor, but it is not a separate interpolated 120 Hz renderer. Parts of their fixed-clock fallback explicitly cite the DKC1 pacer as precedent. The earlier DKC3 interpolation work remains a plan; no ready-made interpolation implementation exists in these checked-out hosts.

Do not copy DKC2/3's unpaced audio-prime/recovery loop into DKC1: it can execute extra guest frames to fill the device queue. Preserve DKC1's existing simulation cadence and recovery contract.

## Recommended ports

| Priority | Improvement | Concrete DKC1 gap | Proposed scope |
| --- | --- | --- | --- |
| 1 | Stable refresh qualification and independent pacing tests | Metal chooses one versus two repetitions from a **single** target interval using a 90 Hz threshold. It has no sustained refresh qualification or general divisor model. | Adapt the donor's refresh/divisor validation and tests into a pure presentation scheduler. Preserve the independent 60 Hz producer; qualify 60/120 Hz, treat isolated late callbacks as stalls, and use timestamp-based frame selection for other cadences. |
| 2 | Small, bounded host audio rate correction | DKC1 queues a fixed number of samples per frame and drops a submission when the software queue reaches 250 ms. It has no gradual correction for host/device clock drift. | Apply a resettable host-side resampler to the mixed SPC + MSU-1 PCM stream. Keep canonical guest audio production fixed; smooth queue observations, bound the adjustment, and retain current preroll/stall handling. |
| 3 | Controller bindings, analog sticks, and two-player routing | DKC1's Mac `PollInput` merges hard-coded keyboard bindings and the first controller's buttons into one input word. It does not read analog axes or route a second pad. | Port the backend-independent input mapper and tests, then expose bindings, deadzones, player source, and assist bindings through native Mac UI. Preserve DKC1's asynchronous rumble worker. |
| 4 | Bounded rewind and fast-forward assist controls | DKC1 has quick save/load and a flight recorder, but no equivalent player-facing rewind/fast-forward controls in its Mac host. | Port the small rewind buffer/model and adapt host actions. Integrate state-load reconciliation with the flight recorder, Metal queue, audio, MSU-1, and controller schedules. Particularly useful for intermittent aquatic bugs. |

### 1. Refresh qualification

DKC1's decision is in [macos_metal_presenter.m:250](/Users/briantate/Documents/GitHub/DKC1Recomp/runner/macos_metal_presenter.m:250). The display queue requests 120 Hz, but treats every target interval between 11.11 and 100 ms as a one-repeat cadence. An isolated late target can therefore change policy immediately.

The donor [desktop_pacer.c:22](/Users/briantate/Documents/GitHub/DKC2Recomp/runner/desktop_pacer.c:22) checks integer divisors 1–4, a permitted rate tolerance, eight initial observations, a smoothed period, and reset behavior. Its [tests](/Users/briantate/Documents/GitHub/DKC2Recomp/tests/test_desktop_pacer.c) cover 59.94/60/119.88/120/240 Hz, incompatible 50/75/90/144 Hz, rate changes, and stalls.

Adapt those capabilities rather than transplanting its guest-clock lock or copying its moving average unchanged. One long interval can also temporarily unlock the donor's average. DKC1 needs explicit late-callback handling and a stable selection policy for its immutable frame queue; the game clock must remain independent. Requesting 120 Hz does not prove that every target or physical scanout arrived at that cadence.

Reanalysis of **historical August 30** DKC1 evidence (`build/repros/macos-metal-pacing-20260830/final-short-visible-3/scanout.jsonl`) confirms that this is an exercised branch: after 120 warm-up callbacks, 278 samples use repeat goal 2 and three use goal 1, with six goal transitions. The existing analyzer reports physical interval p99 **12.122917 ms** and source-transition p99 **20.470358 ms**, with no steady queue drops, skips, or starvation. This identifies a policy worth hardening; it does **not** prove those switches caused every visible timing outlier or predict the improvement of an unbuilt candidate. The current Metal file is unchanged from its checked-in implementation; the trace is historical, not a new measurement.

Acceptance should compare actual scanout and source-transition spacing at 60/120 Hz, sleep/wake, focus/minimize, display changes, and a controlled host stall. Verify immutable source ordering and guest/audio hashes as well as CPU timing.

### 2. Audio drift control

[DKC1 PumpAudio](/Users/briantate/Documents/GitHub/DKC1Recomp/runner/sdl_host.c:1353) accumulates `32040 / pacing_fps` samples and calls `RtlRenderAudio` plus the MSU-1 mixer. At `kAudioMaximumQueuedFrames` it returns without generating or submitting that frame's output. Preroll and long-stall recovery are already implemented.

DKC2/3 add a continuous stereo resampler and a smoothed queue controller in [desktop_audio_rate.c](/Users/briantate/Documents/GitHub/DKC2Recomp/runner/desktop_audio_rate.c:52). Their SDL integration bounds output-rate adjustment to **±0.5%**, smooths observed fill with weight 0.02, and sizes the target from the actual device pull plus two frame buffers. Its state resets on load/pause/rewind. The model tests cover continuity, unity ratio, output bounds, positive/negative adjustment, and invalid inputs.

This is useful drift protection, not evidence of a currently reproduced DKC1 audio failure. A small resampling adjustment changes host PCM and can affect pitch; do not repeat the donor header's unconditional audibility claim. Use a sufficiently sized output buffer: the donor's capacity-limited call does not retain arbitrary unconsumed input for a later call. Apply the correction after mixing both audio sources so their host timebase remains consistent. Do not infer CoreAudio starvation from one empty SDL queue observation, and do not copy the donor's extra guest frames during queue priming.

Acceptance needs a long playback trace, pause/load recovery, a controlled stall, device changes, and audible SPC/MSU-1 checks. Short headless determinism runs alone cannot establish device-clock behavior.

### 3. Input and controls

The donor [desktop_input.h](/Users/briantate/Documents/GitHub/DKC2Recomp/runner/desktop_input.h:69) and [desktop_input.c:181](/Users/briantate/Documents/GitHub/DKC2Recomp/runner/desktop_input.c:181) provide configurable keyboard/gamepad bindings, stick/trigger input, per-player deadzones, explicit source routing, two packed 12-bit controller words, and gated host shortcuts. DKC1's [Mac input path](/Users/briantate/Documents/GitHub/DKC1Recomp/runner/sdl_host.c:1221) currently implements a smaller fixed map.

The logic can be adapted without adopting the donor's entire ImGui/OpenGL frontend. Test hotplug, held-button transitions, simultaneous pads, menu focus, and real two-player play. Host rewind/load/menu actions must not leak as game buttons.

### 4. Rewind and fast-forward

The donor [desktop_rewind.c](/Users/briantate/Documents/GitHub/DKC2Recomp/runner/desktop_rewind.c:6) is a bounded stack over a circular buffer of native snapshots. The host captures every third guest frame, holds 300 snapshots, and supports 3× fast-forward. At DKC1's 60 Hz clock that configuration represents about 15 seconds. A currently preserved DKC1 snapshot is 326,776 bytes; 300 equal-sized snapshots would consume roughly 93.5 MiB plus scratch/metadata. Verify snapshot size and its stability for each supported scene before adopting that budget.

DKC1's runtime already exposes memory snapshots. Correct integration also needs to flush future Metal packets, rebase the audio timeline and MSU-1 playback, re-anchor the flight recorder, and clear abandoned playback input. The donor buffer alone does not solve these DKC1-specific responsibilities. Test rewind-and-resume against deterministic replay and check full gameplay closure; fast-forward must deliberately advance canonical guest frames rather than changing only presentation speed.

## Lower-priority or unsuitable wholesale ports

- DKC2's CRT/display work is an optional visual feature, not a fix for frame pacing. Some of that work is currently uncommitted. DKC1 already has three native fullscreen sampling choices; a shader feature should be scoped separately around the desired appearance.
- Do not replace DKC1's Metal path with DKC2/3's SDL/OpenGL presentation merely because those projects are newer. Their architecture is different, and DKC1 already has the separate display loop and physical scanout telemetry needed for this investigation.
- The newer engine history touching `common_rtl.c`, APU, and desktop code contains no newly identified universal pacing/audio fix to justify advancing the entire pinned submodule. The visible differences are predominantly PPU/widescreen work plus optional synthetic SRAM and shader settings. Evaluate individual changes against a specific failure.

## Verification and provenance

Reviewed actual working checkouts: DKC1 parent `ee6d662`, engine base `088bb05`; DKC2 parent `24425bf`, engine `3a929cd`; DKC3 parent `5afdacb`, engine `3b24b9d`. Existing edits, including the DKC1 aquatic candidate, were preserved. DKC2's pacing/audio integration is committed in `63ed6e4` (2026-09-03). The four donor model implementations are identical between DKC2 and DKC3 after normalizing game prefixes, and have no uncommitted changes.

Built and ran all eight existing donor suites: pacing, audio rate, rewind, and desktop input for both projects. **All pass.** Commands, output, source hashes, and current checkout identities are retained in `build/reviews/host-adoption-20260906/donor-tests.json` and `manifest.json`. Reanalyzed the existing DKC1 scanout capture with `tools/analyze_scanout.py --warmup 120`; no live app was moved, resumed, or replaced for this review.

Suggested implementation order: stabilize Metal cadence qualification, add bounded audio drift correction with a long device test, add controller configuration, then integrate rewind/fast-forward as an optional assist feature.

## Implementation follow-up

The four focused host ports are now implemented and locally exercised. See [implementation and limitations](HOST_ADOPTION_IMPLEMENTATION.md) for current behavior, pacing/audio measurements, controls, and rewind validation.
