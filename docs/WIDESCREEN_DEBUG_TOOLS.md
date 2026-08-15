# Widescreen debug tooling plan

Distilled 2026-08-15 from three sources:

- `docs/WIDESCREEN_HANDOFF.md` — the seven open issues and release gates;
- the SuperZSNES emulator effort (`D:\Downloads\DKLR\DKC-Widescreen-358x224`,
  38 BepInEx plugins + 13 offline tools + `docs/WORKLOG.md` bug history);
- what the snesrecomp runtime already provides (WsShadow stats/debug cells,
  `framedump.c`, snapshots, input playback, the DKC2Recomp script set).

The recomp's structural advantage: we own the process. Atomic WRAM snapshots
are a memcpy of `g_ram`, frame boundaries are exact, input replacement and
replay-from-root already exist, and headless runs uncapped. Most of the
emulator effort's hardest-won infrastructure (Harmony hooks, IL signature
gates, bridge servers) is simply unnecessary here — what transfers is the
*method*: atomic evidence, first-divergence search, lifecycle semantics,
non-conclusion vocabulary, and 3x byte-identical repeat gates.

## Implementation status

- **The visible desktop host is now the primary interactive debugger.** It
  accepts the same `DKC1_SCRIPT`, native snapshot, input playback, checkpoint,
  WRAM dump, OAM, lifecycle, input-recording, and widescreen-trace environment
  variables as the batch host. A separate right-side panel shows route/input,
  mode/level/entrance, camera bounds, scanner range, section state, and which
  evidence taps are armed without covering or modifying the captured game
  framebuffer. `F7` pauses/resumes and `F8` advances exactly one frame while
  paused. A completed or failed route pauses in the visible window for
  inspection rather than disappearing. The headless executable remains the
  right choice for stock-vs-wide replay, minimization, and level sweeps.

- **Tool 1 is implemented.** Set `DKC1_WS_TRACE=1` for
  `dkc1_ws_trace.jsonl`, or set it to an explicit output path. The trace is
  default-off and performs no file I/O or frame hashing while disabled.
  Schema: `docs/schemas/dkc1-ws-frame-v1.schema.json`.
- `tools/analyze_ws_trace.py` summarizes decision counts, unsafe raw-fallback
  frames, prefill refreshes, and the exact frames where margin hashes change
  while VRAM, PPU OAM, and WRAM OAM stay identical.
  Shadow-margin hashes use the geometric margin extent on calibration-grace
  frames, so a frame without a new prefill cannot disappear from the trace.
- **Tools 5 and 6 now have their first implementation.** `F1` in
  `dkc1_desktop` toggles margin provenance; `F2` restores the composite and
  `F3`–`F6` isolate BG1/BG2/BG3/OBJ. The same overlay is available headlessly
  with `DKC1_WS_PROVENANCE=1`. `tools/compare_widescreen_regions.py` hashes
  left/native-center/right independently, requires an exact center by
  default, emits a red-pixel diff, and can gate a comparison on matching
  VRAM plus both PPU/WRAM OAM trace hashes.
- **Tool 2 is implemented (schema v1).** The headless host supports checksum-indexed,
  range-selective atomic WRAM captures. Set `DKC1_WRAM_DUMP=7500-7600`,
  `DKC1_WRAM_DUMP_PATH=<raw.bin>`, and optionally
  `DKC1_WRAM_DUMP_RANGES=0000-01ff,192b-1a2a`. The adjacent JSONL manifest
  and frame index use `docs/schemas/dkc1-wram-dump-v1.schema.json`; verify
  payload length, offsets, ranges, and every SHA-256 with
  `tools/verify_wram_dump.py`. Native snapshot input/output is available via
  `DKC1_SAVESTATE_INPUT`, `DKC1_SAVESTATE_OUTPUT`, and optional 1-based
  `DKC1_SAVESTATE_SAVE_AT`; input and output paths must differ so a route
  cannot overwrite its immutable anchor. `tools/run_route_recipe.py`
  validates `dkc1.route.v1` JSON, compiles it to the native frame-boundary
  runner, and supports fixed input, 1/2/4-byte `wait_wram` predicates,
  masks, shifts, signed comparisons, held input, timeouts, and named raw
  checkpoints. The route manifest hashes the recipe, verified ROM, runner,
  and optional anchor. These snapshots use the recomp runtime's native
  format; SuperZSNES `.szst` files cannot be loaded by the recomp.
- **Tool 3 is implemented.** `tools/first_divergence.py` runs an identical
  native/wide route twice, fingerprints every full-WRAM frame in order, then
  captures and classifies raw windows around the first differing frame. A
  real 7,600-frame Jungle route currently reports `no_divergence` rather than
  hiding an expected camera/window delta.
- **Tool 7 is implemented.** Set `DKC1_OAM_LOG=<prefix>` and inspect it with
  `tools/oam_inspect.py`. Capture metadata gates evidence to active gameplay,
  excludes forced blank and menu/map OAM, recognizes DKC's tile-`$FF` unused
  marker, tolerates the normal DMA pipeline, and reports X-high loss only
  when the same WRAM-shadow entry supplies direct contradictory evidence.
  The 7,600-frame Jungle oracle was clean: zero X-high loss suspects, with
  valid left/right margin entries still reported descriptively.

Provenance colors are: green = captured/authentic history, cyan = ROM
prefill, magenta = proven periodic fold, gray = verified transparent blank,
red = unsafe circular-VRAM fallback, yellow = native edge repeat. The wash is
applied only to side margins; the native center remains byte-identical. The
region report schema is `docs/schemas/dkc1-ws-regions-v1.schema.json`.

```powershell
python tools\compare_widescreen_regions.py native.ppm wide.ppm `
  --extra 43 --json-out regions.json --diff-out regions-diff.ppm
```

Example:

```powershell
$env:DKC1_WIDESCREEN = '1'
$env:DKC1_WS_TRACE = "$env:TEMP\dkc1-ws.jsonl"
.\build\dkc1_snesrecomp_headless.exe C:\private\dkc1.sfc 7600
python tools\analyze_ws_trace.py $env:TEMP\dkc1-ws.jsonl `
  --json-out $env:TEMP\dkc1-ws-summary.json
```

Atomic WRAM capture example:

```powershell
$env:DKC1_WRAM_DUMP = '7500-7600'
$env:DKC1_WRAM_DUMP_PATH = "$env:TEMP\route.wram.bin"
$env:DKC1_WRAM_DUMP_RANGES = '0000-01ff,192b-1a2a'
.\build\dkc1_snesrecomp_headless.exe C:\private\dkc1.sfc 7600
python tools\verify_wram_dump.py $env:TEMP\route.wram.bin
```

Recipe validation and execution:

```powershell
python tools\run_route_recipe.py recipes\fresh-entry-smoke.json `
  --validate-only --script-out $env:TEMP\route.script

python tools\run_route_recipe.py recipes\fresh-entry-smoke.json `
  --rom C:\private\dkc1.sfc `
  --runner .\build\dkc1_snesrecomp_headless.exe `
  --session-dir $env:TEMP\dkc1-route-smoke

# Interactive: launch the same route in the visible debugger and return.
# It pauses on the final frame so the result can be inspected.
python tools\run_route_recipe.py recipes\fresh-entry-smoke.json `
  --rom C:\private\dkc1.sfc `
  --visible `
  --session-dir $env:TEMP\dkc1-route-visible
```

Checkpoint names are restricted to safe filenames and duplicates are
rejected. A timed-out predicate exits nonzero and cannot produce later
checkpoint evidence. Named checkpoints contain full 128 KiB WRAM plus
SHA-256 for WRAM, VRAM, WRAM OAM shadow, and PPU OAM. The smoke route was
accepted only after three independent runs produced byte-identical WRAM.

Cross-cutting rules adopted from the emulator worklog:

- every tool is default-off and provably inert when off;
- transitions, not per-frame dumps, for long recordings;
- symbolic PC/WRAM labels are grounded in byte-exact assertions against the
  verified ROM before any conclusion is published;
- OAM tooling always labels WRAM shadow ($0200/$0400) vs PPU OAM and reads
  both (the X-high bug produced a wrong published conclusion from reading one
  copy one VBlank early);
- capture targets never silently substitute surfaces (the DKC2-era shimmer
  bug was misdiagnosed for a session because `composed` fell back to `main`);
- actors are matched by source record + world position, never by slot;
- fresh-entry evidence is kept separate from loaded-state evidence (two
  emulator bugs were unprovable from save states that had serialized
  already-corrupt VRAM);
- raw bytes gate, images illustrate ("PNG comparison is not accepted in
  place of raw BG1 bytes").

## Tier 0 — substrate (build first; everything else consumes these)

### 1. Per-frame widescreen decision trace (`DKC1_WS_TRACE`)
**Status: implemented (schema v1).**
The record the handoff already specifies, as default-off JSONL from the
headless host, one object per frame: host+SNES frame; mode/entrance/fade;
source-signature fields (map bank/base, metatile base, stream VRAM base);
BGMODE/BGSC/main/sub/wide-layer mask/terrain layer; camera + all four scroll
pairs; per-layout `matches/decodable` calibration scores; selected layout,
grace/miss counters; which of {WsShadowReset, cold init, WsShadowFrame,
prefill, BG3 repeat, edge extension, centered fallback} ran;
per-layer world keys and margin tile counts; `WsShadowGetMarginStats`
deltas; and separate hashes of left margin / native center / right margin
plus per-BG-plane margin hashes.
**Serves:** open issues 1–3 directly (the immediate question: *which exact
frame and reason changes retained margin pixels while VRAM/OAM are
identical*). **Reuses:** DKC2's `widescreen_frame` trace pattern.
**Cost:** ~1 day. Build nothing else calibration-related until this exists.

### 2. Route harness: wait-predicates, snapshots, fresh-entry recipes
**Status: implemented (schema v1); the route library is intentionally small.**
Formalize `SNESRECOMP_INPUT_PLAY` into recipe JSON (port of
`DKCLevelAutomation`): steps are `automation` / `checkpoint`, with
`wait_wram` predicates (mask/shift/signed compares) instead of fixed frame
counts — this kills the map-entry timing roulette permanently. Add:
per-frame atomic WRAM dump ranges (`DKC1_WRAM_DUMP=7500-7600`, raw stream +
SHA-256 JSONL index); `DKC1_SAVESTATE_INPUT` /
`DKC1_SAVESTATE_SAVE_AT` via the runtime's `RtlSaveSnapshot`/`RtlLoadSnapshot`
so routes can anchor mid-game; a named library of fresh-entry recipes
(boot→file→map→each level) since fresh entry is the only valid evidence for
initializer bugs. **Serves:** every other tool; open issues 5–6.
The engine, schema, native snapshots, checksum-indexed WRAM ranges, manifest,
and smoke recipe are present. Adding authored routes for every level remains
coverage work rather than a missing substrate. **Cost:** implemented.

### 3. Stock-vs-wide first-divergence locator
**Status: implemented; initial 7,600-frame route is WRAM-identical.**
Port of `DKCFirstDivergenceLocator` / `DKCDualRuntimeDifferential`, much
simpler natively: run the same recipe under `DKC1_WIDESCREEN=0` and `=1`,
stream per-frame WRAM, compare with order-sensitive window hashes (endpoint
comparison misses transient divergences that reconverge), binary-search to
the exact first frame, then confirm by independent re-replay. Report BOTH
`firstRawFrame` (any of 128 KiB) and `firstUnexpectedFrame` (predicate-
selected), with include-groups ported from the emulator: `core_gameplay`,
`actor_pool`, `object_bookkeeping` ($192B table), `scanner`,
`section_controller`, `camera_and_bounds` — the last filtered through an
expected-widescreen profile so legitimate presentation deltas don't mask the
first real gameplay diff. **Serves:** open issue 4 (wide-vs-native WRAM
divergence at frame 7,600 is currently unclassified). **Cost:** 2–3 days.

## Tier 1 — bug-class instruments

### 4. Object lifecycle tracer + prefetch phase auditor
The emulator program's crown jewels (`DKCObjectLifecycleTracer`,
`DKCObjectPrefetchPhaseAuditor`), ported to native sampling of `g_ram`:
per-frame decode of the bank-$BD actor pool, the $192B–$1A2A bookmark
table, scanner window/cursors, type-9 section state, and the authored
entrance list with decoded type-5 children; emit transitions only; gate on
gameplay identity so map/menu WRAM is never decoded as actors. Allocator
semantics hooked at the opcode-verified PCs ($BDF3B1/$BDF3D2 exhaustion,
$BDF3B5/$BDF3D6 success — verify bytes against the ROM first; the emulator
shipped these inverted once). Phase auditor aligns stock/wide episodes by
source record (never slot), compares identity/position/motion/state/
animation plus the conservative collision-scratch range $0C35..$109D at the
first stock allocation frame, and uses the honest vocabulary:
`harmless_visual_prefetch`, `behavior_phase_advancement`,
`wide_persists_stock_culls` (= indeterminate, never "harmless").
**Serves:** open issues 4–5; the grouped-child-retry and early-activation
release gates. **Cost:** 3–5 days; highest-value single instrument.

### 5. Margin provenance overlay + plane isolation (desktop host)
Debug hotkeys in `dkc1_desktop`: BG1/BG2/BG3/OBJ isolation toggles, and a
false-color margin mode painting every margin tile by its WsShadow source —
captured-from-VRAM / prefill guess / blank fallback / periodic fold /
edge repeat / raw-VRAM fallback (the dangerous one) — via
`WsShadowDebugCell` plus a small per-source tag extension. Also an on-screen
strip showing terrain_ready, layout, calibration score, world keys.
**Serves:** open issues 1–2 visually — the "inconsistent dark shapes near
margin edges" become instantly attributable to a source class.
**Cost:** 1–2 days.

### 6. Region-aware frame/plane differ (raw-bytes oracle)
Port `compare_frames.py` and the framebuffer-oracle discipline: split every
wide capture into left-margin / native-center / right-margin; gate on
center == native oracle pixel-exact and hash margins separately; declare a
comparison invalid when raw PPU inputs differ (only equal inputs isolate a
renderer difference). Add headless dumps of the live rolling tilemap and
the WsShadow world-keyed store as raw bytes for direct comparison against
the ROM-decoded expectation — three-way: ROM decode vs VRAM vs shadow.
**Serves:** open issues 1, 6; the transition release gate ("no prior world
tiles may survive in either margin"). **Cost:** ~1 day.

### 7. OAM inspector with shadow-vs-PPU dual view and wrap detector
**Status: implemented and live-route validated.**
Per-frame dump/compare of WRAM OAM shadow ($0200/$0400) AND PPU OAM,
labeled, with 9-bit X decode, size bits, and world back-projection via the
camera; automatic flag for entries whose art X sits in [256, 256+extra] but
whose X-high bit is 0 (the wrap signature from emulator bugs 5–6), and for
left-margin entries wrapping from negative X. Bakes in the two-VBlank rule:
a one-frame PPU/shadow disagreement is lag, not a bug.
**Serves:** margin sprite work (cull adapters, rope/banana private paths).
**Cost:** ~1 day.

## Tier 2 — route/regression infrastructure

### 8. Regression recipes + closure contracts + 3x repeat gate
Port `run_regression.py` / `run_softlock_closure.py` / the offline
`verify_*` pattern: recipe checkpoints carry `expect` blocks; contracts are
machine-readable JSON that reject any `write_wram`; the gate is three
repeats with byte-identical full-WRAM SHA-256 at every checkpoint. Seed with
the emulator's proven route set (they port as route *definitions*, not
states): cave banana position AND pickup, cave exit traversal, barrel-cannon
type-5 child retry, Slipslide type-9 progression, Croctopus/Poison
completion, K. Rool fresh entry. **Serves:** open issue 5 and most release
gates. **Cost:** 2–3 days plus route re-recording.

### 9. Input flight recorder + macro minimizer
**Status: rolling visible-host recorder implemented; minimizer implemented
separately.** Set `DKC1_FLIGHT_RECORDER=1` before launching the desktop host.
It retains about one minute of resolved controller input plus a native snapshot
anchor every 300 frames entirely in memory. Pressing **F9** exports a versioned
repro bundle containing the covered anchor and current snapshots, exact
per-frame input masks, full WRAM/VRAM/CGRAM, both WRAM-shadow and PPU OAM, and
SHA-256 provenance. `tools/verify_flight_bundle.py` validates the bundle and can
replay it through a supplied runner to prove the final 128 KiB WRAM hash.
`DKC1_FLIGHT_RECORDER_DIR` selects the export root. The recorder is default-off,
allocates and writes nothing when disabled, and performs no disk I/O while
playing until F9 is pressed. The cost when armed is a native in-memory snapshot
every five seconds and storage for sixteen anchors; it should remain a
playtest/debug facility rather than a release default.

The companion `tools/minimize_route.py` performs transition-preserving ddmin
with repeated outcome checks and treats nondeterminism as an abort. Together
these tools turn a playtester's F9 bundle into a deterministic, shrinkable route.
**Serves:** every future playtest report.

### 10. Whole-game level sweep harness
The emulator effort's biggest structural gap, and far more achievable here:
headless, uncapped, iterate every entrance — fresh entry, then either a
simple scripted traversal or a camera-probe mode (force the presentation
camera along the level while calibrating/decoding margins every frame) —
recording calibration scores, layout locks, margin-stat rawFallback counts,
wide-vs-stock WRAM divergence class, and margin continuity per level.
Nightly HTML report. Finds unknown layouts (bonus rooms, mine carts, water,
vertical) before players do. **Serves:** open issues 5–6 at coverage scale.
**Cost:** 3–4 days once tools 2/3/6 exist.

### 11. Unified timeline viewer
Port `DKCObjectSectionTimeline`'s self-contained HTML (camera/player paths,
per-record eligibility/booking/actor-lifetime bands, scanner decisions,
first-anomaly marker, clickable checkpoints) with schema adapters for the
recomp's JSONL streams. The emulator team correlated these streams by hand
for weeks before building it. **Cost:** ~1 day (mostly adaptation).

### 12. Indirect-dispatch target logger (the `$BE8179` gap)
Pre-opcode hook on unauthorized dispatch sites logging the actual computed
targets seen at runtime, aggregated into proposed `indirect_dispatch`
cfg contracts. Closes open issue 7 with evidence instead of guesswork, and
generalizes to any future dispatch gap. **Cost:** ~half a day.

## Additional tools worth adding

These four are recomp-specific opportunities that were much harder to build
reliably through the emulator boundary:

### 13. CPU control-flow and stack integrity sentinel

Validate every RTS/RTL/RTI destination against the imported instruction map,
record the last 256 calls/returns, and stop on the *first* invalid stack frame.
This turns an eventual black screen into the exact producer instruction. It
would have caught the type-$05 retry's incorrect 16-bit push at the first bad
PLA rather than after execution returned into `$BD:FE01`. The downside is hot
CPU instrumentation, so it must be diagnostic-only and should use a compact
address bitmap rather than symbol lookup per instruction.

### 14. Tile/OAM boundary metamorphic fuzzer

Replay a deterministic frame while sweeping only host presentation width,
camera bias, and fine-scroll phase across `-1/0/+1`, 7/8, 15/16, 255/256,
and the two viewport endpoints. Gameplay WRAM must remain identical; center
pixels must remain the native oracle; only newly exposed margins may change.
This systematically finds the off-by-one guard-column and 9-bit OAM mistakes
that ordinary play reaches rarely. It cannot validate authored object timing,
so it supplements rather than replaces route tests.

### 15. Render/interaction correspondence oracle

For every visible object matched by source record and world position, compare
its rendered OAM bounds with conservative collision/pickup bounds and report
screen-space disagreement. This directly targets the historic “banana looks
right but pickup remains at the stock location” class. Collision formats vary
by actor family, so the tool must report `unsupported` instead of guessing
when a semantic adapter is absent.

### 16. Transition-state contamination bisector

At every source-signature, PPU-mode, or terrain-ready transition, retain the
last good and first bad trace records plus raw VRAM/OAM/shadow snapshots. Then
binary-search which reset/prefill/write first made a margin cell differ from
fresh-entry output. This is the quickest path for title/bonus/map transitions
that preserve stale side art. It depends on tools 1, 2, and 6 and should not be
built as a separate capture format—the bundle must reference their hashes.

## Suggested order against the current open issues

1 → 5 → 6 (attack the margin nondeterminism at frame 7,600 with evidence)
→ 3 → 4 (classify the wide-vs-native WRAM divergence and early activation)
→ 2 → 8 (make the object-fix routes provable) → 12 → 9 → 10 → 11.

Add tool 13 beside 12, tool 14 after the region-aware differ, tool 15 after
the lifecycle/OAM pair, and tool 16 once route snapshots and raw planes exist.

## Session results (2026-08-15, tool build-out)

Substrate and tools landed (commits b73d599, f14f769, a6376f7+):
script engine with wait/hold/pulse predicates and checkpoint/state
directives; evidence taps (WRAM hash log, input recorder, OAM dual log,
transition-only lifecycle trace with exact-frame sampling); wram_dump
ranged raw captures; first-divergence locator; OAM inspector; prefetch
phase auditor; regression contract runner (3x byte-identical gate);
macro minimizer; timeline exporter; level sweep grader; dispatch resolver.
Isolated tool build: build_host_tools.bat -> dkc1_headless_tools.exe.

First real evidence produced:

- **$BE8179 resolved** (open issue 7): the animation-callback
  `JML [$007A]` behind `PHK/PEA $810D`. force_lle + DKC1_TRACE_PC harvested
  12 targets; authorized as a ptrcall contract in bankbe.cfg. Routes now
  complete with zero unresolved-abandon reports (2,599 AOT variants).
  Re-harvest on pointer_match misses; coverage grows with routes.
- **First stock-vs-wide divergence located and classified** (open issue 4):
  frame 7332 is byte-identical end to end; frame 7333 (first level frame)
  differs in 31 bytes — widened scanner window right ($0140 -> $0196),
  scanner record index 6 -> 8, two margin actors allocated (records 6/7,
  one at x=$0190 inside the right margin), bookmarks set. Divergence is
  entirely activation-window driven at entry; nothing outside the
  expected adapters fired earlier.
- **Prefetch audit over the Jungle route**: 59 episodes — 14 matched,
  34 indeterminate_without_stock_allocation (short route; margins activate
  records stock never reaches), 4 wide_persists_stock_culls
  (indeterminate — queue for the WRAM pass at the reported frames),
  1 behavior_phase_difference, 6 needing exact-frame samples.
- **WS trace step-0 capture** on the predicate route: zero raw fallbacks,
  zero margin-change-while-static events, zero prefill refreshes across
  7,645 frames. The margin-nondeterminism repro (issue 1) still needs the
  original fixed-frame 7,500-7,599 route and a gameplay-to-title
  transition capture under this trace.

Still open, deliberately left for the session owning dkc1_game.c:
two-phase calibrate/commit restructure (issue 2) and hard identity
invalidators (issue 3) — the trace now provides the evidence they need.

## Validation pass (2026-08-15, later)

- **Regression gate works end to end**: contracts/jungle-entry.json PASSES —
  all expects hold, 3 repeats byte-identical at both checkpoints.
- **Native baseline must be re-pinned**: the handoff's frame-7,600 native
  hash table was captured while $BE8179 callbacks were still being skipped.
  With the dispatch authorized, native runs are deterministic (two identical
  runs) at NEW values: frame dc629702..., WRAM 8a108fd6..., audio fnv1a
  5a54239ccb9cfcfe; CGRAM unchanged (2f6ce319...). This is a correctness fix
  changing the oracle, not adapter leakage — but the wide-vs-native
  inertness statement needs re-proving against the new baseline.
- **Transition margins are clean**: level->(Kong swap)->continue route under
  DKC1_WS_TRACE shows every centered frame's margins hashing to one constant
  (black); zero raw fallbacks. A true gameplay->map/title capture still
  needs a route that exhausts both Kongs (single-hit routes just swap to
  Diddy).
- **Two leads for the dkc1_game.c owner**: (1) WIDE<->CENTERED flapping at
  level entry (frames ~7304-7331) — the calibration flip-flop issue 2
  predicts; (2) the wide terrain world key unwraps to camX=$FFF0 (-16) on
  the first widened entry frame — check Dkc1VideoUnwrapPpuScroll around the
  zero boundary.
- wide_persists_stock_culls (4 records, +14..20 frames each) still needs
  the queued WRAM pass before any of them may be called benign.

## Save states (status)

Native full-machine snapshots exist at three layers: script directives
`state_save`/`state_load` (both hosts), env anchors DKC1_SAVESTATE_INPUT /
DKC1_SAVESTATE_OUTPUT / DKC1_SAVESTATE_SAVE_AT, and the F9 repro bundle's
embedded anchor. Loading resets the widescreen shadow by design
(loaded-state vs fresh-entry evidence discipline). Interactive F11/F12 quick
save/load is implemented in the desktop host.
SuperZSNES .szst states remain forensic inputs only; they cannot load here.

The visible host now has a reproducible Jungle snapshot library generated by
`recipes/capture_jungle_snapshots.dks`: map-before-entry, first valid wide
camera bounds, and stable gameplay. The local binary states live under
`build/snapshots/` and are intentionally not committed. Launch one without
replaying the boot route:

```powershell
tools\launch_visible_snapshot.ps1 gameplay -Trace
tools\launch_visible_snapshot.ps1 bounds -Trace
tools\launch_visible_snapshot.ps1 map -Trace
```

`recipes/snapshot_smoke.dks` is the two-frame visible-host load check.

## Follow-through pass (2026-08-15, latest)

- **Persists records classified from raw WRAM** (tools/analyze_persists.py):
  all three authored wide_persists_stock_culls records grade
  release_delayed_by_wider_window -- constant state/anim across every
  extension frame, drifting off-screen left at walk speed, freed at the
  widened despawn threshold (the 14-20 extra frames equal the extra ~56px
  at ~3px/frame). The fourth was a source-0 (non-authored backlink)
  alignment artifact; the auditor now refuses to align source <= 0.
- **Adapter inertness re-proven against the new baseline**: a generated
  tree built WITHOUT apply_dkc1_widescreen_overrides.py
  (build_host_noadapt.bat -> dkc1_headless_noadapt.exe) produces
  byte-identical native frame/WRAM/VRAM/OAM hashes and audio FNV to the
  adapters-applied build at frame 7,600. The re-pinned baseline is backed
  by a real no-adapter oracle, not just determinism.
- **$BE8179 contract extended to 15 targets** ($BE993E/$BE9945/$BE994C
  observed via a temporary force_lle pass on a contact-heavy route);
  2,602 AOT variants, zero unresolved-abandon.
- **F11/F12 quick save/load** added to the desktop host (native snapshots
  to quicksave.state beside the exe; loading intentionally resets the
  widescreen shadow so presentation state recalibrates).
- **NEW LEAD -- contact damage does not land in the current build, in BOTH
  native and wide modes.** Deterministic repro: recipes/route_death.dks
  walks DK into the first Jungle Hijinxs Gnawty and stands in its patrol
  path; DK/Diddy overlap it for thousands of frames unharmed, identically
  at 4:3 and 342px, and identically with the callback dispatch executing
  via forced LLE -- so it is NOT a widescreen or dispatch-contract defect.
  Suspect surface: player-sprite collision path in the recomp runtime (or
  an authentic-behavior misread -- verify the same stand-still against
  real-hardware/emulator ground truth first). Evidence:
  build/death_final.png (wide), build/death_native.png (native),
  ws_death.jsonl.
