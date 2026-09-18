# DKC1Recomp — complete tool reference

Companion to `SKILL.md`. Everything documented here lives in the repo;
paths are repo-relative. `<rom>` = headerless DKC1 USA v1.0.

## Build scripts (repo root)

| script | purpose |
|---|---|
| `build_host_tools.bat` | Isolated tool-session build (own obj dir/exe names, never contends with the primary session): `build/dkc1_headless_tools.exe`, `dkc1_desktop_tools.exe`, `dkc1_layer_capture.exe` |
| `build_host.bat` | Primary build: `dkc1_snesrecomp_headless.exe`, `dkc1_desktop.exe` |
| `scripts/package_windows.py --output DIR` | After a clean committed `build_host.bat`, creates the native Windows ZIP, embedded commit/file manifest and SHA-256 sidecar from a fixed allowlist. Rejects dirty or stale executable identity; never gathers private ROM/state/build directories. |
| `build_macos.sh` | Builds and signs the stock HD app plus its bundled `DKC1Recomp-HD-Dixie` helper. When `generated/snesrecomp_dixie` is absent, pass a verified clean ROM so `scripts/generate_macos_dixie.py` can synthesize the pinned mod image and generate its private AOT sources. |
| `build_host_noadapt.bat` | Builds from a generated tree WITHOUT the widescreen adapters (`build/gen_noadapt`) — the no-adapter oracle used to prove adapter inertness |
| `build_phaseguard_headless.bat` | Prefetch-phase-guard instrumented headless |
| `rebuild_widescreen_runtime.bat` | Regenerate + rebuild after recomp/cfg changes |
| `link_desktop_candidate.bat` / `rebuild_diagnostics_candidate.bat` | Link/rebuild under candidate exe names while a running visible exe holds the standard name |
| `build/link_desktop_retry.bat` | Link tool-session desktop to `_new` name, then retry the standard name |

All inject build identity (`git commit(+dirty) / config / timestamp`) shown
in the window title, debug panel, and written to `<state>.buildinfo.json`
sidecars; loading a state from a different build warns.

## Hosts

- **`dkc1_desktop_tools.exe <rom>`** — visible debugger. Dark-themed,
  menu bar (File: quick/save/load state via dialogs, export bundle;
  Emulation: pause/step; View: fullscreen, panel, provenance, FPS,
  layers). Keys: arrows+Z/X/S/A/Q/W game input, Enter=Start,
  F1 provenance overlay, F2–F6 layer isolation, F7 pause, F8 step,
  F9 export repro bundle, F11/F12 quick save/load, Alt+Enter fullscreen
  (Esc returns), Esc quit. **Click any pixel** → provenance report
  (world/tile/entry/writer/OAM/actor) in the panel.
- **`dkc1_headless_tools.exe <rom> [frames]`** — deterministic runner;
  prints end-of-run frame/WRAM/VRAM/CGRAM/OAM sha256 + audio fnv1a.
- **`dkc1_layer_capture.exe <rom> <state> <outdir>`** — same-frame layer
  isolation: reloads the snapshot per mask → backdrop/composite/BG1/BG2/
  BG3/OBJ PPMs + occupancy masks; aborts if frames differ.

## Environment variables (all default-off)

**Run control:** `DKC1_WIDESCREEN` (default 1; 0=native 4:3),
`DKC1_DIXIE` (macOS/Windows process-selection override: 1=Dixie, 0=stock),
`DKC1_DIXIE_ROM` (automation-only clean/pinned-mod ROM override),
`DKC1_WIDESCREEN_EDGE` (level-wall presentation: `glide` default = the
inward clamp released over eight margins of travel, `reflect`, `bars`,
`shift` = pre-policy inward clamp, the A/B reference for wall reports),
`DKC1_SCRIPT` (route .dks), `SNESRECOMP_INPUT_PLAY` (raw input replay),
`DKC1_STARTUP_SCRIPT` (macOS input/wait-only .dks route run unthrottled before
the first interactive frame; state and checkpoint directives fail closed),
`DKC1_ALLOW_ROM_SHA256` (development-only exact 4 MB modified-ROM pin;
retail verification remains the default and malformed/non-matching pins fail),
`DKC1_SAVESTATE_INPUT` (load state at boot), `DKC1_SAVESTATE_OUTPUT` /
`DKC1_SAVESTATE_SAVE_AT` (save at frame), `DKC1_SRAM_INPUT`,
`DKC1_SUPERZSNES_STATE` (import emulator state bundle),
`DKC1_ROUTE_FRAME_LIMIT` / `DKC1_ROUTE_AUTOCLOSE_MS` /
`DKC1_ROUTE_RESULT` (visible-host automation), `SNESRECOMP_FPS`,
`DKC1_PRESENT_HZ` (optional 30-240 Hz presentation-cadence override;
normally the Windows host locks to the display through the Direct3D 11
swap chain's frame-latency waitable object at an exact 60 Hz divisor),
`DKC1_PRESENTER=gdi` (Windows: force the GDI fallback, paced by `DwmFlush`),
`DKC1_SCALING=sharp|nearest|linear` (Windows sampler; sharp bilinear is the
default), `DKC1_SQUARE_PIXELS=1` (Windows: 8:7 square pixels instead of the
7:6 SNES pixel aspect), `DKC1_WINDOW_SCALE=1..8` (Windows windowed integer
scale; default follows the monitor DPI), `DKC1_FULLSCREEN=1` (Windows: start
fullscreen), `DKC1_PRESENT_WARMUP_MS=0..5000` (Windows: present the initial
frame for that long before frame 1 so the compositor's one-time
presentation-path change lands outside a timing capture; evidence runs use
2500, play leaves 0),
`DKC1_USE_DISPLAY_LINK_PACING` (macOS A/B: opt into window-bound display-link
cadence), `DKC1_KEEP_RENDERER_VSYNC` (macOS A/B: restore blocking SDL Metal
vsync), `DKC1_DISABLE_DISPLAY_LINK` / `DKC1_DISABLE_VSYNC` (explicit negative
overrides), `DKC1_DISABLE_METAL_PRESENTER` (macOS A/B: restore SDL
presentation). The macOS release default keeps emulation on one fixed 60 Hz
Mach authority while a host-only Metal display link presents immutable frames
independently at the requested 120 Hz panel cadence.

**Smooth animation / frame generation (Windows, default off):**
`DKC1_FRAMEGEN=1` (View menu / F10) smooths held sprite poses at 60 Hz using
four frames of look-ahead (66.7 ms visual delay). On 120/240 Hz displays an
immutable midpoint is submitted by a worker between each pair of delayed
real images. The native framebuffer and guest state are unchanged.
Unsupported composition/overlaps retain raw output. See `docs/HOST_PACING.md`
and `docs/FRAMEGEN_REVIEW.md` for constraints and measured scope.
`DKC1_FRAMEGEN=force` tests the 120 Hz software path on a 60 Hz display.
`DKC1_FRAMEGEN_DUMP_START/_COUNT/_DIR` writes raw prev/cur, delayed display F,
and its following mid F+0.5 PPMs plus metadata (`pose_source_frame`,
`pose_actors`, `pose_pixels`, `pose_mismatch`). `DKC1_POSE_LOG` writes OAM
track/pose-interval JSONL; `DKC1_POSE_DEBUG=1` prints composition failures.
`DKC1_FRAMEGEN_TWEEN=1/2` enables the superseded adjacent-frame flow/dissolve
for A/B diagnosis only (default 0). `DKC1_FRAMEGEN_LIVE_SCROLL=0` also selects
an unsupported old path. The v4 pacing trace adds `mid_after_frame`,
`real_to_mid_ms`, `mid_to_real_ms` to the existing midpoint statistics.

`python tools/verify_framegen.py --exe <desktop.exe> --rom <rom> --state
<immutable.state> --output <new-dir> --wide 0|1` runs one off capture and at
least three repeats at 60/forced-120, checks every raw/display/mid image and
WRAM/OAM byte and final checkpoint VRAM hashes, then separately gates
undumped timing/audio. `--input <route.dks>
--frames <count>` supplies another deterministic schedule. Runs are serial,
restore a private root, and close gracefully. Force mode is not scanout proof.
Waitable DXGI timing also gates missing statistics, held/early refreshes and
disjoint statistics; passing CPU budgets alone is not a display-cadence pass.
`--capture-window` additionally captures the actual window during the first
dumped run of each enabled mode, recording its process/build/root/input identity.
Window capture is excluded from timing runs. The capture helper temporarily
uses per-monitor DPI coordinates to avoid cropping a scaled Windows window.
`frames_with_generated_poses` counts nonzero `pose_actors`;
`frames_with_generated_pixels` counts changed display pixels, including BG work.

Within enabled frame generation, the Mode 1 background smoother samples each
isolated BG at its own fractional scroll using the same four-frame look-ahead.
`DKC1_FRAMEGEN_BG=0` disables that stage for A/B; `DKC1_FRAMEGEN_BG_SYNC=1`
uses its serial fallback instead of the Windows immutable-frame worker.
`DKC1_BG_MOTION_LOG=<path>` records source phase, filtered pixel count,
`oracle_failed_rows`, row-112 scroll offsets and coarse elapsed time. These
diagnostics are default-off. Native raw images remain unchanged; unsupported
composition fails closed. See `docs/BACKGROUND_SMOOTHING_REVIEW.md`.

`python tools/analyze_bg_frame_steps.py --capture <framegen-dump> --layers
<same-frame-layer-capture> --start <source-frame> --count 16 --cadence 120
--output <motion.json>` follows exact textured BG pixel correspondences in
display F / midpoint F+0.5. `--cadence 60` uses only real display phases.
The independent BG1/BG2/BG3 captures identify the visible seed pixels; ambiguous
or insufficient texture is rejected. NumPy/Pillow required. This is spatial
motion evidence, never a physical refresh/drop counter. See
`docs/BACKGROUND_PACING_REVIEW.md` for the cleared-running reproduction.
Add `--fractional` for subpixel output: this fits actual RGB samples against
the independent native layer references at 1/16-pixel resolution with final
RGB-rounding tolerance. It does not consume proposed runtime motion values.
Ambiguous/insufficient layers remain ungraded, and surviving correspondences
are carried between frames. The original exact-integer tracker remains the
default for unsmoothed captures.

`verify_pacing_soak.py --exe EXE --rom ROM --state ROOT --input ROUTE
--output NEW_DIR [--trace-directory TRACE_DIR] [--queue 1|2]` performs serial
continuous 60 Hz runs. Defaults: 190 x 570 frames, two repeats in windowed and
fullscreen modes. The elevated trace-only `pacing_trace_helper.ps1
-EvidenceDirectory TRACE_DIR` requires a separately provisioned PresentMon.exe.
It accepts only named start/stop capture requests and exits when
`TRACE_DIR/trace-helper-stop` exists. API-only ETW coverage is independently
correlated to host QPC/PID by `analyze_presentmon.py`; DXGI counters remain the
display oracle. `--kernel --kernel-phase audio_ms --kernel-trigger-ms 10`
requests bounded wait stacks and stops at a hitch; this path is unvalidated
(the September 18 WPR stop failed with `0xc5580612`) and is never an
acceptance run. `--log-off` is an observer-effect control, with no host/DXGI
acceptance claim. See `docs/PACING_HARDENING_REVIEW.md` for exact limitations.
`live_test_guard.py` prevents concurrent game windows. Async pacing logs require
their `.status.json` completion sidecar with zero drops/I/O errors.

**Evidence taps:** `DKC1_PACING_LOG` (desktop-host frame work/wait/submit/
present timing jsonl, per-frame frame-generation plan statistics, and on
Windows v5 the swap chain's DXGI frame statistics: which refresh each
present landed on, the scanout oracle; summarize with
`tools/analyze_pacing.py`, which reports refreshes per displayed present
and repeated refreshes), `DKC1_ANIM_CADENCE_LOG` (any
host: per-frame camera and live-actor displayed pose/position jsonl;
summarize pose hold lengths with `tools/analyze_anim_cadence.py`),
`DKC1_SCANOUT_LOG` (macOS physical drawable `presentedTime`, source-frame
repeat, queue, camera, and PPU-scroll jsonl; summarize with
`tools/analyze_scanout.py` while the app is visibly unobscured),
`DKC1_WS_TRACE` (per-frame widescreen decision/hash
jsonl), `DKC1_WRAM_DUMP`=first-last + `DKC1_WRAM_DUMP_PATH` (+optional
`_RANGES`) raw WRAM frames, `DKC1_WRAM_HASH_LOG` (per-frame WRAM
fingerprint), `DKC1_WRAM_OUTPUT`/`DKC1_VRAM_OUTPUT` (final memory),
`DKC1_OAM_LOG` (shadow+PPU OAM per frame), `DKC1_LIFECYCLE_TRACE`
(+`DKC1_LIFECYCLE_SAMPLE_EVERY_FRAME`), `DKC1_INPUT_RECORD`,
`DKC1_SESSION_DIR` (checkpoint output), `DKC1_FRAME_PPM*` (frame images),
`DKC1_AUDIO_PCM`, `DKC1_STATE_TRACE`, `DKC1_STREAM_DEBUG`,
`DKC1_TRACE_PC` (PC probes on interpreter-tier execution).

**Detectors (integrity, all counted + logged):**
`DKC1_INVARIANT_MONITOR`=jsonl|1 — 9-verdict cross-subsystem monitor;
`DKC1_BLANK_SCAN`=jsonl — rendered-blank margin columns;
`SNESRECOMP_WS_CACHE_LOG`=jsonl — scene-local cache out-of-range events;
`SNESRECOMP_WS_RETRODICT`=jsonl — served-margin vs stream-truth
mismatches; `SNESRECOMP_WS_WRITE_TRACE`=1 — per-cell last-writer
attribution (powers click-to-provenance); `DKC1_WS_PROVENANCE`,
`DKC1_MARGIN_PROXIES`/`_LOG`/`_RENDER`, `DKC1_PREFETCH_PHASE_GUARD`,
`DKC1_PREFETCH_TRANSACTION_DEBUG`, `DKC1_WS_COLD_STATE_LOAD`,
`DKC1_WS_FORCE_FALLBACK_FRAME`, `SNESRECOMP_WS_YLOG`.

**Capture:** `DKC1_FLIGHT_RECORDER`=1 (+`_DIR`) — rolling ~60s inputs +
periodic state anchors; F9/auto exports a bundle (states, inputs, WRAM/
VRAM/CGRAM/dual OAM, PPU regs, manifest w/ hashes + build id, layer
captures, post-failure tail). `DKC1_AUTO_EXPORT`=1 — any detector hit
triggers the export. `DKC1_DESKTOP_DEBUG_PANEL`=0 hides the panel.

**Tier2 discovery captures:** default-named `tier2_*.json(l)` files land
in `build/tier2/` (hosts set `SNESRECOMP_TIER2_DIR` automatically);
`SNESRECOMP_TIER2_MANIFEST`/`_JOURNAL` override paths explicitly;
`SNESRECOMP_TIER2_VERBOSE` for detail.

**Trace-hook build** (`build_host_trace.bat` -> `dkc1_headless_trace.exe`,
lean `SNESRECOMP_FUNC_ENTRY_HOOK` mode): `SNESRECOMP_FUNC_PROFILE`=jsonl
per-function call counts/frames/contexts (+`SNESRECOMP_PROFILE_CONTEXT_ADDR`,
DKC1 uses 0032); `SNESRECOMP_WATCH`=addr:len[,...] +
`SNESRECOMP_WATCH_LOG` — bounded WRAM watchpoints reporting net byte changes
at matched generated-function entry/exit boundaries. Parent tails after a
callee return remain attributed to the parent; changes observed with no AOT
window active (including top-level interpreter execution) are explicitly
`host/outside-function-window`, never assigned to the stale previous entry.
Multiple writes that cancel inside one function
window are below this lean mode's resolution; use force_lle + `DKC1_TRACE_PC`
for instruction/store-level attribution. Invalid/overlapping/out-of-WRAM specs
fail closed, and `reverse_watch.py` refuses conclusions from truncated logs.

**Engine diagnostics:** `SNESRECOMP_PPU_PROFILE` (tool build only; aggregate
per-stage renderer CPU time at exit), `SNESRECOMP_DSPOUT`, `SNESRECOMP_*_TRACE_FILE`,
`SNESRECOMP_OFFRAILS_STDERR`, `SNESRECOMP_APU_TOUCH_CYCLES`.
The tool build also retains `SNESRECOMP_STACKBAL_AUDIT`; normal player builds
compile out its per-return hash-table update while preserving the semantic
recomp call stack and interpreter/AOT return handling.

## Route DSL (`recipes/*.dks`)

```
MASK [* N]                 # input mask (hex, snes9x bit order) for N frames
wait ADDR OP VALUE [width|mask|shift|signed|timeout N]
pulse MASK ON OFF [base HEX] ADDR OP VALUE [timeout N]   # press until predicate
checkpoint NAME            # WRAM/VRAM/OAM hashes + wram dump into session dir
state_save PATH / state_load PATH
```
Bits: 1=B 2=Y 4=Select 8=Start 10=Up 20=Down 40=Left 80=Right 100=A
200=X 400=L 800=R. Level-entry edge = frame counter `$0028` reset.
Routes whose first op is `state_load` are dependent legs (sweep skips
them). Contracts (`contracts/*.json`) bind a route to checkpoint
expectations, ws-trace assertions, integrity `budgets` (ratchets), and an
optional `quickload` leg seeded by a state the entry route itself saves.

## Tool catalog (`tools/`)

**Understanding / naming**
- `reverse_watch.py --rom R --route S --address HEX[:len] --before-frame F`
  — who last changed this address before frame F, function-attributed,
  with context and escalation hints (one deterministic forward pass).
- `impact.py ADDR|NAME` — change blast radius: structured callers from the
  exported IDA call graph (exact instruction operands if unavailable),
  dispatch membership, routes that executed it, and required regression
  gates. Pseudocode substring matches are not caller evidence.
- `build_profile_corpus.py --rom R` — all-or-nothing per-route function
  profiles into build/profiles/ (feeds impact/profile_diff). Existing profiles
  are removed before rebuilding; any failed route or missing/invalid profile
  returns nonzero and publishes no partial corpus.
- `capability_manifest.py` — docs/CAPABILITIES.json: per-scene
  host-widescreen status (proven/degraded/centered/unproven), strictly
  evidence-based from successful sweep routes. `proven` requires complete
  calibration and zero raw fallback, aggregate blank serves, gameplay
  pillarbox frames, or unstable margins; blockers are emitted explicitly.
- `ir_validate.py --stage1|--stage2|--stage3|--all` — validation gates for
  the staged 65816 IR (lossless decode, CFG/SSA/width facts, typed-memory
  coverage). A ROM is required only for the stage-1 opcode-byte oracle.
- `irview.py ADDR|NAME [--ssa]` — structured view from the validated IR.
  Instruction-index function labels are seed boundaries, so the renderer
  explicitly marks external tail fallthroughs, unresolved indirect successors,
  CFG/SSA problems, width conflicts, and unreachable blocks. It never silently
  splices a neighboring seed into the selected routine.
- `slice.py --store HEX [--callers --readers]` — static complement of
  reverse_watch: every IR-proven write site covering a WRAM address,
  each with the SSA backward slice of the stored value (constants,
  loads, merges, entry params). `--value-of OPADDR` slices A at one op.
  Validated: `--store 1595` reproduces the damage chain (BFC745 #$0001,
  SteelKeg BFD005 #$0040) that reverse_watch proved at runtime.
- `oracle_spec.py NAME | --emit-all` — per-function differential-oracle
  capture/compare manifests from control-flow-closed IR effects. Proven
  external tail fallthroughs and direct tail jumps are followed; unresolved
  continuations fail closed to `needs-lle-shadow`
  (build/ir/oracle_specs.json). Honest eligibility: indirect writes,
  MMIO ordering, or deep calls mark a function needs-lle-shadow instead
  of pretending state-diff suffices.
- `ir/summarize.py` (run as module) — build/ir/summaries.json:
  per-function proven read/write sets with widths, indexed-ness and op
  sites; feeds atlas ("IR-proven writers" on WRAM view), impact.py
  (write set + data-coupled readers), slice.py, oracle_spec.py.
  Regenerate after any disassembly/rename_map update.
- `structure.py ADDR|NAME` — flat symbolized 1:1 listing (curated RAM
  names, context-qualified define annotations, local cross-reference
  labels); display aid, no reconstructed blocks or semantic claims.

**Mod layer (docs/MOD_LAYER.md)**
- `gen_symbols.py [--show ADDR]` — build/ir/symbols.json: ONE canonical
  generated record per function (names+provenance, proven entry/exit
  M/X, symbolic read/write sets, callers, dispatch roles, runtime-route
  evidence, oracle eligibility). rename_map.json stays the only
  hand-edited name source. Regenerate after summarize/oracle/profile
  updates.
- `gen_wram_header.py [--check]` — runner/dkc1_wram_gen.h: named WRAM
  offsets, little-endian view accessors over live WRAM (never copies),
  actor SoA per-field accessors, struct mirrors. `--check` = staleness
  gate + independent cross-parser address agreement.
- `mod_conflicts.py mods/*.json` — routine-replacement conflicts, WRAM
  write-set overlaps between mods, presentation-class violations
  (presentation mods may not replace gameplay-writing routines), and
  oracle-eligibility / no-runtime-evidence warnings.
- `oracle_run.py FN --rom R --route S --out LOG [--exe EXE]` — one
  differential-oracle capture leg: arms the trace host
  (`SNESRECOMP_ORACLE`/`_RANGES`/`_LOG`, ranges derived from the
  function's oracle spec) and replays a deterministic route; per
  outermost call it logs entry/exit registers, flags byte, WRAM
  ranges, and cycle delta. Zero captures = the route never ran the
  function (not equivalence).
- `oracle_diff.py A.jsonl B.jsonl` — byte-identical = proven-equivalent
  over that route; otherwise the FIRST divergent call with field-level
  breakdown and an upstream-vs-local verdict (entry states matching
  means the function itself diverged).
- `gen_replacements.py --rom R [--bless]` — fail-closed staging for
  DKC1_REPLACE: supported-ROM sha, blessed region-byte hash, proven
  entry-mode match, single defining TU; emits the build override that
  renames the generated variant to `*_original` and links
  `runner/replacements/`. Then `build_host_replace.bat` builds
  `build/dkc1_headless_replace_trace.exe` (`DKC1_REPLACE_DISABLE=1`
  falls back to originals at runtime). Validate stock-vs-replace with
  oracle_run/oracle_diff + end-of-run hashes; see docs/MOD_LAYER.md.
- `coverage_explorer.py` — docs/COVERAGE.md + build/coverage.json: the
  full 256-entrance universe joined against capabilities + sweep
  evidence, with a ranked next-evidence worklist (centered-only scenes
  first — a route already exists; then unobserved *_Main levels).
  Multiple scene variants for one entrance aggregate conservatively: an
  entrance is proven only when every observed variant is proven.
  never-observed = absence of evidence, never assumed-unreachable.
- `promote_bundle.py BUNDLE --rom R [--name N]` — flight-recorder
  capture -> LOCAL regression asset (recipes/promoted/ +
  contracts/promoted/, both gitignored: snapshots are never committed).
  Gates: manifest/ROM/every-declared-file hashes, path-safe promotion name,
  Nx byte-identical end-WRAM replay,
  and match against the bundle's own final.wram.bin
  (--allow-capture-drift records instead of refusing, for captures from
  older builds). Emits a state_load + run-length-MASK replay.dks and a
  contract with scene-identity checkpoints and zero budgets — promoted
  contracts inherit the full ratchet discipline immediately.
- `sync_names.py` — derive `<Base>_StateN` names for dispatch-contract
  targets in literal table-ordinal order (provenance-tagged, curated map
  always wins) -> safe generated `docs/derived_names.json`, consumed by
  the state catalog and structure display tools; never writes `reference/`.
- `state_catalog.py` — docs/STATE_MACHINES.md: per state machine, each
  state in literal dispatch order, with conservative static refs/immediate
  stores; `--lifecycle` marks matching observed NorSpr actor states.
- `profile_diff.py A [B]` — coverage + "functions exclusive to run A"
  behavioral isolation from trace-build profiles. Its coverage denominator is
  the exact address-bearing `CpuState` alias declaration set in `funcs.h`, not
  M/X variants or handwritten helpers.
- `poke_test.py --state S --set ADDR=HEX --run N --expect EXPR` — WRAM
  fault injection (proves downstream reaction, not natural production).

**Navigation / knowledge**
- `atlas.py ADDR|7EXXXX|name:TERM [--callers] [--json]` — unified query
  across IDA names+descriptions, disassembly+pseudocode, live recomp
  variant, dispatch contracts, WRAM labels, known issues.
- `export_ida_dispatch.py [--apply]` — push cfg dispatch contracts into
  the seeded IDA DB as user xrefs (idat headless).
- `ingest_dkc1_disasm.py` — disassembly ingestion used for seeding.

**Regression / sweeps**
- `analyze_pacing.py LOG [--warmup N] [--json]` — summarize desktop
  scheduler submit cadence separately from emulation/render work and GDI
  completion; v1 and v2 pacing logs are accepted.
- `analyze_anim_cadence.py LOG [--warmup N] [--min-samples N] [--json-out]`
  — from a `DKC1_ANIM_CADENCE_LOG` capture, report per sprite id how many
  frames each displayed pose is held (effective animation rate at 60 Hz)
  and camera/actor pixels-per-frame, i.e. what presentation interpolation
  can and cannot smooth.
- `run_regression.py CONTRACTS --rom R [--json-out]` — 3×-identical gate
  (checkpoints + end-of-run renderer/audio hashes + integrity budgets),
  entry + quickload legs.
- `level_sweep.py --rom R` — run every standalone recipe; grades
  calibration, raw-fallbacks, blank serves, pillarbox-in-gameplay, margin
  instability, cache OOB, OAM X-high-loss signature.
- `make_dashboard.py` — regenerate `docs/DASHBOARD.md` + `dashboard.html`
  from results + sweep + `KNOWN_ISSUES.json`.
- `fresh_entry_stress_sweep.py`, `grade_fresh_entry_sweep.py`,
  `world_map_fresh_entry_sweep.py`, `snapshot_widescreen_stress.py`,
  `run_imported_state_suite.py`, `triage_stress_lifecycle.py` —
  entry/state stress suites.
- `check_widescreen_capability_floor.py REPORT` — fail-closed release ratchet
  for the exact committed 40 gameplay entrances. It rejects a lost entrance,
  nondeterminism, a failed widescreen grade, raw margin pixels, terrain misses,
  and unexpected centered/4:3 gameplay.

**Divergence / differential**
- `first_divergence.py --rom R --script S --frames N [--profile]` —
  stock-vs-wide first-divergence locator (resolve-then-replay, full-WRAM
  fingerprints, intended-differences profile =
  `contracts/wide-intended-differences.json`).
- `compare_widescreen_regions.py` — region differ.
- `bisect_transition_contamination.py`,
  `transition_contamination_sentinel.py`, `detect_legacy_width_cull.py` —
  transition/contamination hunters.

**Widescreen margin analysis**
- `analyze_ws_trace.py` — grade a `DKC1_WS_TRACE` (policy violations,
  fallback/blank counts, decision stats).
- `analyze_retrodiction.py LOG` — cluster served-vs-stream mismatches
  (attribute-byte vs wrong-tile, worst columns).
- `verify_shadow_localization.py`, `verify_vertical_rope_margins.py`,
  `verify_prefetch_soft_fallback.py`, `verify_margin_proxy_ab.py`,
  `build_margin_proxy_manifest.py`, `verify_widescreen_savestate.py`,
  `verify_blank_scan_detector.bat` — targeted margin verifiers.

**Object lifecycle / OAM**
- `lifecycle_by_source.py TRACE [--html-out]` — re-key lifecycle events
  by authored source record; FREED-IN-VIEW/THRASH flags; swimlane HTML.
- `oam_inspect.py` — shadow-vs-PPU OAM with 9-bit X decode, lost-X-high
  windows, DMA-lag streak logic.
- `analyze_persists.py` — grade wide-persists-stock-culls from raw WRAM.
- `audit_prefetch_phases.py`, `audit_prefetch_wram.py`,
  `audit_prefetch_transaction.py`, `analyze_prefetch_write_sets.py` —
  stock-vs-wide object phase auditors (match by source, conservative
  verdicts).
- `export_timeline.py` — event timeline export.

**Dispatch**
- `resolve_dispatch.py`, `audit_animation_dispatch.py`,
  `audit_indirect_tables.py`, `audit_dispatch_contracts.ps1` — harvest/
  verify indirect-dispatch targets feeding `recomp/*.cfg` contracts.

**Repro minimization**
- `macro_minimize.py INPUTS --predicate '{...}' [--snapshot-input]` —
  ddmin input shrink with 3×-consistency soundness.
- `minimize_bundle.py BUNDLE --rom R --predicate '{...}'` — minimize
  straight from a flight-recorder bundle (replays from its anchor).
- `verify_flight_bundle.py`, `verify_wram_dump.py` — evidence validators.
- `run_route_recipe.py` — single-recipe runner.

**Visible-host automation (PowerShell)**
- `launch_visible_snapshot.ps1`, `capture_visible_snapshot_library.ps1`,
  `validate_visible_snapshot_library.ps1`, `capture_process_window.ps1`.

## Key WRAM addresses (opcode-verified; full dictionary via atlas)

`$0028` frame ctr (resets at level entry) · `$0032` mode · `$003E`
entrance · `$0500/$0504` joypad held/pressed (P1) · `$088B/$0895` camera ·
`$1B23/$1B25` camera bounds (in-level: upper ≥ $100) · scanner `$00A0/A2/
A4` + window `$00EF/$00F1` · actor arrays indexed by even slot `$02..$32`:
id `$0D45`, source `$15FD`, x `$0B19`, y `$0BC1`, state `$1029`, anim
`$10D1`, pose `$0AE5/$0D11` · events `$1595` (**consumed same-frame**;
$40 damage, $01/$20 death → `Player_HandleHitEvents $BFA0F7`) ·
bookkeeping `$192B` len $100 · collision flags `$12A5` · invuln `$11A1`.

## Knowledge sources (read-only; addresses join them — use the atlas)

- `reference/disassembly/` (in-repo consolidated copy; original at
  `D:\Downloads\DKLR\DKC1_Disassembly\`) — labeled disassembly + RAM map
  (semantic intent; its `Custom\Patches\*Widescreen*` and `ROM_Map_HACK_*`
  belong to the LEGACY hack — ignore for recomp work)
- `reference/disassembly/DKC1/Pseudocode/` — mechanical C lift +
  lossless listing + `instruction_index.csv` (a stale recomp snapshot;
  never diagnose recomp behavior from it)
- `reference/disassembly/Tools/IDA/DKC1_U1.i64` +
  `work\rename_map.json` — curated names/descriptions + our runtime
  dispatch xrefs; rebuildable headlessly
- `reference/legacy-widescreen/` — the retired SuperZSNES-era emulator
  hack (worklogs/tools; prior art only, never current workflow)

## Aquatic presentation A/B switches (2026-09-06)

`DKC1_WS_PIXEL_BOUNDARIES=1`, `DKC1_WS_LIVE_SCROLL=1`, and
`DKC1_WS_WALL_ADJACENCY=1` independently opt into the DKC2/DKC3 presentation
backports. All require the exact value `1` and default off pending the full
40-entrance gate. The first protects native pixels in straddling 4bpp chunks
and repeat bands; the second uses live scanline scroll for shadow X; the third
uses structurally constrained map adjacency for vertical wall continuation.
Trace additions: root `presentation_features` is a 1/2/4 bitmask;
`boundary_adjacency_tiles` counts successful adjacency-derived 8x8 entries and
is included in `boundary_continuation_tiles`. Failed/ambiguous chains leave
the original decoded tile intact. `DKC1_ASPECT=16:10` now selects the existing
308x224 mode in headless and layer-capture hosts when widescreen is enabled.
Use `DKC1_WIDESCREEN_EDGE=shift` for an unbiased native-center oracle.
See `docs/WIDESCREEN_AQUATIC_BACKPORTS.md` for commands, acceptance scope, and
the paused local candidate bundle.

## September 6 Mac host and cache-boundary additions

- `DKC1_WS_SCROLL_REBASE=1`: default-off, currently calibrated cache rebuild on the exhausted-window frame. WS trace feature bit 8 and `decision.cache_rebase` identify it. `verify_shadow_localization.py` requires a calibrated cold commit for any marked origin change. See `docs/WIDESCREEN_WATER_FLASH.md` and `recipes/croctopus-cache-crossing.json`.
- Native **Game → Controls and Assist…** exposes source routing, remapping, analog deadzones, and opt-in rewind/3× fast-forward. Default Assist holds: Backspace/Tab or left/right triggers. Game time remains canonical. Rewind memory is capped at 128 MiB; actual history duration depends on serialized-state capacity. See `docs/HOST_ADOPTION_IMPLEMENTATION.md`.
- `DKC1_ASSIST_TEST_INPUT=<input file>`: default-off Mac-only host-action schedule using the existing hex/repetition format; masks 1=rewind, 2=fast-forward, 4=quick-save, 8=quick-load. It enables Assist only for that run. Save/load actions operate the normal quicksave path; do not use those bits against a tester's only state. `DKC1_ASSIST_TEST_LOG=<path>` optionally records host tick, host/guest frame, pops, and history depth. Gameplay playback stays independent and is abandoned after a rewind/load.
- `DKC1_PAUSE_AFTER_FRAME=<positive host frame>`: default-off Mac exact-frame pause. Clears gameplay and host-action schedules and stops sound/rumble; preserves the actual submitted pixels for window capture. `DKC1_SAVESTATE_OUTPUT` also works at graceful Mac shutdown. Use private paths, not the user's normal slots.
- `DKC1_PACING_LOG` adds `audio_ratio`, `audio_fill_average`, and `audio_target_frames`. Canonical production is unchanged; only mixed host PCM is resampled. `DKC1_SCANOUT_LOG` repeat goal 0 means unqualified/non-integer cadence using target timestamps; goals 1–4 are qualified divisors.
- Flight bundles and post-failure input tails preserve both controllers as six-digit masks. The verifier accepts both historical three-digit and new six-digit masks. Existing bundle schema and hashes remain valid.

## Mac graphics presentation checks (2026-09-06)

- **Escape** opens the native pause panel when windowed (exits fullscreen first). **View → Graphics Settings…** opens its graphics tab. Reconstruct, CRT, and color profiles affect only displayed pixels. Raw plane/WS traces remain the oracle. Settings persist under `GraphicsV1`; use a separate bundle identifier for preference-isolated QA.
- `DKC1_USER_DIR=<absolute existing directory>` overrides the Mac host's normal save/working directory. Use it with a separate QA bundle to protect user slots. It does not isolate NSUserDefaults by itself. Default behavior remains SDL's application support path.
- Optional Mac startup overrides: `DKC1_DISPLAY=flat|crt`, `DKC1_UPSCALER=nearest|bilinear|reconstruct|sharp-bilinear`, `DKC1_SCREEN=raw|crt|composite|trinitron`, `DKC1_CRT_PRESET=living-room|studio|soft`, `DKC1_RECONSTRUCT_MODE=0..4`, and `DKC1_RECONSTRUCT_STRENGTH`, `DKC1_RECONSTRUCT_SOFTNESS`, `DKC1_RECONSTRUCT_SHADING` in 0..100. Invalid numeric settings clamp; omitted variables retain saved/default settings.
- `cmake --build build/macos --target test_macos_graphics`, then `build/macos/test_macos_graphics runner/macos_graphics.metal [existing-output-directory] [input.ppm]` runs offscreen Metal native-pixel, shader, repeated-frame, and cache-invalidation checks. Input is binary P6; omitted input generates an edge/dither pattern. Output contains twelve PPMs. `DKC1_TEST_SCALE=1..16` (default 4) and `DKC1_TEST_PIXEL_ASPECT=1` optionally control output geometry. Exit 77 means no Metal device; exit 1 is failure. These test-only variables do not affect the app.
- Five slots use `quicksave.state`, `slot2.state` … `slot5.state`. Quick Save/Load and their keyboard/Assist actions operate the selected slot. See `docs/GRAPHICS_OPTIONS_PORT.md` for architecture, donor comparison, screenshots, and scope.

## Coral Capers authored seam capability (2026-09-06)

`DKC1_WS_WALL_SEAMS=1` is a separate default-off presentation opt-in. It continues the two verified faces of one offscreen rock-wall junction: the west face uses its authored three-row period; the east face uses source-verified donor triples with an identical native edge cell. Exact source-layout and full junction-byte checks contain it; native pixels and guest memory are untouched. WS trace feature bit 16 and `wall_seam_tiles` identify its use. Preserve this environment setting explicitly when replaying its evidence. See `docs/WIDESCREEN_WALL_SEAM.md` for the exact save, source proof, 16:10/16:9 A/B, and missing Coral Capers fresh-entry gate. This does not grant a general capability to replace populated margin art.

## Local HD scene experiment (default off)

The separate HD app enables these options only when its private scene resources
are bundled; the headless/shared host defaults remain off.

- `DKC1_HD_SPRITES=1`: master 4× presentation switch; F10 toggles it in the app.
- `DKC1_HD_SCENE=1`: full Jungle slice compositor. `DKC1_HD_SCENE_PACK` selects
  hash-keyed materials and `DKC1_HD_SCENE_EXPORT` exports original PAM/JSON data.
- `DKC1_HD_SCENE_PRELOAD=1`: eagerly load the complete level pack at the first
  supported scene, then retain its immutable HD rasters until process exit.
  First run `python tools/build_hd_preload_manifest.py PACK` to validate every
  DKHD header/size and atomically write the sorted `preload.txt` index. Malformed
  or incomplete packs fail closed to original pixels with a startup diagnostic;
  preload mode never falls back to texture-file reads during gameplay. This is
  CPU/shared-memory residency, not a GPU compositor or an expanded scene allowlist.
- `DKC1_HD_METAL=1`: opt into GPU HD composition in the Mac presenter. Requires
  scene/master HD and a successfully preloaded pack. Raw screen color mode is
  supported; other screen color models retain the CPU compositor. Display/CRT
  and reconstruction filters remain available. Unsupported frames, unavailable
  Metal, or exhausted frame slots retain the established fallback. Art is uploaded
  once; immutable frame packets and the HD texture remain resident until GPU work
  completes. The existing scene allowlist is unchanged.
- `DKC1_HD_METAL_VALIDATE=1`: headless macOS CPU/GPU pixel oracle for every eligible
  frame. This deliberately reads GPU output back and runs the CPU compositor;
  never enable it for performance measurement. A mismatch exits with code 22.
- `DKC1_HD_METAL_SHADER`: optional shader source override; its sibling
  `dkc1_hd_gpu_types.h` supplies the shared ABI. The app uses bundled resources.
- `DKC1_HD_METAL_TRACE`: JSONL `frame`, `gpu_ms`, `gpu_error` per GPU submission;
  validation adds `validated_pixels`, `mismatch_pixels`, `first_mismatch`.
  Native GPU timing includes final display filters in that command buffer;
  headless timing includes validation readback. `DKC1_SCANOUT_LOG` records
  `hd_gpu` to identify presented GPU frames. CPU scene trace statistics are only
  emitted when the CPU compositor runs; use audit/Metal traces for GPU coverage.
- `DKC1_HD_SCENE_TRACE`: per-present JSONL with frame, mismatch count/bounds,
  material hits/misses, and object count. Hits/misses count opaque source samples.
  `cache_entries` is the occupied material-slot count (at most 4096);
  `cache_evictions` and `cache_failures` are cumulative entry replacements and
  refused native-raster lookups/allocations. Missing HD artwork is represented
  by material misses, not cache failures. These are host-only diagnostics.
  `resident_materials` and `resident_bytes` describe the preloaded art;
  `material_file_reads` counts cumulative DKHD open attempts (including preload).
  It must remain constant after startup in preload mode, even across eviction.
- `DKC1_HD_SCENE_AUDIT`: low-resolution reconstruction comparison every eligible
  frame, including runs that do not request an HD presentation.
- `DKC1_HD_SCENE_DEBUG=1`: first mismatch coordinates/colors in an armed audit.
- `DKC1_HD_FRAME_PPM`: final 4× presentation. The existing `DKC1_FRAME_PPM`
  remains the unchanged native oracle.
- `DKC1_HD_SEQUENCE=1`: existing `DKC1_FRAME_PPM_PREFIX` sequence exports HD,
  using its normal start/end/step controls.
- `DKC1_HD_RENDER_EVERY_FRAME=1`: headless HD composition each frame for coverage
  and performance checks, even without saving an image sequence.
- `DKC1_HD_PACK`, `DKC1_HD_EXPORT`, `DKC1_HD_TRACE`: earlier DK-only prototype.

Use `upscale_hd_assets.py`, `build_hd_scene_pack.py`, `verify_hd_scene.py`, and
`run_hd_slice.sh` for the private pipeline, validation, and app. Current art
restoration runs locally with Real-ESRGAN. See `docs/HD_SPRITE_EXPERIMENT.md`.
`verify_hd_scene.py ROM OUTPUT --pack PACK --preload` additionally checks that
every eligible HD frame has the complete resident pack and no further DKHD
opens. Its existing guest/native oracle and three-repeat checks still apply.
Add `--metal` on macOS to require pixel-exact GPU output on every audited frame.
Add `--exact-centers --connected-world --polish 65` for the private connected
Jungle experiment. These switches are opt-in, not global scene capabilities.
The Metal comparison still checks the raw compositor; intentional cleanup is
tested independently. Connected resident scenery bypasses much of the context
cache, so its cache-pressure route requires the post-reload oracle and no cache
failures, but does not require 4096 evictions. The cache unit test covers actual
saturation and eviction separately.
For an immutable tester state, add `--cases replay --state STATE --input-play
INPUTS --frames N`. The input file and positive frame count are required; the
report includes their identity alongside the existing build/ROM/state hashes.
`--coverage` writes per-run coverage JSONL and reports supported frames, missing
visible BG/OBJ samples, and frames with BG misses. These are diagnostic totals,
not unique assets or an automatic zero-miss acceptance gate.
`python -m unittest discover -s tests -p test_hd_metal.py -v` checks synthetic
transparency, palette/color math, OAM priority, fallbacks, immutable snapshots,
and frame-pool exhaustion/reuse under ASan/UBSan. The existing Metal graphics
harness also compares CPU-upload and resident-texture inputs across all filters
and inset viewports. See `docs/HD_METAL_COMPOSITOR_REVIEW.md`.

### Connected Nano scenery and sprite edges (private, default-off)

- `DKC1_HD_EXACT_CENTERS=1`: load `background-centers.txt` (`DKHCv001`, sorted
  source SHA-256 and resident material key pairs). BG only; byte equality is
  required and malformed indices fail closed.
- Object silhouette index: preload loads `object-silhouettes.txt` (`DKHOs001`,
  sorted exact native mask/size SHA-256 and resident material key pairs) when
  present, plus optional `object-bases.bin` (`DKHDb001`) registered native
  colors. OBJ only; live CGRAM/night palette changes cannot invent a new
  identity. A silhouette hit reuses resident HD and applies the live palette
  through the authored base colors. The first registered mask wins; later
  duplicate masks are skipped.
  Background center aliases stay isolated. Malformed indices or base blobs fail
  closed. Build with `tools/build_hd_object_silhouettes.py`.
- HD scene prepare loads the resident pack on the first prepare, including fade
  and ineligible scenes. Full-scene composition stays fail-closed for forced
  blank, mosaic, and unsupported rooms, but INIDISP brightness 1-14 keeps HD
  and scales the reconstructed image the same way the PPU does. CGWSEL prevent-
  math alone does not reject a frame when CGADSUB is 0 (level fade-in).
- `DKC1_HD_CONNECTED_WORLD=1`: load `connected-world.bin` (`DKHWv002`), containing
  three grid dimensions, 256 canonical palette colors, and each cell's 64-byte
  material key plus 4096 native BGRA bytes. Requires the verified Jungle scene
  tuple. Registration follows actual PPU scrolling, validates each 8x8 source
  tile, and uses a separate exact mixed-cell fallback only for unverified
  subtiles. Verified world art stays primary even when a neighboring subtile
  changes. Both materials are pinned; CPU and Metal use the same selection.
  BG0 ring-period selection anchors at the visible left edge, including its
  partially visible 32px cell; a native camera crossing the ring boundary must
  not bind that cell to the next world period. Negative world cells fail closed.
  Palette animation does not invent a new source identity. No guest writes.
- `DKC1_HD_COVERAGE_TRACE=PATH`: JSONL `supported`, `scene_guard`, `camera`,
  `native_mismatch_pixels`, `source_samples`, `missing_samples`, `visible_pixels`,
  `visible_missing` (BG0/BG1/BG2/OBJ), and `center_alias_hits`. Visible counts use
  the native opaque top layer; they are not subpixel HD coverage. Expensive
  diagnostic, never enable for performance measurements.
- `DKC1_HD_MISSING_EXPORT=DIR`: exact missing original PAM/JSON, canonical world
  contexts (`DKCWv002` identity prefix), and unknown canonical object candidates.
  A canonical object candidate is not proof its palette was valid when authored.
- `DKC1_HD_SCENE_EXPORT` also writes `source-vram.bin`, `source-cgram.bin`, and
  `source-wram.bin` with the first atlas export.
- `DKC1_HD_POLISH=0..100`: spatial Metal edge cleanup; default 0. The native
  Graphics → HD scenery → Edge cleanup control saves this setting.
  `DKC1_HD_POLISH_DEFAULT` is only used when no saved setting exists. F10 retains
  original/HD comparison; strength 0 shows raw art. All OBJ/HUD bounds, original
  fallback, and reconstruction mismatches are conservatively protected. No
  temporal accumulation or production readback; outputs are pooled per GPU slot.
- `DKC1_HD_FINISH=0..100`: optional full-frame grounded finish after HD
  composition and edge cleanup; default 0. Strength 33 approximates the
  original tested 92% saturation, soft highlight shoulder, and deterministic
  luminance-only grain; 100 extrapolates all three to 3x for a visibly stronger
  comparison. Graphics → HD scenery → Grounded finish previews and saves the
  setting; `DKC1_HD_FINISH_DEFAULT` applies only when no saved value exists.
  The pass is applied exactly once: the GPU scene compositor owns ordinary HD
  scenes, while the macOS CPU upload path owns already-composited 4x fixed-room
  frames such as Hoard and Treehouse. Native 224-line uploads are unchanged,
  and direct GPU texture presentation is not finished a second time. The raw
  CPU/Metal oracle remains the pre-finish texture.

Offline tools require the private captured/generated inputs and Pillow, NumPy,
and SciPy. They do not call an image provider or change the ROM:

```sh
python tools/audit_hd_jungle_map.py --work-dir PRIVATE_POLISH --rom VERIFIED_ROM
python tools/build_hd_connected_pack.py --work-dir PRIVATE_POLISH
python tools/build_hd_stream_boundaries.py --work-dir PRIVATE_POLISH --pack NEW_PACK --captures MISSING_DIR
python tools/hd_sprite_matte.py --source REGISTERED_NANO_DIRECTORY --pack IMMUTABLE_PACK --output NEW_DIRECTORY
python tools/build_hd_object_silhouettes.py --registration REGISTRATION.json --originals ORIGINALS --output PACK/object-silhouettes.txt --pack PACK --merge PACK/object-silhouettes.txt --bases PACK/object-bases.bin
python tools/build_hd_preload_manifest.py NEW_PACK
```

`hd_sprite_matte.py` requires a new output directory, verifies original content
keys, handles registered facings, and breaks hardlinks before changing assets.
It samples the known #505050 sheet matte, fits foreground/matte mixtures only
within two native pixels of the source edge, vetoes source gray paint, and
checks IoU ≥ .93, contour p95 ≤ .75 native pixels, and bounds error ≤ .5 pixels.
Ambiguous silhouettes retain their alpha and receive clean edge RGB. It writes
`matte-report.json`, review PNGs and a complete eager-load pack. Geometry checks
do not replace motion/appearance review. `test_hd_sprite_matte.py` covers gray
interiors, unmixing, thin appendages, canvas preservation and determinism.
See `docs/HD_NANO_POLISH_REVIEW.md` for current evidence and remaining limits.

`extract_jungle_sprite_inventory.py ROM ASSET_INDEX OUTPUT` creates a private
Jungle Hijinxs sprite-family superset. It locks the clean ROM SHA-256, uses the
asset index only for addresses/names, decodes pixels/palettes from the ROM, and
records source hashes, anchors, aliases, and decode failures. It refuses existing
output directories. It includes full families, not only poses visited by a route.

`magnific_precision_experiment.py prepare OUTPUT --source INVENTORY_DIRECTORY
[--tight]` verifies all original BGRA hashes before making native 768×768 plates.
The source directory contains `inventory.json` and `original-frames/`.
Default cells are 96×96; tight deterministic shelves retain six native pixels of
context. Use Magnific MCP separately: simulate cost, upload/finalize, submit
`ultra-sublime`, 4×, sharpness 7, grain 0; record IDs, show/wait, register download.
Retain provider bytes and decoded 3072×3072 PNGs at `raw-results/plate-NN.png`.
`assemble OUTPUT` verifies input hashes/output dimensions, crops fixed rectangles,
restores source alpha, normalizes exact mirrored/symmetric raster equivalents,
and emits PNGs, both-facing `.dkhd` materials, provenance, and a pose viewer.
Sparse alpha uses nearest interpolation when bicubic silhouette IoU falls below
0.93. Fully opaque candidates are accepted only when their verified source is
also fully opaque; accidentally flattened transparent sprites remain rejected.
Generated pixels and geometry scores do not certify internal landmarks or animation.
No assets, ROMs, provider responses, or private states belong in Git. See
`docs/MAGNIFIC_PRECISION_V2_EXPERIMENT.md` for scope and replay evidence.

`magnific_scene_materials.py prepare OUTPUT --captures CAPTURE_MATERIALS...`
verifies each 32×32 background center against its captured atlas's 48×48 context.
Identical centers share a canonical context and explicit runtime-key aliases;
empty centers need no AI call. After MCP generation, `assemble OUTPUT
[--edge-feather 0..4]` crops exact 4× centers, restores nearest source alpha, and
optionally blends the outer native pixels toward source colors to reduce seams.
Default feather is zero; the September experiment selects two. Unknown context
keys still use the existing runtime fallback.

`magnific_banana_hud.py prepare ROM OUTPUT` checksum-locks and decodes the eight
banana frames and ten normal HUD digits. `assemble SOURCE OUTPUT --pack PACK`
requires all exact 4× primitive materials and builds 800 combined normal HUD
rasters (counts 0–99, eight phases), preserving the source layout and crop.
It refuses existing output, checks primitive headers/dimensions/content hashes,
and records every composed source key. It does not alter counters or gameplay.

`check_hd_sprite_alignment.py SOURCE CANDIDATE [--landmarks JSON] [--output JSON]`
compares transparent sprite geometry without modifying or automatically aligning
images. The candidate must have the same canvas aspect and at least 4× resolution.
It reports silhouette intersection/union, symmetric contour distances, bounds,
and optional named landmarks. Landmark coordinates are source/native pixels and
candidate/image pixels. A silhouette pass requires IoU >= 0.93, contour p95 <=
0.75 native pixels, and bounds error <= 0.5. Each landmark defaults to a 0.5
native-pixel tolerance. Missing landmarks cannot claim geometry acceptance.
Exit 0 means geometry passes; exit 2 means incomplete/failed geometry review.
Art quality and animation consistency still require visual review. Uses the
private upscaling venv's Pillow, NumPy and SciPy. Seven contract tests cover
translation, internal-feature displacement, aspect, transparency and invalid data.

`verify_hd_scene.py ROM OUTPUT [--pack CANDIDATE_DIRECTORY]` runs the existing
36-replay native/wide, fresh/idle/walk matrix against a private material pack.
The default remains `build/hd-slice/scene-pack`. Enabled runs compose every HD
frame so walking-only replacements are exercised even when the route ends idle.
The result records the selected pack path and content hash alongside guest and
HD determinism evidence. This does not certify artwork or landmark alignment.

`hd_sprite_pack.py registered REGISTRATION_JSON OUTPUT --originals ORIGINAL_DIR
--groups Idle Walk Run Jump Land Turn Hurt LookUp` builds both source-facing
and horizontally mirrored materials from an explicitly selected registration.
It verifies each original BGRA content hash, dimensions, transparency, candidate
containment and conflicting keys before writing. Opposite-facing HD texels are
an exact mirror of the same candidate, replacing stale earlier art. The output
manifest is `registered-directions.json`; selection is a human review decision,
not an automatic geometry-quality approval. Preserve the baseline in a separate
private directory, and restart the app after installation.

`verify_hd_scene.py` additionally accepts `--cases fresh idle walk directions
actions run hurt hurt-left idle-extended barrel-right barrel-left barrel-jump` and `--export-materials` (exact raster
PAM/JSON files on each first enabled repeat). Defaults retain the 36-replay
matrix. Each selected case runs twelve native/wide, HD off/on, three-repeat
legs. The checked-in `hd-*.dks` routes use the private immutable Jungle root;
they exercise direction changes, ground slap/duck, run/jump and contact damage
while facing either way. `idle-extended` runs 1,800 neutral frames. `final_hd`
is the last eligible composition trace, not proof that the endpoint is HD when
the route leaves the supported entrance. See `docs/HD_ANIMATION_REVIEW.md` for
actual material coverage and remaining animation-art defects.

`barrel-right` (500 frames) and `barrel-left` (940 frames) start at that same
immutable root. They jump past the first enemy, pick up the DK barrel from
opposite sides, carry and turn both ways, then throw in the opposite direction
and wait for recovery. Preserve per-frame material exports to distinguish
actual animation coverage from poses that merely exist in a candidate pack.
`barrel-jump` (870 frames) adds leftward and rightward jumps while carrying,
then throws and waits for recovery. It uses the same immutable root and gates.

`idle-cycle-right` (600 frames), `idle-cycle-left` (660), `bounce-right` and
`bounce-left` (340 each) add explicit material coverage gates. These cases
require a `registered-directions.json` selection containing all 21 Idle and
24 BeatChest poses, or all 16 Bounce poses, in the route's facing direction.
Every required material must exist and its exact original raster key must be
encountered in the first enabled repeat for each aspect. Exports are automatic
for these cases. A deterministic run that skips a required pose fails.
The routes start from the same private immutable Jungle entry root, use only
controller input and finish after recovery. See `docs/HD_BOUNCE_IDLE_REVIEW.md`.

### HD ground slap and transition coverage

`ground-slap-right` (330 frames) and `ground-slap-left` (436) exercise Down+Y
from the immutable Jungle root, then release for 120 recovery frames. The
left route moves clear of the cave and turns through controller input.
`slap-transitions-right` (787) and `slap-transitions-left` (893) exercise
Down-first, attack-first and different release orders. Every case requires
all 30 GroundSlap and 22 Duck poses in the appropriate direction, exact
displayed-raster coverage, every frame eligible for audit, and zero native
reconstruction mismatch. All encountered DK rasters, including other groups
such as Roll, must have installed materials. Checking GroundSlap alone missed
the one-frame Duck transition fallback.

`--dk-originals` selects the complete private original DK PNG corpus (default
`build/hd-slice/reimagined/all-animations/original-frames`). These four cases
require Pillow; use `build/hd-slice/upscale-venv/bin/python`. `--state` selects
an immutable tester root for non-fresh cases; `fresh` still starts from boot.
Use spaces around `*` in `.dks` repeats. The old 380-frame left route entered
the cave and is rejected by the full-frame audit. See
`docs/HD_GROUND_SLAP_REVIEW.md` for exact-state and fresh-entry evidence.

### HD material-cache pressure regression

`verify_hd_scene.py ROM OUTPUT --pack PRIVATE_PACK --cases cache-pressure`
runs 3,347 frames per leg. A controller traversal exceeds 4096 material
identities, then a normal scripted load restores `build/hd-slice/entry.state`
before 966 frames of idle, walking, turning, rolling and jumping. The case
requires actual eviction, zero cache failures and no reconstruction mismatch
in those final 966 frames. Run from the HD checkout with its private root
and pack available; subprocesses use the repository working directory.
The native/wide, HD off/on, three-repeat guest comparison is unchanged.
`tests/test_hd_material_cache.py` compiles the actual compositor cache under
address/undefined-behavior sanitizers and tests full capacity, safe refusal,
repeated eviction, retained atlas pointers and current-frame objects.
See `docs/HD_MATERIAL_CACHE_REVIEW.md` for the observed failure and scope.

### Private Jungle Bonus 1 HD cave

`DKC1_HD_CONNECTED_WORLD=1` also loads optional `connected-cave.bin`, using the
same `DKHWv002` source/HD registration as Jungle. Its BG0 origin is world X
`$6900`; selection is restricted to level 9, mode 1, entrance 6 and the verified
DA:0000/BF00 map tuple and `$6900..$6C00` camera bounds. Bonus return uses the
proven Jungle source tuple even when the entrance ID is 8. Failed source
verification still falls back per subtile. Both world packs preload together.

Cave color math supports a uniform empty color-window span (including its
inverted full-line case). Spatial windows still fail closed, including an HDMA
change after frame preparation. OBJ palettes 0..3 are exempt from color math;
`HdGpuObject.math_exempt` requires a matching shader/header/executable bundle.
Coverage now reports the actual entrance, and rejected scene records include
PPU guard registers. `verify_hd_scene.py` records auxiliary index hashes in
`pack_indices` separately from the unchanged raster-pack digest contract.

```sh
python tools/build_hd_object_composites.py --pack PRIVATE_PACK \
  --captures CAPTURE_MISSING_DIR --libraries EXACT_SOURCE_PRIMITIVES
# Explicitly authorized source reconstruction for covered interleaved OAM cells:
python tools/build_hd_object_composites.py --reconstruct-unowned \
  --pack PRIVATE_PACK --captures CAPTURE_MISSING_DIR \
  --libraries EXACT_SOURCE_PRIMITIVES
python tools/build_hd_preload_manifest.py PRIVATE_PACK
```

This offline assembler reuses installed 4x Nano primitives only where all
opaque source pixels match and cover the entire captured object. One unknown
pixel rejects the composite by default. It can also resolve exact silhouette
aliases through `object-silhouettes.txt` and apply the registered
`object-bases.bin` palette delta before composition. `--reconstruct-unowned`
is an explicit opt-in for OAM-interleaved captures: it reuses approved art per
source-owned native cell and reconstructs the bounded remainder as one continuous
Lanczos-scaled source contour with a continuous ownership mask. This avoids visible
4x cell-grid seams on small impact and smoke effects. It refuses whole-source
invention, caps reconstructed
cells at 25 percent, and records the method and count in provenance. Existing
materials are preserved. Its provenance report lists source keys, art aliases,
offsets, composition method, and reconstruction count; it makes no provider
calls. Use a private candidate pack, and preserve the report from each invocation. See
`docs/HD_NANO_BONUS_REVIEW.md` for measured coverage and residual original sprites.
