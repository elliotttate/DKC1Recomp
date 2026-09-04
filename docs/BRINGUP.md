# DKC1 bring-up log

## 2026-08-15 â€” repo created, first generation

- Scaffolded from the DKC2Recomp layout; `snesrecomp` pinned as a submodule at
  `cd941fc` (the same revision DKC2Recomp validates against).
- `tools/ingest_dkc1_disasm.py` generated `recomp/*.cfg` from the disassembly
  pipeline (`Tools/IDA/work` in the Yoshifanatic1 disassembly repo):
  13 banks, 1,276 function entries with proven `entry_mx`, 350 interior
  data regions, 25 indirect-dispatch contracts (293 targets) resolved from the
  disassembly's own `dw` tables.
- `v2_sync_funcs_h.py` accepted all cfgs (1,276 declarations).

### First generation result (native analyzer, 19.6s)

```
analysis: 1285 roots -> 1398 exact variants, 3433 edges
analysis: 922 AOT-eligible, 476 LLE-only
```

LLE-only reason classes (from `program_manifest.json`; address-specific
suffixes folded): 606 `unproven_call`, 462 `truncated_call_continuation`,
456 `unproven_callee_exit`, 18 `brk`, 15 `structural_poison`. Most of the
unproven-call volume is cascade from a smaller set of roots â€” raising AOT
coverage means fixing bounds/dispatch at the leaves (the DKC2 pattern).

### Known open items

- **6 unresolved dispatch sites** read pointer tables in WRAM
  (`$0508`, `$0002`, `$0000` via B5/B6 script engines) â€” marked as
  `# TODO(dispatch)` comments in the cfgs. Deriving their target sets needs
  the writers of those tables enumerated (the sprite spawn-script system).
- **9 dynamic RAM-pointer jumps** were initially listed in the disassembly
  pipeline's `recomp_seed/dynamic_jumps.txt`. The animation opcode dispatcher
  at `$BE8179` is now closed by a source-backed 197-target contract and an
  exact audit; the remaining sites still need equivalent writer/table proofs.
- No host application yet: next milestones are (1) clean generation,
  (2) build the generated static library, (3) port DKC2Recomp's runner
  integration (`runner/`, `src/`, `app/`, CMake) and boot-probe workflow,
  (4) LLE-vs-AOT differential validation on the attract loop.

### Provenance

Structural metadata derives from the GPL-3 Yoshifanatic1 DKC1 disassembly.
Semantic names come from the 2026-08-15 IDA analysis pass documented in the
disassembly repo (`Docs/RE_Findings_DKC1.md`); several were corroborated by
the disassembly's own `RAM_Map_DKC1.asm`.

## 2026-08-15 — merged Pseudocode cfg set; the game runs

- Discovered `DKC1_Disassembly/DKC1/Pseudocode/` — a prior SNESRecomp ingest of
  the same disassembly. Its cfg set (2,557 bounded funcs, 118 dispatch tables)
  replaced the first-pass cfgs, with our semantic names merged onto matching
  entries (45 renames). Result: **2,591 exact AOT variants, 0 LLE-only (100%
  static coverage)**.
- Ported DKC2Recomp's headless host adapters (`runner/`), plus a single-file
  Win32 GDI/waveOut interactive host (`runner/win32_host.c`) — no SDL needed.
- `build_host.bat` (direct MSVC, no CMake required) produces:
  - `build/dkc1_snesrecomp_headless.exe <rom> [frames]` — validation harness
    (state trace, frame PPM dumps, WRAM/VRAM dumps, audio PCM, input playback).
  - `build/dkc1_desktop.exe <rom>` — playable window (Z=B, X=Y, S=A, A=X,
    Q/W=L/R, Enter=Start, RShift=Select, arrows=D-pad, Esc=quit).
- **Validation:** 3,600-frame headless run completes cleanly. The boot shows
  "Nintendo PRESENTS" + Rare logo (frame 600), the full DKC intro cutscene
  (DK + boombox on the treetops, Cranky's gramophone) renders correctly with
  continuous audio (3,276/3,600 audio-active frames). Mode transitions and
  camera movement look sane.

## 2026-08-15 -- presentation-camera widescreen

Same presentation architecture as DKC2Recomp, informed by the tested
SuperZSNES ROM patch but deliberately avoiding its camera-bound strategy.
Logical camera coordinates, collision, movement clamps, exits, boss arenas,
and tile streaming stay stock. Generated adapters widen only visibility and
placed-object activation after the host proves a supported gameplay layout.

- `Dkc1VideoDecodeLevelTile` decodes DKC1's level maps straight from ROM
  (metatile formats from `$81:8705` horizontal / `$81:8DFA` vertical; flips
  in cell bits 14/15). Unlike DKC2 (WRAM maps), the source is static ROM, so
  prefill cannot race a decompressor.
- Per-frame runtime calibration decodes the native viewport and requires
  >=70% agreement with the live rolling tilemap before margins are prefilled.
  A Mode-1/64-column register shape alone is not accepted. Unknown layouts,
  logos, title screens, and transitions are centered with black sides. The
  calibration latch has only a two-frame grace; it does not accumulate a
  multi-second stale-layout hold during long gameplay sessions.
- Terrain layer = the wide layer whose tilemap base matches the streamer
  base at `$7E1B13`; world keys unwrap PPU scroll against camera
  `$088B/$0895`. Non-terrain wide layer folds periodically (parallax).
  Fixed screens (logos, title, map transitions) pillarbox via
  `PpuSetExtraSpaceCentered`.
- `DKC1_WIDESCREEN=1` opts in the headless harness; the desktop host
  defaults to 16:9 (`DKC1_WIDESCREEN=0` starts in 4:3).
  `DKC1_WIDESCREEN_EDGE=reflect|bars|shift|glide` selects the level-wall
  presentation (default `glide`: the inward clamp released over eight
  margins of camera travel; `reflect`: view locked to the camera, terrain
  mirrored past the wall; `bars`: view locked, margin clamped at the wall;
  `shift`: the former inward clamp). The macOS app offers the same choice
  under View > Level Edge. See `docs/WIDESCREEN.md`. The visible host also
  exposes `View -> Aspect Ratio` so either presentation can be selected while
  paused or playing without reloading the ROM or save state.
- The post-generation override pass ports the proven SuperZSNES visibility
  fixes: 18 left activation sites, 14 span sites, two right-prefetch sites,
  common sprite culls, banana-private culls/OAM X-high, vertical-rope
  culls/OAM X-high, and retry of missing type-$05 group children.
- BG1/BG2 world layers widen independently; bounded BG3 may edge-repeat only
  after gameplay calibration. This fixes Jungle Hijinxs' black sky margin.
- **Validated in real gameplay:** deterministic scripted input enters Jungle
  Hijinxs and runs right. At frame 7,600 the wide frame is calibrated and
  coherent; a stock-width run retains its pre-adapter frame, WRAM, VRAM, OAM,
  and audio hashes. A 14,000-frame wide run completes.

Known issues / next:
- Wider placed-object activation can advance actor simulation relative to a
  stock-width run. The type-$05 retry fixes the proven one-shot child loss,
  but every level and object family still needs deterministic route coverage.
- Fixed/unknown screens intentionally pillarbox until given a scene-specific,
  oracle-tested extension. The Nintendo frame-600 partial Rare graphic also
  appears in the 256x224 run and is not a widescreen regression.
- Audit vertical levels, underwater stages, bosses, object-pool pressure, and
  save-state restoration with the deterministic tooling used by the ROM hack.
- `$BE8179` is resolved by an exact 197-target source-backed contract. The
  former incomplete route harvest caused real gameplay breakage (a repeating
  jump) and is guarded by `tools/audit_animation_dispatch.py` plus a
  deterministic one-tap regression. Other indirect dispatch sites remain
  independent bring-up risks unless source-audited. The new
  `tools/audit_indirect_tables.py` proves the other 118 cfg contracts exactly
  from disassembly tables/records (zero missing or extra targets), so no
  current indirect allowlist depends only on route harvesting.
- Contact damage and the Jungle death/non-gameplay transition now pass a
  two-checkpoint, three-repeat byte-identical closure contract. Every centered
  transition frame has exact black margins with no raw fallback; map/title,
  bonus, save-select, and cross-level routes remain to be covered.
- The full boot/fixed-screen/map-to-Jungle entry contract applies the same
  trace gate in the other direction. Its three 7,645-frame runs have identical
  traces and zero raw fallback, nonblack centered sides, policy violations, or
  stable-input margin mutations.

### Native widescreen decision trace

`DKC1_WS_TRACE` enables a default-off JSONL record at the exact presentation
boundary. Each frame records scene/source identity, PPU registers, both layout
scores, the selected calibration/grace state, shadow reset/prefill/fallback
decisions, world keys, margin-stat deltas, region hashes, and raw VRAM, CGRAM,
plus both OAM-copy hashes. The analyzer also requires equal PPU/scroll,
camera/world, and center-pixel state before reporting a stable-input margin
mutation. This is the causal substrate for the planned provenance
overlay, first-divergence locator, and lifecycle tools. See
`docs/WIDESCREEN_DEBUG_TOOLS.md` for usage and schema details.

## 2026-08-27 — native macOS host and deadline frame pacing

- **Symptom:** the first native SDL host presented each completed frame and
  then used relative `SDL_Delay` calls. Variable emulation, AppKit, and
  renderer cost therefore appeared as alternating short/long submissions.
  Its eight-frame late guard could also burst several frames after a short
  pause.
- **Root cause/domain:** host presentation timing only. Cartridge state,
  streaming, activation, gameplay coordinates, and generated code were not
  involved.
- **Change:** the arm64 macOS host now schedules the native NTSC cadence
  (60.098811862 Hz) on absolute Mach deadlines. It coarse-waits until an
  adaptive measured-work window, samples input, runs one cartridge frame,
  fine-waits, and presents. A miss over 2 ms re-anchors instead of catching
  up. Pause/resume, single-step, save/load, focus, resizing, fullscreen, and
  aspect changes explicitly re-anchor. `DKC1_FPS_STATS=1` reports submission
  intervals, measured host work, stalls, and re-anchors. This is deliberately
  host-only and does not skip or synthesize cartridge frames.
- **Source/build:** clean USA ROM SHA-256
  `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`;
  source commit `b404cb7` plus the working-tree macOS changes; final measured
  arm64 executable SHA-256
  `6b0c7ebf7e321a199e7e873208475cc42057cccfe44f8bcfc336ae84c76f6dbf`.
- **Determinism:** three independent 600-frame headless runs were
  byte-identical, including full logs. Frame SHA-256 was
  `a7366146a96b47e75d48d3814bfe0859c71f68bcc3fac3d7429befb9b50430dd`;
  WRAM `405229e90a31728901261cf0c9804b1cad668ba78ec99a94ea096010ed8ae792`;
  VRAM `628205f68fcfb17ebff713087d104ca0d896205b38855678dc0578b5386b6aec`;
  CGRAM `ab7733ad35514f04d2ddaacc0298a3d9dfe983857bf465c2b96ce1300eb8c4a1`;
  OAM `44ddd2f478477ebd1c1cd5b99400af48cd46033c59173195f48870e608cec810`.
- **Visible QA:** a real 1,200-frame application-window run on an Apple M3
  Max/Liquid Retina XDR display completed normally. Including three host
  stalls, submission telemetry reported 59.706 Hz average, 16.749 ms average
  interval, 15.330-44.712 ms range, three intervals over 1.25 native frames,
  2.336 ms average measured work, and 26.788 ms maximum work. The pacer
  re-anchored rather than issuing catch-up bursts. These are host-submission
  timestamps, not WindowServer scanout timestamps. Raw output is retained at
  `build/macos/pacing-validation/visible_1200.log`.
- **Validation:** `./build_macos.sh`, 205 Python tests (one unavailable local
  imported-state fixture skipped), `git diff --check`, deep ad-hoc signature
  verification, visible native menu/aspect QA, and the three-repeat headless
  gate passed.
- **Residual scope:** this validates boot/title presentation and host timing,
  not the complete gameplay/cross-layout matrix. Extended gameplay pacing,
  explicit fixed-60-Hz versus adaptive-refresh display comparison, and direct
  compositor/scanout measurement remain untested. No widescreen capability or
  gameplay behavior was promoted by this change.

## 2026-08-29 — macOS 16:10 and full-panel presentation

- **Symptom/first visible failure:** 16:10 appeared narrower than 16:9, and a
  4112x2658 fullscreen capture retained about 81 black pixels at both sides.
  The active raster was 3949x2464, exactly an 11x integer presentation of the
  359x224 display-space image.
- **Root cause/domain:** host presentation plus selectable margin geometry.
  The macOS renderer treated 256/308/342-wide SNES source pixels as square and
  retained integer scaling in fullscreen. The cartridge image, logical camera,
  collision, exits, and timing were not the source of the outer border.
- **Change:** macOS now offers 308x224 symmetric 16:10 (26 source pixels per
  side), while retaining 256x224 native and 342x224 16:9. The SDL host applies
  the SNES 7:6 pixel aspect only at presentation, keeps integer scaling in a
  window, and uses an explicit fractional nearest-neighbor aspect fit against
  the live high-DPI drawable in fullscreen. The texture and native center are
  not resampled or cropped before the final host presentation. A default-off
  `DKC1_START_FULLSCREEN=1` path supports deterministic visible QA.
- **Source/build:** clean USA ROM SHA-256
  `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`;
  source commit `23fa62d` plus the working-tree changes; arm64 executable
  SHA-256
  `a8d3317ecf594b073877664bedff3b7ab1a524181cef83277e232cc2636b5cf0`.
- **Visible QA:** a real paused fullscreen application capture on the
  4112x2658 Mac panel fit the 359:224 presentation at 4112x2566, leaving zero
  side bars and the unavoidable 46-pixel top/bottom remainder. The retained
  downsampled capture at
  `build/macos/fullscreen-validation/visible_16x10_fullscreen.jpeg` measured
  1189x743 at x=0/y=13 inside its 1189x768 frame; SHA-256
  `2d887ad4ed33ed038cf58f16984ff81d0bbcda2e708629e035de0c718a11c0ee`.
- **Validation:** `./build_macos.sh`, 206 Python tests (one unavailable local
  imported-state fixture skipped), `git diff --check`, deep ad-hoc signature
  verification, native aspect-menu QA, and the real fullscreen capture passed.
- **Residual scope:** 16:10 is optional and not a promoted replacement for the
  default 16:9 mode. The complete 40-entrance fresh-entry matrix was not rerun,
  so this record validates host scaling and the observed scene rather than all
  layout/gameplay closures. Filling the remaining 46-pixel vertical bands
  would require crop or distortion and is intentionally not done.

## 2026-08-30 — display-linked macOS traversal pacing

- **Symptom/first visible failure:** horizontally scrolling terrain still
  showed small hitches after the first Metal-vsync candidate. In the tester's
  live run the first attributed whole-host miss was frame 148 (52.191 ms), but
  other 21-29 ms CPU submission intervals occurred while the Mach deadline was
  still 4-10 ms early. The title's near-60 average therefore did not describe
  visible scanout cadence.
- **Root cause/domain:** host presentation timing only. SDL2's Metal
  `SDL_RenderPresent` return is an enqueue timestamp, not a scanout timestamp.
  Skipping the final Mach wait whenever the renderer advertised vsync allowed
  frames to enter Core Animation with a varying amount of queue lead, so
  PRESENTVSYNC by itself did not phase-lock cartridge frames to ProMotion.
  Independent 14-52 ms spikes were real AppKit/window-event work and needed to
  be distinguished from emulation, PPU, diagnostics, audio, and title work.
- **Change:** macOS 14+ now creates a window-bound `CADisplayLink` on a private
  user-interactive run loop and requests 60.098811862 Hz. The main SDL thread
  waits for a new callback, prepares exactly one cartridge frame for its
  `targetTimestamp`, and never catches up a callback that became stale during
  host work. SDL's accelerated Metal vsync remains enabled, while the absolute
  Mach scheduler and its 6 ms adaptive guard are retained strictly as the
  compatibility fallback. Audio production follows the measured callback
  cadence, preventing the tested panel's 60 Hz grant from draining nearly one
  queued sample per frame. The periodic WindowServer title update is off by
  default (`DKC1_LIVE_TITLE=1` restores it). The Mac visible host also accepts
  `SNESRECOMP_INPUT_PLAY`, matching the existing Windows deterministic-input
  path.
- **Diagnostics/A-B:** `DKC1_FPS_STATS=1` now prints display callback cadence,
  stale callbacks/timeouts, target-deadline phase, Metal enqueue wait, and an
  events/input/emulation/PPU/diagnostics/audio/title split for every host-work
  frame over 8 ms. `DKC1_DISABLE_DISPLAY_LINK=1` selects the absolute-clock
  fallback and `DKC1_DISABLE_VSYNC=1` disables Metal presentation sync; both
  switches remain default-off.
- **Source/build:** clean USA ROM SHA-256
  `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`;
  source commit `d38d7ea` plus the working-tree macOS presentation changes.
  The final signed arm64 executable SHA-256 is
  `3b70146ea9377ac07fd19c276e62409e4f821673295238c8a879db074c92f7bf`.
- **Fresh visible traversal:** a 2,640-frame fullscreen 16:10 replay loaded the
  tester's immutable quick state inside Temple Tempest, held run-right, and
  issued regular held jumps until the visible application returned to the next
  world-map node. This exercised scrolling rather than a stationary menu. The
  display link supplied 2,639 measured intervals at 59.881 Hz overall
  (16.700 ms average, 16.667 ms minimum, 29.185 ms maximum), with zero wait
  timeouts. All 21 explicitly skipped callbacks belonged to the asynchronous
  fullscreen setup. After that transition there was no cartridge-frame work
  over one 16.667 ms display interval; the only logged traversal work sample
  over 8 ms was 8.571 ms.
- **Validation:** `./build_macos.sh` completed, all 210 Python tests passed
  (one unavailable local imported-state fixture skipped), `git diff --check`
  passed, the executable links the system QuartzCore framework and bundled
  SDL2, and `codesign --verify --deep --strict` accepted the packaged app and
  nested SDL framework. A final packaged-app smoke run rechecks active Metal
  vsync and display-link startup before handoff.
- **Evidence boundary/residual scope:** a full-screen ScreenCaptureKit probe
  was deliberately excluded as a cadence oracle because encoding the 4096 x
  2648 desktop dropped capture samples and itself introduced an AppKit miss.
  The display link resolved the 60.0988 Hz request to exactly 60 Hz on the
  tested ProMotion panel; gameplay is consequently about 0.16 percent slower
  while audio remains real-time. Fullscreen transition work is still expected
  before traversal begins. This host-only change does not alter cartridge
  state, pixels, camera/collision, streaming, or claim a complete cross-layout
  gameplay matrix.

## 2026-08-30 — fixed-clock macOS traversal pacing supersession

- **Symptom/first bad steady frame:** the display-linked fullscreen cave A/B
  still produced a 29.147 ms submission at traversal frame 133 after warm-up;
  ten later frames exceeded 20 ms, including a 45.835 ms interval at frame
  441. Ordinary frame work was only 1.5-4.2 ms at those misses.
- **Root cause/domain:** host presentation timing only. On the tested
  ProMotion panel, the window-bound callback target stream itself occasionally
  advanced by about 29 ms even when the requested rate was exactly 60 Hz.
  Driving cartridge simulation directly from that variable callback stream
  therefore reproduced the visible traversal hitch. A second blocking SDL
  renderer-vsync gate was unnecessary and could add another quantization
  boundary.
- **Change:** the Mac release path now has one exact 60 Hz Mach-clock authority
  with renderer vsync off. Texture upload and render-copy encoding finish
  before the final wait; only drawable submission occurs at a four-millisecond
  lead. Display-link pacing and renderer vsync remain independent opt-in A/B
  controls. A true display-link half-interval is rejected by measured callback
  spacing, while a normally spaced callback delivered slightly late is kept
  when enough measured work budget remains. The main thread runs at
  user-interactive QoS.
- **Movement-correlated work:** MSU-1 PCM tracks are mapped at pack open rather
  than refilled synchronously through `fread` in the frame loop. Stomp rumble
  is queued to a worker thread. A fixed-clock gap of at least three frames now
  requests the same CoreAudio repreroll used after a long display-link gap.
- **Presentation sampling:** fullscreen defaults to linear host-only sampling
  so a fractional Retina fit does not alternate nearest-neighbor column widths
  while the camera moves. The View menu now provides persistent checked
  `Smooth (Linear)` and `Pixel Sharp (Nearest)` fullscreen choices; both fill
  the same area. Windowed presentation remains nearest-neighbor. The source
  framebuffer, native center, cartridge memories, camera, collision, streaming,
  and gameplay logic are unchanged.
- **A/B evidence:** after a 60-frame warm-up, the 720-frame display-link leg had
  p99 29.1673 ms, max 45.8350 ms, and 11 intervals over 20 ms. The default
  final fixed-clock leg had p50/p95/p99/max
  16.6667/16.6696/16.6764/16.6908 ms, zero steady overruns, 0.0031 ms standard
  deviation, zero audio starvation/drop, and zero engine underflow. Evidence
  is under `build/repros/macos-motion-pacing-20260830/`.
- **Rejected experiment:** a cached widescreen-calibration fast path did not
  reduce render p99 on the exact cave traversal, so it was removed rather than
  expanding presentation-state complexity without measured value.
- **State/build identity:** clean ROM SHA-256
  `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`;
  the exact tester state was preserved outside the repository as external
  evidence with SHA-256
  `4563f96051ae1b7a7b2618b38bf93d36bece208e03e5fd6f64928c32e612992e`.
- **Validation:** `./build_macos.sh` completed; 223 Python tests passed with
  one unavailable imported-state fixture skipped; `git diff --check` passed;
  three exact 780-frame headless replays were byte-identical in framebuffer,
  WRAM, VRAM, CGRAM, both OAM domains, and audio; the real fullscreen app
  visibly filled the panel and exposed native Game, Music, and View menus; the
  checked fullscreen-scaling submenu was exercised in both Pixel Sharp and
  Smooth modes; replacement track 12 played with stock effects; and deep
  strict code-sign verification passed. The `v0.0.4` release asset is
  identified by its published SHA-256 sidecar.
- **Residual scope:** the asynchronous fullscreen Space transition can still
  block AppKit during the first few startup frames and is excluded by the
  warm-up. This is a host-only traversal/presentation fix; it does not promote
  a new cartridge widescreen capability or claim a complete 40-entrance matrix.

## 2026-08-30 — decoupled 120 Hz Metal scanout and sharp-bilinear scaling

- **Symptom/domain:** after the fixed 60 Hz Mach-clock work, traversal was
  substantially better but still not visually perfect on the ProMotion Mac.
  The earlier trace ended at CPU submission and could not distinguish a game
  cadence miss from a drawable scanned late by Core Animation. This change is
  entirely in host presentation; cartridge state and the native 256 x 224
  image are unchanged.
- **Change:** a native `CAMetalDisplayLink` now owns a `CAMetalLayer` overlay
  on the SDL window. The main thread continues input, emulation, PPU rendering,
  and audio at one absolute 60 Hz deadline, then copies each complete frame and
  immutable camera/PPU metadata into a three-slot queue. The display thread
  starts with one buffered frame and normally scans each source twice at 120
  Hz. It may repeat a completed frame but never advances game state or performs
  a short catch-up. Focus and minimize pause the link and discard stale
  host-only queue contents before resume. Setting
  `DKC1_DISABLE_METAL_PRESENTER=1` retains the SDL compatibility path.
- **Sampling:** the View menu now exposes three persistent fullscreen modes.
  `Sharp Bilinear` is the new default, using a narrow approximately
  one-output-pixel blend only at source-texel boundaries; `Smooth (Linear)` and
  `Pixel Sharp (Nearest)` remain selectable. All use the same maximum-area fit
  and change no source pixels.
- **Physical evidence:** `DKC1_SCANOUT_LOG` writes the
  `dkc1.scanout.v1` trace from drawable completion, including actual
  `presentedTime`, target timestamps, source/host frame, repeat index, queue
  integrity counters, camera X/Y, and BG1-BG3 scroll. A clean visible sample
  after 120 warm-up presentations recorded 271 physical draws at approximately
  120 Hz: p50/p95/p99/max spacing was
  8.333333/8.337395/8.337888/8.339292 ms, 135 source frames were repeated twice,
  and there were no missing/backward source frames, queue drops/skips, or
  starved callbacks.
- **Determinism/state identity:** the clean ROM SHA-256 remains
  `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.
  The immutable Slip-Slide Ride state and 780-frame uphill/downhill input were
  replayed three times. All result files have SHA-256
  `5ed0caf63e9da2b38ec8c118cb1ccf24ef616c08d92665e189de4632156f172e`;
  final framebuffer/WRAM/VRAM hashes are respectively
  `01bad752bf22b100b83384b92d32b280d46968f2b0e4ee7bdfcbd94d215b1935`,
  `1ded5bfb40ca4472b8f49ae33cc89efd51fac395754f19d956c37fa39ba8425a`,
  and `7baf4eb402e9570852877c4f71aab3b60588de78c320167be2fe428bd24214fa`.
- **Validation:** `./build_macos.sh` completed, 224 Python tests passed with one
  unavailable local imported-state fixture skipped, `git diff --check` passed,
  deep strict code-sign verification accepted the app, and the executable
  links Metal and QuartzCore. The real fullscreen application and all three
  checked View-menu choices were inspected.
- **Residual scope:** later visible samples kept perfect queue/source integrity
  while actual drawable p99 rose to about 12 ms and source-transition p99 to
  about 20.5 ms; their CPU cadence remained near 16.667 ms. Covered or occluded
  windows also produce a zero `presentedTime` and are excluded. The remaining
  physical scanout variability is therefore tracked as an open host-compositor
  issue. This host-only change does not claim a new cartridge widescreen
  capability or a complete 40-entrance matrix.

## 2026-09-03 — toggleable Baby Kong reference mod

- **Decision:** implement the requested Baby Kong option as Kiddy Kong from
  DKC3, but keep both Nintendo ROMs and every extracted asset outside Git. The
  exact 4 MiB headerless North American DKC3 ROM is hash-gated, and all 354
  mapped gameplay frames plus the active palette are decoded only into process
  memory. The source map contains names, offsets, and sizes derived from
  H4v0c21 DKC3 disassembly revision
  `bed96892f5e85eabd5c920306f00b361c2e1f34c`.
- **Presentation:** the host correlates the active Donkey actor with a
  contiguous OAM range, uses the PPU's bounded remove-from-composite capture,
  and draws the selected Kiddy frame at the same actor anchor. Identification
  failures retain stock Donkey rather than removing an uncertain sprite.
  Diddy and every unrelated OBJ remain cartridge-rendered.
- **Gameplay:** isolated, testable tuning preserves DKC1 collision and actor
  logic while adding capped run/roll momentum and a shorter, heavier jump arc.
  Kiddy walk, run, roll, jump, land, idle-look, and swim groups are selected
  from observed DKC1 state. DKC3 team-up, water-skip, and game-specific player
  states are not reconstructed and remain explicitly outside this milestone.
- **Toggle:** macOS exposes **Mods > Baby Kong** and **Choose DKC3 ROM...**;
  path and toggle preference persist, while `DKC1_BABY_KONG_ROM` and
  `DKC1_BABY_KONG` provide deterministic launch control. Missing or invalid
  private input makes enablement a no-op.
- **Evidence:** a zero-frame render of the existing external Slip-Slide Ride
  snapshot retained the established disabled framebuffer, WRAM, VRAM, CGRAM,
  and OAM hashes. The enabled leg changed only the framebuffer at that state;
  a 60-frame jump-run leg changed gameplay WRAM and rendered a moving Kiddy
  frame. A mismatched ROM kept the exact stock framebuffer and WRAM hashes.
  These private images and WRAM dumps stayed under `/tmp`. All 230 Python
  tests passed with one unavailable imported-state fixture skipped;
  `./build_macos.sh` completed and deep strict code-sign verification accepted
  the resulting app.

## 2026-09-03 — Baby Kong visual-layout correction

- **Acceptance failure:** the first implementation passed its source tests and
  state/hash checks but failed visible gameplay review. Kiddy was assembled
  from scrambled 8x8 tiles, and Donkey reappeared during the jump arc. The
  earlier nonvisual evidence was therefore insufficient for this feature.
- **Root causes:** the decoder treated each 16x16 piece as four consecutive
  ROM tiles. DKC3 instead references a 16-tile-wide virtual VRAM sheet whose
  source data may be split across two DMA segments. Separately, the replacement
  policy treated DKC1 actor Y as screen Y; that coincidence held on the ground
  but failed as the airborne actor coordinate and camera diverged.
- **Change:** sprite pieces now resolve through the header's group-1 count,
  group-2 destination/count, and the virtual 16-tile row stride. Donkey's OAM
  identity remains a contiguous, matching-attribute run near the validated
  actor X, without using the unstable actor Y. The renderer captures that run
  across the visible height and aligns Kiddy's opaque lower edge to the native
  sprite pixels after scanout, including OAM that wraps through scanline 255.
- **Provenance:** the format interpretation was cross-checked read-only against
  RainbowZ Editor revision `3a8badfec278ba11c1581ea3df02463077666619`.
  Its all-rights-reserved code, comments, binaries, and assets were not copied.
- **Visible evidence:** the exact external Jungle Hijinxs snapshot and
  500-frame route were recaptured with frames sampled every ten frames. Manual
  inspection covered grounded motion, takeoff, the wrapped high point,
  descent, and landing. Kiddy remained coherently assembled and present at all
  ten selected checkpoints (relative frames 200, 240, 300, 320, 330, 340,
  350, 360, 400, and 490); Donkey did not remain or reappear. The private PPM,
  PNG, OAM, and WRAM evidence stayed under `/tmp`.
- **Validation:** all 231 Python tests passed with one unavailable local
  imported-state fixture skipped; `./build_macos.sh`, `git diff --check`, ZIP
  integrity, and deep strict code-sign verification passed. The disabled
  zero-frame Slip-Slide oracle retained framebuffer hash
  `23d8a78dabff683f2959f25e17736265b3815c6cb5d4a3a4ad28323270db6b0e`.
  Enabling Baby Kong changed only that framebuffer: WRAM, VRAM, CGRAM, PPU
  OAM, and source OAM retained their disabled hashes. The rebuilt arm64 ZIP is
  `2e4188b5e0a236b6cea80a3c125333e93aa5e8f23d13e4d2930b3e164d034329`.

## 2026-09-03 — Baby Kong animation-state correction

- **Acceptance failure:** visible play after the layout correction still showed
  Kiddy floating in a mostly fixed jump pose during ordinary movement. The
  sprite was assembled correctly, but the selected animation was wrong.
- **Root cause:** DKC1's grounded Donkey actor normally carries Y velocity
  `-$0300`; the mod treated every nonzero Y velocity as airborne. Live
  per-frame lifecycle traces proved that grounded idle, walk, and run use
  actor state `0` with animation IDs `1`, `3`, and `2`, while jump, roll, and
  hurt use IDs `5`, `24`, and `16`. The old `state >= $20` swim shortcut also
  mislabeled entrance and transition states.
- **Change:** the cartridge's semantic Donkey animation ID now selects the
  closest Kiddy group for the complete ID range `$0001-$0068`. Looping groups
  cycle, one-shot groups stop on their last pose, and jump frames follow the
  actual rise/fall velocity only after the animation ID identifies a jump.
  Grounded movement is based on observed actor states `0`, `18`, and `19`;
  state `1` owns airborne tuning. Ground poses remain foot-aligned, while
  airborne, swimming, rope, and suspended poses align by opaque center.
- **Provenance:** Donkey animation ID meanings were cross-checked against the
  GPL-3 Yoshifanatic1 DKC1 disassembly revision
  `c2080f40469c716923f550706509a0d354229841`. Only numeric identifiers and
  semantic names were consulted; no assembly, comments, graphics, or data were
  copied.
- **Visible evidence:** the same external Jungle Hijinxs snapshot was replayed
  with the mod enabled. Manual inspection of 66 ten-frame captures covered the
  entrance, idle, walk, roll, landing, and hurt sequence, and a second five-
  frame capture set covered takeoff, rise, apex, descent, enemy bounce, and
  landing. Kiddy displayed distinct poses throughout and Donkey did not
  reappear. All PPM/PNG images, traces, and private snapshots remain under the
  ignored `build/repros/` tree.
